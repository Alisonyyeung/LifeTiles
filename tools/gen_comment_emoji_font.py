#!/usr/bin/env python3
"""Generate comment_emoji_24 — preset message-board emoji only (small flash).

Uses Segoe UI Emoji on Windows or any .ttf path via --font.

  python tools/gen_comment_emoji_font.py
  pio run

Symbols are base emoji only (no skin-tone modifiers or VS16 — those render as boxes).
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_C = ROOT / "src" / "fonts" / "comment_emoji_24.c"
# ✨ ❤ 😁 😎 👍 💪
EMOJI_SYMBOLS = "\u2728\u2764\ud83d\ude01\ud83d\ude0e\ud83d\udc4d\ud83d\udcaa"
WIN_FONT = Path(r"C:\WINDOWS\Fonts\seguiemj.ttf")


def find_font() -> Path:
    if WIN_FONT.is_file():
        return WIN_FONT
    for p in (ROOT / "data" / "fonts").glob("*.ttf"):
        if "emoji" in p.name.lower() or "segui" in p.name.lower():
            return p
    raise SystemExit("No emoji .ttf found. Install Segoe UI Emoji or pass --font path.ttf")


def main() -> None:
    font = find_font()
    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        "npx",
        "--yes",
        "lv_font_conv",
        "--font",
        str(font),
        "--size",
        "24",
        "--bpp",
        "4",
        "--format",
        "lvgl",
        "--no-compress",
        "--lv-font-name",
        "comment_emoji_24",
        "--force-fast-kern-format",
        "-o",
        str(OUT_C),
        "--symbols",
        EMOJI_SYMBOLS,
    ]
    print("Generating", OUT_C.name, "from", font.name)
    subprocess.run(cmd, check=True, cwd=ROOT, shell=(sys.platform == "win32"))
    text = OUT_C.read_text(encoding="utf-8")
    text = text.replace('#include "lvgl/lvgl.h"', '#include "lvgl.h"')
    OUT_C.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
