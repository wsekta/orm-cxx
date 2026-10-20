"""Validate Markdown, runnable snippets and the generated public API reference.

Requires Python 3.12+, Doxygen 1.9.8+ and Graphviz. No C++ build or database
dependencies are needed. All generated files stay in build/docs.
"""

import argparse
from collections import Counter
from html import unescape
from html.parser import HTMLParser
from pathlib import Path
import posixpath
import re
import shutil
import subprocess
import sys
from urllib.parse import unquote, urlsplit
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"\[[^\]]*\]\(([^\s)]+)\)")
INCLUDE = re.compile(r"<!-- include: ([^\n]+) -->\s*```[^\n]*\n(.*?)\n```", re.S)
FENCED = re.compile(r"^([ \t]*)(`{3,}|~{3,})[^\n]*\n(.*?)^\1\2[ \t]*$", re.M | re.S)
REQUIRED = {
    "orm::Database": {"connect", "orm", "beginTransaction"},
    "orm::OrmContext": {"select", "insert", "update", "remove"},
    "orm::Query": {"where"},
    "orm::ProjectionQuery": {"project"},
    "orm::migrations::MigrationProject": {"compare", "make", "finalize"},
    "orm::reflection::FixedString": set(),
    **{f"orm::db::{name.lower()}::{name}Backend": {"capabilities"}
       for name in ("Sqlite", "Postgresql", "Mysql", "Mariadb")},
}


def markdown_files(root):
    return sorted(root.glob("*.md")) + sorted((root / "docs").glob("*.md"))


def prose(source):
    """Remove fenced examples so their comments aren't mistaken for headings."""
    return FENCED.sub("", source)


def anchors(source):
    result = set()
    counts = Counter()
    for line in prose(source).splitlines():
        heading = re.match(r"^#{1,6}\s+(.+?)\s*#*\s*$", line)
        if heading:
            title = re.sub(r"<[^>]*>", "", heading[1])
            title = re.sub(r"\[([^]]+)\]\([^)]*\)", r"\1", title)
            slug = re.sub(r"[^\w\- ]", "", unescape(title).lower()).replace(" ", "-")
            number = counts[slug]
            counts[slug] += 1
            result.add(slug + (f"-{number}" if number else ""))
    result.update(re.findall(r'<a\s+(?:id|name)=["\']([^"\']+)', source))
    return result


def check_links(root):
    checked = 0
    for path in markdown_files(root):
        source = path.read_text(encoding="utf-8")
        for match in LINK.finditer(prose(source)):
            url = urlsplit(match[1])
            if url.scheme or url.netloc:
                continue
            target = (path.parent / unquote(url.path)).resolve() if url.path else path
            if not target.is_relative_to(root.resolve()) or not target.exists():
                raise ValueError(f"{path.relative_to(root)}: missing local target {match[1]}")
            if url.fragment and target.suffix == ".md":
                if unquote(url.fragment) not in anchors(target.read_text(encoding="utf-8")):
                    raise ValueError(f"{path.relative_to(root)}: missing anchor {match[1]}")
            checked += 1
    return checked


def check_snippets(root):
    sources = set()
    for path in markdown_files(root):
        for name, snippet in INCLUDE.findall(path.read_text(encoding="utf-8")):
            target = (root / name).resolve()
            if not target.is_relative_to(root.resolve()) or not target.is_file():
                raise ValueError(f"{path.name}: missing snippet source {name}")
            if snippet.rstrip() != target.read_text(encoding="utf-8").rstrip():
                raise ValueError(f"{path.name}: snippet differs from {name}")
            sources.add(name)
    expected = {f"examples/quickstart/{name}" for name in ("main.cpp", "CMakeLists.txt", "expected.txt")}
    if not expected <= sources:
        raise ValueError(f"Quickstart snippets are missing: {sorted(expected - sources)}")
    return len(sources)


def public_source(source):
    """Present module declarations as C++ to Doxygen 1.9.8's pre-module parser.

    This documentation-only view preserves declarations and their comments.
    It is never installed, compiled, or used as a public-header fallback.
    """
    # Unwrap export blocks without changing namespace or class scope. Braces in
    # comments and literals must not influence the matching closing brace.
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|R"[^\n(]{0,16}\(.*?\)[^\n"]{0,16}"|'
                        r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\\n])\'', re.S)
    masked = tokens.sub(lambda m: re.sub(r"[^\n]", " ", m[0]), source)
    characters = list(source)
    for block in re.finditer(r"\bexport\s*\{", masked):
        opening = block.end() - 1
        depth = 1
        closing = opening + 1
        while closing < len(masked) and depth:
            depth += (masked[closing] == "{") - (masked[closing] == "}")
            closing += 1
        if depth:
            raise ValueError("Unbalanced C++ export block in documentation input")
        for index in range(block.start(), opening + 1):
            if characters[index] != "\n":
                characters[index] = " "
        characters[closing - 1] = " "
    source = "".join(characters)
    # Older Doxygen confuses dependent requires-clauses with data members.
    # Keep them as source comments rather than inventing unconstrained API docs.
    lines = source.splitlines(keepends=True)
    constraint_indent = None
    for index, line in enumerate(lines):
        indent = len(line) - len(line.lstrip())
        if re.match(r"\s*requires\b", line):
            constraint_indent = indent
        elif line.strip() and constraint_indent is not None and indent <= constraint_indent:
            constraint_indent = None
        if constraint_indent is not None:
            lines[index] = "// " + line
    source = "".join(lines)
    source = re.sub(r"^(?:export\s+)?(?:module|import)\b[^;]*;", "", source, flags=re.M)
    return re.sub(r"^(\s*)export\s+", r"\1", source, flags=re.M)


def prepare_inputs(root, work):
    stage = work / "input"
    # Never reuse a source view: deleted declarations must disappear on reruns.
    if stage.is_symlink() or stage.resolve().parent != work.resolve():
        raise ValueError("Documentation input directory must stay inside its output directory")
    if stage.exists():
        shutil.rmtree(stage)
    for path in (root / "modules").rglob("*.cppm"):
        relative = path.relative_to(root)
        target = stage / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(public_source(path.read_text(encoding="utf-8")), encoding="utf-8")
    replacements = {}
    for name in ("Sqlite", "Postgresql", "Mysql", "Mariadb"):
        replacements[f"ORM_CXX_{name.upper()}_BACKEND_ENABLED"] = "true"
        replacements[f"ORM_CXX_{name.upper()}_BACKEND_DECLARATIONS"] = (
            root / f"cmake/{name}BackendDeclarations.cppm.in").read_text(encoding="utf-8")
    for name in ("BuildConfig", "BackendRelations"):
        source = (root / f"cmake/{name}.cppm.in").read_text(encoding="utf-8")
        source = re.sub(r"@([A-Z_]+)@", lambda m: replacements[m[1]], source)
        (stage / f"modules/{name}.cppm").write_text(public_source(source), encoding="utf-8")
    for path in markdown_files(root):
        source = path.read_text(encoding="utf-8")

        def source_link(match):
            url = urlsplit(match[1])
            # Doxygen 1.9.8 turns wrapped @ref labels into an unterminated
            # quoted argument, exposing subsequent sections as literal markup.
            label = re.sub(r"\s+", " ", match[0][1:match[0].rfind("](")])
            link = f"[{label}]({match[1]})"
            if url.scheme or url.netloc:
                return link
            target = (path.parent / unquote(url.path)).resolve() if url.path else path
            if target.suffix == ".md":
                prefix = target.relative_to(root.resolve()).with_suffix("").as_posix().replace("/", "-")
                heading = re.search(r"^#\s+(.+)$", target.read_text(encoding="utf-8"), re.M)
                fragment = url.fragment or next(iter(anchors(heading[0])))
                return f"[{label}](@ref {prefix}-{fragment})"
            relative = target.relative_to(root.resolve()).as_posix()
            return f"[{label}](https://github.com/wsekta/orm-cxx/blob/main/{relative})"

        target = stage / path.relative_to(root)
        target.parent.mkdir(parents=True, exist_ok=True)
        # Doxygen's automatic heading IDs differ from GitHub's Markdown slugs.
        # Give headings unique explicit IDs in this generated view only.
        # Rewrite prose links only; examples must retain their literal content.
        pieces = []
        end = 0
        for block in FENCED.finditer(source):
            pieces.extend((LINK.sub(source_link, source[end:block.start()]), block[0]))
            end = block.end()
        pieces.append(LINK.sub(source_link, source[end:]))
        source = "".join(pieces)
        prefix = path.relative_to(root).with_suffix("").as_posix().replace("/", "-")
        counts = Counter()
        fence = None
        lines = []
        for line in source.splitlines():
            marker = re.match(r"\s*(`{3,}|~{3,})", line)
            if marker:
                fence = None if fence == marker[1] else marker[1]
            if not fence:
                # Doxygen treats ::reflection as an explicit link even inside
                # inline code, stripping :: when it cannot resolve the name.
                line = line.replace("orm-cxx::", "orm-cxx::%")
            if not fence and re.match(r"^#{1,6}\s+", line):
                slug = next(iter(anchors(line)))
                number = counts[slug]
                counts[slug] += 1
                slug += f"-{number}" if number else ""
                line += f" {{#{prefix}-{slug}}}"
            lines.append(line)
        target.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return stage


class HTMLPage(HTMLParser):
    def __init__(self, source):
        super().__init__()
        self.anchors = set()
        self.links = []
        self.text = []
        self.prose = []
        self.heading_anchors = set()
        self.tables = 0
        self.codeblocks = []
        self.stack = []
        self.feed(source)

    def handle_starttag(self, tag, attributes):
        attrs = dict(attributes)
        classes = attrs.get("class", "").split()
        block = [] if (tag == "pre" or "fragment" in classes) and not any(
            item[2] is not None for item in self.stack) else None
        if block is not None:
            self.codeblocks.append(block)
        if tag == "table" and "markdownTable" in classes:
            self.tables += 1
        if re.fullmatch(r"h[1-6]", tag) or any(
                re.fullmatch(r"h[1-6]", item[0]) for item in self.stack):
            self.heading_anchors.update(attrs[key] for key in ("id", "name") if attrs.get(key))
        self.anchors.update(attrs[key] for key in ("id", "name") if attrs.get(key))
        if attrs.get("href"):
            self.links.append(attrs["href"])
        if tag not in {"area", "base", "br", "col", "embed", "hr", "img", "input",
                       "link", "meta", "param", "source", "track", "wbr"}:
            self.stack.append((tag, classes, block))

    def handle_endtag(self, tag):
        for index in range(len(self.stack) - 1, -1, -1):
            if self.stack[index][0] == tag:
                del self.stack[index:]
                break

    def handle_data(self, data):
        self.text.append(data)
        for _, _, block in self.stack:
            # Doxygen appends hidden symbol tooltips inside fragment containers.
            if block is not None and not any("ttc" in classes for _, classes, _ in self.stack):
                block.append(data)
        if not any(tag in {"code", "pre", "script", "style"} or "fragment" in classes
                   for tag, classes, _ in self.stack):
            self.prose.append(data)


def check_html(directory):
    files = {path.relative_to(directory).as_posix(): path
             for path in directory.rglob("*") if path.is_file()}
    pages = {name: HTMLPage(path.read_text(encoding="utf-8"))
             for name, path in files.items() if name.endswith(".html")}
    for name, page in pages.items():
        if re.search(r"(?:@|\\)(?:section|subsection|ref)\s|"
                     r"</?(?:strong|table|tr|th|td|tt|ul|ol|li|h[1-6])(?:\s[^>]*)?>",
                     "".join(page.prose)):
            raise ValueError(f"{name}: unrendered markup in visible text")
        for link in page.links:
            url = urlsplit(link)
            if url.scheme or url.netloc:
                continue
            target = posixpath.normpath(posixpath.join(posixpath.dirname(name), unquote(url.path))) \
                if url.path else name
            if target not in files:
                raise ValueError(f"{name}: missing generated target {link}")
            if url.fragment and target in pages and unquote(url.fragment) not in pages[target].anchors:
                raise ValueError(f"{name}: missing generated anchor {link}")
    index = pages.get("index.html")
    if index is None:
        raise ValueError("Doxygen did not generate html/index.html")
    for name in ("orm-cxx::orm-cxx", "orm-cxx::reflection"):
        if name not in "".join(index.text):
            raise ValueError(f"Doxygen changed the spelling of {name} in the main page")


def check_rendered_markdown(stage, directory):
    """Check every guide's structure and examples, not just linked anchors."""
    pages = {path.name: HTMLPage(path.read_text(encoding="utf-8"))
             for path in directory.glob("*.html")}
    for path in [*sorted((stage / "docs").glob("*.md")),
                 stage / "CONTRIBUTING.md", stage / "ROADMAP.md"]:
        source = path.read_text(encoding="utf-8")
        heading_ids = re.findall(r"^#{1,6} .*?\{#([^}]+)\}", prose(source), re.M)
        if not heading_ids:
            raise ValueError(f"{path.name}: missing page title")
        name = "index.html" if path == stage / "docs/main.md" else heading_ids[0] + ".html"
        page = pages.get(name)
        if page is None:
            raise ValueError(f"{path.name}: missing rendered guide")
        missing = set(heading_ids[1:]) - page.heading_anchors
        if missing:
            raise ValueError(f"{path.name}: missing rendered headings {sorted(missing)}")
        tables = len(re.findall(r"^\s*\|?\s*:?-{3,}:?\s*\|.*$", prose(source), re.M))
        if page.tables != tables:
            raise ValueError(f"{path.name}: expected {tables} rendered tables, got {page.tables}")
        expected = [re.sub(r"\s+", "", block[3]) for block in FENCED.finditer(source)]
        actual = [re.sub(r"\s+", "", "".join(block)) for block in page.codeblocks]
        if expected != actual:
            raise ValueError(f"{path.name}: rendered code blocks differ from Markdown examples")


def fix_hidden_class_navigation(directory):
    # Doxygen 1.9.8 emits a Classes summary link even when EXTRACT_PRIVATE=NO
    # or EXCLUDE_SYMBOLS hides that entire section. Remove only that empty
    # summary item, never authored links or real public nested-class sections.
    for path in directory.glob("*.html"):
        source = path.read_text(encoding="utf-8")
        if 'href="#nested-classes"' in source and "nested-classes" not in HTMLPage(source).anchors:
            updated = re.sub(r'(<div class="summary">\s*)<a href="#nested-classes">Classes</a>\s*(?:&#124;)?',
                             r'\1', source)
            if updated != source:
                path.write_text(updated, encoding="utf-8")


def check_api(xml):
    index = ET.parse(xml / "index.xml").getroot()
    compounds = {item.findtext("name"): item for item in index.findall("compound")}
    for name, members in REQUIRED.items():
        compound = compounds.get(name)
        if compound is None:
            raise ValueError(f"Doxygen reference is missing public type {name}")
        declared = {member.findtext("name") for member in compound.findall("member")}
        if members - declared:
            raise ValueError(f"Doxygen reference is missing {name} members: {sorted(members - declared)}")
        page = xml.parent / "html" / (compound.attrib["refid"] + ".html")
        if not page.is_file():
            raise ValueError(f"Doxygen reference is missing HTML page for {name}")


def generate(root, work):
    # A clean generated tree prevents stale pages from surviving a rename.
    for name in ("html", "xml"):
        directory = work / name
        if directory.is_symlink() or directory.resolve().parent != work.resolve():
            raise ValueError("Generated documentation must stay inside its output directory")
        if directory.exists():
            shutil.rmtree(directory)
    stage = prepare_inputs(root, work)
    config = (root / "Doxyfile").read_text(encoding="utf-8")
    styles = [root / "doxygen-awesome-css" / name for name in
              ("doxygen-awesome.css", "doxygen-awesome-sidebar-only.css")]
    style_config = " ".join(f'"{p.as_posix()}"' for p in styles if p.is_file())
    overrides = {
        "OUTPUT_DIRECTORY": f'"{work.as_posix()}"',
        "INPUT": '"docs" "modules" "CONTRIBUTING.md" "ROADMAP.md"',
        "USE_MDFILE_AS_MAINPAGE": '"docs/main.md"',
        "WARN_LOGFILE": f'"{(work / "warnings.log").as_posix()}"',
        "HTML_EXTRA_STYLESHEET": style_config,
        "QUIET": "YES",
    }
    config += "\n" + "\n".join(f"{key} = {value}" for key, value in overrides.items()) + "\n"
    result = subprocess.run(["doxygen", "-"], input=config, text=True, cwd=stage,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    warnings = work / "warnings.log"
    diagnostics = warnings.read_text(encoding="utf-8") if warnings.exists() else ""
    if result.returncode or diagnostics.strip() or re.search(r"\b(?:warning|error):", result.stdout, re.I):
        raise ValueError(f"Doxygen generation failed:\n{result.stdout}\n{diagnostics}")
    if not (work / "html/index.html").is_file():
        raise ValueError("Doxygen did not generate html/index.html")
    check_api(work / "xml")
    fix_hidden_class_navigation(work / "html")
    check_html(work / "html")
    check_rendered_markdown(stage, work / "html")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/docs")
    args = parser.parse_args()
    try:
        links = check_links(ROOT)
        snippets = check_snippets(ROOT)
        if not shutil.which("doxygen"):
            raise ValueError("Install Doxygen 1.9.8+ and Graphviz before running this command")
        work = args.output.resolve()
        work.mkdir(parents=True, exist_ok=True)
        generate(ROOT, work)
        print(f"Documentation OK: {links} local links, {snippets} snippets, {len(REQUIRED)} API types")
        print(f"HTML: {work / 'html/index.html'}")
    except (ValueError, OSError, ET.ParseError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
