#!/usr/bin/env python3
"""可复现的打开/预处理测量。

用法:
  measure_performance.py open   --ffi build/src/ffi/libgs3d_ffi.so --bundle DIR [--repeat N]
  measure_performance.py pack   --pack build/src/gs3d_pack --source data.csv --out DIR [--repeat N]

打开测量走引擎 C ABI（gs3d_engine_create_simple + gs3d_engine_load_dataset），
和界面开包是同一条路径；峰值内存取 /proc/self/status 的 VmHWM。
测完打印耗时、点数和峰值 RSS，便于和 docs/architecture.md 的预算对照。
"""

import argparse
import ctypes
import json
import os
import shutil
import resource
import subprocess
import sys
import time


def peak_rss_kib() -> int:
    with open("/proc/self/status", encoding="ascii") as status:
        for line in status:
            if line.startswith("VmHWM:"):
                return int(line.split()[1])
    return 0


def measure_open(ffi_path: str, bundle: str) -> dict:
    library = ctypes.CDLL(os.path.abspath(ffi_path))
    library.gs3d_engine_create_simple.restype = ctypes.c_void_p
    library.gs3d_engine_destroy.argtypes = [ctypes.c_void_p]
    library.gs3d_engine_load_dataset.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    library.gs3d_engine_load_dataset.restype = ctypes.c_int
    library.gs3d_engine_point_count.argtypes = [ctypes.c_void_p]
    library.gs3d_engine_point_count.restype = ctypes.c_uint64

    engine = library.gs3d_engine_create_simple()
    if not engine:
        raise SystemExit("gs3d_engine_create_simple returned null (no Vulkan device?)")
    try:
        started = time.perf_counter()
        rc = library.gs3d_engine_load_dataset(engine, os.fsencode(bundle))
        elapsed = time.perf_counter() - started
        if rc != 0:
            raise SystemExit(f"gs3d_engine_load_dataset failed with {rc}")
        points = library.gs3d_engine_point_count(engine)
    finally:
        library.gs3d_engine_destroy(engine)
    return {"seconds": round(elapsed, 2), "points": int(points),
            "peak_rss_mib": round(peak_rss_kib() / 1024)}


def measure_pack(pack: str, source: str, out_dir: str, write_lod: bool = True) -> dict:
    shutil.rmtree(out_dir, ignore_errors=True)
    started = time.perf_counter()
    args = [pack, "--csv", source, "--out", out_dir]
    if not write_lod:
        args.append("--no-lod")
    completed = subprocess.run(args, capture_output=True, text=True)
    elapsed = time.perf_counter() - started
    if completed.returncode != 0:
        raise SystemExit(f"gs3d_pack failed:\n{completed.stderr}")
    manifest = json.load(open(os.path.join(out_dir, "manifest.json"), encoding="utf-8"))
    bundle_bytes = sum(
        os.path.getsize(os.path.join(out_dir, "points", name))
        for name in os.listdir(os.path.join(out_dir, "points"))
    )
    peak_kib = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    return {"seconds": round(elapsed, 2),
            "peak_rss_mib": round(peak_kib / 1024),
            "points": int(manifest["points"]["count"]),
            "chunks": len(manifest.get("chunks", [])),
            "bundle_points_mib": round(bundle_bytes / (1 << 20)),
            "lod": os.path.exists(os.path.join(out_dir, "lod.gs3dlod")),
            "stages": sorted(manifest.get("job", {}).get("stages", {}))}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    open_parser = sub.add_parser("open")
    open_parser.add_argument("--ffi", required=True)
    open_parser.add_argument("--bundle", required=True)
    open_parser.add_argument("--repeat", type=int, default=1, help="warm runs after the first")

    pack_parser = sub.add_parser("pack")
    pack_parser.add_argument("--pack", required=True)
    pack_parser.add_argument("--source", required=True)
    pack_parser.add_argument("--out", required=True)
    pack_parser.add_argument("--repeat", type=int, default=1, help="runs after the first")
    pack_parser.add_argument("--no-lod", action="store_true",
                             help="跳过 lod 阶段（会失去 1.5 s 级开包，但预处理快一倍）")
    args = parser.parse_args()

    for index in range(max(1, args.repeat)):
        if args.command == "open":
            result = measure_open(args.ffi, args.bundle)
        else:
            result = measure_pack(args.pack, args.source, args.out,
                                  write_lod=not args.no_lod)
        print(json.dumps(result, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
