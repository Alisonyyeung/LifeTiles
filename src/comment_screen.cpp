#include "comment_screen.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_theme.h"
#include "comment_bubble_deco_images.h"
#include "comment_font.h"
#include "comment_storage.h"
#include "comment_text_normalize.h"
#include "lvgl_port.h"
#include "screen_nav.h"
#include "user_profile.h"

#define BUBBLE_W           700
#define BUBBLE_MIN_H       200
#define BUBBLE_MAX_H       320
#define BUBBLE_PAD         28
#define BUBBLE_TAIL_SIZE   18
#define STYLE_ICON_GAP     8

static lv_obj_t *tile_root;
static lv_obj_t *label_title;
static lv_obj_t *label_hint;
static lv_obj_t *bubble_col;
static lv_obj_t *label_to;
static lv_obj_t *bubble_card;
static lv_obj_t *style_icon;
static lv_obj_t *bubble_tail;
static lv_obj_t *label_warning_hdr;
static lv_obj_t *msg_area;
static lv_obj_t *label_message;
static lv_obj_t *label_received;
static lv_obj_t *btn_clear;
static lv_obj_t *lbl_clear;
static lv_timer_t *marquee_timer;
static lv_timer_t *rainbow_timer;
static uint8_t rainbow_phase;
static lv_coord_t marquee_x;
static comment_display_t s_display;
static bool screen_ready = false;

#define MARQUEE_SEP          "          "
#define MARQUEE_BUF_EXTRA    128

static bool festive_marquee_active(void);

static const lv_img_dsc_t *style_icon_image_for(comment_style_t style)
{
    switch (style) {
    case COMMENT_STYLE_FESTIVE:
        return &comment_style_icon_festive;
    case COMMENT_STYLE_LOVE:
        return &comment_style_icon_love;
    case COMMENT_STYLE_WARNING:
        return &comment_style_icon_warning;
    default:
        return &comment_style_icon_dialogue;
    }
}

static void apply_style_icon(void)
{
    if (!style_icon) {
        return;
    }
    lv_img_set_src(style_icon, style_icon_image_for(s_display.style));
    lv_obj_clear_flag(style_icon, LV_OBJ_FLAG_HIDDEN);
}

static void format_received_line(time_t t, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (t <= 100000) {
        return;
    }
    struct tm tm_local;
    if (!localtime_r(&t, &tm_local)) {
        return;
    }
    snprintf(out, out_len, "Received  %04d-%02d-%02d  %02d:%02d", tm_local.tm_year + 1900,
             tm_local.tm_mon + 1, tm_local.tm_mday, tm_local.tm_hour, tm_local.tm_min);
}

static const lv_font_t *message_font_for(const comment_display_t *d)
{
    if (!d) {
        return comment_font_for_size(COMMENT_FONT_SIZE_24);
    }
    return comment_font_for_size(d->font_size);
}

static lv_coord_t message_inner_w(void)
{
    return BUBBLE_W - BUBBLE_PAD * 2;
}

static void normalize_one_line(const char *src, char *dst, size_t dst_sz)
{
    if (!dst || dst_sz == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < dst_sz; ++i) {
        char c = src[i];
        if (c == '\n' || c == '\r') {
            c = ' ';
        }
        if (j > 0 && c == ' ' && dst[j - 1] == ' ') {
            continue;
        }
        dst[j++] = c;
    }
    dst[j] = '\0';
}

static lv_coord_t measure_text_w(const char *text, const lv_font_t *font)
{
    if (!text || !font || text[0] == '\0') {
        return 0;
    }
    return lv_txt_get_width(text, (uint32_t)strlen(text), font, 0, LV_TEXT_FLAG_NONE);
}

static void build_marquee_line(const char *utf8, char *out, size_t out_sz, lv_coord_t view_w, const lv_font_t *font)
{
    char line[COMMENT_STORAGE_MAX_BYTES + 1];
    normalize_one_line(utf8, line, sizeof(line));
    if (line[0] == '\0') {
        out[0] = '\0';
        return;
    }

    size_t pos = (size_t)snprintf(out, out_sz, "%s", line);
    const lv_coord_t target_w = view_w + view_w / 3 + 32;

    while (pos + 1 < out_sz) {
        if (measure_text_w(out, font) >= target_w) {
            break;
        }
        const size_t sep_len = strlen(MARQUEE_SEP);
        const size_t line_len = strlen(line);
        if (pos + sep_len + line_len >= out_sz) {
            break;
        }
        memcpy(out + pos, MARQUEE_SEP, sep_len);
        pos += sep_len;
        memcpy(out + pos, line, line_len);
        pos += line_len;
        out[pos] = '\0';
    }
}

static void stop_marquee(void)
{
    if (marquee_timer) {
        lv_timer_del(marquee_timer);
        marquee_timer = NULL;
    }
    marquee_x = 0;
    if (label_message) {
        lv_obj_set_style_translate_x(label_message, 0, LV_PART_MAIN);
    }
    if (msg_area) {
        lv_obj_set_height(msg_area, LV_SIZE_CONTENT);
    }
}

static void marquee_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!label_message || !festive_marquee_active()) {
        stop_marquee();
        return;
    }

    const lv_font_t *font = message_font_for(&s_display);
    const char *txt = lv_label_get_text(label_message);
    const lv_coord_t content_w = measure_text_w(txt, font);
    const lv_coord_t view_w = message_inner_w();

    if (content_w <= view_w) {
        return;
    }

    marquee_x += 2;
    if (marquee_x > content_w + (lv_coord_t)strlen(MARQUEE_SEP)) {
        marquee_x = 0;
    }
    lv_obj_set_style_translate_x(label_message, -marquee_x, LV_PART_MAIN);
}

static const uint32_t k_rainbow_border[] = {
    0xFF0000, 0xFF7A00, 0xFFD000, 0x40E040, 0x40C8FF, 0x6060FF, 0xE040FF,
};

static void stop_rainbow_border(void)
{
    if (rainbow_timer) {
        lv_timer_del(rainbow_timer);
        rainbow_timer = NULL;
    }
}

static void rainbow_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!bubble_card || s_display.style != COMMENT_STYLE_FESTIVE ||
        s_display.festive_color != COMMENT_FESTIVE_RAINBOW) {
        stop_rainbow_border();
        return;
    }

    const size_t n = sizeof(k_rainbow_border) / sizeof(k_rainbow_border[0]);
    rainbow_phase = (uint8_t)((rainbow_phase + 1) % n);
    lv_obj_set_style_border_color(bubble_card, lv_color_hex(k_rainbow_border[rainbow_phase]), LV_PART_MAIN);
}

static void sync_rainbow_border(void)
{
    stop_rainbow_border();
    if (!bubble_card || s_display.style != COMMENT_STYLE_FESTIVE ||
        s_display.festive_color != COMMENT_FESTIVE_RAINBOW) {
        return;
    }

    rainbow_phase = 0;
    lv_obj_set_style_border_color(bubble_card, lv_color_hex(k_rainbow_border[0]), LV_PART_MAIN);
    rainbow_timer = lv_timer_create(rainbow_timer_cb, 180, NULL);
}

static void festive_palette(comment_festive_color_t c, uint32_t *bg, uint32_t *border, uint32_t *text)
{
    switch (c) {
    case COMMENT_FESTIVE_RED:
        *bg = 0x5C1010;
        *border = 0xFF5050;
        *text = 0xFFE8E8;
        break;
    case COMMENT_FESTIVE_GREEN:
        *bg = 0x0A3D20;
        *border = 0x48D070;
        *text = 0xE8FFE8;
        break;
    case COMMENT_FESTIVE_BLUE:
        *bg = 0x102850;
        *border = 0x58A0FF;
        *text = 0xE8F4FF;
        break;
    case COMMENT_FESTIVE_RAINBOW:
        *bg = 0x281040;
        *border = k_rainbow_border[0];
        *text = 0xFFFFFF;
        break;
    default:
        *bg = 0x4A3800;
        *border = 0xFFD700;
        *text = 0xFFF8E0;
        break;
    }
}

static void style_bubble_part(lv_obj_t *obj, uint32_t fill)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(fill), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(obj, 4, LV_PART_MAIN);
}

static void apply_to_visibility(void)
{
    if (!label_to) {
        return;
    }
    if (s_display.show_to) {
        char line[64];
        snprintf(line, sizeof(line), "To %s:", user_profile_get_display_name());
        lv_label_set_text(label_to, line);
        lv_obj_clear_flag(label_to, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(label_to, LV_OBJ_FLAG_HIDDEN);
    }
}

static void apply_received_time(time_t t)
{
    if (!label_received) {
        return;
    }
    char line[48];
    format_received_line(t, line, sizeof(line));
    if (line[0] == '\0') {
        lv_label_set_text(label_received, "");
        lv_obj_add_flag(label_received, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(label_received, line);
    lv_obj_clear_flag(label_received, LV_OBJ_FLAG_HIDDEN);
}

static bool festive_marquee_active(void)
{
    return s_display.style == COMMENT_STYLE_FESTIVE && s_display.scroll == COMMENT_SCROLL_MARQUEE;
}

static const char *prepare_message_utf8(const char *utf8, char *scratch, size_t scratch_len)
{
    if (!utf8 || utf8[0] == '\0') {
        return utf8;
    }
    strncpy(scratch, utf8, scratch_len - 1);
    scratch[scratch_len - 1] = '\0';
    comment_text_normalize(scratch, scratch_len);
    return scratch;
}

static void set_message_label_text(const char *utf8)
{
    if (!label_message || !msg_area) {
        return;
    }

    stop_marquee();

    const lv_coord_t inner_w = message_inner_w();
    const lv_font_t *font = message_font_for(&s_display);
    static char norm_buf[COMMENT_STORAGE_MAX_BYTES + 1];

    if (!utf8 || utf8[0] == '\0') {
        lv_label_set_long_mode(label_message, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label_message, inner_w);
        lv_label_set_text(label_message,
                          "No message yet.\n"
                          "Post from the web comment board.\n"
                          "暫無留言。");
        return;
    }

    utf8 = prepare_message_utf8(utf8, norm_buf, sizeof(norm_buf));

    if (!festive_marquee_active()) {
        lv_label_set_long_mode(label_message, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label_message, inner_w);
        lv_obj_align(label_message, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_label_set_text(label_message, utf8);
        return;
    }

    static char marquee_buf[COMMENT_STORAGE_MAX_BYTES + MARQUEE_BUF_EXTRA];
    build_marquee_line(utf8, marquee_buf, sizeof(marquee_buf), inner_w, font);

    const lv_coord_t line_h = lv_font_get_line_height(font) + 8;
    lv_obj_set_height(msg_area, line_h);
    lv_obj_clear_flag(msg_area, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    lv_label_set_long_mode(label_message, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label_message, LV_SIZE_CONTENT);
    lv_label_set_text(label_message, marquee_buf);
    lv_obj_align(label_message, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_update_layout(label_message);

    marquee_timer = lv_timer_create(marquee_timer_cb, 40, NULL);
}

static void apply_display_style(void)
{
    const app_theme_colors_t *c = app_theme_colors();
    uint32_t bg = c->bg_card;
    uint32_t border = 0;
    uint32_t text = c->text_primary;
    lv_coord_t border_w = 0;

    if (bubble_tail) {
        lv_obj_add_flag(bubble_tail, LV_OBJ_FLAG_HIDDEN);
    }
    if (label_warning_hdr) {
        lv_obj_add_flag(label_warning_hdr, LV_OBJ_FLAG_HIDDEN);
    }

    switch (s_display.style) {
    case COMMENT_STYLE_FESTIVE:
        festive_palette(s_display.festive_color, &bg, &border, &text);
        border_w = 4;
        break;
    case COMMENT_STYLE_LOVE:
        bg = 0x4A2038;
        border = 0xFF88AA;
        text = 0xFFE8F0;
        border_w = 3;
        break;
    case COMMENT_STYLE_WARNING:
        bg = 0x3A2E00;
        border = 0xFFB000;
        text = 0xFFF0D0;
        border_w = 4;
        break;
    default:
        if (bubble_tail) {
            lv_obj_clear_flag(bubble_tail, LV_OBJ_FLAG_HIDDEN);
        }
        break;
    }

    if (bubble_card) {
        lv_obj_set_style_bg_color(bubble_card, lv_color_hex(bg), LV_PART_MAIN);
        lv_obj_set_style_border_color(bubble_card, lv_color_hex(border), LV_PART_MAIN);
        lv_obj_set_style_border_width(bubble_card, border_w, LV_PART_MAIN);
        lv_obj_set_style_border_opa(bubble_card, border_w > 0 ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    }
    if (bubble_tail && s_display.style == COMMENT_STYLE_DIALOGUE) {
        lv_obj_set_style_bg_color(bubble_tail, lv_color_hex(bg), LV_PART_MAIN);
    }
    if (label_message) {
        lv_obj_set_style_text_font(label_message, message_font_for(&s_display), LV_PART_MAIN);
        lv_obj_set_style_text_color(label_message, lv_color_hex(text), LV_PART_MAIN);
    }
    apply_style_icon();
    apply_to_visibility();
    sync_rainbow_border();
}

static void apply_message_text(const char *utf8)
{
    if (!label_message) {
        return;
    }
    if (!utf8 || utf8[0] == '\0') {
        set_message_label_text(NULL);
        apply_received_time(0);
        return;
    }
    set_message_label_text(utf8);
}

static void style_clear_btn(void)
{
    if (!btn_clear) {
        return;
    }
    lv_obj_set_style_bg_color(btn_clear, lv_color_hex(0x7A8490), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_clear, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_clear, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_clear, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_clear, 0, LV_PART_MAIN);
    if (lbl_clear) {
        lv_obj_set_style_text_color(lbl_clear, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    }
}

static void on_btn_clear_click(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    comment_storage_clear();
    stop_marquee();
    stop_rainbow_border();
    apply_message_text(NULL);
    apply_received_time(0);
    if (bubble_card) {
        lv_obj_scroll_to_y(bubble_card, 0, LV_ANIM_OFF);
    }
}

static void reload_from_storage(void)
{
    comment_storage_load_display(&s_display);
    apply_display_style();

    char saved[COMMENT_STORAGE_MAX_BYTES + 1];
    time_t received_at = 0;
    if (comment_storage_load_ex(saved, sizeof(saved), &received_at)) {
        apply_message_text(saved);
        apply_received_time(received_at);
    } else {
        apply_message_text(NULL);
    }
}

void comment_screen_create(lv_obj_t *parent)
{
    if (screen_ready || !parent) {
        return;
    }

    const app_theme_colors_t *c = app_theme_colors();
    comment_display_defaults(&s_display);
    comment_font_init();

    tile_root = parent;
    lv_obj_clear_flag(tile_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(tile_root, lv_color_hex(c->bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile_root, LV_OPA_COVER, LV_PART_MAIN);

    label_title = lv_label_create(tile_root);
    lv_label_set_text(label_title, "Message");
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_hex(c->text_primary), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 14);

    label_hint = lv_label_create(tile_root);
    lv_label_set_text(label_hint, "Drag right: home  |  Down: menu");
    lv_obj_set_style_text_font(label_hint, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_hint, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_obj_align(label_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    bubble_col = lv_obj_create(tile_root);
    lv_obj_set_width(bubble_col, BUBBLE_W + 40);
    lv_obj_set_height(bubble_col, LV_SIZE_CONTENT);
    lv_obj_align(bubble_col, LV_ALIGN_CENTER, 0, 8);
    lv_obj_clear_flag(bubble_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(bubble_col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(bubble_col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bubble_col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(bubble_col, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(bubble_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bubble_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(bubble_col, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    label_to = lv_label_create(bubble_col);
    lv_obj_set_width(label_to, LV_PCT(100));
    lv_label_set_long_mode(label_to, LV_LABEL_LONG_DOT);
    lv_obj_set_style_align(label_to, LV_ALIGN_LEFT_MID, LV_PART_MAIN);
    lv_obj_set_style_text_font(label_to, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_to, lv_color_hex(c->text_secondary), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_to, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    apply_to_visibility();

    style_icon = lv_img_create(bubble_col);
    lv_img_set_src(style_icon, &comment_style_icon_dialogue);
    lv_obj_clear_flag(style_icon, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_bottom(style_icon, STYLE_ICON_GAP, LV_PART_MAIN);

    bubble_card = lv_obj_create(bubble_col);
    lv_obj_set_width(bubble_card, BUBBLE_W);
    lv_obj_set_height(bubble_card, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(bubble_card, BUBBLE_MIN_H, LV_PART_MAIN);
    lv_obj_set_style_max_height(bubble_card, BUBBLE_MAX_H, LV_PART_MAIN);
    lv_obj_set_style_radius(bubble_card, 22, LV_PART_MAIN);
    style_bubble_part(bubble_card, c->bg_card);
    lv_obj_set_style_pad_all(bubble_card, BUBBLE_PAD, LV_PART_MAIN);
    lv_obj_set_scroll_dir(bubble_card, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(bubble_card, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(bubble_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bubble_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    label_warning_hdr = lv_label_create(bubble_card);
    lv_label_set_text(label_warning_hdr, LV_SYMBOL_WARNING "  WARNING");
    lv_obj_set_width(label_warning_hdr, BUBBLE_W - BUBBLE_PAD * 2);
    lv_obj_set_style_text_font(label_warning_hdr, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_warning_hdr, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_add_flag(label_warning_hdr, LV_OBJ_FLAG_HIDDEN);

    msg_area = lv_obj_create(bubble_card);
    lv_obj_set_width(msg_area, message_inner_w());
    lv_obj_set_height(msg_area, LV_SIZE_CONTENT);
    lv_obj_clear_flag(msg_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(msg_area, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(msg_area, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(msg_area, 0, LV_PART_MAIN);

    label_message = lv_label_create(msg_area);
    lv_obj_set_width(label_message, message_inner_w());
    lv_label_set_long_mode(label_message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label_message, comment_font_for_size(COMMENT_FONT_SIZE_24), LV_PART_MAIN);
    lv_obj_set_style_text_color(label_message, lv_color_hex(c->text_primary), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_message, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(label_message, 8, LV_PART_MAIN);

    label_received = lv_label_create(bubble_col);
    lv_obj_set_width(label_received, BUBBLE_W);
    lv_label_set_long_mode(label_received, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label_received, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_received, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_received, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_pad_top(label_received, 6, LV_PART_MAIN);
    lv_obj_add_flag(label_received, LV_OBJ_FLAG_HIDDEN);

    bubble_tail = lv_obj_create(bubble_col);
    lv_obj_set_size(bubble_tail, BUBBLE_TAIL_SIZE, BUBBLE_TAIL_SIZE);
    lv_obj_set_style_radius(bubble_tail, 3, LV_PART_MAIN);
    style_bubble_part(bubble_tail, c->bg_card);
    lv_obj_clear_flag(bubble_tail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_shadow_width(bubble_tail, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_angle(bubble_tail, 450, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(bubble_tail, BUBBLE_TAIL_SIZE / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(bubble_tail, BUBBLE_TAIL_SIZE / 2, LV_PART_MAIN);
    lv_obj_align_to(bubble_tail, bubble_card, LV_ALIGN_OUT_BOTTOM_LEFT, 36, -10);

    btn_clear = lv_btn_create(tile_root);
    lv_obj_set_size(btn_clear, 96, 44);
    lv_obj_align(btn_clear, LV_ALIGN_BOTTOM_RIGHT, -16, -12);
    lv_obj_add_event_cb(btn_clear, on_btn_clear_click, LV_EVENT_CLICKED, NULL);
    lbl_clear = lv_label_create(btn_clear);
    lv_label_set_text(lbl_clear, "Clear");
    lv_obj_set_style_text_font(lbl_clear, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(lbl_clear);
    style_clear_btn();
    lv_obj_move_foreground(btn_clear);

    reload_from_storage();
    screen_ready = true;
}

bool comment_screen_is_ready(void)
{
    return screen_ready;
}

void comment_screen_destroy(void)
{
    if (!screen_ready || !tile_root) {
        return;
    }
    stop_marquee();
    stop_rainbow_border();
    screen_ready = false;
    lv_obj_clean(tile_root);
    label_title = NULL;
    label_hint = NULL;
    bubble_col = NULL;
    label_to = NULL;
    style_icon = NULL;
    bubble_card = NULL;
    label_warning_hdr = NULL;
    msg_area = NULL;
    label_message = NULL;
    label_received = NULL;
    bubble_tail = NULL;
    btn_clear = NULL;
    lbl_clear = NULL;
}

void comment_screen_refresh_message(void)
{
    if (!screen_ready) {
        return;
    }
    reload_from_storage();
}

lv_obj_t *comment_screen_get_root(void)
{
    return tile_root;
}

void comment_screen_set_message(const char *utf8)
{
    comment_storage_load_display(&s_display);
    apply_display_style();
    apply_message_text(utf8);
    time_t received_at = 0;
    char scratch[COMMENT_STORAGE_MAX_BYTES + 1];
    if (comment_storage_load_ex(scratch, sizeof(scratch), &received_at)) {
        apply_received_time(received_at);
    } else {
        apply_received_time(0);
    }
    if (bubble_card) {
        lv_obj_scroll_to_y(bubble_card, 0, LV_ANIM_OFF);
    }
}

void comment_screen_show(void)
{
    screen_nav_show_comment();
}

static volatile bool s_pending_show = false;

void comment_screen_request_show(const char *utf8)
{
    (void)utf8;
    s_pending_show = true;
}

void comment_screen_dispatch_pending(void)
{
    if (!s_pending_show) {
        return;
    }
    s_pending_show = false;

    lvgl_port_lock(-1);
    comment_screen_refresh_message();
    comment_screen_show();
    lvgl_port_unlock();
}

void comment_screen_apply_theme(void)
{
    if (!screen_ready) {
        return;
    }
    const app_theme_colors_t *c = app_theme_colors();
    if (tile_root) {
        lv_obj_set_style_bg_color(tile_root, lv_color_hex(c->bg), LV_PART_MAIN);
    }
    if (label_title) {
        lv_obj_set_style_text_color(label_title, lv_color_hex(c->text_primary), LV_PART_MAIN);
    }
    if (label_hint) {
        lv_obj_set_style_text_color(label_hint, lv_color_hex(c->text_muted), LV_PART_MAIN);
    }
    if (label_to) {
        lv_obj_set_style_text_color(label_to, lv_color_hex(c->text_secondary), LV_PART_MAIN);
    }
    if (label_received) {
        lv_obj_set_style_text_color(label_received, lv_color_hex(c->text_muted), LV_PART_MAIN);
    }
    style_clear_btn();
    apply_display_style();
}
