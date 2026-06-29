#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import re
import sys
from collections import defaultdict


INCLUDE_RE = re.compile(r'^\s*#include\s+"([A-Za-z0-9_]+)/([^"]+)"')

MODULE_ORDER = {
    "app": 0,
    "scene": 1,
    "render": 2,
    "camera": 2,
    "data": 2,
    "core": 3,
    "platform": 4,
    "util": 4,
    "math": 4,
}

# Stage-1 transitional exemptions. Keep the list short and explicit.
EXEMPT_EDGES = {
    ("render", "camera"),
}

SCAN_ROOTS = ("include", "src")
SCAN_SUFFIXES = {".hpp", ".cpp"}


def module_for(path: pathlib.Path, root: pathlib.Path) -> str | None:
    try:
        rel = path.relative_to(root)
    except ValueError:
        return None

    if len(rel.parts) < 2:
        return None

    return rel.parts[0]


def collect_edges(root: pathlib.Path):
    edges = defaultdict(set)
    for scan_root in SCAN_ROOTS:
        base = root / scan_root
        if not base.exists():
            continue

        for path in base.rglob("*"):
            if path.suffix not in SCAN_SUFFIXES or not path.is_file():
                continue

            src_module = module_for(path, base)
            if src_module is None:
                continue

            with path.open("r", encoding="utf-8") as handle:
                for line_number, line in enumerate(handle, start=1):
                    match = INCLUDE_RE.match(line)
                    if not match:
                        continue

                    dst_module = match.group(1)
                    if dst_module == src_module:
                        continue

                    edges[(src_module, dst_module)].add(
                        f"{path.relative_to(root)}:{line_number}"
                    )

    return edges


def is_violation(src_module: str, dst_module: str) -> bool:
    if (src_module, dst_module) in EXEMPT_EDGES:
        return False

    if src_module not in MODULE_ORDER or dst_module not in MODULE_ORDER:
        return False

    return MODULE_ORDER[src_module] >= MODULE_ORDER[dst_module]


def main() -> int:
    repo_root = pathlib.Path(__file__).resolve().parents[1]
    edges = collect_edges(repo_root)

    violations = []
    for (src_module, dst_module), refs in sorted(edges.items()):
        if is_violation(src_module, dst_module):
            violations.append((src_module, dst_module, sorted(refs)))

    print("GeoScatter3D include dependency check")
    print(f"repo = {repo_root}")
    print("tracked modules =", ", ".join(MODULE_ORDER.keys()))
    print("exempt edges =", ", ".join(f"{a}->{b}" for a, b in sorted(EXEMPT_EDGES)))

    if not violations:
        print("result = OK")
        return 0

    print("result = VIOLATION")
    for src_module, dst_module, refs in violations:
        print(f"- {src_module} -> {dst_module}")
        for ref in refs[:10]:
            print(f"  {ref}")
        if len(refs) > 10:
            print(f"  ... and {len(refs) - 10} more")

    return 1


if __name__ == "__main__":
    sys.exit(main())
