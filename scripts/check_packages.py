"""Exercise the staged release archive through a real package manager."""

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading

from prepare_packages import ROOT, write_json


def run(*command, cwd=None):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=cwd, check=True)


def stage_test_recipes(distribution, work, metadata, source_url):
    """Use the prepared recipes, replacing only their public source URL."""
    shutil.copytree(distribution / "vcpkg", work / "vcpkg", dirs_exist_ok=True)
    portfile = work / "vcpkg/orm-cxx/portfile.cmake"
    portfile_text = portfile.read_text(encoding="utf-8")
    if metadata["url"] not in portfile_text:
        raise ValueError("The staged vcpkg port does not reference the release archive")
    portfile.write_text(portfile_text.replace(metadata["url"], source_url), encoding="utf-8", newline="\n")

    shutil.copytree(distribution / "conan", work / "conan", dirs_exist_ok=True)
    conandata = work / "conan/recipes/orm-cxx/all/conandata.yml"
    data = json.loads(conandata.read_text(encoding="utf-8"))
    source = data.get("sources", {}).get(metadata["version"])
    if not isinstance(source, dict) or source.get("url") != metadata["url"]:
        raise ValueError("The staged Conan recipe does not reference the release archive")
    source["url"] = source_url
    write_json(conandata, data)


def check_conan(args, metadata, work):
    conan = [sys.executable, "-m", "conans.conan"]
    os.environ.setdefault("CONAN_HOME", str(ROOT / "build/conan-home"))
    profile_options = []
    if args.conan_profile:
        profile_options += ["--profile:all", args.conan_profile]
    else:
        run(*conan, "profile", "detect", "--force")
    for setting in args.conan_setting:
        profile_options += ["-s", setting]
    for config in args.conan_conf:
        profile_options += ["-c", config]
    sqlite = args.backends in ("sqlite", "both")
    postgres = args.backends in ("postgresql", "both")
    run(*conan, "create", work / "conan/recipes/orm-cxx/all",
        "--version", metadata["version"], "--build=missing",
        "-s", "compiler.cppstd=20", "-s", "build_type=Release",
        "-o", f"orm-cxx/*:with_sqlite3={sqlite}",
        "-o", f"orm-cxx/*:with_postgresql={postgres}",
        "-c", "tools.cmake.cmaketoolchain:generator=Ninja",
        "-c", "tools.build:jobs=2", *profile_options)


def check_vcpkg(args, metadata, work):
    vcpkg_root = args.vcpkg_root.resolve()
    vcpkg = vcpkg_root / ("vcpkg.exe" if os.name == "nt" else "vcpkg")
    triplet = args.triplet or ("x64-windows-static" if os.name == "nt" else "x64-linux")
    features = []
    if args.backends in ("sqlite", "both"):
        features.append("sqlite")
    if args.backends in ("postgresql", "both"):
        features.append("postgresql")
    compiler_options = []
    if os.name == "nt" and triplet.endswith("-static"):
        compiler_options.append("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>")
    manifest_dir = work / "consumer"
    baseline = subprocess.check_output(["git", "-C", str(vcpkg_root), "rev-parse", "HEAD"], text=True).strip()
    write_json(manifest_dir / "vcpkg.json", {"builtin-baseline": baseline, "dependencies": [{
        "name": "orm-cxx", "default-features": False, "features": features
    }]})
    run(vcpkg, "format-manifest", work / "vcpkg/orm-cxx/vcpkg.json")
    # A clean manifest and install tree cannot accidentally use the developer's
    # bundled SOCI, SQLite headers, or previous backend configurations.
    build = work / "consumer-build"
    run("cmake", "-S", ROOT / "tests/package_consumer", "-B", build, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_TOOLCHAIN_FILE={vcpkg_root}/scripts/buildsystems/vcpkg.cmake",
        f"-DVCPKG_MANIFEST_DIR={manifest_dir}", f"-DVCPKG_OVERLAY_PORTS={work}/vcpkg",
        f"-DVCPKG_TARGET_TRIPLET={triplet}",
        "-DVCPKG_INSTALL_OPTIONS=--enforce-port-checks",
        f"-DORM_CXX_EXPECT_SQLITE={'ON' if 'sqlite' in features else 'OFF'}",
        f"-DORM_CXX_EXPECT_POSTGRESQL={'ON' if 'postgresql' in features else 'OFF'}",
        *compiler_options)
    run("cmake", "--build", build, "--parallel", "2")
    run("ctest", "--test-dir", build, "--output-on-failure")
    reflection_build = work / "reflection-build"
    run("cmake", "-S", ROOT / "tests/reflection_consumer", "-B", reflection_build,
        "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_PREFIX_PATH={build}/vcpkg_installed/{triplet}", *compiler_options)
    run("cmake", "--build", reflection_build, "--parallel", "2")
    run("ctest", "--test-dir", reflection_build, "--output-on-failure")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manager", choices=("conan", "vcpkg"), required=True)
    parser.add_argument("--backends", choices=("sqlite", "postgresql", "both", "core"), default="sqlite")
    parser.add_argument("--distribution", type=Path, default=ROOT / "build/distribution")
    parser.add_argument("--vcpkg-root", type=Path, default=ROOT / "externals/vcpkg")
    parser.add_argument("--triplet")
    parser.add_argument("--conan-profile", help="Existing profile for both host and build contexts; skips detection")
    parser.add_argument("--conan-setting", action="append", default=[], help="Additional Conan host setting")
    parser.add_argument("--conan-conf", action="append", default=[], help="Additional Conan configuration")
    parser.add_argument("--work", type=Path)
    args = parser.parse_args()
    distribution = args.distribution.resolve()
    metadata = json.loads((distribution / "release.json").read_text(encoding="utf-8"))
    work = (args.work or ROOT / "build" / f"package-{args.manager}-{args.backends}").resolve()
    work.mkdir(parents=True, exist_ok=True)
    handler = partial(SimpleHTTPRequestHandler, directory=str(distribution))
    server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        url = f"http://127.0.0.1:{server.server_port}/{metadata['archive']}"
        stage_test_recipes(distribution, work, metadata, url)
        if args.manager == "conan":
            check_conan(args, metadata, work)
        else:
            check_vcpkg(args, metadata, work)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == "__main__":
    main()
