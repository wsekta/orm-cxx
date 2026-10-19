"""Regression controls for failures that used to pass documentation CI."""

from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import check_docs as docs


class DocumentationChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "docs").mkdir()

    def write(self, name, content):
        target = self.root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")

    def test_missing_file_is_rejected(self):
        self.write("docs/main.md", "[Read this](missing.md)")
        with self.assertRaisesRegex(ValueError, "missing local target"):
            docs.check_links(self.root)

    def test_link_labels_wrapped_across_lines_are_checked(self):
        self.write("docs/main.md", "[Backend\nportability](missing.md#definition-of-done)")
        with self.assertRaisesRegex(ValueError, "missing local target"):
            docs.check_links(self.root)

    def test_missing_anchor_is_rejected(self):
        self.write("docs/main.md", "# Start\n[Read this](#missing)")
        with self.assertRaisesRegex(ValueError, "missing anchor"):
            docs.check_links(self.root)

    def test_code_examples_do_not_define_headings_or_links(self):
        self.write("docs/main.md", "# Start\n```sh\n# Hidden\n[ignore](missing.md)\n```\n[Go](#start)")
        self.assertEqual(docs.check_links(self.root), 1)
        self.assertNotIn("hidden", docs.anchors((self.root / "docs/main.md").read_text()))

    def test_relative_encoded_path_and_duplicate_heading(self):
        self.write("docs/a guide.md", "# Hello, `world`!\n## Hello, world!\n")
        self.write("README.md", "[Go](docs/a%20guide.md#hello-world-1)")
        self.assertEqual(docs.check_links(self.root), 1)

    def test_link_outside_checkout_is_rejected(self):
        self.write("README.md", "[Outside](../README.md)")
        with self.assertRaisesRegex(ValueError, "missing local target"):
            docs.check_links(self.root)

    def test_drifted_quickstart_snippet_is_rejected(self):
        self.write("examples/quickstart/main.cpp", "int main() {}\n")
        self.write("docs/main.md", "<!-- include: examples/quickstart/main.cpp -->\n```cpp\nold code\n```\n")
        with self.assertRaisesRegex(ValueError, "snippet differs"):
            docs.check_snippets(self.root)

    def test_removed_quickstart_marker_is_rejected(self):
        self.write("docs/main.md", "# Quickstart\n")
        with self.assertRaisesRegex(ValueError, "Quickstart snippets are missing"):
            docs.check_snippets(self.root)

    def test_removed_public_symbol_is_rejected(self):
        self.write("xml/index.xml", "<doxygenindex/>")
        with self.assertRaisesRegex(ValueError, "missing public type orm::Database"):
            docs.check_api(self.root / "xml")

    def test_removed_public_method_is_rejected(self):
        self.write("xml/index.xml", '<doxygenindex><compound refid="database"><name>orm::Database</name>'
                   '</compound></doxygenindex>')
        with patch.dict(docs.REQUIRED, {"orm::Database": {"connect"}}, clear=True):
            with self.assertRaisesRegex(ValueError, "missing orm::Database members"):
                docs.check_api(self.root / "xml")

    def test_missing_html_is_rejected_even_with_complete_xml(self):
        self.write("xml/index.xml", '<doxygenindex><compound refid="database"><name>orm::Database</name>'
                   '<member><name>connect</name></member></compound></doxygenindex>')
        with patch.dict(docs.REQUIRED, {"orm::Database": {"connect"}}, clear=True):
            with self.assertRaisesRegex(ValueError, "missing HTML page"):
                docs.check_api(self.root / "xml")

    def test_export_blocks_preserve_namespace_scope_and_ignore_literal_braces(self):
        source = 'export module orm;\nnamespace orm {\nexport {\nclass Database {\n' \
                 'const char* value = "}"; /* } */\n};\n}\n}\n'
        result = docs.public_source(source)
        self.assertNotIn("export", result)
        self.assertIn('namespace orm {\n        \nclass Database', result)
        self.assertTrue(result.endswith("};\n \n}\n"))

    def test_constraint_continuations_do_not_consume_declarations(self):
        source = 'template <typename T>\n    requires T::value &&\n             T::other\nauto get(T value);\n'
        result = docs.public_source(source)
        self.assertIn('//     requires T::value', result)
        self.assertIn('\nauto get(T value);', result)

    def test_broken_generated_link_is_rejected(self):
        self.write("html/index.html", '<a href="guide.md#start">Start</a>')
        with self.assertRaisesRegex(ValueError, "missing generated target"):
            docs.check_html(self.root / "html")

    def test_broken_generated_anchor_is_rejected(self):
        self.write("html/index.html", '<a href="#missing">Start</a>')
        with self.assertRaisesRegex(ValueError, "missing generated anchor"):
            docs.check_html(self.root / "html")

    def test_doxygen_target_name_corruption_is_rejected(self):
        self.write("html/index.html", '<p>orm-cxxreflection</p>')
        with self.assertRaisesRegex(ValueError, "changed the spelling"):
            docs.check_html(self.root / "html")

    def test_generated_pages_with_valid_links_and_target_names_pass(self):
        self.write("html/index.html", '<p>orm-cxx::reflection orm-cxx::orm-cxx</p>'
                   '<a href="guide.html#start">Start</a>')
        self.write("html/guide.html", '<h1 id="start">Start</h1><a href="index.html">Home</a>')
        docs.check_html(self.root / "html")

    def test_only_empty_private_class_summary_navigation_is_removed(self):
        for name in ("classValue.html", "module_8cppm.html"):
            self.write(f"html/{name}", '<div class="summary">\n'
                       '<a href="#nested-classes">Classes</a> &#124;\n</div>'
                       '<p><a href="#missing">Authored link</a></p>')
        docs.fix_hidden_class_navigation(self.root / "html")
        for name in ("classValue.html", "module_8cppm.html"):
            source = (self.root / f"html/{name}").read_text()
            self.assertNotIn('href="#nested-classes"', source)
            self.assertIn('href="#missing"', source)

    def test_real_public_nested_class_navigation_is_preserved(self):
        source = '<div class="summary"><a href="#nested-classes">Classes</a></div>' \
                 '<h2 id="nested-classes">Classes</h2>'
        self.write("html/classValue.html", source)
        docs.fix_hidden_class_navigation(self.root / "html")
        self.assertEqual((self.root / "html/classValue.html").read_text(), source)


if __name__ == "__main__":
    unittest.main()
