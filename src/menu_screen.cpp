#include "menu_screen.h"

#include <Arduino.h>
#include <lvgl.h>

#include "app_theme.h"
#include "image_screen.h"
#include "screen_nav.h"
#include "ESP_Panel_Conf.h"

#define MENU_BTN_W       320
#define MENU_BTN_H       64
#define MENU_GRID_TOP_Y  108
#define MENU_ANIM_MS     320

static lv_obj_t *menu_panel;
static lv_obj_t *btn_grid;
static bool content_ready = false;

static void build_menu_content(lv_obj_t *parent);
static void menu_panel_anim_y(void *obj, int32_t y);
static void on_dismiss_anim_ready(lv_anim_t *a);

static void menu_panel_anim_y(void *obj, int32_t y)
{
    lv_obj_set_y((lv_obj_t *)obj, y);
}

static void on_dismiss_anim_ready(lv_anim_t *a)
{
    lv_obj_t *panel = (lv_obj_t *)a->var;
    if (panel) {
        lv_obj_del(panel);
    }
    if (menu_panel == panel) {
        menu_panel = NULL;
    }
    screen_nav_on_menu_dismissed();
}

static int iabs(int v)
{
    return v < 0 ? -v : v;
}

void menu_screen_dismiss(void)
{
    if (!menu_panel) {
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, menu_panel);
    lv_anim_set_exec_cb(&a, menu_panel_anim_y);
    lv_anim_set_values(&a, lv_obj_get_y(menu_panel), ESP_PANEL_LCD_V_RES);
    lv_anim_set_time(&a, MENU_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&a, on_dismiss_anim_ready);
    lv_anim_start(&a);
}

bool menu_screen_is_active(void)
{
    return menu_panel != NULL;
}

static void try_dismiss_swipe_up(int dy, int dx)
{
    const int ady = iabs(dy);
    const int adx = iabs(dx);
    if (dy > -72 || ady <= adx * 2) {
        return;
    }
    menu_screen_dismiss();
}

static void on_backdrop_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    menu_screen_dismiss();
}

static void on_menu_nav(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    const intptr_t id = (intptr_t)lv_event_get_user_data(e);
    if (menu_panel) {
        lv_obj_del(menu_panel);
        menu_panel = NULL;
    }
    switch (id) {
    case 0:
        screen_nav_show_home();
        break;
    case 1:
        screen_nav_show_weather();
        break;
    case 2:
        screen_nav_show_image();
        break;
    case 3:
        screen_nav_show_comment();
        break;
    case 4:
        screen_nav_show_todo();
        break;
    case 5:
        screen_nav_show_settings();
        break;
    default:
        break;
    }
}

static void menu_swipe_event(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    static lv_point_t swipe_start;
    if (code == LV_EVENT_PRESSED) {
        lv_indev_t *indev = lv_indev_get_act();
        if (indev) {
            lv_indev_get_point(indev, &swipe_start);
        }
        return;
    }
    if (code == LV_EVENT_GESTURE) {
        lv_indev_t *indev = lv_indev_get_act();
        if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) {
            menu_screen_dismiss();
        }
        return;
    }
    if (code != LV_EVENT_RELEASED) {
        return;
    }
    lv_point_t end;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_indev_get_point(indev, &end);
    try_dismiss_swipe_up(end.y - swipe_start.y, end.x - swipe_start.x);
}

static lv_obj_t *add_menu_btn(lv_obj_t *parent, const char *title, intptr_t id)
{
    const app_theme_colors_t *c = app_theme_colors();
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, MENU_BTN_W, MENU_BTN_H);
    lv_obj_add_event_cb(btn, on_menu_nav, LV_EVENT_CLICKED, (void *)id);
    lv_obj_set_style_bg_color(btn, lv_color_hex(c->bg_card), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(c->text_primary), LV_PART_MAIN);
    lv_obj_center(lbl);
    return btn;
}

static void build_menu_content(lv_obj_t *parent)
{
    const app_theme_colors_t *c = app_theme_colors();

    lv_obj_t *backdrop = lv_obj_create(parent);
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_align(backdrop, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(backdrop, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(backdrop, 0, LV_PART_MAIN);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(backdrop, on_backdrop_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Menu");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(c->text_primary), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, "Swipe up or tap outside buttons to close");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 56);

    btn_grid = lv_obj_create(parent);
    lv_obj_set_size(btn_grid, MENU_BTN_W + 24, ESP_PANEL_LCD_V_RES - MENU_GRID_TOP_Y - 16);
    lv_obj_align(btn_grid, LV_ALIGN_TOP_MID, 0, MENU_GRID_TOP_Y);
    lv_obj_set_scroll_dir(btn_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(btn_grid, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(btn_grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(btn_grid, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn_grid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_grid, LV_OBJ_FLAG_CLICKABLE);

    add_menu_btn(btn_grid, "Home", 0);
    add_menu_btn(btn_grid, "Weather", 1);
    add_menu_btn(btn_grid, "Images", 2);
    add_menu_btn(btn_grid, "Messages", 3);
    add_menu_btn(btn_grid, "To-Do", 4);
    add_menu_btn(btn_grid, "Settings", 5);

    lv_obj_add_event_cb(parent, menu_swipe_event, LV_EVENT_ALL, NULL);
}

void menu_screen_init(void)
{
    content_ready = true;
}

void menu_screen_show(void)
{
    if (menu_panel) {
        return;
    }

    /* Top layer — not lv_scr_act(): tileview scroll leaves (0,0) over the comment column. */
    lv_obj_t *layer = lv_layer_top();
    if (!layer) {
        return;
    }

    image_screen_set_playback(false);

    const app_theme_colors_t *c = app_theme_colors();
    menu_panel = lv_obj_create(layer);
    lv_obj_set_size(menu_panel, ESP_PANEL_LCD_H_RES, ESP_PANEL_LCD_V_RES);
    lv_obj_set_pos(menu_panel, 0, ESP_PANEL_LCD_V_RES);
    lv_obj_clear_flag(menu_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(menu_panel, lv_color_hex(c->bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(menu_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(menu_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(menu_panel, 0, LV_PART_MAIN);
    lv_obj_move_foreground(menu_panel);

    build_menu_content(menu_panel);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, menu_panel);
    lv_anim_set_exec_cb(&a, menu_panel_anim_y);
    lv_anim_set_values(&a, ESP_PANEL_LCD_V_RES, 0);
    lv_anim_set_time(&a, MENU_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void menu_screen_apply_theme(void)
{
    if (!menu_panel) {
        return;
    }
    const app_theme_colors_t *c = app_theme_colors();
    lv_obj_set_style_bg_color(menu_panel, lv_color_hex(c->bg), LV_PART_MAIN);
}
