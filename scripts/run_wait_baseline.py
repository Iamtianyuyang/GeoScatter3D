#!/usr/bin/env python3

import datetime
import json
import os
import pathlib
import re
import subprocess
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
BENCH_DIR = REPO_ROOT / "bench"
BENCH_BINARY = REPO_ROOT / "build" / "GeoScatter3DBenchmark"

BENCH_LINE_RE = re.compile(r"^\[BENCH\]\s+([A-Za-z0-9_]+)\s*=\s*(.+)$")
LOD_ACTIVE_RE = re.compile(r"^\[LOD\]\s+active level = \d+, points = (\d+)")
DATASET_POINT_RE = re.compile(r"^point_count = (\d+)$")
GPU_POINT_RE = re.compile(r"^gpu point_count = (\d+)$")


def run_benchmark(label, extra_env=None, extra_args=None):
    env = os.environ.copy()
    env["GS3D_BENCHMARK_FRAMES"] = env.get("GS3D_BENCHMARK_FRAMES", "600")
    if extra_env:
        env.update(extra_env)

    command = [str(BENCH_BINARY)]
    if extra_args:
        command.extend(extra_args)

    completed = subprocess.run(
        command,
        cwd=REPO_ROOT,
        env=env,
        check=True,
        capture_output=True,
        text=True,
    )

    output = completed.stdout
    metrics = {}
    dataset_point_count = None
    lod_visible_points = None
    full_gpu_point_count = None

    for line in output.splitlines():
        bench_match = BENCH_LINE_RE.match(line.strip())
        if bench_match:
            key, value = bench_match.groups()
            metrics[key] = parse_value(value)
            continue

        dataset_match = DATASET_POINT_RE.match(line.strip())
        if dataset_match:
            dataset_point_count = int(dataset_match.group(1))
            continue

        lod_match = LOD_ACTIVE_RE.match(line.strip())
        if lod_match and lod_visible_points is None:
            lod_visible_points = int(lod_match.group(1))
            continue

        gpu_match = GPU_POINT_RE.match(line.strip())
        if gpu_match:
            full_gpu_point_count = int(gpu_match.group(1))

    result = {
        "label": label,
        "frame_count": metrics.get("frame_count"),
        "present_mode": metrics.get("present_mode"),
        "dataset_point_count": dataset_point_count,
        "effective_rendered_points": (
            full_gpu_point_count
            if full_gpu_point_count is not None
            else lod_visible_points
        ),
        "metrics": extract_metric_groups(metrics),
        "raw_log_path": str(write_raw_log(label, output)),
    }
    return result


def parse_value(value):
    value = value.strip()
    if value in {"FIFO", "IMMEDIATE", "MAILBOX", "OTHER"}:
        return value
    if value.isdigit():
        return int(value)
    try:
        return float(value)
    except ValueError:
        return value


def extract_metric_groups(metrics):
    grouped = {}
    suffixes = ("_p50", "_p95", "_p99")
    for key, value in metrics.items():
        for suffix in suffixes:
            if key.endswith(suffix):
                base = key[: -len(suffix)]
                percentile = suffix[1:]
                grouped.setdefault(base, {})[percentile] = value
                break
    return grouped


def write_raw_log(label, output):
    BENCH_DIR.mkdir(exist_ok=True)
    date_tag = datetime.date.today().strftime("%Y%m%d")
    path = BENCH_DIR / f"wait_benchmark_{label}_{date_tag}.log"
    path.write_text(output, encoding="utf-8")
    return path


def make_summary(default_fifo, default_immediate, fullres_immediate):
    default_wall = default_immediate["metrics"]["wall_frame_ms"]["p50"]
    fullres_wall = fullres_immediate["metrics"]["wall_frame_ms"]["p50"]
    default_points = default_immediate["effective_rendered_points"]
    fullres_points = fullres_immediate["effective_rendered_points"]

    return {
        "default_immediate_wall_p50_ms": default_wall,
        "fullres_immediate_wall_p50_ms": fullres_wall,
        "wall_p50_growth_factor": (
            fullres_wall / default_wall if default_wall else None
        ),
        "default_effective_points": default_points,
        "fullres_effective_points": fullres_points,
        "rendered_point_growth_factor": (
            fullres_points / default_points
            if default_points and fullres_points
            else None
        ),
        "default_fifo_present_mode": default_fifo["present_mode"],
        "default_immediate_present_mode": default_immediate["present_mode"],
        "fullres_immediate_present_mode": fullres_immediate["present_mode"],
    }


def main():
    if not BENCH_BINARY.exists():
        raise SystemExit(f"benchmark binary not found: {BENCH_BINARY}")

    default_fifo = run_benchmark("default_fifo", extra_env={"GS3D_BENCHMARK_PRESENT_MODE": "fifo"})
    default_immediate = run_benchmark("default_immediate", extra_env={"GS3D_BENCHMARK_PRESENT_MODE": "immediate"})
    fullres_immediate = run_benchmark(
        "fullres_immediate",
        extra_env={"GS3D_BENCHMARK_PRESENT_MODE": "immediate"},
        extra_args=["--config", str(REPO_ROOT / "bench" / "viewer_fullres.toml")],
    )

    date_tag = datetime.date.today().strftime("%Y%m%d")
    output_path = BENCH_DIR / f"wait_baseline_{date_tag}.json"
    document = {
        "generated_at": datetime.datetime.now().isoformat(timespec="seconds"),
        "benchmark_binary": str(BENCH_BINARY),
        "default_scene": {
            "fifo": default_fifo,
            "immediate": default_immediate,
        },
        "large_scene_immediate": fullres_immediate,
        "scaling_summary": make_summary(
            default_fifo,
            default_immediate,
            fullres_immediate,
        ),
    }
    output_path.write_text(
        json.dumps(document, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    print(f"[BASELINE] wrote {output_path}")
    print(
        "[BASELINE] default immediate wall p50(ms) =",
        document["scaling_summary"]["default_immediate_wall_p50_ms"],
    )
    print(
        "[BASELINE] fullres immediate wall p50(ms) =",
        document["scaling_summary"]["fullres_immediate_wall_p50_ms"],
    )
    print(
        "[BASELINE] wall growth factor =",
        document["scaling_summary"]["wall_p50_growth_factor"],
    )


if __name__ == "__main__":
    main()
