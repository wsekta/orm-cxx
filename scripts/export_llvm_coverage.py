#!/usr/bin/env python3
"""Export LCOV, removing only uninstrumented implicit class-declaration maps."""

import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
import subprocess
import sys


CLASS_DECLARATION = re.compile(r"\s*(?:class|struct)\s+(?P<name>[A-Za-z_]\w*)\s*")
PROFILE_FUNCTION = re.compile(r"^  (.+):\r?\n    Hash: 0x[0-9a-fA-F]+\r?$", re.MULTILINE)
PROFILE_TOTAL = re.compile(r"^Total functions: (\d+)\r?$", re.MULTILINE)
PROFILE_SHOWN = re.compile(r"^Functions shown: (\d+)\r?$", re.MULTILINE)
EXCLUDES = [r".*/externals/.*", r".*/tests/.*", r".*/src/.*Test\.cpp",
            r".*/(postgres_ext|libpq-fe|libpq-events)\.h"]


def instrumented_functions(profile_text: str) -> set[str]:
    names = PROFILE_FUNCTION.findall(profile_text)
    total = PROFILE_TOTAL.search(profile_text)
    shown = PROFILE_SHOWN.search(profile_text)
    if not names or not total or not shown or len(names) != int(total[1]) or len(names) != int(shown[1]):
        raise ValueError("LLVM profdata instrumentation records are missing or incomplete")
    return set(names)


def implicit_maps(coverage: dict, profile_names: set[str], source_root: Path):
    """Require every zero region to be exactly a module's class-name span."""
    modules = (source_root / "modules").resolve()
    sources = {}
    functions = [function for entry in coverage["data"] for function in entry["functions"]]
    discarded = set()
    locations_by_function = {}
    for function_index, function in enumerate(functions):
        if function["count"] != 0 or function["name"] in profile_names or not function["regions"]:
            continue
        locations = set()
        for region in function["regions"]:
            start_line, start_column, end_line, end_column, count, file_index, _, kind = region
            source = Path(function["filenames"][file_index]).resolve()
            if count != 0 or kind != 0 or start_line != end_line or source.suffix != ".cppm":
                break
            if not source.is_relative_to(modules) or not source.is_file():
                break
            if source not in sources:
                sources[source] = source.read_text(encoding="utf-8").splitlines()
            if start_line < 1 or start_line > len(sources[source]):
                break
            declaration = CLASS_DECLARATION.fullmatch(sources[source][start_line - 1])
            if not declaration or (start_column, end_column) != (
                declaration.start("name") + 1, declaration.end("name") + 1
            ):
                break
            locations.add((source, start_line))
        else:
            discarded.add(function_index)
            locations_by_function[function_index] = locations

    # Multiple mappings can share a name. Retain all if any is not an implicit map.
    retained_names = {function["name"] for index, function in enumerate(functions) if index not in discarded}
    discarded = {index for index in discarded if functions[index]["name"] not in retained_names}
    candidates = defaultdict(set)
    for index in discarded:
        for source, line in locations_by_function[index]:
            candidates[source].add(line)

    # A genuine function region on the same line must keep that line in the gate.
    for function_index, function in enumerate(functions):
        if function_index in discarded:
            continue
        for region in function["regions"]:
            if region[7] != 0:
                continue
            source = Path(function["filenames"][region[5]]).resolve()
            candidates[source].difference_update(
                line for line in tuple(candidates[source]) if region[0] <= line <= region[2]
            )
    return {functions[index]["name"] for index in discarded}, candidates


def normalize_lcov(raw: str, discarded_names: set[str], candidates: dict):
    output = []
    removed_lines = 0
    for record in raw.split("end_of_record"):
        lines = record.strip().splitlines()
        if not lines:
            continue
        source_line = next((line for line in lines if line.startswith("SF:")), None)
        if source_line is None:
            raise ValueError("LCOV record has no source file")
        removable = candidates.get(Path(source_line[3:]).resolve(), set())
        kept = []
        function_hits = []
        line_hits = []
        for line in lines:
            if line.startswith(("FN:", "FNDA:")):
                value, name = line.split(":", 1)[1].split(",", 1)
                if name in discarded_names:
                    continue
                if line.startswith("FNDA:"):
                    function_hits.append(int(value))
            elif line.startswith("DA:"):
                number, hits, *_ = line[3:].split(",")
                if int(number) in removable and int(hits) == 0:
                    removed_lines += 1
                    continue
                line_hits.append(int(hits))
            elif line.startswith(("FNF:", "FNH:", "LF:", "LH:")):
                continue
            kept.append(line)
        kept.extend([f"FNF:{len(function_hits)}", f"FNH:{sum(hit > 0 for hit in function_hits)}",
                     f"LF:{len(line_hits)}", f"LH:{sum(hit > 0 for hit in line_hits)}", "end_of_record"])
        output.extend(kept)
    return "\n".join(output) + "\n", removed_lines


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-cov", nargs="+", required=True)
    parser.add_argument("--llvm-profdata", nargs="+", required=True)
    parser.add_argument("--target-binary", type=Path, required=True)
    parser.add_argument("--profile-data", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        command = arguments.llvm_cov + ["export", str(arguments.target_binary),
                                      "-instr-profile=" + str(arguments.profile_data), "--skip-branches"]
        command += ["-ignore-filename-regex=" + pattern for pattern in EXCLUDES]
        coverage = json.loads(subprocess.check_output(command, text=True, encoding="utf-8"))
        profile_text = subprocess.check_output(arguments.llvm_profdata + ["show", "--all-functions", "--counts",
                                              str(arguments.profile_data)], text=True, encoding="utf-8")
        discarded, candidates = implicit_maps(coverage, instrumented_functions(profile_text), arguments.source_root)
        raw = subprocess.check_output(command + ["-format=lcov"], text=True, encoding="utf-8")
        normalized, removed_lines = normalize_lcov(raw, discarded, candidates)
        arguments.output.write_text(normalized, encoding="utf-8", newline="\n")
        print(f"Normalized compiler-synthesized implicit maps: {len(discarded)}; removed declaration lines: {removed_lines}")
    except (OSError, ValueError, KeyError, IndexError, subprocess.CalledProcessError) as error:
        print(f"LLVM coverage export failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
