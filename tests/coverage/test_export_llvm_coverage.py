"""Safeguards for LLVM's uninstrumented implicit-member coverage maps."""

import importlib.util
from pathlib import Path
import shutil
import unittest
from uuid import uuid4


SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "export_llvm_coverage.py"
SPEC = importlib.util.spec_from_file_location("export_llvm_coverage", SCRIPT)
EXPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORT)


class ImplicitCoverageTests(unittest.TestCase):
    def setUp(self):
        scratch = Path(__file__).resolve().parents[2] / "build" / "coverage-script-tests"
        self.root = scratch / str(uuid4())
        (self.root / "modules").mkdir(parents=True)
        self.addCleanup(shutil.rmtree, self.root)
        self.source = self.root / "modules" / "example.cppm"
        self.source.write_text("class Predicate\nclass AggregatePredicate\n    struct JunctionView\n", encoding="utf-8")

    def function(self, name="implicit", line=1, start=7, end=16, count=0, regions=None, source=None):
        return {"name": name, "count": count, "filenames": [str(source or self.source)],
                "regions": regions or [[line, start, line, end, count, 0, 0, 0]]}

    def normalized(self, functions, profile_names=None, hits=None):
        discarded, candidates = EXPORT.implicit_maps({"data": [{"functions": functions}]},
                                                    profile_names or {"instrumented"}, self.root)
        raw = f"TN:\nSF:{self.source}\n"
        for function in functions:
            raw += f"FN:{function['regions'][0][0]},{function['name']}\nFNDA:{function['count']},{function['name']}\n"
        for line, count in (hits or {1: 0, 2: 0, 3: 0}).items():
            raw += f"DA:{line},{count}\n"
        raw += "FNF:99\nFNH:99\nLF:99\nLH:99\nend_of_record\n"
        output, removed = EXPORT.normalize_lcov(raw, discarded, candidates)
        return output, removed, discarded

    def test_three_actual_class_name_maps_are_removed_without_instrumentation(self):
        functions = [self.function("predicate-copy"), self.function("aggregate-copy", 2, 7, 25),
                     self.function("junction-move", 3, 12, 24)]
        output, removed, discarded = self.normalized(functions)
        self.assertEqual(removed, 3)
        self.assertEqual(len(discarded), 3)
        self.assertNotIn("DA:", output)
        self.assertNotIn("FN:", output)
        self.assertIn("FNF:0\nFNH:0\nLF:0\nLH:0\n", output)

    def test_zero_hit_instrumented_member_remains_a_failure(self):
        output, removed, discarded = self.normalized([self.function()], {"implicit"})
        self.assertEqual((removed, discarded), (0, set()))
        self.assertIn("FNDA:0,implicit", output)
        self.assertIn("DA:1,0", output)

    def test_actual_method_body_is_never_removed(self):
        self.source.write_text("class Predicate\n{\n    void run() {}\n};\n", encoding="utf-8")
        output, removed, discarded = self.normalized([self.function(line=3, start=5, end=18)])
        self.assertEqual((removed, discarded), (0, set()))
        self.assertIn("DA:3,0", output)

    def test_regions_extending_beyond_the_declaration_remain(self):
        _, removed, discarded = self.normalized([self.function(regions=[[1, 7, 2, 25, 0, 0, 0, 0]])])
        self.assertEqual((removed, discarded), (0, set()))

    def test_additional_body_region_prevents_normalization(self):
        _, removed, discarded = self.normalized([self.function(regions=[[1, 7, 1, 16, 0, 0, 0, 0],
                                                                       [2, 7, 2, 26, 0, 0, 0, 0]])])
        self.assertEqual((removed, discarded), (0, set()))

    def test_non_module_source_is_never_normalized(self):
        other = self.source.with_suffix(".cpp")
        other.write_text("class Predicate\n", encoding="utf-8")
        _, removed, discarded = self.normalized([self.function(source=other)])
        self.assertEqual((removed, discarded), (0, set()))

    def test_module_outside_project_is_never_normalized(self):
        other = self.root / "external.cppm"
        other.write_text("class Predicate\n", encoding="utf-8")
        _, removed, discarded = self.normalized([self.function(source=other)])
        self.assertEqual((removed, discarded), (0, set()))

    def test_non_exact_identifier_span_remains(self):
        _, removed, discarded = self.normalized([self.function(start=1)])
        self.assertEqual((removed, discarded), (0, set()))

    def test_declaration_with_an_inline_body_remains(self):
        self.source.write_text("class Predicate { void run() {} };\n", encoding="utf-8")
        _, removed, discarded = self.normalized([self.function()])
        self.assertEqual((removed, discarded), (0, set()))

    def test_real_region_overlapping_candidate_keeps_zero_line(self):
        real = self.function("explicit-function", regions=[[1, 1, 2, 25, 0, 0, 0, 0]])
        output, removed, discarded = self.normalized([self.function(), real], {"explicit-function"})
        self.assertEqual((removed, discarded), (0, {"implicit"}))
        self.assertIn("DA:1,0", output)
        self.assertIn("FNDA:0,explicit-function", output)

    def test_positive_line_counts_are_preserved(self):
        output, removed, _ = self.normalized([self.function()], hits={1: 4})
        self.assertEqual(removed, 0)
        self.assertIn("DA:1,4", output)
        self.assertIn("LF:1\nLH:1", output)

    def test_positive_function_counts_are_never_normalized(self):
        _, removed, discarded = self.normalized([self.function(count=1)])
        self.assertEqual((removed, discarded), (0, set()))

    def test_gap_region_is_never_normalized(self):
        _, removed, discarded = self.normalized([self.function(regions=[[1, 7, 1, 16, 0, 0, 0, 3]])])
        self.assertEqual((removed, discarded), (0, set()))

    def test_same_name_with_real_mapping_is_never_removed(self):
        implicit = self.function()
        real = self.function(regions=[[2, 1, 2, 25, 0, 0, 0, 0]])
        output, removed, discarded = self.normalized([implicit, real])
        self.assertEqual((removed, discarded), (0, set()))
        self.assertIn("FNDA:0,implicit", output)
        self.assertIn("DA:1,0", output)

    def test_llvm18_profile_records_include_zero_function_counts(self):
        text = "Counters:\n  unused-function:\n    Hash: 0x0000000000000000\n    Counters: 1\n    Function count: 0\n"
        text += "Functions shown: 1\nTotal functions: 1\n"
        self.assertEqual(EXPORT.instrumented_functions(text), {"unused-function"})

    def test_partial_profile_records_fail_closed(self):
        text = "Counters:\n  recognized:\n    Hash: 0x0000000000000000\n"
        text += "    unrecognized:\n      Hash: 0x0000000000000000\nFunctions shown: 2\nTotal functions: 2\n"
        with self.assertRaises(ValueError):
            EXPORT.instrumented_functions(text)

    def test_filtered_profile_records_fail_closed(self):
        text = "Counters:\n  recognized:\n    Hash: 0x0000000000000000\nFunctions shown: 1\nTotal functions: 2\n"
        with self.assertRaises(ValueError):
            EXPORT.instrumented_functions(text)

    def test_unrecognized_profile_format_fails_closed(self):
        with self.assertRaises(ValueError):
            EXPORT.instrumented_functions("No matching LLVM profdata records\n")


if __name__ == "__main__":
    unittest.main()
