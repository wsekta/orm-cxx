#!/usr/bin/env python3
"""Enforce the first-party no-macro policy without scanning dependencies."""

import argparse
import ast
from bisect import bisect_right
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
CPP_SUFFIXES = {".c", ".cc", ".cpp", ".cppm", ".ixx", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inc", ".ipp", ".tpp"}
COMPILER_CONDITIONS = {
    "modules/orm.reflection.cppm": {"_MSC_VER", "__clang__", "__GNUC__"},
    "include/orm-cxx/reflection/Reflection.hpp": {"__clang__"},
    "include/orm-cxx/reflection/MemberName.hpp": {"_MSC_VER"},
    "include/orm-cxx/reflection/detail/SignatureParser.hpp": {"_MSC_VER", "__clang__", "__GNUC__"},
    "tests/static_plan_allocations/main.cpp": {"_MSC_VER"},
    "tests/database/ProjectionQueryTest.cpp": {"__SIZEOF_INT128__"},
}
CPP_NON_CODE = re.compile(
    r'R"(?P<delimiter>[^\s()\\]{0,16})\([\s\S]*?\)(?P=delimiter)"'
    r'|"(?:\\[\s\S]|[^"\\\n])*"'
    r"|'(?:\\[\s\S]|[^'\\\n])*'"
    r"|//[^\n]*|/\*[\s\S]*?\*/"
)
DIRECTIVE = re.compile(r"^[ \t]*#[ \t]*(\w+)\b([^\n]*)", re.MULTILINE)
IDENTIFIER = re.compile(r"\b[A-Za-z_]\w*\b")
DEFINE_FLAG = re.compile(r"(?:^|[\s;:])(?:-D|/D)(?:\s*(?:[A-Za-z_]\w*|\$\{[^}]+\})|$)")
CMAKE_TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\[(=*)\[[\s\S]*?\]\1\]|[^\s()]+')
INCLUDE_FLAGS_VARIABLE = "${orm_cxx_compile_include_flags}"


def blank(match: re.Match) -> str:
    """Keep line numbers while masking comments and literals."""
    return re.sub(r"[^\n]", " ", match.group())


def check_cpp(path: str, source: str) -> list[str]:
    # Translation phase 2 joins escaped newlines before comments are recognized.
    removed_newlines = []
    removed_characters = 0

    def splice(match: re.Match) -> str:
        nonlocal removed_characters
        removed_newlines.append(match.start() - removed_characters)
        removed_characters += len(match.group())
        return ""

    source = re.sub(r"\\\r?\n", splice, source)
    code = CPP_NON_CODE.sub(blank, source)
    findings = []
    for directive in DIRECTIVE.finditer(code):
        kind, arguments = directive.groups()
        line = code.count("\n", 0, directive.start()) + bisect_right(removed_newlines, directive.start()) + 1
        if kind in {"define", "undef", "cmakedefine", "cmakedefine01"}:
            findings.append(f"{path}:{line}: #{kind} is forbidden; use C++ declarations or generated constants")
        elif kind in {"if", "ifdef", "ifndef", "elif", "elifdef", "elifndef"}:
            names = set(IDENTIFIER.findall(arguments)) - {"defined"}
            forbidden = names - COMPILER_CONDITIONS.get(path, set())
            if forbidden:
                findings.append(f"{path}:{line}: conditional macro(s) outside compiler allowlist: {', '.join(sorted(forbidden))}")
    return findings


def cmake_commands(source: str):
    """Read command bodies while preserving strings and skipping CMake comments."""
    comment_or_string = re.compile(
        r'"(?:\\.|[^"\\])*"'
        r'|\[(?P<literal_equals>=*)\[[\s\S]*?\](?P=literal_equals)\]'
        r'|#\[(?P<comment_equals>=*)\[[\s\S]*?\](?P=comment_equals)\]|#[^\n]*'
    )
    code = comment_or_string.sub(lambda m: blank(m) if m.group().startswith("#") else m.group(), source)
    start = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
    bracket_start = re.compile(r"\[(=*)\[")
    position = 0
    while match := start.search(code, position):
        depth = 1
        end = match.end()
        quoted = False
        while end < len(code) and depth:
            if not quoted and (bracket := bracket_start.match(code, end)):
                closing = "]" + bracket.group(1) + "]"
                closing_position = code.find(closing, bracket.end())
                end = len(code) if closing_position == -1 else closing_position + len(closing)
                continue
            character = code[end]
            if character == "\\" and quoted:
                end += 2
                continue
            if character == '"':
                quoted = not quoted
            elif not quoted:
                depth += (character == "(") - (character == ")")
            end += 1
        body = code[match.end():end - 1]
        tokens = []
        for token_match in CMAKE_TOKEN.finditer(body):
            token = token_match.group()
            if token.startswith('"'):
                token = token[1:-1]
            elif bracket := bracket_start.match(token):
                token = token[bracket.end():-(len(bracket.group(1)) + 2)]
            tokens.append(token)
        yield match.group(1).lower(), tokens, code.count("\n", 0, match.start()) + 1
        position = end


def check_cmake(path: str, source: str) -> list[str]:
    findings = []
    commands = list(cmake_commands(source))
    variables = {}
    for command, tokens, _ in commands:
        if command == "set" and tokens:
            variables.setdefault(tokens[0], []).extend(tokens[1:])
        elif command == "list" and len(tokens) >= 2 and tokens[0].upper() in {"APPEND", "PREPEND"}:
            variables.setdefault(tokens[1], []).extend(tokens[2:])

    def has_macro_flag(tokens: list[str], seen: frozenset = frozenset()) -> bool:
        for token in tokens:
            if DEFINE_FLAG.search(token):
                return True
            for name in re.findall(r"\$\{([^}]+)\}", token):
                if name not in seen and has_macro_flag(variables.get(name, []), seen | {name}):
                    return True
        return False

    for command, tokens, line in commands:
        reason = None
        if command in {"target_compile_definitions", "add_compile_definitions", "add_definitions"}:
            reason = "compiler macro definitions are forbidden"
        elif command in {"target_compile_options", "add_compile_options"}:
            if has_macro_flag(tokens):
                reason = "compiler -D or /D macro flags are forbidden"
        elif command in {"set", "list"}:
            variable_index = 1 if command == "list" else 0
            variable = tokens[variable_index] if len(tokens) > variable_index else ""
            values = tokens[variable_index + 1:]
            if re.match(r"CMAKE_(?:C|CXX|CUDA)_FLAGS(?:_|$)|CMAKE_REQUIRED_DEFINITIONS$", variable):
                if has_macro_flag(values):
                    reason = "compiler flag variables must not define macros"
            if variable == "orm_cxx_compile_include_flags" and any(
                not token.startswith(("-I", "/I")) for token in values
            ):
                reason = "try_compile include flags must contain only -I or /I arguments"
        if command in {"set_target_properties", "set_source_files_properties", "set_property"}:
            upper = [token.upper() for token in tokens]
            for index, token in enumerate(upper):
                if re.fullmatch(r"(?:INTERFACE_)?COMPILE_DEFINITIONS(?:_[A-Z0-9_]+)?", token):
                    reason = "compiler macro properties are forbidden"
                elif token in {"COMPILE_FLAGS", "COMPILE_OPTIONS", "INTERFACE_COMPILE_OPTIONS"}:
                    if has_macro_flag(tokens[index + 1:]):
                        reason = "compiler property -D or /D flags are forbidden"
        if command == "try_compile" and "COMPILE_DEFINITIONS" in tokens:
            start = tokens.index("COMPILE_DEFINITIONS") + 1
            values = tokens[start:]
            for index, token in enumerate(values):
                if token in {"OUTPUT_VARIABLE", "COPY_FILE", "COPY_FILE_ERROR", "CMAKE_FLAGS", "LINK_OPTIONS", "LINK_LIBRARIES", "NO_CACHE", "LOG_DESCRIPTION"}:
                    values = values[:index]
                    break
            if any(token != INCLUDE_FLAGS_VARIABLE and not token.startswith(("-I", "/I")) for token in values):
                reason = "try_compile COMPILE_DEFINITIONS accepts include paths only"
        if reason:
            findings.append(f"{path}:{line}: {reason}")
    return findings


def check_conan(path: str, source: str) -> list[str]:
    findings = []
    tree = ast.parse(source, filename=path)
    for node in ast.walk(tree):
        if isinstance(node, ast.Attribute) and node.attr == "defines":
            findings.append(f"{path}:{node.lineno}: Conan compiler macro definitions are forbidden")
        if isinstance(node, (ast.Assign, ast.AnnAssign, ast.AugAssign)):
            targets = node.targets if isinstance(node, ast.Assign) else [node.target]
            if any(isinstance(target, ast.Attribute) and target.attr in {"cflags", "cxxflags", "cppflags"} for target in targets):
                if any(isinstance(value, ast.Constant) and isinstance(value.value, str) and DEFINE_FLAG.search(value.value) for value in ast.walk(node.value)):
                    findings.append(f"{path}:{node.lineno}: Conan compiler -D or /D macro flags are forbidden")
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute):
            container = node.func.value
            if isinstance(container, ast.Attribute) and container.attr in {"cflags", "cxxflags", "cppflags"}:
                if any(isinstance(value, ast.Constant) and isinstance(value.value, str) and DEFINE_FLAG.search(value.value) for argument in node.args for value in ast.walk(argument)):
                    findings.append(f"{path}:{node.lineno}: Conan compiler -D or /D macro flags are forbidden")
    return findings


def source_kind(path: str) -> str | None:
    parts = Path(path).parts
    if not parts or parts[0] in {"externals", "build", "html", ".git", ".cache", "vcpkg_installed"}:
        return None
    if any(part.startswith("build-") or part.startswith("build_") for part in parts[:-1]):
        return None
    if path.startswith("cmake/third_party/") or path == "cmake/cmake-coverage.cmake":
        return None
    candidate = Path(path[:-3] if path.endswith(".in") else path)
    if candidate.suffix in CPP_SUFFIXES:
        return "cpp"
    if candidate.name == "CMakeLists.txt" or candidate.suffix == ".cmake":
        return "cmake"
    if candidate.name == "conanfile.py":
        return "conan"
    return None


def check_repository(root: Path) -> tuple[list[str], int]:
    result = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=root, check=True, stdout=subprocess.PIPE,
    )
    findings = []
    count = 0
    scanners = {"cpp": check_cpp, "cmake": check_cmake, "conan": check_conan}
    for path in sorted(set(result.stdout.decode("utf-8").split("\0")) - {""}):
        kind = source_kind(path)
        file = root / path
        if kind and file.is_file():
            count += 1
            findings.extend(scanners[kind](path, file.read_text(encoding="utf-8")))
    return findings, count


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    arguments = parser.parse_args()
    try:
        findings, count = check_repository(arguments.root.resolve())
    except (OSError, subprocess.CalledProcessError, UnicodeError, SyntaxError) as error:
        print(f"Macro policy check failed: {error}", file=sys.stderr)
        return 2
    if findings:
        print("\n".join(findings), file=sys.stderr)
        return 1
    print(f"Macro policy passed: {count} first-party source/configuration files; no owned macro definitions.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
