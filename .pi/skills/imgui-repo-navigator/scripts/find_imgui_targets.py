#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable, List, Optional, Sequence, Tuple

DEFAULT_GITHUB_URL = "https://github.com/ocornut/imgui"
DEFAULT_REF = "master"
TERM_ALIASES = {
    "dx9": ["directx9", "d3d9"],
    "dx10": ["directx10", "d3d10"],
    "dx11": ["directx11", "d3d11"],
    "dx12": ["directx12", "d3d12"],
    "vk": ["vulkan"],
    "wgpu": ["webgpu"],
    "ogl": ["opengl", "opengl2", "opengl3"],
}
CORE_DOC_FALLBACKS = [
    "imgui.h",
    "imgui.cpp",
    "imgui_demo.cpp",
    "imgui_draw.cpp",
    "imgui_widgets.cpp",
    "imgui_tables.cpp",
    "docs/BACKENDS.md",
    "docs/EXAMPLES.md",
    "docs/FAQ.md",
    "docs/FONTS.md",
]
CONTENT_FETCH_LIMIT = 40
MAX_TOTAL_HITS = 24
MAX_HITS_PER_FILE = 3


@dataclass
class ContentHit:
    path: str
    line_no: int
    text: str
    is_upstream: bool
    github_url: Optional[str] = None


@dataclass
class LookupResults:
    source: str
    repo_label: str
    ref: Optional[str]
    example_dirs: List[str]
    backend_files: List[str]
    docs_core_files: List[str]
    content_hits: List[ContentHit]
    notes: List[str]

    def has_matches(self, has_query_terms: bool) -> bool:
        if not has_query_terms:
            return True
        return bool(self.example_dirs or self.backend_files or self.docs_core_files or self.content_hits)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Find Dear ImGui targets locally or on GitHub.")
    parser.add_argument("terms", nargs="*", help="Search terms such as win32 dx11, glfw opengl3, font")
    parser.add_argument("--repo", help="Explicit local Dear ImGui root or vendored subtree")
    parser.add_argument("--mode", choices=["auto", "local", "upstream"], default="auto", help="Lookup mode")
    parser.add_argument("--github-url", default=DEFAULT_GITHUB_URL, help="Upstream GitHub repository URL")
    parser.add_argument("--ref", default=DEFAULT_REF, help="GitHub ref for upstream lookup: branch, tag, or commit")
    parser.add_argument("--max-content-hits", type=int, default=MAX_TOTAL_HITS, help="Maximum content hits to print")
    return parser.parse_args()


def normalize_terms(raw_terms: Sequence[str]) -> List[str]:
    normalized: List[str] = []
    seen = set()
    for term in raw_terms:
        clean = term.strip().lower()
        if not clean:
            continue
        if clean not in seen:
            normalized.append(clean)
            seen.add(clean)
    return normalized


def term_variants(term: str) -> List[str]:
    variants = [term]
    for alias in TERM_ALIASES.get(term, []):
        if alias not in variants:
            variants.append(alias)
    return variants


def any_variant_matches(text: str, term: str) -> bool:
    text = text.lower()
    return any(variant in text for variant in term_variants(term))


def all_terms_match(text: str, terms: Sequence[str]) -> bool:
    lowered = text.lower()
    return all(any_variant_matches(lowered, term) for term in terms)


def any_term_matches(text: str, terms: Sequence[str]) -> bool:
    lowered = text.lower()
    return any(any_variant_matches(lowered, term) for term in terms)


def looks_like_imgui_root(path: Path) -> bool:
    return (path / "imgui.h").is_file() and (path / "backends").is_dir() and (path / "examples").is_dir() and (path / "docs").is_dir()


def find_local_repo_root(start_dir: Path) -> Optional[Path]:
    start_dir = start_dir.resolve()
    for candidate in [start_dir, *start_dir.parents]:
        if looks_like_imgui_root(candidate):
            return candidate

    max_depth = 5
    for current_root, dirnames, filenames in os.walk(start_dir):
        current_path = Path(current_root)
        try:
            depth = len(current_path.relative_to(start_dir).parts)
        except ValueError:
            depth = max_depth + 1
        if depth > max_depth:
            dirnames[:] = []
            continue
        dirnames[:] = [name for name in dirnames if name not in {".git", "__pycache__", ".hg", ".svn"}]
        if "imgui.h" in filenames and looks_like_imgui_root(current_path):
            return current_path
    return None


def score_path(path: str, terms: Sequence[str]) -> int:
    if not terms:
        return 0
    lower_path = path.lower()
    lower_name = PurePosixPath(path).name.lower()
    score = 0
    for term in terms:
        variants = term_variants(term)
        if any(variant in lower_name for variant in variants):
            score += 6
        elif any(variant in lower_path for variant in variants):
            score += 3
    return score


def is_candidate_content_path(path: str) -> bool:
    pure = PurePosixPath(path)
    name = pure.name
    if pure.parent == PurePosixPath("."):
        return bool(re.fullmatch(r"imgui.*\.(h|cpp)", name))
    if pure.parts[:1] == ("docs",):
        return name.endswith((".md", ".txt"))
    if pure.parts[:1] == ("backends",):
        return name.endswith((".h", ".cpp", ".mm"))
    if len(pure.parts) >= 3 and pure.parts[0] == "examples" and pure.parts[1].startswith("example_"):
        return name in {"main.cpp", "README.md", "README.txt", "Makefile", "CMakeLists.txt"}
    return False


def select_content_paths(paths: Sequence[str], terms: Sequence[str]) -> List[str]:
    ranked: List[Tuple[int, str]] = []
    seen = set()
    fallback_set = set(CORE_DOC_FALLBACKS)
    for path in paths:
        if not is_candidate_content_path(path):
            continue
        score = score_path(path, terms)
        if path in fallback_set:
            score += 1
        if score > 0 or path in fallback_set:
            ranked.append((score, path))
    ranked.sort(key=lambda item: (-item[0], item[1]))

    selected: List[str] = []
    for _, path in ranked:
        if path not in seen:
            selected.append(path)
            seen.add(path)
        if len(selected) >= CONTENT_FETCH_LIMIT:
            break
    return selected


def search_text_content(text: str, path: str, is_upstream: bool, github_url: Optional[str], terms: Sequence[str], max_total_hits: int) -> List[ContentHit]:
    hits: List[ContentHit] = []
    for line_no, line in enumerate(text.splitlines(), start=1):
        if any_term_matches(line, terms):
            hits.append(ContentHit(path=path, line_no=line_no, text=line.strip(), is_upstream=is_upstream, github_url=github_url))
            if len(hits) >= MAX_HITS_PER_FILE or len(hits) >= max_total_hits:
                break
    return hits


def gather_local_paths(repo_root: Path) -> List[str]:
    paths: List[str] = []
    for file_path in sorted(repo_root.glob("imgui*.h")):
        paths.append(file_path.relative_to(repo_root).as_posix())
    for file_path in sorted(repo_root.glob("imgui*.cpp")):
        paths.append(file_path.relative_to(repo_root).as_posix())
    for file_path in sorted((repo_root / "docs").glob("*")):
        if file_path.is_file():
            paths.append(file_path.relative_to(repo_root).as_posix())
    for file_path in sorted((repo_root / "backends").glob("*")):
        if file_path.is_file():
            paths.append(file_path.relative_to(repo_root).as_posix())
    for example_dir in sorted((repo_root / "examples").glob("example_*")):
        if not example_dir.is_dir():
            continue
        for name in ["main.cpp", "README.md", "README.txt", "Makefile", "CMakeLists.txt"]:
            candidate = example_dir / name
            if candidate.is_file():
                paths.append(candidate.relative_to(repo_root).as_posix())
    return paths


def build_local_results(repo_root: Path, terms: Sequence[str], max_content_hits: int) -> LookupResults:
    all_paths = gather_local_paths(repo_root)
    example_dirs = []
    for example_dir in sorted((repo_root / "examples").glob("example_*")):
        if example_dir.is_dir():
            rel = example_dir.relative_to(repo_root).as_posix()
            if not terms or all_terms_match(example_dir.name, terms):
                example_dirs.append(rel)

    backend_files = []
    for rel in all_paths:
        if rel.startswith("backends/") and (not terms or any_term_matches(PurePosixPath(rel).name, terms)):
            backend_files.append(rel)

    docs_core_files = []
    for rel in all_paths:
        if rel.startswith("backends/"):
            continue
        if rel.startswith("examples/") and "/" in rel:
            continue
        if not terms or any_term_matches(PurePosixPath(rel).name, terms):
            docs_core_files.append(rel)

    content_hits: List[ContentHit] = []
    if terms:
        for rel in select_content_paths(all_paths, terms):
            file_path = repo_root / rel
            try:
                text = file_path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            for hit in search_text_content(text, rel, False, None, terms, max_content_hits - len(content_hits)):
                content_hits.append(hit)
                if len(content_hits) >= max_content_hits:
                    break
            if len(content_hits) >= max_content_hits:
                break

    return LookupResults(
        source="local",
        repo_label=str(repo_root),
        ref=None,
        example_dirs=example_dirs,
        backend_files=backend_files,
        docs_core_files=docs_core_files,
        content_hits=content_hits,
        notes=[],
    )


def parse_github_repo(url: str) -> Tuple[str, str, str]:
    cleaned = url.strip().rstrip("/")
    ssh_match = re.fullmatch(r"git@github\.com:(?P<owner>[^/]+)/(?P<repo>[^/.]+)(?:\.git)?", cleaned)
    if ssh_match:
        owner = ssh_match.group("owner")
        repo = ssh_match.group("repo")
        return owner, repo, f"https://github.com/{owner}/{repo}"
    https_match = re.fullmatch(r"https://github\.com/(?P<owner>[^/]+)/(?P<repo>[^/.]+)(?:\.git)?", cleaned)
    if https_match:
        owner = https_match.group("owner")
        repo = https_match.group("repo")
        return owner, repo, f"https://github.com/{owner}/{repo}"
    raise ValueError(f"Unsupported GitHub URL: {url}")


def http_get(url: str) -> bytes:
    request = urllib.request.Request(
        url,
        headers={
            "User-Agent": "imgui-repo-navigator/1.0",
            "Accept": "application/vnd.github+json, text/plain;q=0.9, */*;q=0.1",
        },
    )
    with urllib.request.urlopen(request, timeout=20) as response:
        return response.read()


def fetch_github_tree(repo_url: str, ref: str) -> Tuple[str, List[str], List[str]]:
    owner, repo, normalized_url = parse_github_repo(repo_url)
    ref_quoted = urllib.parse.quote(ref, safe="")
    api_url = f"https://api.github.com/repos/{owner}/{repo}/git/trees/{ref_quoted}?recursive=1"
    payload = json.loads(http_get(api_url).decode("utf-8"))
    paths = [item["path"] for item in payload.get("tree", []) if item.get("type") == "blob"]
    notes = []
    if payload.get("truncated"):
        notes.append("GitHub tree response was truncated; some upstream paths may be missing.")
    return normalized_url, paths, notes


def github_blob_url(repo_url: str, ref: str, path: str, line_no: Optional[int] = None) -> str:
    suffix = f"#L{line_no}" if line_no is not None else ""
    return f"{repo_url}/blob/{urllib.parse.quote(ref, safe='')}/{path}{suffix}"


def fetch_upstream_text(repo_url: str, ref: str, path: str) -> str:
    owner, repo, _ = parse_github_repo(repo_url)
    raw_url = f"https://raw.githubusercontent.com/{owner}/{repo}/{urllib.parse.quote(ref, safe='')}/{path}"
    return http_get(raw_url).decode("utf-8", errors="replace")


def gather_upstream_example_dirs(paths: Sequence[str]) -> List[str]:
    example_dirs = set()
    for path in paths:
        pure = PurePosixPath(path)
        if len(pure.parts) >= 2 and pure.parts[0] == "examples" and pure.parts[1].startswith("example_"):
            example_dirs.add(f"examples/{pure.parts[1]}")
    return sorted(example_dirs)


def build_upstream_results(repo_url: str, ref: str, terms: Sequence[str], max_content_hits: int) -> LookupResults:
    normalized_url, all_paths, notes = fetch_github_tree(repo_url, ref)
    example_dirs = [path for path in gather_upstream_example_dirs(all_paths) if not terms or all_terms_match(PurePosixPath(path).name, terms)]
    backend_files = [path for path in sorted(all_paths) if path.startswith("backends/") and (not terms or any_term_matches(PurePosixPath(path).name, terms))]
    docs_core_files = []
    for path in sorted(all_paths):
        pure = PurePosixPath(path)
        if path.startswith("backends/"):
            continue
        if len(pure.parts) >= 2 and pure.parts[0] == "examples" and pure.parts[1].startswith("example_"):
            continue
        if pure.parent == PurePosixPath(".") or pure.parts[:1] == ("docs",):
            if not terms or any_term_matches(pure.name, terms):
                docs_core_files.append(path)

    content_hits: List[ContentHit] = []
    if terms:
        for path in select_content_paths(all_paths, terms):
            try:
                text = fetch_upstream_text(normalized_url, ref, path)
            except (urllib.error.HTTPError, urllib.error.URLError):
                continue
            base_url = github_blob_url(normalized_url, ref, path)
            for hit in search_text_content(text, path, True, base_url, terms, max_content_hits - len(content_hits)):
                hit.github_url = github_blob_url(normalized_url, ref, path, hit.line_no)
                content_hits.append(hit)
                if len(content_hits) >= max_content_hits:
                    break
            if len(content_hits) >= max_content_hits:
                break

    return LookupResults(
        source="upstream",
        repo_label=normalized_url,
        ref=ref,
        example_dirs=example_dirs,
        backend_files=backend_files,
        docs_core_files=docs_core_files,
        content_hits=content_hits,
        notes=notes,
    )


def print_results(results: LookupResults, fallback_note: Optional[str]) -> None:
    if fallback_note:
        print(f"Lookup source: {results.source} ({fallback_note})")
    else:
        print(f"Lookup source: {results.source}")
    if results.source == "local":
        print(f"Repo root: {results.repo_label}")
    else:
        print(f"GitHub repo: {results.repo_label}")
        print(f"Ref: {results.ref}")
    for note in results.notes:
        print(f"Note: {note}")

    print("\nExample directories:")
    if results.example_dirs:
        for path in results.example_dirs:
            if results.source == "upstream":
                print(f"  {github_blob_url(results.repo_label, results.ref or DEFAULT_REF, path)}")
            else:
                print(f"  {path}")
    else:
        print("  (no directory-name match)")

    print("\nBackend files:")
    if results.backend_files:
        for path in results.backend_files:
            if results.source == "upstream":
                print(f"  {github_blob_url(results.repo_label, results.ref or DEFAULT_REF, path)}")
            else:
                print(f"  {path}")
    else:
        print("  (no backend filename match)")

    print("\nDocs and core files:")
    if results.docs_core_files:
        for path in results.docs_core_files:
            if results.source == "upstream":
                print(f"  {github_blob_url(results.repo_label, results.ref or DEFAULT_REF, path)}")
            else:
                print(f"  {path}")
    else:
        print("  (no filename match)")

    print("\nContent hits:")
    if results.content_hits:
        for hit in results.content_hits:
            prefix = hit.github_url if hit.is_upstream and hit.github_url else f"{hit.path}:{hit.line_no}"
            print(f"  {prefix}: {hit.text}")
    else:
        print("  (no content hits)")


def main() -> int:
    args = parse_args()
    terms = normalize_terms(args.terms)
    local_repo: Optional[Path] = None
    if args.repo:
        local_repo = Path(args.repo).expanduser().resolve()
        if not looks_like_imgui_root(local_repo):
            print(f"Invalid Dear ImGui repo root: {local_repo}", file=sys.stderr)
            return 1
    elif args.mode in {"auto", "local"}:
        local_repo = find_local_repo_root(Path.cwd())

    fallback_note: Optional[str] = None
    chosen_results: Optional[LookupResults] = None

    if args.mode in {"auto", "local"} and local_repo is not None:
        chosen_results = build_local_results(local_repo, terms, args.max_content_hits)
        if args.mode == "local":
            print_results(chosen_results, None)
            return 0
        if chosen_results.has_matches(bool(terms)):
            print_results(chosen_results, None)
            return 0
        fallback_note = "upstream fallback after no local matches"

    if args.mode == "local":
        print("Could not locate a local Dear ImGui repo root from the current directory.", file=sys.stderr)
        return 1

    try:
        upstream_results = build_upstream_results(args.github_url, args.ref, terms, args.max_content_hits)
    except (ValueError, urllib.error.HTTPError, urllib.error.URLError) as exc:
        if chosen_results is not None:
            print_results(chosen_results, None)
            print(f"\nUpstream fallback failed: {exc}", file=sys.stderr)
            return 1
        print(f"Upstream lookup failed: {exc}", file=sys.stderr)
        return 1

    print_results(upstream_results, fallback_note)
    return 0


if __name__ == "__main__":
    sys.exit(main())
