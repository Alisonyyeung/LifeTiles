#include "screen_nav.h"

#include <lvgl.h>

#include "app_theme.h"
#include "comment_screen.h"
#include "image_screen.h"
#include "main_screen.h"
#include "menu_screen.h"
#include "nav_gestures.h"
#include "settings_screen.h"
#include "todo_screen.h"
#include "weather_screen.h"
#include "wifi_settings_screen.h"

#define NAV_ANIM_MS 320

static lv_obj_t *tileview;
static lv_obj_t *tile_main;
static lv_obj_t *tile_image;
static lv_obj_t *tile_comment;
static lv_obj_t *tile_todo;

static void update_image_playback(void)
{
    if (!tileview || !tile_image) {
        return;
    }
    if (menu_screen_is_active()) {
        image_screen_set_playback(false);
        return;
    }
    const bool on_image_tile = lv_tileview_get_tile_act(tileview) == tile_image;
    const bool tileview_visible = lv_scr_act() == tileview;
    image_screen_set_playback(on_image_tile && tileview_visible);
}

static void on_tileview_changed(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    if (tile_todo && lv_tileview_get_tile_act(tileview) == tile_todo) {
        todo_screen_refresh();
    }
    update_image_playback();
}

void screen_nav_on_menu_dismissed(void)
{
    update_image_playback();
}

void screen_nav_init(void)
{
    tileview = lv_tileview_create(NULL);
    lv_obj_set_style_bg_color(tileview, lv_color_hex(app_theme_colors()->bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tileview, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);

    /* Comment (0) | Home (1) | Image (2) | To-Do (3) */
    tile_comment = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
    tile_main = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    tile_image = lv_tileview_add_tile(tileview, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    tile_todo = lv_tileview_add_tile(tileview, 3, 0, LV_DIR_LEFT);

    comment_screen_create(tile_comment);
    main_screen_create(tile_main);
    image_screen_create(tile_image);
    todo_screen_create(tile_todo);

    lv_obj_add_flag(tile_comment, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(tile_main, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(tile_image, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(tile_todo, LV_OBJ_FLAG_EVENT_BUBBLE);
    nav_gestures_enable_tree_bubble(tile_comment);
    nav_gestures_enable_tree_bubble(tile_main);
    nav_gestures_enable_tree_bubble(tile_image);
    nav_gestures_enable_tree_bubble(tile_todo);
    nav_gestures_attach_tileview(tileview);

    lv_obj_add_event_cb(tileview, on_tileview_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_tile(tileview, tile_main, LV_ANIM_OFF);
    lv_scr_load(tileview);
    update_image_playback();
}

lv_obj_t *screen_nav_get_tileview(void)
{
    return tileview;
}

lv_obj_t *screen_nav_get_tile_main(void)
{
    return tile_main;
}

lv_obj_t *screen_nav_get_tile_image(void)
{
    return tile_image;
}

lv_obj_t *screen_nav_get_tile_comment(void)
{
    return tile_comment;
}

lv_obj_t *screen_nav_get_tile_todo(void)
{
    return tile_todo;
}

bool screen_nav_is_tileview_active(void)
{
    return tileview && lv_scr_act() == tileview;
}

static void go_to_tile(lv_obj_t *tile)
{
    if (!tileview || !tile) {
        return;
    }
    if (lv_scr_act() != tileview) {
        lv_obj_set_tile(tileview, tile, LV_ANIM_OFF);
        lv_scr_load(tileview);
    } else {
        lv_obj_set_tile(tileview, tile, LV_ANIM_ON);
    }
    update_image_playback();
}

void screen_nav_show_home_tile(lv_obj_t *tile)
{
    if (!tileview || !tile) {
        return;
    }
    const bool from_overlay = lv_scr_act() != tileview;
    if (from_overlay) {
        lv_scr_load_anim(tileview, LV_SCR_LOAD_ANIM_MOVE_RIGHT, NAV_ANIM_MS, 0, false);
        lv_obj_set_tile(tileview, tile, LV_ANIM_OFF);
    } else {
        go_to_tile(tile);
    }
    update_image_playback();
}

void screen_nav_show_home(void)
{
    screen_nav_show_home_tile(tile_main);
}

void screen_nav_show_weather(void)
{
    image_screen_set_playback(false);
    weather_screen_show();
    lv_scr_load_anim(weather_screen_get_screen(), LV_SCR_LOAD_ANIM_MOVE_LEFT, NAV_ANIM_MS, 0, false);
}

void screen_nav_show_settings(void)
{
    image_screen_set_playback(false);
    settings_screen_show();
    lv_scr_load_anim(settings_screen_get_screen(), LV_SCR_LOAD_ANIM_MOVE_LEFT, NAV_ANIM_MS, 0, false);
}

void screen_nav_show_wifi_settings(void)
{
    image_screen_set_playback(false);
    wifi_settings_screen_show();
    lv_scr_load_anim(wifi_settings_screen_get_screen(), LV_SCR_LOAD_ANIM_MOVE_LEFT, NAV_ANIM_MS, 0, false);
}

void screen_nav_show_image(void)
{
    go_to_tile(tile_image);
}

void screen_nav_show_comment(void)
{
    comment_screen_refresh_message();
    go_to_tile(tile_comment);
}

void screen_nav_show_todo(void)
{
    if (!tile_todo) {
        return;
    }
    todo_screen_refresh();
    go_to_tile(tile_todo);
}

void screen_nav_show_menu(void)
{
    menu_screen_show();
}

void screen_nav_apply_theme(void)
{
    if (tileview) {
        lv_obj_set_style_bg_color(tileview, lv_color_hex(app_theme_colors()->bg), LV_PART_MAIN);
    }
}
