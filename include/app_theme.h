#pragma once

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t bg;
    uint32_t bg_card;
    uint32_t bg_scroll;
    uint32_t text_primary;
    uint32_t text_secondary;
    uint32_t text_muted;
    uint32_t btn_bg;
} app_theme_colors_t;

bool app_theme_is_dark(void);
void app_theme_load(void);
void app_theme_set_dark(bool dark);
void app_theme_save(void);
void app_theme_apply_all(void);
const app_theme_colors_t *app_theme_colors(void);

/** Icon / toolbar button (settings, weather). */
void app_theme_style_icon_btn(lv_obj_t *btn, lv_obj_t *icon_label);
/** Segmented control option (e.g. Dark / Light). */
void app_theme_style_segment_btn(lv_obj_t *btn, lv_obj_t *label, bool selected);
/** Full-width action button. */
void app_theme_style_action_btn(lv_obj_t *btn, lv_obj_t *label);
/** Text area + on-screen keyboard theming. */
void app_theme_style_textarea(lv_obj_t *ta);
void app_theme_style_keyboard(lv_obj_t *kb);

#ifdef __cplusplus
}
#endif
