#!/usr/bin/env python3
"""Run bounded local checks for a branch without claiming skipped checks passed."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
CPP_EXTENSIONS = {".c", ".cc", ".cpp", ".cppm", ".ixx", ".inc", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
SOURCE_EXTENSIONS = {".c", ".cc", ".cpp", ".cppm", ".ixx", ".cxx"}


class Report:
    def __init__(self):
        self.failed = []
        self.skipped = []

    def skip(self, name, reason):
        self.skipped.append(name)
        print(f"SKIP {name}: {reason}", flush=True)

    def run(self, name, command):
        print(f"RUN  {name}: {subprocess.list2cmdline([str(x) for x in command])}", flush=True)
        started = time.monotonic()
        try:
            result = subprocess.run(command, cwd=ROOT, check=False)
            ok = result.returncode == 0
        except OSError as error:
            print(f"Could not execute {name}: {error}", file=sys.stderr, flush=True)
            ok = False
        print(f"{'PASS' if ok else 'FAIL'} {name} ({time.monotonic() - started:.1f}s)", flush=True)
        if not ok:
            self.failed.append(name)
        return ok


def git_paths(*args):
    result = subprocess.run(["git", *args], cwd=ROOT, stdout=subprocess.PIPE, check=True)
    return [os.fsdecode(item) for item in result.stdout.split(b"\0") if item]


def changed_files(base):
    staged = set(git_paths("diff", "--cached", "--name-only", "-z", "--"))
    unstaged = set(git_paths("diff", "--name-only", "-z", "--"))
    partially_staged = sorted(staged & unstaged)
    if partially_staged:
        raise ValueError("partially staged files cannot be checked as a single tree: " + ", ".join(partially_staged))
    paths = set(git_paths("diff", "--name-only", "--diff-filter=ACMR", "-z", base, "--"))
    paths.update(git_paths("ls-files", "--others", "--exclude-standard", "-z"))
    paths = sorted(path for path in paths if (ROOT / path).is_file())
    cpp = [p for p in paths if Path(p).suffix.lower() in CPP_EXTENSIONS and not p.startswith("externals/")]
    cmake_files = [p for p in paths if (Path(p).name == "CMakeLists.txt" or Path(p).suffix == ".cmake")
                   and not p.startswith(("externals/", "cmake/third_party/")) and p != "cmake/cmake-coverage.cmake"]
    return paths, cpp, cmake_files


def default_preset(platform, which):
    if platform == "nt":
        return "msvc-debug"
    if which("clang++-18"):
        return "linux-clang-debug"
    if which("g++-15"):
        return "linux-gcc-debug"
    return None


def preset_requirements(preset):
    """Resolve checked-in preset cache inheritance, preserving child and first-parent priority."""
    data = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
    presets = {entry["name"]: entry for entry in data["configurePresets"]}

    def resolve(name, visiting):
        if name in visiting:
            raise ValueError(f"cyclic preset inheritance: {name}")
        item = presets[name]
        parents = item.get("inherits", [])
        if isinstance(parents, str):
            parents = [parents]
        cache = {}
        for parent in reversed(parents):
            cache.update(resolve(parent, visiting | {name}))
        cache.update(item.get("cacheVariables", {}))
        return cache

    cache = resolve(preset, set())
    cache = {key: value.get("value") if isinstance(value, dict) else value for key, value in cache.items()}

    def enabled(key, default):
        value = cache.get(key, default)
        return str(value).upper() not in {"0", "OFF", "FALSE", "NO", "N", "IGNORE", "NOTFOUND", "NONE", ""}

    paths = []
    if not enabled("ORM_CXX_USE_SYSTEM_SOCI", False):
        paths.append("externals/soci/CMakeLists.txt")
    if enabled("ORM_CXX_BUILD_TESTS", True):
        paths.append("externals/googletest/CMakeLists.txt")
    if "externals/vcpkg/scripts/buildsystems/vcpkg.cmake" in str(cache.get("CMAKE_TOOLCHAIN_FILE", "")):
        paths.append("externals/vcpkg/scripts/buildsystems/vcpkg.cmake")
    return cache.get("CMAKE_CXX_COMPILER"), paths


def versioned_tool(names, expected):
    for name in names:
        executable = shutil.which(name)
        if not executable:
            continue
        try:
            result = subprocess.run([executable, "--version"], capture_output=True, text=True, check=False)
        except OSError:
            continue
        if result.returncode == 0 and expected in (result.stdout + result.stderr):
            return executable
    return None


def minimum_version(executable, minimum):
    try:
        result = subprocess.run([executable, "--version"], capture_output=True, text=True, check=False)
        match = re.search(r"\b(\d+)\.(\d+)(?:\.(\d+))?", result.stdout)
    except OSError:
        return False
    if result.returncode or not match:
        return False
    return tuple(int(part or 0) for part in match.groups()) >= minimum


def clang_tidy(report, cpp, build_dir):
    sources = [path for path in cpp if Path(path).suffix.lower() in SOURCE_EXTENSIONS]
    headers = [path for path in cpp if path not in sources]
    if not cpp:
        return
    tidy = versioned_tool(["clang-tidy-18", "clang-tidy"], "version 18.")
    database_path = ROOT / build_dir / "compile_commands.json" if build_dir else None
    if not tidy or not database_path or not database_path.is_file():
        report.skip("clang-tidy", "requires clang-tidy 18 and --tidy-build with a current compilation database and BMIs")
        return
    try:
        entries = json.loads(database_path.read_text(encoding="utf-8"))
        compiled = set()
        for entry in entries:
            directory = Path(entry["directory"])
            file = Path(entry["file"])
            compiled.add((file if file.is_absolute() else directory / file).resolve())
    except (OSError, ValueError, KeyError, TypeError) as error:
        report.skip("clang-tidy", f"cannot read compilation database: {error}")
        return
    matched = [path for path in sources if (ROOT / path).resolve() in compiled]
    unmatched = [path for path in sources if path not in matched]
    if matched:
        report.run("clang-tidy", [tidy, "-p", str(database_path.parent), "--quiet", *matched])
    if unmatched or headers or not matched:
        report.skip("clang-tidy coverage", f"no direct compilation entry for {len(unmatched)} source(s) and {len(headers)} header(s)")


def build(report, args, relevant):
    if not relevant:
        if args.test_regex:
            report.skip("selected tests", "no C++ or CMake changes selected for build")
        return
    if not shutil.which("cmake") or not shutil.which("ninja"):
        report.skip("build", "cmake and Ninja are required")
        if args.test_regex:
            report.skip("selected tests", "build unavailable")
        return
    if not minimum_version("cmake", (3, 31, 0)) or not minimum_version("ninja", (1, 11, 0)):
        report.skip("build", "requires CMake >= 3.31 and Ninja >= 1.11")
        if args.test_regex:
            report.skip("selected tests", "build unavailable")
        return
    preset = args.preset
    if not preset:
        preset = default_preset(os.name, shutil.which)
        if not preset:
            report.skip("build", "no supported default compiler found; choose an installed --preset")
            if args.test_regex:
                report.skip("selected tests", "build unavailable")
            return
    try:
        compiler, required = preset_requirements(preset)
    except (OSError, KeyError, TypeError, ValueError) as error:
        report.skip("build", f"cannot resolve checked-in preset {preset}: {error}")
        if args.test_regex:
            report.skip("selected tests", "build unavailable")
        return
    if compiler and not shutil.which(compiler):
        report.skip("build", f"{compiler} is unavailable in this environment")
        if args.test_regex:
            report.skip("selected tests", "build unavailable")
        return
    missing = [path for path in required if not (ROOT / path).is_file()]
    if missing:
        report.skip("build", "missing dependencies: " + ", ".join(missing))
        if args.test_regex:
            report.skip("selected tests", "build unavailable")
        return
    target = "orm-cxx-UT" if args.test_regex else "orm-cxx"
    if not report.run("configure", ["cmake", "--preset", preset]):
        if args.test_regex:
            report.skip("selected tests", "configure failed")
        return
    if not report.run("build", ["cmake", "--build", "--preset", preset, "--target", target]):
        if args.test_regex:
            report.skip("selected tests", "build failed")
        return
    if args.test_regex:
        if not shutil.which("ctest"):
            report.skip("selected tests", "ctest unavailable")
        else:
            report.run("selected tests", ["ctest", "--preset", preset, "-R", args.test_regex, "--no-tests=error", "--output-on-failure"])
    else:
        report.skip("selected tests", "no --test-regex supplied; select relevant tests explicitly")


def main():
    started = time.monotonic()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="origin/main", help="base ref for the complete working tree diff")
    parser.add_argument("--preset", help="CMake configure/build/test preset for a supported local compiler")
    parser.add_argument("--test-regex", help="CTest regex selecting relevant tests")
    parser.add_argument("--tidy-build", help="existing fresh Clang 18 build with compilation database and BMIs")
    args = parser.parse_args()
    report = Report()
    if not report.run("diff", ["git", "diff", "--check", args.base, "--"]):
        return 1
    try:
        paths, cpp, cmake_files = changed_files(args.base)
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"FAIL changed-file discovery: {error}", file=sys.stderr)
        return 1
    if cpp:
        formatter = versioned_tool(["clang-format-18", "clang-format"], "version 18.")
        if formatter:
            report.run("C++ format", [formatter, "--dry-run", "--Werror", "--", *cpp])
        else:
            report.skip("C++ format", "clang-format 18 unavailable")
    if cmake_files:
        formatter = versioned_tool(["cmake-format"], "0.6.13")
        if formatter:
            report.run("CMake format", [formatter, "--check", *cmake_files])
        else:
            report.skip("CMake format", "cmake-format 0.6.13 unavailable")
    clang_tidy(report, cpp, args.tidy_build)
    build(report, args, bool(cpp or cmake_files or "CMakePresets.json" in paths))
    elapsed = time.monotonic() - started
    print(f"Summary: {len(report.failed)} failed, {len(report.skipped)} skipped, {elapsed:.1f}s elapsed", flush=True)
    if elapsed > 600:
        print("The 10-minute warm-run target was exceeded; inspect the reported step timings.", flush=True)
    if report.skipped:
        print("INCOMPLETE: report skipped checks and rely on CI for those checks.", flush=True)
    return 1 if report.failed else 0


if __name__ == "__main__":
    sys.exit(main())
