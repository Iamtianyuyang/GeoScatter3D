#!/usr/bin/env python3

from __future__ import annotations

import ast
import os
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

SHARED_VSCODE_FILES = {".vscode/launch.json", ".vscode/tasks.json", ".vscode/extensions.json"}

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
IGNORE_SCOPE_PROBES = (
    "docs/guardrail-screenshot.png",
    "docs/guardrail-report.html",
    "examples/guardrail-session.log",
    "src/data/guardrail-new-source.cpp",
)
LINE_BUDGETS = {
    "src/app/AppConfig.cpp": 763,
}


def line_budgets_from_source(source: str) -> dict[str, int] | None:
    try:
        module = ast.parse(source)
        assignment = next(
            node for node in module.body
            if isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == "LINE_BUDGETS"
                for target in node.targets
            )
        )
        parsed = ast.literal_eval(assignment.value)
    except (StopIteration, SyntaxError, ValueError):
        return None

    if not isinstance(parsed, dict) or not all(
        isinstance(subject, str) and isinstance(budget, int)
        for subject, budget in parsed.items()
    ):
        return None
    return parsed


def line_budgets_at_revision(
    root: pathlib.Path,
    revision: str,
) -> dict[str, int] | None:
    previous = subprocess.run(
        ["git", "show", f"{revision}:scripts/check_engineering_guardrails.py"],
        cwd=root,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        # Git stores source as UTF-8.  Do not use Windows' process locale
        # (cp1252 on hosted runners), because historical comments are Chinese.
        encoding="utf-8",
    )
    if previous.returncode != 0:
        return None
    return line_budgets_from_source(previous.stdout)


def git_revision(
    root: pathlib.Path,
    *args: str,
) -> str | None:
    result = subprocess.run(
        ["git", *args],
        cwd=root,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
    )
    if result.returncode != 0:
        return None
    return result.stdout.strip()


def line_budget_baseline(
    root: pathlib.Path,
) -> tuple[dict[str, int], str] | None:
    """Return the immutable line-budget baseline for this checked tree.

    Local runs compare against HEAD^ for fast feedback.  Pull-request CI sets
    GITHUB_BASE_REF and compares against the merge-base with that base branch,
    so a budget cannot be raised in one commit and cosmetically lowered in a
    later head commit.  This first PR predates the guardrail on main, so its
    transitional baseline is the first commit on the PR that introduced a
    complete LINE_BUDGETS mapping; once merged, all later PRs use main.
    """
    base_ref = os.environ.get("GITHUB_BASE_REF")
    if not base_ref:
        parent = git_revision(root, "rev-parse", "--verify", "HEAD^")
        if parent is None:
            return None
        budgets = line_budgets_at_revision(root, parent)
        if budgets is None:
            return None
        return budgets, f"parent {parent[:12]}"

    merge_base = git_revision(
        root,
        "merge-base",
        "HEAD",
        f"origin/{base_ref}",
    )
    if merge_base is None:
        return None

    budgets = line_budgets_at_revision(root, merge_base)
    if budgets is not None:
        return budgets, f"merge-base {merge_base[:12]}"

    introduced_revisions = git_revision(
        root,
        "rev-list",
        "--reverse",
        f"{merge_base}..HEAD",
        "--",
        "scripts/check_engineering_guardrails.py",
    )
    if introduced_revisions is None:
        return None
    for revision in introduced_revisions.splitlines():
        budgets = line_budgets_at_revision(root, revision)
        if budgets is not None:
            return budgets, f"first guardrail policy {revision[:12]}"
    return None


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
        if item and (root / item).is_file()
    ]


def is_ignored(root: pathlib.Path, candidate: str) -> bool:
    result = subprocess.run(
        ["git", "check-ignore", "--quiet", "--no-index", candidate],
        cwd=root,
        check=False,
    )
    return result.returncode == 0


def check_android_release_contract(
    root: pathlib.Path,
    violations: list[str],
) -> None:
    """Validate Android store filtering and release-signing invariants."""
    manifest_path = root / "ui_flutter/android/app/src/main/AndroidManifest.xml"
    gradle_path = root / "ui_flutter/android/app/build.gradle.kts"

    if not manifest_path.is_file():
        violations.append("Android manifest is missing")
    else:
        manifest = manifest_path.read_text(encoding="utf-8")
        vulkan_feature = re.search(
            r"<uses-feature\b[^>]*android:name=\"android\.hardware\.vulkan\.version\""
            r"[^>]*/>",
            manifest,
            flags=re.DOTALL,
        )
        if vulkan_feature is None:
            violations.append("Android manifest must require Vulkan 1.1")
        else:
            feature = vulkan_feature.group(0)
            if 'android:version="0x00401000"' not in feature:
                violations.append("Android manifest Vulkan feature is below Vulkan 1.1")
            if 'android:required="true"' not in feature:
                violations.append("Android Vulkan feature must be required")

    if not gradle_path.is_file():
        violations.append("Android app Gradle build file is missing")
    else:
        gradle = gradle_path.read_text(encoding="utf-8")
        if 'signingConfigs.getByName("debug")' in gradle:
            violations.append("Android release build must not fall back to the debug signing config")
        if "releaseTaskRequested" not in gradle:
            violations.append("Android release build must guard missing production signing config")
        if 'signingConfig = signingConfigs.getByName("release")' not in gradle:
            violations.append("Android release build must use the production release signing config")


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    tracked = tracked_files(root)
    violations: list[str] = []

    baseline = line_budget_baseline(root)
    if baseline is None:
        violations.append(
            "cannot verify line-budget baseline; fetch the PR base and history"
        )
    else:
        baseline_budgets, baseline_description = baseline
        for subject, budget in LINE_BUDGETS.items():
            baseline_budget = baseline_budgets.get(subject)
            if baseline_budget is None:
                violations.append(
                    f"baseline line budget missing: {subject}"
                )
            elif budget > baseline_budget:
                violations.append(
                    "line budget increased: "
                    f"{subject}={budget}, {baseline_description}={baseline_budget}"
                )

    for path in tracked:
        path_string = path.as_posix()
        if (path_string.startswith(FORBIDDEN_TRACKED_PREFIXES) and
                path_string not in SHARED_VSCODE_FILES):
            violations.append(f"tracked local/tool state: {path_string}")

        absolute_path_candidate = (
            path in PORTABILITY_PATHS or
            path.parts[:1] in {("cmake",), ("config",), ("include",), ("src",), ("tests",)}
        )
        if absolute_path_candidate and (
            path.suffix in {".cmake", ".cpp", ".hpp", ".toml", ""} or
            path.name == "CMakeLists.txt"
        ):
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

    for candidate in IGNORE_SCOPE_PROBES:
        if is_ignored(root, candidate):
            violations.append(f"overly broad ignore rule: {candidate}")

    check_android_release_contract(root, violations)

    app_config_path = root / "src/app/AppConfig.cpp"
    if app_config_path.is_file():
        subject = "src/app/AppConfig.cpp"
        line_count = len(
            app_config_path.read_text(encoding="utf-8").splitlines()
        )
        budget = LINE_BUDGETS[subject]
        if line_count > budget:
            violations.append(
                "source line budget exceeded: "
                f"{subject}={line_count}, budget={budget}"
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
