#pragma once

#include <lvgl.h>

#include "comment_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(comment_cjk_24);

#define COMMENT_MESSAGE_FONT (&comment_cjk_24)

/** Call once before UI; links color emoji imgfont fallback on message fonts. */
void comment_font_init(void);

/** Message font for the selected size (CJK/emoji at 24, Latin sizes include emoji fallback). */
const lv_font_t *comment_font_for_size(comment_font_size_t size);

#ifdef __cplusplus
}
#endif
