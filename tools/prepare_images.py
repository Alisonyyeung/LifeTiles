#!/usr/bin/env python3
"""Resize images in data/images/ to fit the display before LittleFS upload.

Requires: pip install Pillow

GIFs larger than the screen are scaled down (keeps animation).
JPEGs larger than the screen are scaled and re-saved (baseline JPEG).
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

MAX_W = 800
MAX_H = 480
IMAGES_DIR = Path(__file__).resolve().parent.parent / "data" / "images"


def atomic_replace(src: Path, dest: Path) -> None:
    """Replace dest with src; fall back if dest is open (e.g. in the editor)."""
    try:
        os.replace(src, dest)
    except OSError:
        fallback = dest.parent / f"{dest.stem}_scaled{dest.suffix}"
        if fallback.exists():
            fallback.unlink()
        os.replace(src, fallback)
        try:
            dest.unlink()
            os.replace(fallback, dest)
        except OSError:
            print(
                f"    (close {dest.name} in your editor, delete it, then re-run prepare_images)"
            )


def fit_size(w: int, h: int, max_w: int, max_h: int) -> tuple[int, int]:
    if w <= max_w and h <= max_h:
        return w, h
    scale = min(max_w / w, max_h / h)
    return max(1, int(w * scale)), max(1, int(h * scale))


def resize_gif(path: Path, max_w: int, max_h: int, dry_run: bool) -> bool:
    from PIL import Image, ImageSequence

    with Image.open(path) as im:
        w, h = im.size
        new_w, new_h = fit_size(w, h, max_w, max_h)
        if (new_w, new_h) == (w, h):
            return False

        print(f"  GIF {path.name}: {w}x{h} -> {new_w}x{new_h}")
        if dry_run:
            return True

        frames: list[Image.Image] = []
        durations: list[int] = []
        default_duration = im.info.get("duration", 100)

        for frame in ImageSequence.Iterator(im):
            rgba = frame.convert("RGBA")
            resized = rgba.resize((new_w, new_h), Image.Resampling.LANCZOS)
            frames.append(resized)
            durations.append(frame.info.get("duration", default_duration))

        if not frames:
            return False

        out_path = path.parent / f"{path.stem}.__resize__.gif"
        frames[0].save(
            out_path,
            format="GIF",
            save_all=True,
            append_images=frames[1:],
            duration=durations,
            loop=im.info.get("loop", 0),
            disposal=2,
            optimize=True,
        )
        atomic_replace(out_path, path)
        return True


def resize_jpeg(path: Path, max_w: int, max_h: int, dry_run: bool) -> bool:
    from PIL import Image

    with Image.open(path) as im:
        w, h = im.size
        new_w, new_h = fit_size(w, h, max_w, max_h)
        if (new_w, new_h) == (w, h):
            return False

        print(f"  JPEG {path.name}: {w}x{h} -> {new_w}x{new_h}")
        if dry_run:
            return True

        if im.mode not in ("RGB", "L"):
            im = im.convert("RGB")
        resized = im.resize((new_w, new_h), Image.Resampling.LANCZOS)
        out_path = path.parent / f"{path.stem}.__resize__.jpg"
        resized.save(out_path, format="JPEG", quality=85, optimize=True, progressive=False)
        atomic_replace(out_path, path)
        return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Resize media in data/images for MyScreen")
    parser.add_argument("--dir", type=Path, default=IMAGES_DIR, help="Images folder")
    parser.add_argument("--max-w", type=int, default=MAX_W)
    parser.add_argument("--max-h", type=int, default=MAX_H)
    parser.add_argument("-n", "--dry-run", action="store_true")
    args = parser.parse_args()

    img_dir: Path = args.dir
    if not img_dir.is_dir():
        print(f"No folder: {img_dir}", file=sys.stderr)
        return 1

    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        print("Install Pillow: pip install Pillow", file=sys.stderr)
        return 1

    changed = 0
    print(f"Preparing images in {img_dir} (max {args.max_w}x{args.max_h})")

    for path in sorted(img_dir.iterdir()):
        if not path.is_file() or path.name.startswith(".") or ".__resize__." in path.name:
            continue
        ext = path.suffix.lower()
        try:
            if ext == ".gif":
                if resize_gif(path, args.max_w, args.max_h, args.dry_run):
                    changed += 1
            elif ext in (".jpg", ".jpeg"):
                if resize_jpeg(path, args.max_w, args.max_h, args.dry_run):
                    changed += 1
        except Exception as exc:
            print(f"  ERROR {path.name}: {exc}", file=sys.stderr)

    print(f"Done. {changed} file(s) {'would be ' if args.dry_run else ''}updated.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
