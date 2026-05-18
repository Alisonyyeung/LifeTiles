#include "todo_screen.h"

#include <Arduino.h>
#include <lvgl.h>
#include <string.h>

#include "app_theme.h"
#include "lvgl_port.h"
#include "nav_gestures.h"
#include "screen_nav.h"
#include "todo_storage.h"
#include "ESP_Panel_Conf.h"

#define TODO_LIST_H        (ESP_PANEL_LCD_V_RES - 120)
#define TODO_LIST_W        (ESP_PANEL_LCD_H_RES - 48)
#define TODO_ENCOURAGE_W   190

static lv_obj_t *tile_root;
static lv_obj_t *s_encourage_banner;
static lv_timer_t *s_encourage_timer;
static lv_obj_t *label_title;
static lv_obj_t *label_hint;
static lv_obj_t *list_scroll;
static bool screen_ready = false;
static volatile bool s_pending_refresh = false;

static void good_job_anim_exec(void *obj, int32_t zoom)
{
    lv_obj_set_style_transform_zoom((lv_obj_t *)obj, (uint16_t)zoom, LV_PART_MAIN);
}

#define ENCOURAGE_SHOW_MS  3000

static void encourage_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_encourage_timer = NULL;
    if (s_encourage_banner) {
        lv_obj_del(s_encourage_banner);
        s_encourage_banner = NULL;
    }
}

static const char *pick_encouragement(void)
{
    static const char *const lines[] = {
        "Good job!",
        "Nice!",
        "Keep going!",
        "Well done!",
        "You got this!",
        "Awesome!",
        "Great work!",
        "Way to go!",
        "Crushed it!",
        "On a roll!",
        "So good!",
        "Nailed it!",
    };
    const size_t n = sizeof(lines) / sizeof(lines[0]);
    return lines[esp_random() % n];
}

static void show_encouragement(void)
{
    if (!tile_root || !list_scroll) {
        return;
    }

    if (s_encourage_timer) {
        lv_timer_del(s_encourage_timer);
        s_encourage_timer = NULL;
    }
    if (s_encourage_banner) {
        lv_obj_del(s_encourage_banner);
        s_encourage_banner = NULL;
    }

    s_encourage_banner = lv_label_create(tile_root);
    lv_label_set_text(s_encourage_banner, pick_encouragement());
    lv_label_set_long_mode(s_encourage_banner, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_encourage_banner, TODO_ENCOURAGE_W);
    lv_obj_set_style_text_font(s_encourage_banner, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_encourage_banner, lv_color_hex(0x90F0A8), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_encourage_banner, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_encourage_banner, lv_color_hex(0x1A2428), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_encourage_banner, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_encourage_banner, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(s_encourage_banner, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_encourage_banner, 16, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_encourage_banner, LV_OPA_40, LV_PART_MAIN);
    lv_obj_align_to(s_encourage_banner, list_scroll, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_move_foreground(s_encourage_banner);

    lv_obj_update_layout(s_encourage_banner);
    const lv_coord_t pw = lv_obj_get_width(s_encourage_banner);
    const lv_coord_t ph = lv_obj_get_height(s_encourage_banner);
    lv_obj_set_style_transform_pivot_x(s_encourage_banner, pw / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(s_encourage_banner, ph / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_zoom(s_encourage_banner, 128, LV_PART_MAIN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_encourage_banner);
    lv_anim_set_exec_cb(&a, good_job_anim_exec);
    lv_anim_set_values(&a, 128, 256);
    lv_anim_set_time(&a, 420);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_start(&a);

    s_encourage_timer = lv_timer_create(encourage_timer_cb, ENCOURAGE_SHOW_MS, NULL);
    lv_timer_set_repeat_count(s_encourage_timer, 1);
}

static void style_task_label(lv_obj_t *lbl, bool done, const app_theme_colors_t *c)
{
    if (done) {
        lv_obj_set_style_text_color(lbl, lv_color_hex(c->text_muted), LV_PART_MAIN);
        lv_obj_set_style_text_decor(lbl, LV_TEXT_DECOR_STRIKETHROUGH, LV_PART_MAIN);
    } else {
        lv_obj_set_style_text_color(lbl, lv_color_hex(c->text_primary), LV_PART_MAIN);
        lv_obj_set_style_text_decor(lbl, LV_TEXT_DECOR_NONE, LV_PART_MAIN);
    }
}

static void style_task_checkbox(lv_obj_t *cb, bool done)
{
    if (done) {
        lv_obj_add_state(cb, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(cb, LV_STATE_CHECKED);
    }
}

static void on_task_row_click(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    lv_obj_t *cb = (lv_obj_t *)lv_event_get_user_data(e);
    if (!cb) {
        return;
    }
    if (lv_obj_has_state(cb, LV_STATE_CHECKED)) {
        lv_obj_clear_state(cb, LV_STATE_CHECKED);
    } else {
        lv_obj_add_state(cb, LV_STATE_CHECKED);
    }
    lv_event_send(cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void on_task_toggle(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    const intptr_t idx = (intptr_t)lv_event_get_user_data(e);
    todo_list_t list;
    if (!todo_storage_get_list(&list) || idx < 0 || (uint8_t)idx >= list.count) {
        return;
    }

    lv_obj_t *cb = lv_event_get_target(e);
    const bool now_done = lv_obj_has_state(cb, LV_STATE_CHECKED);
    if (!todo_storage_set_done_id(list.tasks[idx].id, now_done)) {
        return;
    }

    lv_obj_t *row = lv_obj_get_parent(cb);
    lv_obj_t *lbl = lv_obj_get_child(row, 1);
    const app_theme_colors_t *c = app_theme_colors();
    style_task_checkbox(cb, now_done);
    if (lbl) {
        style_task_label(lbl, now_done, c);
    }

    if (now_done) {
        show_encouragement();
    }
}

static void on_task_checkbox_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    lv_obj_t *cb = lv_event_get_target(e);
    if (lv_obj_has_state(cb, LV_STATE_CHECKED)) {
        show_encouragement();
    }
}

void todo_screen_refresh(void)
{
    if (!list_scroll || !screen_ready) {
        return;
    }

    const app_theme_colors_t *c = app_theme_colors();
    lv_obj_clean(list_scroll);

    todo_list_t list;
    todo_storage_get_list(&list);

    if (list.count == 0) {
        lv_obj_t *empty = lv_label_create(list_scroll);
        lv_label_set_text(empty, "No tasks yet.\nAdd tasks on the web page.");
        lv_obj_set_width(empty, lv_pct(100));
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_20, LV_PART_MAIN);
        lv_obj_set_style_text_color(empty, lv_color_hex(c->text_muted), LV_PART_MAIN);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        return;
    }

    const lv_coord_t text_w = TODO_LIST_W - 32 - 40;

    for (uint8_t i = 0; i < list.count; ++i) {
        lv_obj_t *row = lv_obj_create(list_scroll);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_column(row, 12, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t *cb = lv_checkbox_create(row);
        lv_checkbox_set_text(cb, "");
        lv_obj_set_width(cb, 36);
        style_task_checkbox(cb, list.tasks[i].done);
        lv_obj_add_event_cb(cb, on_task_toggle, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(cb, on_task_checkbox_clicked, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, list.tasks[i].text);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl, text_w);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
        lv_obj_set_style_text_line_space(lbl, 4, LV_PART_MAIN);
        style_task_label(lbl, list.tasks[i].done, c);

        lv_obj_add_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(lbl, on_task_row_click, LV_EVENT_CLICKED, cb);
    }
}

void todo_screen_create(lv_obj_t *parent)
{
    if (screen_ready || !parent) {
        return;
    }

    const app_theme_colors_t *c = app_theme_colors();
    todo_storage_init();

    tile_root = parent;
    lv_obj_clear_flag(tile_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(tile_root, lv_color_hex(c->bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(tile_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile_root, 0, LV_PART_MAIN);

    label_title = lv_label_create(tile_root);
    lv_label_set_text(label_title, "To-Do");
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_hex(c->text_primary), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 16);

    label_hint = lv_label_create(tile_root);
    lv_label_set_text(label_hint, "Tap to check off  |  Drag right: images");
    lv_obj_set_style_text_font(label_hint, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_hint, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_obj_align(label_hint, LV_ALIGN_TOP_MID, 0, 52);

    list_scroll = lv_obj_create(tile_root);
    lv_obj_set_size(list_scroll, TODO_LIST_W, TODO_LIST_H);
    lv_obj_align(list_scroll, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(list_scroll, lv_color_hex(c->bg_card), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list_scroll, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(list_scroll, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(list_scroll, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list_scroll, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list_scroll, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(list_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(list_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_scroll, LV_SCROLLBAR_MODE_AUTO);

    nav_gestures_attach(list_scroll, NAV_GESTURE_MENU_DOWN);

    todo_screen_refresh();
    screen_ready = true;
}

lv_obj_t *todo_screen_get_root(void)
{
    return tile_root;
}

void todo_screen_show(void)
{
    screen_nav_show_todo();
}

void todo_screen_request_refresh(void)
{
    s_pending_refresh = true;
}

void todo_screen_dispatch_pending(void)
{
    if (!s_pending_refresh) {
        return;
    }
    s_pending_refresh = false;
    lvgl_port_lock(-1);
    if (screen_ready) {
        todo_screen_refresh();
    }
    lvgl_port_unlock();
}

void todo_screen_apply_theme(void)
{
    if (!screen_ready) {
        return;
    }
    const app_theme_colors_t *c = app_theme_colors();
    lv_obj_set_style_bg_color(tile_root, lv_color_hex(c->bg), LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_hex(c->text_primary), LV_PART_MAIN);
    lv_obj_set_style_text_color(label_hint, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_obj_set_style_bg_color(list_scroll, lv_color_hex(c->bg_card), LV_PART_MAIN);
    todo_screen_refresh();
}
