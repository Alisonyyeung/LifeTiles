#include "wifi_settings_screen.h"

#include <Arduino.h>
#include <string.h>
#include <lvgl.h>

#include "app_theme.h"
#include "screen_nav.h"
#include "settings_screen.h"
#include "lvgl_port.h"
#include "wifi_manager.h"
#include "wifi_services.h"
#include "ESP_Panel_Conf.h"
#include "wifi_storage.h"

static lv_obj_t *screen;
static lv_obj_t *label_title;
static lv_obj_t *label_hint;
static lv_obj_t *label_status;
static lv_obj_t *ta_ssid;
static lv_obj_t *ta_password;
static lv_obj_t *sw_show_password;
static lv_obj_t *history_list;
static lv_obj_t *keyboard;
static lv_obj_t *btn_save;
static lv_obj_t *lbl_save;
static lv_obj_t *wifi_card;

static lv_point_t swipe_start;
static bool screen_ready = false;

static void refresh_history_list(void);

static void hide_keyboard(void)
{
    if (!keyboard) {
        return;
    }
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(keyboard, NULL);
    if (ta_ssid) {
        lv_obj_clear_state(ta_ssid, LV_STATE_FOCUSED);
    }
    if (ta_password) {
        lv_obj_clear_state(ta_password, LV_STATE_FOCUSED);
    }
}

static void focus_ta(lv_obj_t *ta)
{
    if (!keyboard || !ta) {
        return;
    }
    lv_keyboard_set_textarea(keyboard, ta);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    if (wifi_card) {
        lv_obj_update_layout(wifi_card);
        lv_obj_scroll_to_view(ta, LV_ANIM_ON);
    }
}

static void on_ta_focus(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_FOCUSED) {
        focus_ta(lv_event_get_target(e));
    }
}

static void on_keyboard_event(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        hide_keyboard();
    }
}

static void apply_password_visibility(void)
{
    if (!ta_password || !sw_show_password) {
        return;
    }
    const bool show = lv_obj_has_state(sw_show_password, LV_STATE_CHECKED);
    lv_textarea_set_password_mode(ta_password, !show);
}

static void on_show_password_changed(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    apply_password_visibility();
}

static void wifi_swipe_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        lv_indev_t *indev = lv_indev_get_act();
        if (indev) {
            lv_indev_get_point(indev, &swipe_start);
        }
        return;
    }

    if (code == LV_EVENT_GESTURE) {
        lv_indev_t *indev = lv_indev_get_act();
        if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_LEFT) {
            screen_nav_show_settings();
        }
        return;
    }

    if (code == LV_EVENT_RELEASED) {
        lv_point_t end;
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev) {
            return;
        }
        lv_indev_get_point(indev, &end);
        const int dx = swipe_start.x - end.x;
        const int dy = swipe_start.y - end.y;
        if (dx > 80 && dx > (dy < 0 ? -dy : dy) * 2) {
            screen_nav_show_settings();
        }
    }
}

static void on_connect_done(bool connected, void *user_data)
{
    LV_UNUSED(user_data);
    lvgl_port_lock(-1);
    if (connected) {
        wifi_storage_history_record_connected();
        wifi_services_on_connected();
        lv_label_set_text(label_status,
                          "Connected. Static IP set - edit in Settings if needed.");
        refresh_history_list();
    } else {
        lv_label_set_text(label_status, "Could not connect. Check SSID/password.");
    }
    settings_screen_refresh_network();
    lvgl_port_unlock();
}

static void start_connect_from_fields(void)
{
    const char *ssid = lv_textarea_get_text(ta_ssid);
    const char *pass = lv_textarea_get_text(ta_password);
    if (!ssid || ssid[0] == '\0') {
        lv_label_set_text(label_status, "SSID is required.");
        return;
    }

    if (!wifi_storage_save(ssid, pass ? pass : "")) {
        lv_label_set_text(label_status, "Save failed (SSID too long?).");
        return;
    }

    lv_label_set_text(label_status, "Saved. Connecting...");
    hide_keyboard();
    wifi_manager_connect_async(on_connect_done, NULL);
}

static void on_save(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    start_connect_from_fields();
}

static void on_history_click(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    char ssid[WIFI_STORAGE_SSID_MAX];
    char pass[WIFI_STORAGE_PASS_MAX];
    if (!wifi_storage_history_get(index, ssid, sizeof(ssid), pass, sizeof(pass))) {
        return;
    }

    lv_textarea_set_text(ta_ssid, ssid);
    lv_textarea_set_text(ta_password, pass);

    lv_label_set_text(label_status, "Connecting to saved network...");
    hide_keyboard();
    start_connect_from_fields();
}

static void refresh_history_list(void)
{
    if (!history_list) {
        return;
    }

    lv_obj_clean(history_list);

    wifi_history_list_t list;
    if (!wifi_storage_history_load(&list) || list.count == 0) {
        lv_obj_t *empty = lv_label_create(history_list);
        lv_label_set_text(empty, "No saved networks yet.");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(empty, lv_color_hex(app_theme_colors()->text_muted), LV_PART_MAIN);
        return;
    }

    const app_theme_colors_t *c = app_theme_colors();
    for (uint8_t i = 0; i < list.count; ++i) {
        lv_obj_t *btn = lv_btn_create(history_list);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, 40);
        lv_obj_add_event_cb(btn, on_history_click, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        app_theme_style_action_btn(btn, NULL);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, list.entries[i].ssid);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_hex(c->text_primary), LV_PART_MAIN);
        lv_obj_center(lbl);
    }
}

static void load_fields_from_storage(void)
{
    char ssid[WIFI_STORAGE_SSID_MAX];
    char pass[WIFI_STORAGE_PASS_MAX];

    if (wifi_storage_is_configured() && wifi_storage_get_ssid(ssid, sizeof(ssid))) {
        lv_textarea_set_text(ta_ssid, ssid);
        if (wifi_storage_get_password(pass, sizeof(pass))) {
            lv_textarea_set_text(ta_password, pass);
        } else {
            lv_textarea_set_text(ta_password, "");
        }
        lv_label_set_text(label_hint,
                          "Enter or edit Wi-Fi above, then Save. Tap a saved network below.");
    } else {
        lv_textarea_set_text(ta_ssid, "");
        lv_textarea_set_text(ta_password, "");
        lv_label_set_text(label_hint,
                          "Enter Wi-Fi above and Save, or tap a saved network below.");
    }
    lv_label_set_text(label_status, "");
    refresh_history_list();
    apply_password_visibility();
}

void wifi_settings_screen_init(void)
{
    if (screen_ready) {
        return;
    }

    const app_theme_colors_t *c = app_theme_colors();

    screen = lv_obj_create(NULL);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(c->bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, wifi_swipe_event, LV_EVENT_ALL, NULL);

    label_title = lv_label_create(screen);
    lv_label_set_text(label_title, "Wi-Fi");
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_hex(c->text_primary), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *back_hint = lv_label_create(screen);
    lv_label_set_text(back_hint, "Swipe left for Settings");
    lv_obj_set_style_text_font(back_hint, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(back_hint, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_obj_align(back_hint, LV_ALIGN_TOP_MID, 0, 44);

    wifi_card = lv_obj_create(screen);
    lv_obj_set_size(wifi_card, 720, ESP_PANEL_LCD_V_RES - 80);
    lv_obj_add_flag(wifi_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(wifi_card, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(wifi_card, LV_DIR_VER);
    lv_obj_set_style_bg_color(wifi_card, lv_color_hex(c->bg_card), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(wifi_card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(wifi_card, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(wifi_card, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wifi_card, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(wifi_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wifi_card, 8, LV_PART_MAIN);
    lv_obj_align(wifi_card, LV_ALIGN_TOP_MID, 0, 68);

    label_hint = lv_label_create(wifi_card);
    lv_label_set_long_mode(label_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_hint, lv_pct(100));
    lv_obj_set_style_text_font(label_hint, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_hint, lv_color_hex(c->text_muted), LV_PART_MAIN);

    lv_obj_t *lbl_ssid = lv_label_create(wifi_card);
    lv_label_set_text(lbl_ssid, "Network name (SSID)");
    lv_obj_set_style_text_font(lbl_ssid, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_ssid, lv_color_hex(c->text_muted), LV_PART_MAIN);

    ta_ssid = lv_textarea_create(wifi_card);
    lv_obj_set_width(ta_ssid, lv_pct(100));
    lv_obj_set_height(ta_ssid, 44);
    lv_textarea_set_one_line(ta_ssid, true);
    lv_textarea_set_max_length(ta_ssid, WIFI_STORAGE_SSID_MAX - 1);
    app_theme_style_textarea(ta_ssid);
    lv_obj_add_event_cb(ta_ssid, on_ta_focus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta_ssid, on_keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(ta_ssid, on_keyboard_event, LV_EVENT_CANCEL, NULL);

    lv_obj_t *pass_head = lv_obj_create(wifi_card);
    lv_obj_set_width(pass_head, lv_pct(100));
    lv_obj_set_height(pass_head, LV_SIZE_CONTENT);
    lv_obj_clear_flag(pass_head, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(pass_head, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(pass_head, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pass_head, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(pass_head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pass_head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_pass = lv_label_create(pass_head);
    lv_label_set_text(lbl_pass, "Password");
    lv_obj_set_style_text_font(lbl_pass, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_pass, lv_color_hex(c->text_muted), LV_PART_MAIN);

    lv_obj_t *show_row = lv_obj_create(pass_head);
    lv_obj_set_height(show_row, LV_SIZE_CONTENT);
    lv_obj_set_width(show_row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(show_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(show_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(show_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(show_row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(show_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(show_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(show_row, 8, LV_PART_MAIN);

    lv_obj_t *lbl_show = lv_label_create(show_row);
    lv_label_set_text(lbl_show, "Show");
    lv_obj_set_style_text_font(lbl_show, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_show, lv_color_hex(c->text_secondary), LV_PART_MAIN);

    sw_show_password = lv_switch_create(show_row);
    lv_obj_add_event_cb(sw_show_password, on_show_password_changed, LV_EVENT_VALUE_CHANGED, NULL);

    ta_password = lv_textarea_create(wifi_card);
    lv_obj_set_width(ta_password, lv_pct(100));
    lv_obj_set_height(ta_password, 44);
    lv_textarea_set_one_line(ta_password, true);
    lv_textarea_set_password_mode(ta_password, true);
    lv_textarea_set_max_length(ta_password, WIFI_STORAGE_PASS_MAX - 1);
    app_theme_style_textarea(ta_password);
    lv_obj_add_event_cb(ta_password, on_ta_focus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta_password, on_keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(ta_password, on_keyboard_event, LV_EVENT_CANCEL, NULL);

    btn_save = lv_btn_create(wifi_card);
    lv_obj_set_size(btn_save, lv_pct(100), 48);
    lv_obj_add_event_cb(btn_save, on_save, LV_EVENT_CLICKED, NULL);
    lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "Save and connect");
    lv_obj_set_style_text_font(lbl_save, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(lbl_save);
    app_theme_style_action_btn(btn_save, lbl_save);

    label_status = lv_label_create(wifi_card);
    lv_label_set_long_mode(label_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_status, lv_pct(100));
    lv_obj_set_style_text_font(label_status, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_status, lv_color_hex(c->text_secondary), LV_PART_MAIN);

    lv_obj_t *lbl_hist = lv_label_create(wifi_card);
    lv_label_set_text(lbl_hist, "Saved networks (up to 3)");
    lv_obj_set_style_text_font(lbl_hist, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_hist, lv_color_hex(c->text_muted), LV_PART_MAIN);

    history_list = lv_obj_create(wifi_card);
    lv_obj_set_width(history_list, lv_pct(100));
    lv_obj_set_height(history_list, 120);
    lv_obj_add_flag(history_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(history_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(history_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(history_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(history_list, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(history_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(history_list, 6, LV_PART_MAIN);

    keyboard = lv_keyboard_create(screen);
    lv_obj_set_size(keyboard, ESP_PANEL_LCD_H_RES, 200);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    app_theme_style_keyboard(keyboard);
    lv_obj_add_event_cb(keyboard, on_keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, on_keyboard_event, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

    screen_ready = true;
}

lv_obj_t *wifi_settings_screen_get_screen(void)
{
    if (!screen_ready) {
        wifi_settings_screen_init();
    }
    return screen;
}

void wifi_settings_screen_show(void)
{
    if (!screen_ready) {
        wifi_settings_screen_init();
    }
    load_fields_from_storage();
    hide_keyboard();
}

void wifi_settings_screen_apply_theme(void)
{
    if (!screen_ready) {
        return;
    }
    const app_theme_colors_t *c = app_theme_colors();
    lv_obj_set_style_bg_color(screen, lv_color_hex(c->bg), LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_hex(c->text_primary), LV_PART_MAIN);
    if (wifi_card) {
        lv_obj_set_style_bg_color(wifi_card, lv_color_hex(c->bg_card), LV_PART_MAIN);
    }
    lv_obj_set_style_text_color(label_hint, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_obj_set_style_text_color(label_status, lv_color_hex(c->text_secondary), LV_PART_MAIN);
    app_theme_style_textarea(ta_ssid);
    app_theme_style_textarea(ta_password);
    app_theme_style_action_btn(btn_save, lbl_save);
    app_theme_style_keyboard(keyboard);
    refresh_history_list();
}
