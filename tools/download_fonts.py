#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys
import urllib.request


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
FONT_DIR = REPO_ROOT / "assets" / "fonts"

FONT_SOURCES = [
    (
        "NotoSansCJKsc-Regular.otf",
        "https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf",
    ),
    (
        "NotoSansCJKsc-Bold.otf",
        "https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Bold.otf",
    ),
]


def download(url: str, destination: pathlib.Path) -> None:
    with urllib.request.urlopen(url) as response:
        data = response.read()
    destination.write_bytes(data)


def main() -> int:
    FONT_DIR.mkdir(parents=True, exist_ok=True)

    print(f"[fonts] destination: {FONT_DIR}")
    for filename, url in FONT_SOURCES:
        destination = FONT_DIR / filename
        if destination.exists():
            print(f"[fonts] exists: {destination.name}")
            continue

        print(f"[fonts] downloading: {filename}")
        print(f"[fonts] source: {url}")
        try:
            download(url, destination)
        except Exception as exc:  # noqa: BLE001
            print(f"[fonts] failed: {filename}: {exc}", file=sys.stderr)
            return 1

        if not destination.exists() or destination.stat().st_size == 0:
            print(f"[fonts] invalid download: {destination}", file=sys.stderr)
            return 1

        print(f"[fonts] saved: {destination} ({destination.stat().st_size} bytes)")

    print("[fonts] done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
