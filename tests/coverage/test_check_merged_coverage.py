"""Behavioral tests for the cross-backend LCOV gate."""

import subprocess
import sys
from pathlib import Path
import shutil
import unittest
from uuid import uuid4


SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "check_merged_coverage.py"


class MergedCoverageTests(unittest.TestCase):
    def setUp(self):
        scratch = Path(__file__).resolve().parents[2] / "build" / "coverage-script-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        self.root = scratch / str(uuid4())
        self.root.mkdir()
        self.addCleanup(shutil.rmtree, self.root)
        (self.root / "src").mkdir()
        (self.root / "src" / "example.cpp").write_text("line 1\nline 2\n", encoding="utf-8")
        self.sqlite = self.root / "sqlite.lcov"
        self.postgresql = self.root / "postgresql.lcov"
        self.output = self.root / "merged.lcov"

    def report(self, path, prefix, hits):
        path.write_text(
            f"TN:\nSF:{prefix}/src/example.cpp\n"
            + "".join(f"DA:{line},{count}\n" for line, count in hits.items())
            + "end_of_record\n",
            encoding="utf-8",
        )

    def run_gate(self):
        return subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                str(self.sqlite),
                str(self.postgresql),
                "--source-root",
                str(self.root),
                "--output",
                str(self.output),
            ],
            capture_output=True,
            text=True,
            check=False,
        )

    def test_complementary_hits_merge_across_different_prefixes(self):
        self.report(self.sqlite, "/builder/one", {1: 2, 2: 0})
        self.report(self.postgresql, "/builder/two", {1: 0, 2: 1})
        result = self.run_gate()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("2/2 (0 missing)", result.stdout)
        self.assertIn("SF:src/example.cpp", self.output.read_text(encoding="utf-8"))

    def test_shared_miss_fails_and_is_written_to_merged_report(self):
        self.report(self.sqlite, "/builder/one", {1: 1, 2: 0})
        self.report(self.postgresql, "/builder/two", {1: 1, 2: 0})
        result = self.run_gate()
        self.assertEqual(result.returncode, 1)
        self.assertIn("src/example.cpp:2", result.stdout)
        self.assertIn("DA:2,0", self.output.read_text(encoding="utf-8"))

    def test_missing_input_fails(self):
        self.report(self.sqlite, "/builder/one", {1: 1})
        result = self.run_gate()
        self.assertEqual(result.returncode, 1)
        self.assertIn("is missing", result.stderr)

    def test_empty_input_fails(self):
        self.report(self.sqlite, "/builder/one", {1: 1})
        self.postgresql.write_text("", encoding="utf-8")
        result = self.run_gate()
        self.assertEqual(result.returncode, 1)
        self.assertIn("is empty", result.stderr)

    def test_unrelated_source_path_fails(self):
        self.report(self.sqlite, "/builder/one", {1: 1})
        self.postgresql.write_text("SF:/external/vendor.cpp\nDA:1,1\nend_of_record\n", encoding="utf-8")
        result = self.run_gate()
        self.assertEqual(result.returncode, 1)
        self.assertIn("outside this checkout", result.stderr)


if __name__ == "__main__":
    unittest.main()
