"""Regression tests for the no-macro policy, including permitted build flags."""

import importlib.util
from pathlib import Path
import shutil
import subprocess
import unittest
import uuid


spec = importlib.util.spec_from_file_location(
    "check_macros", Path(__file__).resolve().parents[2] / "scripts" / "check-macros.py"
)
macros = importlib.util.module_from_spec(spec)
spec.loader.exec_module(macros)


class MacroPolicyTests(unittest.TestCase):
    def test_definitions_and_include_guards_are_rejected(self):
        for directive in ("#define CUSTOM(x) x", "#undef CUSTOM", "#cmakedefine CUSTOM", "#cmakedefine01 CUSTOM", "#ifndef CUSTOM"):
            with self.subTest(directive=directive):
                self.assertTrue(macros.check_cpp("include/sample.hpp", directive))

    def test_comments_strings_raw_literals_and_concepts_are_ignored(self):
        source = '''// #define COMMENT 1
/* #undef COMMENT */
const char *text = "#define TEXT 1";
const char *raw = R"tag(
#define RAW 1
)tag";
template <typename T> concept ORM_QUERY_VALUE = true;
auto number = 1'000;
'''
        self.assertEqual(macros.check_cpp("include/sample.hpp", source), [])

    def test_definition_after_digit_separator_is_detected(self):
        self.assertTrue(macros.check_cpp("src/sample.cpp", "auto number = 1'000;\n#define BAD 1\n"))

    def test_multiline_directives_and_line_numbers(self):
        source = "#define MULTI(x) \\\n x\n#\\\ndefine OTHER 1\n#undef OTHER\n"
        findings = macros.check_cpp("include/sample.hpp", source)
        self.assertEqual(len(findings), 3)
        self.assertIn(":3:", findings[1])
        self.assertIn(":5:", findings[2])

    def test_compiler_conditions_are_allowed_only_in_existing_adapters(self):
        source = "#if defined(_MSC_VER)\n#elif defined(__clang__) || defined(__GNUC__)\n#endif\n"
        path = "include/orm-cxx/reflection/detail/SignatureParser.hpp"
        self.assertEqual(macros.check_cpp(path, source), [])
        self.assertEqual(len(macros.check_cpp("include/new.hpp", source)), 2)
        self.assertTrue(macros.check_cpp(path, "#if _MSC_VER && CUSTOM\n#endif\n"))

    def test_commented_cmake_commands_are_ignored(self):
        source = '# target_compile_definitions(x PRIVATE BAD)\n#[=[\nadd_definitions(-DBAD)\n]=]\nmessage("target_compile_definitions(x PRIVATE BAD)")\n'
        self.assertEqual(macros.check_cmake("CMakeLists.txt", source), [])

    def test_cmake_bracket_literals_do_not_hide_following_commands(self):
        source = 'message([=[literal # quote " and ) parentheses (]=])\nadd_compile_definitions(BAD)\n'
        findings = macros.check_cmake("CMakeLists.txt", source)
        self.assertEqual(len(findings), 1)
        self.assertIn(":2:", findings[0])

    def test_cmake_macro_commands_are_rejected(self):
        for command in ("target_compile_definitions(x PRIVATE FLAG=1)", "add_compile_definitions(FLAG)", "add_definitions(-DFLAG)"):
            with self.subTest(command=command):
                self.assertTrue(macros.check_cmake("CMakeLists.txt", command))

    def test_cmake_compiler_flags_and_properties_are_rejected(self):
        for command in (
            'target_compile_options(x PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/DFLAG>")',
            'add_compile_options(-D FLAG)',
            'set(CMAKE_CXX_FLAGS "-DFLAG")',
            'set(CMAKE_CXX_FLAGS "-Wall -DFLAG")',
            'list(APPEND CMAKE_CXX_FLAGS_RELEASE "/DFLAG")',
            'set_target_properties(x PROPERTIES COMPILE_DEFINITIONS "FLAG=1")',
            'set_target_properties(x PROPERTIES COMPILE_DEFINITIONS_DEBUG "FLAG=1")',
            'set_source_files_properties(x.cpp PROPERTIES COMPILE_DEFINITIONS "FLAG=1")',
            'set_source_files_properties(x.cpp PROPERTIES COMPILE_DEFINITIONS_RELEASE "FLAG=1")',
            'set_property(TARGET x PROPERTY INTERFACE_COMPILE_DEFINITIONS_CUSTOM "FLAG=1")',
            'set_property(TARGET x PROPERTY INTERFACE_COMPILE_OPTIONS "-DFLAG")',
        ):
            with self.subTest(command=command):
                self.assertTrue(macros.check_cmake("CMakeLists.txt", command))

    def test_cmake_cache_flags_are_not_compiler_definitions(self):
        source = 'execute_process(COMMAND cmake -S . -B build -DORM_CXX_ENABLE_SQLITE_BACKEND=ON)\nset(ORM_CXX_ENABLE_SQLITE_BACKEND ON CACHE BOOL "Backend option")'
        self.assertEqual(macros.check_cmake("CMakeLists.txt", source), [])

    def test_cmake_flag_variables_and_bracket_arguments_are_checked(self):
        for source in (
            'set(flags -DFLAG)\ntarget_compile_options(x PRIVATE ${flags})',
            'set(base_flags /DFLAG)\nset(flags ${base_flags})\nset_property(TARGET x PROPERTY COMPILE_OPTIONS "${flags}")',
            'target_compile_options(x PRIVATE [=[-DFLAG]=])',
        ):
            with self.subTest(source=source):
                self.assertTrue(macros.check_cmake("CMakeLists.txt", source))
        self.assertEqual(macros.check_cmake("CMakeLists.txt", 'set(flags -DCMAKE_OPTION=ON)\nexecute_process(COMMAND cmake ${flags})'), [])

    def test_try_compile_include_flags_are_permitted_and_validated(self):
        source = '''set(orm_cxx_compile_include_flags)
list(APPEND orm_cxx_compile_include_flags "-I${include_directory}")
list(APPEND orm_cxx_compile_include_flags "/I${include_directory}")
try_compile(result SOURCES sample.cpp COMPILE_DEFINITIONS ${orm_cxx_compile_include_flags} -Iheaders OUTPUT_VARIABLE output)
'''
        self.assertEqual(macros.check_cmake("CMakeLists.txt", source), [])
        self.assertTrue(macros.check_cmake("CMakeLists.txt", source.replace('"-I${include_directory}"', '"-DBAD"')))
        self.assertTrue(macros.check_cmake("CMakeLists.txt", 'try_compile(result SOURCES sample.cpp COMPILE_DEFINITIONS BAD=1 OUTPUT_VARIABLE output)'))

    def test_conan_definitions_and_compiler_flags_are_rejected(self):
        for source in (
            'core.defines = ["FLAG=1"]',
            'self.cpp_info.defines.append("FLAG")',
            'core.cxxflags = ["-DFLAG"]',
            'core.cxxflags = ["-Wall -DFLAG"]',
            'core.cflags.append("/DFLAG")',
        ):
            with self.subTest(source=source):
                self.assertTrue(macros.check_conan("packaging/conan/conanfile.py", source))
        self.assertEqual(macros.check_conan("conanfile.py", '# core.defines = ["FLAG"]\noptions = {"with_sqlite3": [True, False]}'), [])

    def test_dependency_and_generated_files_are_excluded(self):
        for path in ("externals/lib/header.hpp", "build/generated.hpp", "build-msvc/header.hpp", "cmake/third_party/tool.cmake", "cmake/cmake-coverage.cmake"):
            with self.subTest(path=path):
                self.assertIsNone(macros.source_kind(path))
        self.assertEqual(macros.source_kind("cmake/BuildConfig.hpp.in"), "cpp")
        self.assertEqual(macros.source_kind("cmake/config.cmake.in"), "cmake")

    def test_git_inventory_checks_unignored_sources_and_skips_dependencies(self):
        test_root = (Path(__file__).resolve().parents[2] / "build/macro-policy-tests").resolve()
        test_root.mkdir(parents=True, exist_ok=True)
        # Python 3.13's private TemporaryDirectory ACL blocks Git's sandbox token
        # on Windows. A workspace directory inherits the ordinary workspace ACL.
        root = test_root / uuid.uuid4().hex
        root.mkdir()
        try:
            subprocess.run(["git", "init", "--quiet", str(root)], check=True)
            (root / "include").mkdir()
            (root / "externals").mkdir()
            (root / "include/sample.hpp").write_text("#define BAD 1\n")
            (root / "externals/header.hpp").write_text("#define EXTERNAL 1\n")
            findings, count = macros.check_repository(root)
            self.assertEqual(count, 1)
            self.assertEqual(len(findings), 1)
            self.assertIn("include/sample.hpp:1:", findings[0])
        finally:
            if root.resolve().parent != test_root:
                raise RuntimeError("Macro-policy test cleanup target escaped its build directory")
            shutil.rmtree(root)


if __name__ == "__main__":
    unittest.main()
