#!/usr/bin/env python3
"""
Convert an animated GIF to MyScreen .seq (MYSEQ1): JPEG frames + per-frame delay.

The device web upload page also converts GIF → .seq in the browser before saving;
use this script for offline batches or when you are not using the upload UI.

Firmware: src/seq_anim.cpp — magic MYSEQ1, u16 frame count LE, then per frame:
  u16 delay_ms LE, u32 jpeg_len LE, jpeg bytes.

Requires: pip install pillow

Example:
  python tools/gif_to_seq.py data/images/wall.gif data/images/wall.seq \\
      --max-w 800 --max-h 480 --quality 75 --max-frames 20
"""

from __future__ import annotations

import argparse
import io
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Install Pillow: pip install pillow", file=sys.stderr)
    sys.exit(1)

SEQ_MAGIC = b"MYSEQ2"


def scale_to_fit(im: Image.Image, max_w: int, max_h: int) -> Image.Image:
    w, h = im.size
    if w <= max_w and h <= max_h:
        return im
    rw = max_w / w
    rh = max_h / h
    r = min(rw, rh)
    nw = max(1, int(w * r))
    nh = max(1, int(h * r))
    return im.resize((nw, nh), Image.Resampling.LANCZOS)


def frame_to_jpeg_bytes(im: Image.Image, quality: int) -> bytes:
    if im.mode not in ("RGB", "L"):
        im = im.convert("RGB")
    buf = io.BytesIO()
    im.save(buf, format="JPEG", quality=quality, optimize=True, subsampling=2)
    return buf.getvalue()


def subsample_frame_indices(n: int, cap: int) -> list[int]:
    """Evenly pick up to `cap` indices from 0..n-1 (inclusive ends)."""
    if n <= 0 or cap <= 0:
        return []
    if cap == 1:
        return [0]
    if n <= cap:
        return list(range(n))
    out: list[int] = []
    cap1 = cap - 1
    for k in range(cap):
        idx = round(k * (n - 1) / cap1)
        if out and idx <= out[-1]:
            idx = out[-1] + 1
        out.append(min(idx, n - 1))
    return out


def pick_source_indices(n: int, step: int, max_frames: int) -> list[int]:
    """After --step, subsample to at most max_frames source indices."""
    stepped = list(range(0, n, step))
    if not stepped:
        return []
    j_pick = subsample_frame_indices(len(stepped), max_frames)
    return [stepped[j] for j in j_pick]


def _gif_background_rgba(im: Image.Image) -> tuple[int, int, int, int]:
    idx = im.info.get("background")
    if im.palette is not None and idx is not None:
        try:
            color = im.palette.getcolor(int(idx), im.mode)
            if color:
                return (color[0], color[1], color[2], 255)
        except Exception:
            pass
    return (255, 255, 255, 255)


def _frame_disposal(im: Image.Image) -> int:
    disp = getattr(im, "disposal_method", None)
    if disp is not None:
        return int(disp)
    return int(im.info.get("disposal", 0) or 0)


def _frame_offset(im: Image.Image) -> tuple[int, int]:
    if not im.tile:
        return (0, 0)
    for tile in im.tile:
        if tile and len(tile) >= 1:
            off = tile[0]
            return (int(off[0]), int(off[1]))
    return (0, 0)


def _draw_gif_patch(im: Image.Image, canvas: Image.Image) -> None:
    """Composite one GIF patch onto the running canvas."""
    patch = im.convert("RGBA")
    ox, oy = _frame_offset(im)
    layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    layer.paste(patch, (ox, oy), patch)
    canvas.alpha_composite(layer)


def extract_composited_frames(im: Image.Image) -> list[Image.Image]:
    """
    Build full logical-screen RGBA images (GIF89a disposal aware).

    Exporting raw patches or skipping frames when subsampling leaves motion trails
    on the device because each .seq frame must be a complete picture.
    """
    n = im.n_frames
    size = im.size
    bg = _gif_background_rgba(im)
    canvas = Image.new("RGBA", size, bg)
    backup = canvas.copy()
    composed: list[Image.Image] = []

    for i in range(n):
        if i > 0:
            prev_disp = _frame_disposal_at(im, i - 1)
            if prev_disp == 2:
                canvas = Image.new("RGBA", size, bg)
            elif prev_disp == 3:
                canvas = backup.copy()

        backup = canvas.copy()
        im.seek(i)
        _draw_gif_patch(im, canvas)
        composed.append(canvas.copy())

    return composed


def _frame_disposal_at(im: Image.Image, index: int) -> int:
    im.seek(index)
    return _frame_disposal(im)


def rgba_to_fixed_canvas(rgba: Image.Image, max_w: int, max_h: int) -> Image.Image:
    """Letterbox onto a fixed RGB canvas so every JPEG has identical dimensions."""
    scaled = scale_to_fit(rgba, max_w, max_h)
    out = Image.new("RGB", (max_w, max_h), (255, 255, 255))
    ox = (max_w - scaled.width) // 2
    oy = (max_h - scaled.height) // 2
    if scaled.mode == "RGBA":
        out.paste(scaled, (ox, oy), scaled.split()[3])
    else:
        out.paste(scaled, (ox, oy))
    return out


def gif_to_seq(
    src: Path,
    dst: Path,
    *,
    max_w: int,
    max_h: int,
    quality: int,
    step: int,
    max_frames: int,
    default_delay_ms: int,
) -> None:
    src = src.resolve()
    dst = dst.resolve()

    with Image.open(src) as gif:
        total = gif.n_frames
        composed_all = extract_composited_frames(gif)

    want = set(pick_source_indices(total, step, max_frames))
    if not want:
        raise SystemExit("No frames to export")

    frames: list[Image.Image] = []
    delays: list[int] = []
    content_w = 0
    content_h = 0

    with Image.open(src) as gif:
        for i in sorted(want):
            if i < 0 or i >= len(composed_all):
                continue
            scaled = scale_to_fit(composed_all[i], max_w, max_h)
            if content_w == 0:
                content_w, content_h = scaled.size
            if scaled.mode == "RGBA":
                frames.append(scaled.convert("RGB"))
            else:
                frames.append(scaled.convert("RGB"))
            gif.seek(i)
            d = gif.info.get("duration")
            if d is None:
                d = default_delay_ms
            else:
                d = int(d)
                if d < 1:
                    d = default_delay_ms
            delays.append(min(65535, max(1, d)))

    if not frames:
        raise SystemExit("No frames extracted")

    out = bytearray(SEQ_MAGIC)
    out += struct.pack("<H", len(frames))
    out += struct.pack("<H", content_w)
    out += struct.pack("<H", content_h)

    for im, dly in zip(frames, delays):
        jpeg = frame_to_jpeg_bytes(im, quality)
        if len(jpeg) > 400 * 1024:
            raise SystemExit(f"Frame JPEG too large ({len(jpeg)} bytes); lower quality or resolution")
        out += struct.pack("<H", dly)
        out += struct.pack("<I", len(jpeg))
        out += jpeg

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(out)
    print(f"Wrote {dst} ({len(frames)} frames, {len(out)} bytes, {max_w}x{max_h} each)")


def main() -> None:
    p = argparse.ArgumentParser(description="GIF → MyScreen .seq (JPEG frame sequence)")
    p.add_argument("input_gif", type=Path)
    p.add_argument("output_seq", type=Path)
    p.add_argument("--max-w", type=int, default=800, help="Max width (default 800)")
    p.add_argument("--max-h", type=int, default=480, help="Max height (default 480)")
    p.add_argument("--quality", type=int, default=75, help="JPEG quality 1–95 (default 75)")
    p.add_argument("--step", type=int, default=1, help="Consider every Nth source frame before capping (default 1)")
    p.add_argument(
        "--max-frames",
        type=int,
        default=20,
        metavar="N",
        help="Max frames in .seq; GIF is evenly subsampled (default 20)",
    )
    p.add_argument(
        "--default-delay-ms",
        type=int,
        default=50,
        help="Delay if GIF omits duration (default 50)",
    )
    args = p.parse_args()
    if args.quality < 1 or args.quality > 95:
        p.error("--quality must be 1–95")
    if args.step < 1:
        p.error("--step must be >= 1")
    if args.max_frames < 1:
        p.error("--max-frames must be >= 1")

    gif_to_seq(
        args.input_gif,
        args.output_seq,
        max_w=args.max_w,
        max_h=args.max_h,
        quality=args.quality,
        step=args.step,
        max_frames=args.max_frames,
        default_delay_ms=args.default_delay_ms,
    )


if __name__ == "__main__":
    main()
