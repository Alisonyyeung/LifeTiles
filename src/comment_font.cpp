#include "comment_font.h"

#include "comment_emoji_images.h"

#include <lvgl.h>
#include <string.h>

extern const lv_font_t comment_cjk_24;

static lv_font_t s_cjk_msg;
static lv_font_t s_sans20;
static lv_font_t s_sans30;
static lv_font_t s_sans36;
static lv_font_t *s_emoji_imgfont = nullptr;
static bool s_ready = false;

static bool comment_emoji_imgfont_path(const lv_font_t *font, void *img_src, uint16_t len,
                                         uint32_t unicode, uint32_t unicode_next)
{
    LV_UNUSED(font);
    LV_UNUSED(unicode_next);
    LV_UNUSED(len);

    const lv_img_dsc_t *img = nullptr;
    switch (unicode) {
    case 0x2728:
        img = &comment_emoji_sparkles;
        break;
    case 0x2764:
        img = &comment_emoji_heart;
        break;
    case 0x1F601:
        img = &comment_emoji_grin;
        break;
    case 0x1F60E:
        img = &comment_emoji_cool;
        break;
    case 0x1F44D:
        img = &comment_emoji_thumbsup;
        break;
    case 0x1F4AA:
        img = &comment_emoji_muscle;
        break;
    default:
        return false;
    }

    memcpy(img_src, img, sizeof(lv_img_dsc_t));
    return true;
}

void comment_font_init(void)
{
    if (s_ready) {
        return;
    }

    if (!s_emoji_imgfont) {
        s_emoji_imgfont = lv_imgfont_create(COMMENT_EMOJI_IMG_SIZE, comment_emoji_imgfont_path);
    }

    s_cjk_msg = comment_cjk_24;
    s_cjk_msg.fallback = s_emoji_imgfont;

    s_sans20 = lv_font_montserrat_20;
    s_sans20.fallback = s_emoji_imgfont;

    s_sans30 = lv_font_montserrat_30;
    s_sans30.fallback = s_emoji_imgfont;

    s_sans36 = lv_font_montserrat_36;
    s_sans36.fallback = s_emoji_imgfont;

    s_ready = true;
}

const lv_font_t *comment_font_for_size(comment_font_size_t size)
{
    comment_font_init();
    switch (size) {
    case COMMENT_FONT_SIZE_20:
        return &s_sans20;
    case COMMENT_FONT_SIZE_30:
        return &s_sans30;
    case COMMENT_FONT_SIZE_36:
        return &s_sans36;
    default:
        return &s_cjk_msg;
    }
}
