#!/usr/bin/env python3
"""Prepare weather backgrounds for ESP32 JPEGDEC (baseline JPEG only, max 800x480)."""

from __future__ import annotations

import os
import sys
from pathlib import Path

MAX_W = 800
MAX_H = 480
BG_DIR = Path(__file__).resolve().parent.parent / "data" / "weather_background"


def fit_size(w: int, h: int) -> tuple[int, int]:
    if w <= MAX_W and h <= MAX_H:
        return w, h
    scale = min(MAX_W / w, MAX_H / h)
    return max(1, int(w * scale)), max(1, int(h * scale))


def is_progressive(path: Path) -> bool:
    from PIL import Image

    with Image.open(path) as im:
        return "progressive" in im.info


def process_jpeg(path: Path, dry_run: bool = False) -> bool:
    from PIL import Image

    with Image.open(path) as im:
        im = im.convert("RGB")
        w, h = im.size
        nw, nh = fit_size(w, h)
        progressive = "progressive" in im.info
        needs_write = progressive or (nw, nh) != (w, h)

        if not needs_write:
            print(f"  {path.name}: {w}x{h} baseline (ok)")
            return False

        if dry_run:
            why = "progressive" if progressive else "resize"
            print(f"  {path.name}: would fix ({why}) {w}x{h} -> {nw}x{nh}")
            return True

        if (nw, nh) != (w, h):
            im = im.resize((nw, nh), Image.Resampling.LANCZOS)
        tmp = path.with_suffix(path.suffix + ".tmp")
        im.save(
            tmp,
            "JPEG",
            quality=85,
            optimize=True,
            progressive=False,
        )
        os.replace(tmp, path)
        print(f"  {path.name}: {w}x{h} -> {nw}x{nh} baseline")
        return True


def main() -> int:
    if not BG_DIR.is_dir():
        print(f"No folder: {BG_DIR}")
        return 0

    print(f"Preparing weather backgrounds in {BG_DIR}...")
    entries = [p for p in sorted(BG_DIR.iterdir()) if p.is_file()]
    if not entries:
        print("  (empty — add day.jpg, night.jpeg, rain.jpeg)")
        return 0

    changed = False
    for path in entries:
        ext = path.suffix.lower()
        if ext in (".jpg", ".jpeg"):
            try:
                if process_jpeg(path):
                    changed = True
            except Exception as e:
                print(f"  {path.name}: error {e}", file=sys.stderr)
                return 1
        else:
            print(f"  {path.name}: skipped (use .jpg/.jpeg for backgrounds)")

    if changed:
        print("Weather backgrounds updated — run: pio run -t uploadfs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
