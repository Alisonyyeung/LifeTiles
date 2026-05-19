#include "seq_anim.h"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <lvgl.h>
#include <stdlib.h>
#include <string.h>

#include "jpg_decode.h"
#include "ESP_Panel_Conf.h"
#include <esp_heap_caps.h>

#define SEQ_MAGIC     "MYSEQ1"
#define SEQ_MAGIC2    "MYSEQ2"
#define SEQ_MAGIC_LEN 6
#define SEQ_HDR_LEGACY  8
#define SEQ_HDR_EXT     12
#define SEQ_MAX_JPEG  (400 * 1024)

typedef struct {
    lv_obj_t *img;
    lv_timer_t *timer;
    File file;
    uint16_t nframes;
    uint16_t fit_w;
    uint16_t fit_h;
    uint16_t cur;
    uint32_t next_off;
    uint32_t data0;
    lv_img_dsc_t dsc;
    uint8_t *pixels;
    uint8_t *panel_pixels;
    uint16_t view_zoom;
    bool playing;
} seq_anim_t;

#define SEQ_VIEW_ZOOM_DEFAULT 256

static void seq_apply_view_transform(seq_anim_t *s)
{
    if (!s || !s->img) {
        return;
    }
    lv_img_set_zoom(s->img, s->view_zoom);
    lv_obj_center(s->img);
    lv_obj_t *holder = lv_obj_get_parent(s->img);
    if (holder) {
        lv_obj_invalidate(holder);
    }
}

static void seq_pixels_free(uint8_t *pixels)
{
    if (pixels) {
        heap_caps_free(pixels);
    }
}

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void seq_free(seq_anim_t *s)
{
    if (!s) {
        return;
    }
    if (s->timer) {
        lv_timer_del(s->timer);
        s->timer = NULL;
    }
    if (s->file) {
        s->file.close();
    }
    if (s->pixels && s->pixels != s->panel_pixels) {
        seq_pixels_free(s->pixels);
    }
    s->pixels = NULL;
    if (s->panel_pixels) {
        seq_pixels_free(s->panel_pixels);
        s->panel_pixels = NULL;
    }
    memset(&s->dsc, 0, sizeof(s->dsc));
    free(s);
}

static void on_img_delete(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DELETE) {
        return;
    }
    lv_obj_t *img = lv_event_get_target(e);
    seq_anim_t *s = (seq_anim_t *)lv_obj_get_user_data(img);
    if (s) {
        lv_obj_set_user_data(img, NULL);
        seq_free(s);
    }
}

/** Blit decoded JPEG into a reused full-panel RGB565 buffer (clears previous frame ghosts). */
static bool seq_frame_to_panel_canvas(seq_anim_t *s, const lv_img_dsc_t *src, uint8_t *src_pixels,
                                      lv_img_dsc_t *out_dsc, uint8_t **out_pixels)
{
    if (!s || !src || !src_pixels || !out_dsc || !out_pixels) {
        return false;
    }

    const int cw = ESP_PANEL_LCD_H_RES;
    const int ch = ESP_PANEL_LCD_V_RES;
    const int sw = (int)src->header.w;
    const int sh = (int)src->header.h;
    if (sw <= 0 || sh <= 0 || cw <= 0 || ch <= 0) {
        return false;
    }

    const size_t bytes = (size_t)cw * (size_t)ch * 2u;
    if (!s->panel_pixels) {
        s->panel_pixels = (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s->panel_pixels) {
            s->panel_pixels = (uint8_t *)malloc(bytes);
        }
        if (!s->panel_pixels) {
            return false;
        }
    }

    uint16_t *canvas = (uint16_t *)s->panel_pixels;
    for (size_t i = 0; i < (size_t)cw * (size_t)ch; ++i) {
        canvas[i] = 0xFFFF;
    }

    const int ox = (cw - sw) / 2;
    const int oy = (ch - sh) / 2;
    const uint16_t *src_px = (const uint16_t *)src_pixels;
    for (int y = 0; y < sh; ++y) {
        if (oy + y < 0 || oy + y >= ch) {
            continue;
        }
        if (ox < 0 || ox + sw > cw) {
            continue;
        }
        memcpy(&canvas[(oy + y) * cw + ox], &src_px[y * sw], (size_t)sw * 2u);
    }

    jpg_decode_release(src_pixels);

    out_dsc->header.cf = LV_IMG_CF_TRUE_COLOR;
    out_dsc->header.w = (uint16_t)cw;
    out_dsc->header.h = (uint16_t)ch;
    out_dsc->data_size = bytes;
    out_dsc->data = (const uint8_t *)canvas;
    *out_pixels = s->panel_pixels;
    return true;
}

static bool seq_decode_frame_at(seq_anim_t *s, uint32_t *out_delay_ms)
{
    if (!s->file || !s->file.seek(s->next_off, SeekSet)) {
        return false;
    }

    uint8_t fh[6];
    if (s->file.read(fh, 6) != 6) {
        return false;
    }

    const uint16_t dly = rd_u16(fh);
    const uint32_t jlen = rd_u32(fh + 2);
    if (jlen == 0 || jlen > SEQ_MAX_JPEG) {
        Serial.printf("seq: bad jpeg len %u at off %u\n", (unsigned)jlen, (unsigned)s->next_off);
        return false;
    }

    uint8_t *jpeg = (uint8_t *)heap_caps_malloc(jlen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jpeg) {
        jpeg = (uint8_t *)heap_caps_malloc(jlen, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!jpeg) {
        return false;
    }
    if (s->file.read(jpeg, jlen) != (int)jlen) {
        heap_caps_free(jpeg);
        return false;
    }

    lv_img_dsc_t decoded;
    uint8_t *decoded_pixels = NULL;
    const bool ok = jpg_decode_memory(jpeg, jlen, &decoded, &decoded_pixels);
    heap_caps_free(jpeg);

    if (!ok || !decoded_pixels) {
        return false;
    }

    const int cw = ESP_PANEL_LCD_H_RES;
    const int ch = ESP_PANEL_LCD_V_RES;
    const int dw = (int)decoded.header.w;
    const int dh = (int)decoded.header.h;
    const bool letterboxed_on_panel = (dw == cw && dh == ch && (s->fit_w < (uint16_t)cw || s->fit_h < (uint16_t)ch));
    const bool needs_panel = letterboxed_on_panel || dw > cw || dh > ch;

    if (dw > 0 && dh > 0 && !letterboxed_on_panel) {
        s->fit_w = (uint16_t)dw;
        s->fit_h = (uint16_t)dh;
    }

    if (s->pixels && s->pixels != s->panel_pixels) {
        seq_pixels_free(s->pixels);
        s->pixels = NULL;
    }

    if (needs_panel) {
        lv_img_dsc_t panel_dsc;
        uint8_t *display_pixels = NULL;
        if (!seq_frame_to_panel_canvas(s, &decoded, decoded_pixels, &panel_dsc, &display_pixels)) {
            jpg_decode_release(decoded_pixels);
            return false;
        }
        s->pixels = display_pixels;
        memcpy(&s->dsc, &panel_dsc, sizeof(panel_dsc));
    } else {
        s->pixels = decoded_pixels;
        memcpy(&s->dsc, &decoded, sizeof(decoded));
    }

    lv_img_cache_invalidate_src(lv_img_get_src(s->img));
    lv_img_set_src(s->img, &s->dsc);
    seq_apply_view_transform(s);

    s->next_off = s->file.position();
    s->cur++;
    if (s->cur >= s->nframes) {
        s->cur = 0;
        s->next_off = s->data0;
    }

    if (out_delay_ms) {
        *out_delay_ms = (dly < 5u) ? 5u : (uint32_t)dly;
    }
    return true;
}

static void seq_timer_cb(lv_timer_t *t)
{
    seq_anim_t *s = (seq_anim_t *)t->user_data;
    if (!s || !s->playing || !s->img) {
        return;
    }

    uint32_t dly = 33;
    if (!seq_decode_frame_at(s, &dly)) {
        lv_timer_pause(t);
        return;
    }
    lv_timer_set_period(t, dly);
}

lv_obj_t *seq_anim_create(lv_obj_t *parent, const char *littlefs_path)
{
    if (!parent || !littlefs_path) {
        return NULL;
    }

    seq_anim_t *s = (seq_anim_t *)calloc(1, sizeof(seq_anim_t));
    if (!s) {
        return NULL;
    }

    s->file = LittleFS.open(littlefs_path, "r");
    if (!s->file) {
        Serial.printf("seq: cannot open %s\n", littlefs_path);
        free(s);
        return NULL;
    }

    uint8_t hdr[SEQ_HDR_EXT];
    if (s->file.read(hdr, SEQ_HDR_LEGACY) != SEQ_HDR_LEGACY) {
        s->file.close();
        free(s);
        return NULL;
    }

    const bool is_v2 = (memcmp(hdr, SEQ_MAGIC2, SEQ_MAGIC_LEN) == 0);
    const bool is_v1 = (memcmp(hdr, SEQ_MAGIC, SEQ_MAGIC_LEN) == 0);
    if (!is_v1 && !is_v2) {
        Serial.println("seq: bad magic (use tools/gif_to_seq.py)");
        s->file.close();
        free(s);
        return NULL;
    }

    s->nframes = rd_u16(hdr + SEQ_MAGIC_LEN);
    if (s->nframes == 0 || s->nframes > 2000) {
        Serial.printf("seq: bad frame count %u\n", (unsigned)s->nframes);
        s->file.close();
        free(s);
        return NULL;
    }

    s->fit_w = (uint16_t)ESP_PANEL_LCD_H_RES;
    s->fit_h = (uint16_t)ESP_PANEL_LCD_V_RES;
    s->data0 = SEQ_HDR_LEGACY;
    if (is_v2) {
        if (s->file.read(hdr + SEQ_HDR_LEGACY, 4) != 4) {
            s->file.close();
            free(s);
            return NULL;
        }
        const uint16_t cw = rd_u16(hdr + 8);
        const uint16_t ch = rd_u16(hdr + 10);
        if (cw > 0 && ch > 0) {
            s->fit_w = cw;
            s->fit_h = ch;
        }
        s->data0 = SEQ_HDR_EXT;
    }
    s->next_off = s->data0;
    s->cur = 0;

    s->view_zoom = SEQ_VIEW_ZOOM_DEFAULT;

    s->img = lv_img_create(parent);
    lv_obj_set_user_data(s->img, s);
    lv_obj_add_event_cb(s->img, on_img_delete, LV_EVENT_DELETE, NULL);
    lv_img_set_antialias(s->img, false);

    uint32_t first_dly = 33;
    if (!seq_decode_frame_at(s, &first_dly)) {
        lv_obj_del(s->img);
        return NULL;
    }

    s->playing = false;
    s->timer = lv_timer_create(seq_timer_cb, first_dly, s);
    lv_timer_set_repeat_count(s->timer, -1);
    lv_timer_pause(s->timer);

    return s->img;
}

bool seq_anim_is_instance(const lv_obj_t *img)
{
    if (!img) {
        return false;
    }
    const seq_anim_t *s = (const seq_anim_t *)lv_obj_get_user_data((lv_obj_t *)img);
    return s != NULL && s->img == img;
}

void seq_anim_set_playing(lv_obj_t *img, bool play)
{
    if (!img) {
        return;
    }
    seq_anim_t *s = (seq_anim_t *)lv_obj_get_user_data(img);
    if (!s || !s->timer) {
        return;
    }
    s->playing = play;
    if (play) {
        lv_timer_resume(s->timer);
    } else {
        lv_timer_pause(s->timer);
    }
}

bool seq_anim_get_fit_size(const lv_obj_t *img, uint16_t *out_w, uint16_t *out_h)
{
    if (!img || !out_w || !out_h) {
        return false;
    }
    const seq_anim_t *s = (const seq_anim_t *)lv_obj_get_user_data((lv_obj_t *)img);
    if (!s || !seq_anim_is_instance(img)) {
        return false;
    }
    *out_w = s->fit_w;
    *out_h = s->fit_h;
    return *out_w > 0 && *out_h > 0;
}

void seq_anim_set_view_zoom(lv_obj_t *img, uint16_t zoom)
{
    if (!img || zoom == 0) {
        return;
    }
    seq_anim_t *s = (seq_anim_t *)lv_obj_get_user_data(img);
    if (!s || !seq_anim_is_instance(img)) {
        return;
    }
    s->view_zoom = zoom;
    seq_apply_view_transform(s);
}
