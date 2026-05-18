#include "image_screen.h"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <lvgl.h>
#include <string.h>
#include <strings.h>

#if LV_USE_GIF
#include <extra/libs/gif/lv_gif.h>
#endif

#include "ESP_Panel_Conf.h"
#include "app_theme.h"
#include "image_storage.h"
#include "nav_gestures.h"
#include "jpg_decode.h"
#include "lvgl_port.h"
#include "seq_anim.h"
#include "webp_decode.h"

static lv_obj_t *tile_root;
static lv_obj_t *viewer_holder;
static lv_obj_t *viewer_widget;
static lv_obj_t *label_empty;
static lv_obj_t *label_name;
static lv_obj_t *btn_next;
static lv_obj_t *lbl_next;
static lv_obj_t *btn_fit;
static lv_obj_t *lbl_fit;
static lv_timer_t *chrome_hide_timer;
static bool is_fitted = false;
static bool is_gif_viewer = false;
static bool is_seq_viewer = false;
static uint8_t *decoded_pixel_buf = NULL;
static lv_img_dsc_t decoded_img_dsc;
static bool playback_wanted = false;
/** Basename of file currently shown (LittleFS path tail); empty if none. */
static char s_open_basename[64];

#define CHROME_HIDE_MS 3000
#define ZOOM_DEFAULT   256

static void hide_image_chrome(void);
static void show_image_chrome(void);
static void arm_chrome_hide_timer(void);
static void apply_default_view(void);
static void update_fit_btn_label(void);
static void style_image_chrome_buttons(void);

static void clear_viewer(void)
{
    if (viewer_widget) {
        lv_obj_del(viewer_widget);
        viewer_widget = NULL;
    }
    is_gif_viewer = false;
    is_seq_viewer = false;
    is_fitted = false;
    s_open_basename[0] = '\0';
    hide_image_chrome();
    if (decoded_pixel_buf) {
        free(decoded_pixel_buf);
        decoded_pixel_buf = NULL;
    }
}

static bool path_has_ext(const char *path, const char *ext)
{
    const char *dot = strrchr(path, '.');
    if (!dot) {
        return false;
    }
    return strcasecmp(dot, ext) == 0;
}

static bool path_is_jpeg(const char *path)
{
    return path_has_ext(path, ".jpg") || path_has_ext(path, ".jpeg");
}

/** If `file.gif` exists, return path to `file.seq` in `out` when that file is on LittleFS. */
static bool littlefs_seq_sibling(const char *fs_path, char *out, size_t out_len)
{
    if (!fs_path || !out || out_len < 5) {
        return false;
    }
    strncpy(out, fs_path, out_len - 1);
    out[out_len - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (!dot) {
        return false;
    }
    if ((size_t)(dot - out) + 5 >= out_len) {
        return false;
    }
    strcpy(dot, ".seq");
    return LittleFS.exists(out);
}

static bool lvgl_path_to_littlefs(const char *lvgl_path, char *out, size_t out_len)
{
    if (!lvgl_path || !out || out_len == 0) {
        return false;
    }
    if (lvgl_path[0] == 'S' && lvgl_path[1] == ':') {
        snprintf(out, out_len, "%s", lvgl_path + 2);
        return true;
    }
    snprintf(out, out_len, "%s", lvgl_path);
    return true;
}

static bool gif_probe_size(const char *fs_path, uint16_t *out_w, uint16_t *out_h)
{
    File f = LittleFS.open(fs_path, "r");
    if (!f) {
        return false;
    }
    uint8_t hdr[10];
    if (f.read(hdr, 10) != 10) {
        f.close();
        return false;
    }
    f.close();
    if (memcmp(hdr, "GIF", 3) != 0) {
        return false;
    }
    *out_w = (uint16_t)(hdr[6] | (hdr[7] << 8));
    *out_h = (uint16_t)(hdr[8] | (hdr[9] << 8));
    return true;
}

/** LVGL GIF decoder needs ~5 * w * h bytes; reject if unlikely to fit. */
static bool gif_fits_ram(uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0) {
        return false;
    }
    /* Avoid pathological dimensions / overflow */
    if (w > 4096u || h > 4096u) {
        Serial.printf("gif: %ux%u too wide/tall (max 4096)\n", (unsigned)w, (unsigned)h);
        return false;
    }
    const size_t need = w * h * 5u + 8192u;
    const size_t psram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t internal = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (need > psram && need > internal) {
        Serial.printf("gif: %ux%u needs ~%u bytes, not enough RAM\n", (unsigned)w, (unsigned)h, (unsigned)need);
        return false;
    }
    return true;
}

/**
 * LVGL zoom for "Fit screen" (256 = 100%).
 * - Image larger than the panel at 100%: contain (min) so the whole image is visible.
 * - Image already fits inside at 100% but leaves margins: cover (max) to fill the panel
 *   (e.g. 700×480 with height touching edges but empty space on the sides).
 */
static uint16_t calc_fit_zoom(uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0) {
        return ZOOM_DEFAULT;
    }
    const int max_w = ESP_PANEL_LCD_H_RES;
    const int max_h = ESP_PANEL_LCD_V_RES;
    const int zw = (256 * max_w) / (int)w;
    const int zh = (256 * max_h) / (int)h;
    const int sw = (int)((w * (uint64_t)ZOOM_DEFAULT) / 256u);
    const int sh = (int)((h * (uint64_t)ZOOM_DEFAULT) / 256u);
    const bool fits_at_default = (sw <= max_w && sh <= max_h);
    int z = fits_at_default ? ((zw > zh) ? zw : zh) : ((zw < zh) ? zw : zh);
    if (z < 32) {
        z = 32;
    }
    const int z_max = 1024;
    if (z > z_max) {
        z = z_max;
    }
    return (uint16_t)z;
}

static bool get_viewer_pixel_size(uint32_t *out_w, uint32_t *out_h)
{
    if (!viewer_widget || !out_w || !out_h) {
        return false;
    }
    const void *src = lv_img_get_src(viewer_widget);
    if (!src) {
        return false;
    }
    const lv_img_src_t type = lv_img_src_get_type(src);
    if (type == LV_IMG_SRC_VARIABLE) {
        const lv_img_dsc_t *dsc = (const lv_img_dsc_t *)src;
        *out_w = dsc->header.w;
        *out_h = dsc->header.h;
        return *out_w > 0 && *out_h > 0;
    }
    if (type == LV_IMG_SRC_FILE) {
        lv_img_header_t header;
        if (lv_img_decoder_get_info(src, &header) == LV_RES_OK) {
            *out_w = header.w;
            *out_h = header.h;
            return *out_w > 0 && *out_h > 0;
        }
    }
    return false;
}

static void update_fit_btn_label(void)
{
    if (!lbl_fit) {
        return;
    }
    lv_label_set_text(lbl_fit, is_fitted ? "Return default size" : "Fit screen");
}

static void apply_default_view(void)
{
    if (!viewer_widget) {
        return;
    }
    lv_img_set_zoom(viewer_widget, ZOOM_DEFAULT);
    if (is_seq_viewer && seq_anim_is_instance(viewer_widget)) {
        seq_anim_set_view_zoom(viewer_widget, ZOOM_DEFAULT);
    }
    lv_obj_center(viewer_widget);
    is_fitted = false;
    update_fit_btn_label();
}

static void apply_fit_view(void)
{
    uint32_t w = 0;
    uint32_t h = 0;
    if (!viewer_widget) {
        return;
    }
    if (is_seq_viewer && seq_anim_is_instance(viewer_widget)) {
        if (!get_viewer_pixel_size(&w, &h)) {
            uint16_t cw = 0;
            uint16_t ch = 0;
            if (!seq_anim_get_fit_size(viewer_widget, &cw, &ch)) {
                return;
            }
            w = cw;
            h = ch;
        }
    } else if (!get_viewer_pixel_size(&w, &h)) {
        return;
    }
    const uint16_t z = calc_fit_zoom(w, h);
    lv_img_set_zoom(viewer_widget, z);
    if (is_seq_viewer && seq_anim_is_instance(viewer_widget)) {
        seq_anim_set_view_zoom(viewer_widget, z);
    }
    lv_obj_center(viewer_widget);
    lv_obj_invalidate(viewer_widget);
    is_fitted = true;
    update_fit_btn_label();
}

static void chrome_hide_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    hide_image_chrome();
}

static void hide_image_chrome(void)
{
    if (btn_fit) {
        lv_obj_add_flag(btn_fit, LV_OBJ_FLAG_HIDDEN);
    }
    if (btn_next) {
        lv_obj_add_flag(btn_next, LV_OBJ_FLAG_HIDDEN);
    }
    if (label_name) {
        lv_obj_add_flag(label_name, LV_OBJ_FLAG_HIDDEN);
    }
    if (chrome_hide_timer) {
        lv_timer_pause(chrome_hide_timer);
    }
}

static void arm_chrome_hide_timer(void)
{
    if (!chrome_hide_timer) {
        chrome_hide_timer = lv_timer_create(chrome_hide_timer_cb, CHROME_HIDE_MS, NULL);
    } else {
        lv_timer_set_period(chrome_hide_timer, CHROME_HIDE_MS);
        lv_timer_reset(chrome_hide_timer);
    }
    lv_timer_resume(chrome_hide_timer);
}

static void show_image_chrome(void)
{
    if (!viewer_widget) {
        return;
    }
    if (btn_fit) {
        lv_obj_clear_flag(btn_fit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(btn_fit);
    }
    if (btn_next) {
        lv_obj_clear_flag(btn_next, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(btn_next);
    }
    if (label_name && s_open_basename[0] != '\0') {
        lv_obj_clear_flag(label_name, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(label_name);
    }
    arm_chrome_hide_timer();
}

static void style_image_chrome_buttons(void)
{
    app_theme_style_action_btn(btn_next, lbl_next);
    app_theme_style_action_btn(btn_fit, lbl_fit);
}

static void image_bubble_nav(lv_obj_t *obj)
{
    nav_gestures_enable_bubble(obj);
}

static void on_viewer_area_click(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (viewer_widget) {
        show_image_chrome();
    }
}

static void on_btn_fit_click(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (!viewer_widget) {
        return;
    }
    if (is_fitted) {
        apply_default_view();
    } else {
        apply_fit_view();
    }
    show_image_chrome();
}

static void apply_gif_playback(void)
{
#if LV_USE_GIF
    if (viewer_widget && is_gif_viewer) {
        lv_gif_t *gifobj = (lv_gif_t *)viewer_widget;
        if (gifobj->gif && gifobj->timer) {
            if (playback_wanted) {
                lv_timer_resume(gifobj->timer);
            } else {
                lv_timer_pause(gifobj->timer);
            }
            return;
        }
        /* broken GIF object: fall through to .seq handling if applicable */
    }
#endif
    if (viewer_widget && is_seq_viewer && seq_anim_is_instance(viewer_widget)) {
        seq_anim_set_playing(viewer_widget, playback_wanted);
    }
}

void image_screen_set_playback(bool play)
{
    playback_wanted = play;
    apply_gif_playback();
}

static void show_path(const char *path)
{
    clear_viewer();
    lv_obj_add_flag(label_empty, LV_OBJ_FLAG_HIDDEN);

    if (!path || path[0] == '\0') {
        lv_obj_clear_flag(label_empty, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label_name, "");
        return;
    }

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    lv_label_set_text(label_name, base);

    char fs_path[96];
    const bool has_fs_path = lvgl_path_to_littlefs(path, fs_path, sizeof(fs_path));

    char seq_path[96];
    const bool has_seq =
        path_has_ext(path, ".seq") ||
        (has_fs_path && littlefs_seq_sibling(fs_path, seq_path, sizeof(seq_path)));
    const char *seq_fs = path_has_ext(path, ".seq") ? fs_path : seq_path;

    if (has_seq) {
        if (!has_fs_path) {
            lv_label_set_text(label_empty, ".seq path error");
            lv_obj_clear_flag(label_empty, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        viewer_widget = seq_anim_create(viewer_holder, seq_fs);
        if (!viewer_widget) {
            lv_label_set_text(label_empty,
                              ".seq load failed\n"
                              "Re-convert with:\n"
                              "python tools/gif_to_seq.py\n"
                              "in.gif out.seq");
            lv_obj_clear_flag(label_empty, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        is_seq_viewer = true;
        is_gif_viewer = false;
        lv_obj_set_style_bg_color(viewer_holder, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(viewer_holder, LV_OPA_COVER, LV_PART_MAIN);
    } else if (path_has_ext(path, ".gif")) {
        lv_label_set_text(label_empty,
                          "Convert GIF to .seq first\n"
                          "python tools/gif_to_seq.py\n"
                          "fishing.gif fishing.seq");
        lv_obj_clear_flag(label_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    } else if (path_has_ext(path, ".webp")) {
        if (!has_fs_path) {
            lv_label_set_text(label_empty, "WebP path error");
            lv_obj_clear_flag(label_empty, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        if (!webp_decode_file(fs_path, &decoded_img_dsc, &decoded_pixel_buf)) {
            lv_label_set_text(label_empty, "WebP decode failed");
            lv_obj_clear_flag(label_empty, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        viewer_widget = lv_img_create(viewer_holder);
        lv_img_set_src(viewer_widget, &decoded_img_dsc);
        is_seq_viewer = false;
        is_gif_viewer = false;
    } else if (path_is_jpeg(path)) {
        if (!has_fs_path) {
            lv_label_set_text(label_empty, "JPEG path error");
            lv_obj_clear_flag(label_empty, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        if (!jpg_decode_file(fs_path, &decoded_img_dsc, &decoded_pixel_buf)) {
            lv_label_set_text(label_empty, "JPEG decode failed");
            lv_obj_clear_flag(label_empty, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        viewer_widget = lv_img_create(viewer_holder);
        lv_img_set_src(viewer_widget, &decoded_img_dsc);
        is_seq_viewer = false;
        is_gif_viewer = false;
    } else {
        viewer_widget = lv_img_create(viewer_holder);
        lv_img_set_src(viewer_widget, path);
        is_seq_viewer = false;
        is_gif_viewer = false;
    }

    if (!is_seq_viewer) {
        lv_obj_set_style_bg_opa(viewer_holder, LV_OPA_TRANSP, LV_PART_MAIN);
    }
    apply_default_view();
    apply_gif_playback();
    lv_obj_add_event_cb(viewer_widget, on_viewer_area_click, LV_EVENT_CLICKED, NULL);
    image_bubble_nav(viewer_widget);

    snprintf(s_open_basename, sizeof(s_open_basename), "%s", base);
}

void image_screen_close_file_if_displayed(const char *basename)
{
    if (!basename || basename[0] == '\0' || s_open_basename[0] == '\0') {
        return;
    }
    if (strcasecmp(basename, s_open_basename) != 0) {
        return;
    }
    clear_viewer();
    lv_label_set_text(label_name, "");
}

static void on_btn_next(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    image_screen_show_next();
    if (viewer_widget) {
        show_image_chrome();
    }
}

void image_screen_create(lv_obj_t *parent)
{
    tile_root = parent;
    lv_obj_clear_flag(tile_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(tile_root, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(tile_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile_root, 0, LV_PART_MAIN);

    viewer_holder = lv_obj_create(tile_root);
    lv_obj_set_size(viewer_holder, ESP_PANEL_LCD_H_RES, ESP_PANEL_LCD_V_RES);
    lv_obj_align(viewer_holder, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(viewer_holder, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(viewer_holder, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(viewer_holder, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(viewer_holder, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(viewer_holder, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(viewer_holder, on_viewer_area_click, LV_EVENT_CLICKED, NULL);

    label_empty = lv_label_create(viewer_holder);
    lv_label_set_text(label_empty,
                      "Add .seq / .gif / .jpg / .png / .webp\n"
                      "to data/images\n"
                      "(pio run -t uploadfs)");
    lv_obj_set_style_text_font(label_empty, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_empty, lv_color_hex(0x808090), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(label_empty);
    image_bubble_nav(label_empty);

    label_name = lv_label_create(tile_root);
    lv_obj_set_style_text_font(label_name, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_name, lv_color_hex(0x9090A0), LV_PART_MAIN);
    lv_obj_align(label_name, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_flag(label_name, LV_OBJ_FLAG_HIDDEN);

    btn_next = lv_btn_create(tile_root);
    lv_obj_set_size(btn_next, 120, 48);
    lv_obj_align(btn_next, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_obj_add_event_cb(btn_next, on_btn_next, LV_EVENT_CLICKED, NULL);
    lbl_next = lv_label_create(btn_next);
    lv_label_set_text(lbl_next, "Next");
    lv_obj_set_style_text_font(lbl_next, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(lbl_next);
    lv_obj_add_flag(btn_next, LV_OBJ_FLAG_HIDDEN);

    btn_fit = lv_btn_create(tile_root);
    lv_obj_set_size(btn_fit, 248, 48);
    lv_obj_align(btn_fit, LV_ALIGN_TOP_LEFT, 12, 12);
    lv_obj_add_event_cb(btn_fit, on_btn_fit_click, LV_EVENT_CLICKED, NULL);
    lbl_fit = lv_label_create(btn_fit);
    lv_label_set_text(lbl_fit, "Fit screen");
    lv_obj_set_style_text_font(lbl_fit, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(lbl_fit);
    lv_obj_add_flag(btn_fit, LV_OBJ_FLAG_HIDDEN);

    style_image_chrome_buttons();

    image_screen_refresh();
}

void image_screen_show_next(void)
{
    char path[96];
    if (image_storage_next(path, sizeof(path))) {
        show_path(path);
    } else {
        show_path(NULL);
    }
}

void image_screen_show_current(void)
{
    char path[96];
    if (image_storage_path_at(image_storage_current_index(), path, sizeof(path))) {
        show_path(path);
    } else {
        show_path(NULL);
    }
}

enum {
    IMG_PENDING_NONE = 0,
    IMG_PENDING_SHOW_CURRENT,
    IMG_PENDING_REFRESH,
};

static volatile uint8_t s_img_pending = IMG_PENDING_NONE;

void image_screen_request_show_current(void)
{
    s_img_pending = IMG_PENDING_SHOW_CURRENT;
}

void image_screen_request_refresh(void)
{
    s_img_pending = IMG_PENDING_REFRESH;
}

void image_screen_dispatch_pending(void)
{
    const uint8_t job = s_img_pending;
    if (job == IMG_PENDING_NONE) {
        return;
    }
    s_img_pending = IMG_PENDING_NONE;

    lvgl_port_lock(-1);
    if (job == IMG_PENDING_REFRESH) {
        image_storage_rescan();
    }
    image_screen_show_current();
    lvgl_port_unlock();
}

void image_screen_refresh(void)
{
    image_storage_rescan();
    image_screen_show_current();
}

bool image_screen_is_gif_active(void)
{
    return viewer_widget != NULL && (is_gif_viewer || is_seq_viewer);
}

void image_screen_apply_theme(void)
{
    if (!tile_root) {
        return;
    }
    const app_theme_colors_t *c = app_theme_colors();
    const uint32_t bg = app_theme_is_dark() ? 0x000000u : c->bg;
    lv_obj_set_style_bg_color(tile_root, lv_color_hex(bg), LV_PART_MAIN);
    if (label_empty) {
        lv_obj_set_style_text_color(label_empty, lv_color_hex(c->text_muted), LV_PART_MAIN);
    }
    if (label_name) {
        lv_obj_set_style_text_color(label_name, lv_color_hex(c->text_muted), LV_PART_MAIN);
    }
    style_image_chrome_buttons();
}
