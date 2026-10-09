#!/usr/bin/env python3
"""Merge two LLVM LCOV reports and require every project line to be hit."""

import argparse
from collections import defaultdict
from pathlib import Path
import re
import sys


SOURCE_MARKER = re.compile(r"(?:^|/)(include/orm-cxx/|modules/|src/)")


def project_path(raw_path: str, source_root: Path) -> str:
    """Map build-specific absolute paths to paths in this checkout."""
    normalized = raw_path.replace("\\", "/")
    for match in reversed(list(SOURCE_MARKER.finditer(normalized))):
        relative = normalized[match.start(1) :]
        if (source_root / relative).is_file():
            return relative
    raise ValueError(f"Source path is outside this checkout or missing: {raw_path}")


def read_report(path: Path, source_root: Path) -> dict[str, dict[int, int]]:
    if not path.is_file():
        raise ValueError(f"Coverage report is missing: {path}")

    report: dict[str, dict[int, int]] = defaultdict(dict)
    source = None
    records = 0
    for text in path.read_text(encoding="utf-8").splitlines():
        if text.startswith("SF:"):
            if source is not None:
                raise ValueError(f"Unterminated LCOV record in {path}")
            source = project_path(text[3:], source_root)
            records += 1
        elif text.startswith("DA:"):
            if source is None:
                raise ValueError(f"LCOV line without source file in {path}")
            parts = text[3:].split(",")
            if len(parts) < 2:
                raise ValueError(f"Invalid LCOV line in {path}: {text}")
            line, hits = int(parts[0]), int(parts[1])
            if line < 1 or hits < 0:
                raise ValueError(f"Invalid LCOV counts in {path}: {text}")
            report[source][line] = max(report[source].get(line, 0), hits)
        elif text == "end_of_record":
            if source is None:
                raise ValueError(f"LCOV terminator without source file in {path}")
            source = None

    if source is not None:
        raise ValueError(f"Unterminated LCOV record in {path}")
    if records == 0 or not any(lines for lines in report.values()):
        raise ValueError(f"Coverage report is empty: {path}")
    return report


def merge_reports(reports: list[dict[str, dict[int, int]]]) -> dict[str, dict[int, int]]:
    merged: dict[str, dict[int, int]] = defaultdict(dict)
    for report in reports:
        for source, lines in report.items():
            for line, hits in lines.items():
                merged[source][line] = max(merged[source].get(line, 0), hits)
    return merged


def write_report(path: Path, report: dict[str, dict[int, int]]) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as output:
        for source, lines in sorted(report.items()):
            output.write(f"TN:\nSF:{source}\n")
            for line, hits in sorted(lines.items()):
                output.write(f"DA:{line},{hits}\n")
            output.write(f"LF:{len(lines)}\nLH:{sum(hits > 0 for hits in lines.values())}\nend_of_record\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sqlite_report", type=Path)
    parser.add_argument("postgresql_report", type=Path)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()

    try:
        source_root = arguments.source_root.resolve()
        merged = merge_reports(
            [read_report(arguments.sqlite_report, source_root), read_report(arguments.postgresql_report, source_root)]
        )
        write_report(arguments.output, merged)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"Coverage check failed: {error}", file=sys.stderr)
        return 1

    misses = [(source, line) for source, lines in sorted(merged.items()) for line, hits in sorted(lines.items()) if hits == 0]
    total = sum(len(lines) for lines in merged.values())
    print(f"Merged line coverage: {total - len(misses)}/{total} ({len(misses)} missing)")
    for source, line in misses:
        print(f"  {source}:{line}")
    return 1 if misses else 0


if __name__ == "__main__":
    sys.exit(main())
