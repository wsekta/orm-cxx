"""Regression controls for failures that used to pass documentation CI."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import check_docs as docs
import setup_docs


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

    def test_native_module_declarations_and_export_blocks_are_preserved(self):
        source = 'export module orm;\nnamespace orm {\nexport {\nclass Database {\n' \
                 'const char* value = "}"; /* } */\n};\n}\n}\n'
        self.assertEqual(docs.documentation_source(source), source)

    def test_corrupt_cached_tool_archive_is_rejected_before_extraction(self):
        self.write("tools/downloads/tool.tar.gz", "corrupt download")
        with patch.object(setup_docs, "TOOLS", self.root / "tools"), \
                patch.object(setup_docs, "urlopen") as network:
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                setup_docs.download("https://example.invalid/tool.tar.gz", "tool.tar.gz", "0" * 64)
            network.assert_not_called()

    def test_missing_named_module_is_rejected(self):
        self.write("xml/index.xml", '<doxygenindex><compound refid="module__orm" kind="module">'
                   '<name>orm</name></compound></doxygenindex>')
        self.write("html/module__orm.html", "Module orm")
        with patch.dict(docs.REQUIRED, {}, clear=True):
            with self.assertRaisesRegex(ValueError, "missing named module orm.reflection"):
                docs.check_api(self.root / "xml")

    def test_generated_module_and_alphabetical_links_use_existing_anchors(self):
        self.write("xml/index.xml", '<doxygenindex><compound refid="module__orm" kind="module">'
                   '<member refid="namespace_orm_1a123"><name>value</name></member>'
                   '</compound></doxygenindex>')
        self.write("html/module__orm.html", '<a href="#classes">Classes</a>'
                   '<h2 id="header-nested-classes">Classes</h2>')
        self.write("html/namespace_orm.html", '<a id="a123"></a>')
        self.write("html/functions_~.html", '<a id="index__7E"></a>')
        self.write("html/functions_rela.html", '<a href="#index_d">D</a>'
                   '<a href="#index_o">O</a><ul><li>Database</li><li>Other</li></ul>')
        self.write("html/doxygen_crawl.html", '<a href="module__orm.html#a123">value</a>'
                   '<a href="functions_~.html#index_~">~</a>'
                   '<a href="module__orm.html#unknown">broken</a>')
        docs.fix_generator_links(self.root / "html", self.root / "xml")
        source = (self.root / "html/doxygen_crawl.html").read_text(encoding="utf-8")
        self.assertIn('href="namespace_orm.html#a123"', source)
        self.assertIn('href="functions_~.html#index__7E"', source)
        self.assertIn('href="module__orm.html#unknown"', source)
        module = (self.root / "html/module__orm.html").read_text(encoding="utf-8")
        self.assertIn('href="#header-nested-classes"', module)
        related = docs.HTMLPage((self.root / "html/functions_rela.html").read_text(encoding="utf-8"))
        self.assertTrue({"index_d", "index_o"} <= related.anchors)

    def test_constraint_continuations_do_not_consume_declarations(self):
        source = 'template <typename T>\n    requires T::value &&\n             T::other\nauto get(T value);\n'
        result = docs.documentation_source(source)
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

    def test_escaped_formatting_outside_code_is_rejected(self):
        for leak in ('@section broken Title', '&lt;strong&gt;Public behavior&lt;/strong&gt;',
                     '&lt;table class="markdownTable"&gt;'):
            with self.subTest(leak=leak):
                self.write("html/index.html", '<p>orm-cxx::reflection orm-cxx::orm-cxx</p>'
                           f'<div class="textblock"><p>{leak}</p></div>')
                with self.assertRaisesRegex(ValueError, "unrendered markup"):
                    docs.check_html(self.root / "html")

    def test_literal_markup_in_code_is_preserved(self):
        self.write("html/index.html", '<p>orm-cxx::reflection orm-cxx::orm-cxx</p>'
                   '<code>&lt;strong&gt; @section example</code>'
                   '<div class="fragment"><div class="line">&lt;table&gt;</div></div>')
        docs.check_html(self.root / "html")

    def test_rendered_guides_preserve_headings_tables_and_code(self):
        self.write("xml/index.xml", '<doxygenindex><compound kind="page" refid="_contributing">'
                   '<name>CONTRIBUTING</name></compound></doxygenindex>')
        self.write("input/docs/main.md", '# Home {#home}\n\n## Example {#example}\n\n'
                   '| Feature | Detail |\n| --- | --- |\n| Keys | DDL |\n\n'
                   '```cpp\nimport orm;\nimport orm.reflection;\n```\n')
        for name in ("CONTRIBUTING", "ROADMAP"):
            self.write(f"input/{name}.md", f"# {name} {{#{name}}}\n")
            filename = "_contributing" if name == "CONTRIBUTING" else name
            self.write(f"html/{filename}.html", f'<a id="{name}"></a>')
        source = '<a id="home"></a><h1><a id="example"></a>Example</h1>' \
                 '<table class="markdownTable"><tr><td>Keys</td><td>DDL</td></tr></table>' \
                 '<div class="fragment"><div class="line">import orm;</div>' \
                 '<div class="line">import orm.reflection;</div>' \
                 '<div class="ttc">Hidden symbol tooltip</div></div>'
        self.write("html/index.html", source)
        docs.check_rendered_markdown(self.root / "input", self.root / "html")
        for broken, error in ((source.replace('h1>', 'p>'), "missing rendered headings"),
                              (source.replace('markdownTable', 'broken'), "rendered tables"),
                              (source.replace('import orm.reflection;', ''), "rendered code blocks")):
            with self.subTest(error=error):
                self.write("html/index.html", broken)
                with self.assertRaisesRegex(ValueError, error):
                    docs.check_rendered_markdown(self.root / "input", self.root / "html")

    @unittest.skipUnless(docs.doxygen_binary().is_file(), "run python scripts/setup_docs.py")
    def test_wrapped_link_label_does_not_corrupt_following_markdown(self):
        # Exercise the same staged Markdown and real Doxygen parser as publishing.
        self.write("modules/test.cppm", 'export module test;\nnamespace orm {\nexport {\n'
                   'class Value { public: int get() const; };\n}\n}\n')
        for name in ("SqliteBackendDeclarations", "PostgresqlBackendDeclarations",
                     "MysqlBackendDeclarations", "MariadbBackendDeclarations",
                     "BuildConfig", "BackendRelations"):
            self.write(f"cmake/{name}.cppm.in", "")
        self.write("docs/guide.md", "# Guide\n")
        self.write("docs/main.md", '# Home\n\nSee [Backend extension\ncontract](guide.md).\n\n'
                   '`orm-cxx::orm-cxx` and `orm-cxx::reflection`.\n\n'
                   '## Layers\n\n1. **Public behavior** covers CRUD.\n\n'
                   '| Feature | Detail |\n| --- | --- |\n| Keys | DDL |\n\n'
                   '```cpp\nimport orm;\nimport orm.reflection;\n// [leave](guide.md)\n```\n')
        stage = docs.prepare_inputs(self.root, self.root / "work")
        result = subprocess.run([str(docs.doxygen_binary()), "-"], cwd=stage, text=True,
                                input='INPUT = docs modules\nRECURSIVE = YES\n'
                                'FILE_PATTERNS = *.md *.cppm\nEXTENSION_MAPPING = cppm=C++\n'
                                'GENERATE_XML = YES\nEXTRACT_ALL = YES\n'
                                'USE_MDFILE_AS_MAINPAGE = docs/main.md\n'
                                'GENERATE_LATEX = NO\nQUIET = YES\n',
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, 0, result.stdout)
        source = (stage / "html/index.html").read_text(encoding="utf-8")
        page = docs.HTMLPage(source)
        self.assertNotIn("@section", "".join(page.text))
        self.assertIn('id="docs-main-layers"', source)
        self.assertIn('<table class="markdownTable">', source)
        self.assertIn('Public behavior', "".join(page.text))
        self.assertIn('<li>', source)
        self.assertIn('Backend extension contract</a>', source.replace("\n", ""))
        self.assertIn('// [leave](guide.md)', "".join(page.text))
        self.assertIn('orm-cxx::orm-cxx', "".join(page.text))
        self.assertIn('orm-cxx::reflection', "".join(page.text))
        compounds = {item.findtext("name"): item for item in
                     docs.ET.parse(stage / "xml/index.xml").getroot().findall("compound")}
        self.assertEqual(compounds["test"].get("kind"), "module")
        self.assertEqual(compounds["orm::Value"].get("kind"), "class")
        self.assertIn("get", [item.findtext("name") for item in compounds["orm::Value"].findall("member")])


if __name__ == "__main__":
    unittest.main()
