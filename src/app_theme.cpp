#include "app_theme.h"

#include <Arduino.h>
#include <Preferences.h>

#include "image_screen.h"
#include "main_screen.h"
#include "screen_nav.h"
#include "settings_screen.h"
#include "comment_screen.h"
#include "todo_screen.h"
#include "menu_screen.h"
#include "weather_screen.h"
#include "wifi_settings_screen.h"

#define PREFS_NS        "myscreen"
#define PREFS_KEY_THEME "theme"

static bool s_dark = true;

static const app_theme_colors_t s_dark_colors = {
    .bg = 0x0a0a0f,
    .bg_card = 0x141420,
    .bg_scroll = 0x101018,
    .text_primary = 0xffffff,
    .text_secondary = 0xc8c8d8,
    .text_muted = 0x606070,
    .btn_bg = 0x1a1a28,
};

static const app_theme_colors_t s_light_colors = {
    .bg = 0xf2f2f6,
    .bg_card = 0xffffff,
    .bg_scroll = 0xe8e8ee,
    .text_primary = 0x1a1a22,
    .text_secondary = 0x3a3a48,
    .text_muted = 0x707080,
    .btn_bg = 0xd8d8e0,
};

bool app_theme_is_dark(void)
{
    return s_dark;
}

const app_theme_colors_t *app_theme_colors(void)
{
    return s_dark ? &s_dark_colors : &s_light_colors;
}

void app_theme_set_dark(bool dark)
{
    s_dark = dark;
}

void app_theme_load(void)
{
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, true)) {
        s_dark = true;
        return;
    }
    s_dark = prefs.getBool(PREFS_KEY_THEME, true);
    prefs.end();
}

void app_theme_save(void)
{
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, false)) {
        return;
    }
    prefs.putBool(PREFS_KEY_THEME, s_dark);
    prefs.end();
}

void app_theme_style_icon_btn(lv_obj_t *btn, lv_obj_t *icon_label)
{
    if (!btn) {
        return;
    }
    const app_theme_colors_t *c = app_theme_colors();
    lv_obj_set_style_bg_color(btn, lv_color_hex(c->btn_bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    if (icon_label) {
        lv_obj_set_style_text_color(icon_label, lv_color_hex(c->text_primary), LV_PART_MAIN);
    }
}

void app_theme_style_segment_btn(lv_obj_t *btn, lv_obj_t *label, bool selected)
{
    if (!btn) {
        return;
    }
    const app_theme_colors_t *c = app_theme_colors();
    if (selected) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(c->bg_card), LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, lv_color_hex(c->text_secondary), LV_PART_MAIN);
        if (label) {
            lv_obj_set_style_text_color(label, lv_color_hex(c->text_primary), LV_PART_MAIN);
        }
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(c->btn_bg), LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
        if (label) {
            lv_obj_set_style_text_color(label, lv_color_hex(c->text_muted), LV_PART_MAIN);
        }
    }
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
}

void app_theme_style_action_btn(lv_obj_t *btn, lv_obj_t *label)
{
    if (!btn) {
        return;
    }
    const app_theme_colors_t *c = app_theme_colors();
    lv_obj_set_style_bg_color(btn, lv_color_hex(c->btn_bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    if (label) {
        lv_obj_set_style_text_color(label, lv_color_hex(c->text_primary), LV_PART_MAIN);
    }
}

void app_theme_style_textarea(lv_obj_t *ta)
{
    if (!ta) {
        return;
    }
    const app_theme_colors_t *c = app_theme_colors();
    lv_obj_set_style_bg_color(ta, lv_color_hex(c->bg_scroll), LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, lv_color_hex(c->text_primary), LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
}

void app_theme_style_keyboard(lv_obj_t *kb)
{
    if (!kb) {
        return;
    }
    const app_theme_colors_t *c = app_theme_colors();
    lv_obj_set_style_bg_color(kb, lv_color_hex(c->bg_card), LV_PART_MAIN);
    lv_obj_set_style_text_color(kb, lv_color_hex(c->text_primary), LV_PART_MAIN);
}

void app_theme_apply_all(void)
{
    screen_nav_apply_theme();
    main_screen_apply_theme();
    weather_screen_apply_theme();
    settings_screen_apply_theme();
    wifi_settings_screen_apply_theme();
    image_screen_apply_theme();
    comment_screen_apply_theme();
    todo_screen_apply_theme();
    menu_screen_apply_theme();
}
