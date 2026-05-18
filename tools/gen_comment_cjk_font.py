#!/usr/bin/env python3
"""Generate lv_font comment_cjk_24 for the comment board (Traditional Chinese + ASCII).

Requires: Node.js (for npx lv_font_conv)

Recommended font (OFL license, free):
  Noto Sans TC — https://fonts.google.com/noto/specimen/Noto+Sans+TC
  Download "NotoSansTC-Regular.ttf" into data/fonts/

This script can auto-download a subset OTF from Google Noto CJK if the TTF is missing.

Usage:
  python tools/gen_comment_cjk_font.py
  pio run
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SYMBOLS_FILE = Path(__file__).resolve().parent / "comment_cjk_symbols.txt"
FONT_DIR = ROOT / "data" / "fonts"
LOCAL_TTF = FONT_DIR / "NotoSansTC-Regular.ttf"
LOCAL_OTF = FONT_DIR / "NotoSansTC-Regular.otf"
OUT_C = ROOT / "src" / "fonts" / "comment_cjk_24.c"
OUT_H = ROOT / "include" / "comment_font.h"

NOTO_TC_OTF_URL = "https://github.com/googlefonts/noto-cjk/raw/main/Sans/SubsetOTF/TC/NotoSansTC-Regular.otf"


def load_symbols() -> str:
    text = SYMBOLS_FILE.read_text(encoding="utf-8")
    chars: list[str] = []
    seen: set[str] = set()
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        for ch in line:
            if ch not in seen and ch not in "\r\n\t ":
                seen.add(ch)
                chars.append(ch)
    return "".join(chars)


def ensure_font() -> Path:
    FONT_DIR.mkdir(parents=True, exist_ok=True)
    if LOCAL_TTF.is_file():
        return LOCAL_TTF
    if LOCAL_OTF.is_file():
        return LOCAL_OTF
    print(f"Downloading Noto Sans TC subset to {LOCAL_OTF} ...")
    urllib.request.urlretrieve(NOTO_TC_OTF_URL, LOCAL_OTF)
    return LOCAL_OTF


def run_lv_font_conv(font_path: Path, symbols: str) -> None:
    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        "npx",
        "--yes",
        "lv_font_conv",
        "--font",
        str(font_path),
        "--size",
        "24",
        "--bpp",
        "4",
        "--format",
        "lvgl",
        "--no-compress",
        "--lv-font-name",
        "comment_cjk_24",
        "--force-fast-kern-format",
        "-o",
        str(OUT_C),
        "-r",
        "0x20-0x7E",
        "-r",
        "0xA0-0xFF",
        "-r",
        "0x3000-0x303F",
        "--symbols",
        symbols,
    ]
    print("Running lv_font_conv ->", OUT_C.name)
    subprocess.run(cmd, cwd=ROOT, check=True, shell=(sys.platform == "win32"))
    text = OUT_C.read_text(encoding="utf-8")
    text = text.replace('#include "lvgl/lvgl.h"', '#include "lvgl.h"')
    OUT_C.write_text(text, encoding="utf-8")


def write_header() -> None:
    OUT_H.write_text(
        """#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 24px Noto Sans TC subset for the comment board (see tools/gen_comment_cjk_font.py). */
LV_FONT_DECLARE(comment_cjk_24);

#define COMMENT_MESSAGE_FONT (&comment_cjk_24)

#ifdef __cplusplus
}
#endif
""",
        encoding="utf-8",
    )


def dedupe_symbols_file() -> tuple[int, int]:
    """Remove duplicate characters from comment_cjk_symbols.txt (first occurrence wins)."""
    lines = SYMBOLS_FILE.read_text(encoding="utf-8").splitlines()
    header = [ln for ln in lines if not ln.strip() or ln.strip().startswith("#")]
    glyph_lines = [
        ln.strip() for ln in lines if ln.strip() and not ln.strip().startswith("#")
    ]
    raw_len = sum(len(gl) for gl in glyph_lines)
    seen: set[str] = set()
    blocks: list[str] = []
    for gl in glyph_lines:
        block: list[str] = []
        for ch in gl:
            if ch in seen:
                continue
            seen.add(ch)
            block.append(ch)
        blocks.append("".join(block))
    out: list[str] = []
    if header:
        out.append(header[0])
    if len(blocks) >= 1 and blocks[0]:
        out.append(blocks[0])
    if len(blocks) >= 2 and blocks[1]:
        out.append(blocks[1])
    extra = "".join(blocks[2:])
    if extra:
        out.append("# Additional phrases (deduped — no character repeats a line above)")
        out.append(extra)
    SYMBOLS_FILE.write_text("\n".join(out) + "\n", encoding="utf-8")
    return raw_len, len(seen)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--symbols-only",
        action="store_true",
        help="Print merged symbol string and exit",
    )
    parser.add_argument(
        "--dedupe-symbols",
        action="store_true",
        help="Rewrite comment_cjk_symbols.txt with duplicate characters removed",
    )
    args = parser.parse_args()
    if args.dedupe_symbols:
        raw, unique = dedupe_symbols_file()
        print(f"Deduped {SYMBOLS_FILE.name}: {raw} chars -> {unique} unique glyphs")
    symbols = load_symbols()
    if args.symbols_only:
        print(symbols)
        print(f"({len(symbols)} codepoints)", file=sys.stderr)
        return 0
    if not symbols:
        print("No symbols in", SYMBOLS_FILE, file=sys.stderr)
        return 1
    print(
        f"Generating comment_cjk_24 (Noto Sans TC) with {len(symbols)} CJK symbols + ASCII …"
    )
    font_path = ensure_font()
    run_lv_font_conv(font_path, symbols)
    write_header()
    kb = OUT_C.stat().st_size / 1024
    print(f"Wrote {OUT_C} ({kb:.0f} KiB) and {OUT_H}")
    print("Enable LV_FONT_FMT_TXT_LARGE in lib/lv_conf.h if the build asks for it.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
