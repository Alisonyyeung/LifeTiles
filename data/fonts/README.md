# Comment board Chinese font (Traditional)

The device uses a **custom LVGL bitmap font** (`comment_cjk_24`, 24px) for the comment screen.

## Recommended typefaces (free / open license)

| Font | License | Download |
|------|---------|----------|
| **Noto Sans TC** (default in build script) | [OFL](https://scripts.sil.org/OFL) | [Google Fonts](https://fonts.google.com/noto/specimen/Noto+Sans+TC) |
| **Source Han Sans TC** | [OFL](https://scripts.sil.org/OFL) | [Adobe / GitHub](https://github.com/adobe-fonts/source-han-sans) |
| **cwTeXHei** | GPL | [CTAN / cwTeX](https://github.com/LinYuanRu/cwte-x-hei) |
| **全字庫正楷體** | [Open Government License (Taiwan)](https://www.gov.tw/copyright) | [CNS 11643](https://www.cns11643.gov.tw/) |

For **Simplified** Chinese, use Noto Sans SC and change `tools/gen_comment_cjk_font.py` to point at the SC font paths.

## Generate / update the font

1. Install [Node.js](https://nodejs.org/) (for `npx lv_font_conv`).
2. Optional: place `NotoSansTC-Regular.ttf` in this folder (otherwise the script downloads Noto Sans TC OTF).
3. Edit glyph coverage in `tools/comment_cjk_symbols.txt` (Traditional characters you need).
4. Run:

```bash
python tools/gen_comment_cjk_font.py
pio run
```

Output: `src/fonts/comment_cjk_24.c`, `include/comment_font.h`.

## LVGL online converter

You can also build a font at [lvgl.io/tools/fontconverter](https://lvgl.io/tools/fontconverter):

- **Font**: Noto Sans TC (or your `.ttf` / `.otf`)
- **Size**: 24–28
- **Bpp**: 4
- **Range**: `0x20-0x7E`, `0x3000-0x303F`
- **Symbols**: paste text from `tools/comment_cjk_symbols.txt`

Name the result `comment_cjk_24` and add `LV_FONT_DECLARE(comment_cjk_24)` in `lib/lv_conf.h`.
