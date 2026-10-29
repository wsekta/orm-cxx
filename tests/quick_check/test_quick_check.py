import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "quick-check.py"
SPEC = importlib.util.spec_from_file_location("quick_check", SCRIPT)
quick_check = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(quick_check)


class QuickCheckTest(unittest.TestCase):
    def test_changed_file_selection_includes_untracked_and_excludes_deleted_and_third_party(self):
        with tempfile.TemporaryDirectory(dir=SCRIPT.parents[1]) as directory:
            root = Path(directory).resolve()
            self.assertEqual(root.parent, SCRIPT.parents[1].resolve())
            for name in ("modules/a.cppm", "externals/lib/a.cpp", "cmake/third_party/a.cmake", "new/CMakeLists.txt"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("test\n", encoding="utf-8")
            tracked = ["modules/a.cppm", "externals/lib/a.cpp", "deleted.cpp", "cmake/third_party/a.cmake"]
            untracked = ["new/CMakeLists.txt", "modules/a.cppm"]
            with mock.patch.object(quick_check, "ROOT", root), mock.patch.object(
                quick_check, "git_paths", side_effect=[[], [], tracked, untracked]
            ):
                paths, cpp, cmake_files = quick_check.changed_files("base")
        self.assertNotIn("deleted.cpp", paths)
        self.assertEqual(cpp, ["modules/a.cppm"])
        self.assertEqual(cmake_files, ["new/CMakeLists.txt"])

    def test_partially_staged_file_is_rejected(self):
        with mock.patch.object(quick_check, "git_paths", side_effect=[["a.cpp"], ["a.cpp"]]):
            with self.assertRaisesRegex(ValueError, "partially staged"):
                quick_check.changed_files("base")

    def test_failed_command_and_skipped_check_have_distinct_statuses(self):
        report = quick_check.Report()
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertFalse(report.run("broken", [sys.executable, "-c", "import sys; sys.exit(3)"]))
            report.skip("missing", "tool unavailable")
        self.assertIn("FAIL broken", output.getvalue())
        self.assertIn("SKIP missing", output.getvalue())
        self.assertEqual(report.failed, ["broken"])
        self.assertEqual(report.skipped, ["missing"])

    def test_default_compiler_preset_selection(self):
        def available(*commands):
            return lambda name: name if name in commands else None

        self.assertEqual(quick_check.default_preset("nt", available()), "msvc-debug")
        self.assertEqual(quick_check.default_preset("posix", available("clang++-18", "g++-15")), "linux-clang-debug")
        self.assertEqual(quick_check.default_preset("posix", available("g++-15")), "linux-gcc-debug")
        self.assertIsNone(quick_check.default_preset("posix", available()))

    def test_missing_compiler_skips_build_and_selected_tests(self):
        report = quick_check.Report()
        args = argparse.Namespace(preset="msvc-debug", test_regex="Example.*")
        with mock.patch.object(quick_check.shutil, "which", side_effect=lambda name: name if name != "cl" else None), \
             mock.patch.object(quick_check, "minimum_version", return_value=True), \
             mock.patch.object(report, "run") as run, contextlib.redirect_stdout(io.StringIO()):
            quick_check.build(report, args, True)
        run.assert_not_called()
        self.assertEqual(report.skipped, ["build", "selected tests"])
        self.assertEqual(report.failed, [])

    def test_derived_presets_resolve_dependencies_and_compiler(self):
        compiler, paths = quick_check.preset_requirements("msvc-postgresql-debug")
        self.assertEqual(compiler, "cl")
        self.assertIn("externals/googletest/CMakeLists.txt", paths)
        self.assertIn("externals/soci/CMakeLists.txt", paths)
        self.assertIn("externals/vcpkg/scripts/buildsystems/vcpkg.cmake", paths)
        compiler, paths = quick_check.preset_requirements("linux-clang-19-postgresql")
        self.assertEqual(compiler, "clang++-19")
        self.assertIn("externals/googletest/CMakeLists.txt", paths)

    def test_preset_overrides_disable_unneeded_dependencies(self):
        data = {"configurePresets": [
            {"name": "base", "cacheVariables": {"CMAKE_CXX_COMPILER": "g++-15", "ORM_CXX_BUILD_TESTS": True}},
            {"name": "custom", "inherits": "base", "cacheVariables": {
                "ORM_CXX_BUILD_TESTS": False, "ORM_CXX_USE_SYSTEM_SOCI": True}},
        ]}
        with tempfile.TemporaryDirectory(dir=SCRIPT.parents[1]) as directory:
            root = Path(directory).resolve()
            self.assertEqual(root.parent, SCRIPT.parents[1].resolve())
            (root / "CMakePresets.json").write_text(json.dumps(data), encoding="utf-8")
            with mock.patch.object(quick_check, "ROOT", root):
                self.assertEqual(quick_check.preset_requirements("custom"), ("g++-15", []))

    def test_failed_diff_stops_processing_and_returns_nonzero(self):
        with mock.patch.object(sys, "argv", ["quick-check.py"]), \
             mock.patch.object(quick_check.Report, "run", return_value=False), \
             mock.patch.object(quick_check, "changed_files") as discover:
            self.assertEqual(quick_check.main(), 1)
        discover.assert_not_called()


if __name__ == "__main__":
    unittest.main()
