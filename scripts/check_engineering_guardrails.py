#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import re
import subprocess
import sys


FORBIDDEN_TRACKED_PREFIXES = (
    ".cursor/",
    ".dbg/",
    ".idea/",
    ".tools/",
    ".vscode/",
)

SOURCE_SUFFIXES = {".cpp", ".hpp"}
PORTABILITY_PATHS = {
    pathlib.PurePosixPath("CMakeLists.txt"),
}

RAW_LOG_PATTERNS = (
    re.compile(r"\bstd::(?:cout|cerr)\b"),
    re.compile(r"\b(?:std::)?f(?:printf|puts)\s*\(\s*stderr\b"),
)
ABSOLUTE_MACHINE_PATH = re.compile(
    r"(?:/[Hh]ome/|/[Uu]sers/|[A-Za-z]:[\\/](?:Users|home)[\\/])"
)


def tracked_files(root: pathlib.Path) -> list[pathlib.PurePosixPath]:
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=root,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return [
        pathlib.PurePosixPath(item)
        for item in result.stdout.decode("utf-8").split("\0")
        if item
    ]


def function_line_count(path: pathlib.Path, signature: str) -> int:
    source = path.read_text(encoding="utf-8")
    start = source.find(signature)
    if start < 0:
        raise ValueError(f"signature not found: {signature}")

    start_line = source.count("\n", 0, start) + 1
    open_brace = source.find("{", start)
    if open_brace < 0:
        raise ValueError(f"opening brace not found for: {signature}")

    depth = 0
    mode = "code"
    index = open_brace
    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""

        if mode == "line_comment":
            if char == "\n":
                mode = "code"
        elif mode == "block_comment":
            if char == "*" and next_char == "/":
                mode = "code"
                index += 1
        elif mode in {"string", "character"}:
            if char == "\\":
                index += 1
            elif (mode == "string" and char == '"') or (
                mode == "character" and char == "'"
            ):
                mode = "code"
        elif char == "/" and next_char == "/":
            mode = "line_comment"
            index += 1
        elif char == "/" and next_char == "*":
            mode = "block_comment"
            index += 1
        elif char == '"':
            mode = "string"
        elif char == "'":
            mode = "character"
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                end_line = source.count("\n", 0, index) + 1
                return end_line - start_line + 1
        index += 1

    raise ValueError(f"closing brace not found for: {signature}")


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    tracked = tracked_files(root)
    violations: list[str] = []

    for path in tracked:
        path_string = path.as_posix()
        if path_string.startswith(FORBIDDEN_TRACKED_PREFIXES):
            violations.append(f"tracked local/tool state: {path_string}")

        absolute_path_candidate = (
            path in PORTABILITY_PATHS or
            path.parts[:1] in {("config",), ("include",), ("src",)}
        )
        if absolute_path_candidate and path.suffix in {
            ".cmake", ".cpp", ".hpp", ".toml", ""
        }:
            content = (root / path).read_text(encoding="utf-8")
            if ABSOLUTE_MACHINE_PATH.search(content):
                violations.append(f"machine-specific absolute path: {path_string}")

        if path.parts[:1] == ("src",) and path.suffix in SOURCE_SUFFIXES:
            content = (root / path).read_text(encoding="utf-8")
            for pattern in RAW_LOG_PATTERNS:
                if pattern.search(content):
                    violations.append(
                        f"raw runtime diagnostic bypasses util::log: {path_string}"
                    )
                    break

    viewer_path = root / "src/app/ViewerApp.cpp"
    ui_path = root / "src/ui/UiRoot.cpp"
    architecture_path = root / "docs/architecture.md"
    architecture = architecture_path.read_text(encoding="utf-8")
    viewer_match = re.search(
        r"`ViewerApp\.cpp` 当前有 (\d+) 行，其中 "
        r"`ViewerApp::run\(\)` 独占 (\d+) 行",
        architecture,
    )
    ui_match = re.search(r"`UiRoot\.cpp` 仍有 (\d+) 行", architecture)
    if viewer_match is None or ui_match is None:
        violations.append("architecture metrics are missing from docs/architecture.md")
    else:
        viewer_lines = len(viewer_path.read_text(encoding="utf-8").splitlines())
        run_lines = function_line_count(viewer_path, "int ViewerApp::run()")
        ui_lines = len(ui_path.read_text(encoding="utf-8").splitlines())
        documented = tuple(map(int, viewer_match.groups())) + (int(ui_match.group(1)),)
        actual = (viewer_lines, run_lines, ui_lines)
        if documented != actual:
            violations.append(
                "stale architecture metrics: "
                f"documented={documented}, actual={actual}"
            )

    print("GeoScatter3D engineering guardrails")
    if not violations:
        print("result = OK")
        return 0

    print("result = VIOLATION")
    for violation in violations:
        print(f"- {violation}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
