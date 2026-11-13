#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import re
import sys
from collections import defaultdict


INCLUDE_RE = re.compile(r'^\s*#include\s+"([A-Za-z0-9_]+)/([^"]+)"')

MODULE_ORDER = {
    "math": 0,
    "data": 1,
    "camera": 2,
    "render": 3,
    "platform": 4,
    "app": 5,
}

# Stage-1 transitional exemptions. Keep the list short and explicit.
EXEMPT_EDGES = {
    # Transitional: VulkanSwapchain.hpp (dead implementation) still names the
    # GLFW window type; removed when the platform task replaces the legacy tree.
    ("render", "platform"),
}

SCAN_ROOTS = ("include", "src")
SCAN_SUFFIXES = {".h", ".hpp", ".c", ".cc", ".cpp", ".mm"}
ENGINE_INCLUDE_RE = re.compile(r'^\s*#include\s+"([^\"]+)"')
ENGINE_ORDER = {
    "foundation": 0, "data": 1, "pipeline": 2, "render": 3,
    "config": 3, "session": 4, "abi": 5, "cli": 6,
}
ENGINE_FLAT_MODULES = {"config", "abi", "cli"}
# Within one layer, a module may only include modules listed as strictly lower.
ENGINE_INTRA_ORDER = {
    "data": {"core": -1, "format": 0, "csv": 1, "lod": 1, "tile": 1, "verify": 2},
    "session": {"project": 0, "view": 1, "measure": 2},
    "pipeline": {"compute": 0, "writers": 1, "verify": 2},
}


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

    return MODULE_ORDER[src_module] < MODULE_ORDER[dst_module]


def engine_module(parts: tuple[str, ...]) -> tuple[str, ...] | None:
    if not parts or parts[0] not in ENGINE_ORDER:
        return None
    if parts[0] in ENGINE_FLAT_MODULES:
        return (parts[0],)
    if len(parts) < 3:
        return None
    return parts[:2]


def collect_engine_violations(root: pathlib.Path) -> list[str]:
    base = root / "engine"
    violations = []
    if not base.exists():
        return violations
    for path in base.rglob("*"):
        if not path.is_file() or path.suffix not in SCAN_SUFFIXES:
            continue
        src = engine_module(path.relative_to(base).parts)
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            match = ENGINE_INCLUDE_RE.match(line)
            if not match:
                continue
            include = match.group(1)
            if include.startswith("engine/"):
                candidate = base / include.removeprefix("engine/")
            elif include.startswith("."):
                candidate = path.parent / include
            elif include.split("/", 1)[0] in ENGINE_ORDER:
                candidate = base / include
            else:
                continue
            try:
                parts = candidate.resolve().relative_to(base.resolve()).parts
            except ValueError:
                parts = ()
            dst = engine_module(parts)
            invalid = src is None or dst is None
            if not invalid and src != dst:
                if src[0] == dst[0]:
                    order = ENGINE_INTRA_ORDER.get(src[0], {})
                    invalid = order.get(src[1], 99) <= order.get(dst[1], 99)
                else:
                    invalid = (ENGINE_ORDER[src[0]] <= ENGINE_ORDER[dst[0]] or
                               (src[0] == "cli" and dst[0] in {"render", "session", "abi"}))
            if invalid:
                # as_posix：CI 在 Windows 上跑，报告要用与平台无关的斜杠，
                # 测试才能断言具体文件行。
                violations.append(
                    f"{path.relative_to(root).as_posix()}:{line_number}: {src} -> {dst}"
                )
    return sorted(violations)


def main() -> int:
    repo_root = pathlib.Path(__file__).resolve().parents[1]
    edges = collect_edges(repo_root)
    engine_violations = collect_engine_violations(repo_root)

    violations = []
    for (src_module, dst_module), refs in sorted(edges.items()):
        if is_violation(src_module, dst_module):
            violations.append((src_module, dst_module, sorted(refs)))

    print("GeoScatter3D include dependency check")
    print(f"repo = {repo_root}")
    print("tracked modules =", ", ".join(MODULE_ORDER.keys()))
    print("engine layers =", ", ".join(ENGINE_ORDER.keys()))
    print("exempt edges =", ", ".join(f"{a}->{b}" for a, b in sorted(EXEMPT_EDGES)))

    if not violations and not engine_violations:
        print("result = OK")
        return 0

    print("result = VIOLATION")
    for violation in engine_violations:
        print(f"- {violation}")
    for src_module, dst_module, refs in violations:
        print(f"- {src_module} -> {dst_module}")
        for ref in refs[:10]:
            print(f"  {ref}")
        if len(refs) > 10:
            print(f"  ... and {len(refs) - 10} more")

    return 1


if __name__ == "__main__":
    sys.exit(main())
