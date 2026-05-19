#include "settings_screen.h"

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include <string.h>

#include "app_theme.h"
#include "display_backlight.h"
#include "main_screen.h"
#include "nav_gestures.h"
#include "screen_nav.h"
#include "user_profile.h"
#include "lvgl_port.h"
#include "wifi_manager.h"
#include "wifi_services.h"
#include "wifi_storage.h"
#include "ESP_Panel_Conf.h"

static lv_obj_t *screen;
static lv_obj_t *label_title;
static lv_obj_t *label_ip_value;
static lv_obj_t *label_gateway_value;
static lv_obj_t *label_subnet_value;
static lv_obj_t *sw_static_ip;
static lv_obj_t *ta_static_ip;
static lv_obj_t *ta_static_gw;
static lv_obj_t *ta_static_sn;
static lv_obj_t *label_static_status;
static lv_obj_t *btn_save_static;
static lv_obj_t *lbl_save_static;
static lv_obj_t *label_wifi_saved;
static lv_obj_t *label_brightness_value;
static lv_obj_t *slider_brightness;
static lv_obj_t *btn_wifi;
static lv_obj_t *lbl_wifi_btn;
static lv_obj_t *btn_theme_dark;
static lv_obj_t *lbl_theme_dark;
static lv_obj_t *btn_theme_light;
static lv_obj_t *lbl_theme_light;
static lv_obj_t *label_theme_hint;
static lv_obj_t *ta_username;
static lv_obj_t *keyboard;
static lv_obj_t *settings_card;
static lv_obj_t *settings_scroll;

static bool screen_ready = false;

static void settings_clear_widget_ptrs(void)
{
    screen = NULL;
    label_title = NULL;
    label_ip_value = NULL;
    label_gateway_value = NULL;
    label_subnet_value = NULL;
    sw_static_ip = NULL;
    ta_static_ip = NULL;
    ta_static_gw = NULL;
    ta_static_sn = NULL;
    label_static_status = NULL;
    btn_save_static = NULL;
    lbl_save_static = NULL;
    label_wifi_saved = NULL;
    label_brightness_value = NULL;
    slider_brightness = NULL;
    btn_wifi = NULL;
    lbl_wifi_btn = NULL;
    btn_theme_dark = NULL;
    lbl_theme_dark = NULL;
    btn_theme_light = NULL;
    lbl_theme_light = NULL;
    label_theme_hint = NULL;
    ta_username = NULL;
    keyboard = NULL;
    settings_card = NULL;
    settings_scroll = NULL;
}

static void settings_screen_unloaded_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_SCREEN_UNLOADED) {
        return;
    }
    settings_screen_destroy();
}

static void hide_keyboard(void);

static bool static_field_empty(const char *s)
{
    return !s || s[0] == '\0';
}

/** Fill empty static IP fields from current Wi-Fi (DHCP) values; user can edit after. */
static void autofill_static_from_wifi(bool fill_device_ip_if_empty)
{
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    char buf[20];
    if (fill_device_ip_if_empty && ta_static_ip) {
        const char *cur = lv_textarea_get_text(ta_static_ip);
        if (static_field_empty(cur)) {
            snprintf(buf, sizeof(buf), "%s", WiFi.localIP().toString().c_str());
            lv_textarea_set_text(ta_static_ip, buf);
        }
    }

    if (ta_static_gw) {
        const char *cur = lv_textarea_get_text(ta_static_gw);
        if (static_field_empty(cur)) {
            snprintf(buf, sizeof(buf), "%s", WiFi.gatewayIP().toString().c_str());
            lv_textarea_set_text(ta_static_gw, buf);
        }
    }

    if (ta_static_sn) {
        const char *cur = lv_textarea_get_text(ta_static_sn);
        if (static_field_empty(cur)) {
            snprintf(buf, sizeof(buf), "%s", WiFi.subnetMask().toString().c_str());
            lv_textarea_set_text(ta_static_sn, buf);
        }
    }
}

/** After a valid static device IP, copy router gateway/subnet from the live connection. */
static void autofill_gateway_from_wifi_for_valid_ip(void)
{
    if (WiFi.status() != WL_CONNECTED || !ta_static_ip || !ta_static_gw) {
        return;
    }

    const char *ip = lv_textarea_get_text(ta_static_ip);
    if (!wifi_storage_ipv4_valid(ip)) {
        return;
    }

    char buf[20];
    snprintf(buf, sizeof(buf), "%s", WiFi.gatewayIP().toString().c_str());
    lv_textarea_set_text(ta_static_gw, buf);

    if (ta_static_sn) {
        snprintf(buf, sizeof(buf), "%s", WiFi.subnetMask().toString().c_str());
        lv_textarea_set_text(ta_static_sn, buf);
    }
}

static void on_static_ip_field_event(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        if (ta_static_gw && static_field_empty(lv_textarea_get_text(ta_static_gw))) {
            autofill_gateway_from_wifi_for_valid_ip();
        }
        return;
    }

    if (code == LV_EVENT_DEFOCUSED) {
        autofill_gateway_from_wifi_for_valid_ip();
    }
}

static void refresh_network_labels(void)
{
    if (!label_ip_value) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s", WiFi.localIP().toString().c_str());
        lv_label_set_text(label_ip_value, buf);
        if (label_gateway_value) {
            snprintf(buf, sizeof(buf), "%s", WiFi.gatewayIP().toString().c_str());
            lv_label_set_text(label_gateway_value, buf);
        }
        if (label_subnet_value) {
            snprintf(buf, sizeof(buf), "%s", WiFi.subnetMask().toString().c_str());
            lv_label_set_text(label_subnet_value, buf);
        }
        autofill_static_from_wifi(true);
        return;
    }

    wifi_static_config_t st;
    wifi_storage_get_static(&st);
    if (st.use_static && st.ip[0] != '\0') {
        lv_label_set_text(label_ip_value, st.ip);
        if (label_gateway_value) {
            lv_label_set_text(label_gateway_value, st.gateway[0] ? st.gateway : "—");
        }
        if (label_subnet_value) {
            lv_label_set_text(label_subnet_value, st.subnet[0] ? st.subnet : "—");
        }
        return;
    }

    lv_label_set_text(label_ip_value, "Not connected");
    if (label_gateway_value) {
        lv_label_set_text(label_gateway_value, "—");
    }
    if (label_subnet_value) {
        lv_label_set_text(label_subnet_value, "—");
    }
}

static void load_static_fields(void)
{
    wifi_static_config_t st;
    wifi_storage_get_static(&st);

    if (sw_static_ip) {
        if (st.use_static) {
            lv_obj_add_state(sw_static_ip, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(sw_static_ip, LV_STATE_CHECKED);
        }
    }
    if (ta_static_ip) {
        lv_textarea_set_text(ta_static_ip, st.ip);
    }
    if (ta_static_gw) {
        lv_textarea_set_text(ta_static_gw, st.gateway);
    }
    if (ta_static_sn) {
        lv_textarea_set_text(ta_static_sn, st.subnet[0] ? st.subnet : "255.255.255.0");
    }
}

static void on_static_connect_done(bool connected, void *user_data)
{
    LV_UNUSED(user_data);
    lvgl_port_lock(-1);
    if (label_static_status) {
        lv_label_set_text(label_static_status,
                          connected ? "Connected with new IP settings." : "Reconnect failed.");
    }
    if (connected) {
        load_static_fields();
        wifi_services_on_connected();
    }
    refresh_network_labels();
    autofill_static_from_wifi(true);
    lvgl_port_unlock();
}

static void on_save_static_ip(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    wifi_static_config_t st;
    memset(&st, 0, sizeof(st));
    st.use_static = sw_static_ip && lv_obj_has_state(sw_static_ip, LV_STATE_CHECKED);

    if (st.use_static) {
        const char *ip = ta_static_ip ? lv_textarea_get_text(ta_static_ip) : "";
        const char *gw = ta_static_gw ? lv_textarea_get_text(ta_static_gw) : "";
        const char *sn = ta_static_sn ? lv_textarea_get_text(ta_static_sn) : "";
        if (!wifi_storage_ipv4_valid(ip) || !wifi_storage_ipv4_valid(gw) ||
            !wifi_storage_ipv4_valid(sn)) {
            if (label_static_status) {
                lv_label_set_text(label_static_status, "Enter valid IPv4 addresses.");
            }
            return;
        }
        strncpy(st.ip, ip, sizeof(st.ip) - 1);
        strncpy(st.gateway, gw, sizeof(st.gateway) - 1);
        strncpy(st.subnet, sn, sizeof(st.subnet) - 1);
    }

    if (!wifi_storage_save_static(&st)) {
        if (label_static_status) {
            lv_label_set_text(label_static_status, "Could not save IP settings.");
        }
        return;
    }

    hide_keyboard();
    if (label_static_status) {
        lv_label_set_text(label_static_status, "Saved. Reconnecting...");
    }

    if (!wifi_storage_is_configured()) {
        if (label_static_status) {
            lv_label_set_text(label_static_status, "Set up Wi-Fi first.");
        }
        refresh_network_labels();
        return;
    }

    wifi_manager_connect_async(on_static_connect_done, NULL);
}

static void refresh_wifi_saved_label(void)
{
    if (!label_wifi_saved) {
        return;
    }
    char ssid[WIFI_STORAGE_SSID_MAX];
    if (wifi_storage_is_configured() && wifi_storage_get_ssid(ssid, sizeof(ssid))) {
        char buf[80];
        snprintf(buf, sizeof(buf), "Saved network: %s", ssid);
        lv_label_set_text(label_wifi_saved, buf);
    } else {
        lv_label_set_text(label_wifi_saved, "No Wi-Fi saved - tap below to set up");
    }
}

static void update_brightness_label(uint8_t pct)
{
    if (!label_brightness_value) {
        return;
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)pct);
    lv_label_set_text(label_brightness_value, buf);
}

static void update_theme_buttons(void)
{
    const bool dark = app_theme_is_dark();
    app_theme_style_segment_btn(btn_theme_dark, lbl_theme_dark, dark);
    app_theme_style_segment_btn(btn_theme_light, lbl_theme_light, !dark);
}

static void hide_keyboard(void)
{
    if (!keyboard) {
        return;
    }
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(keyboard, NULL);
    if (ta_username) {
        lv_obj_clear_state(ta_username, LV_STATE_FOCUSED);
    }
}

static void focus_ta(lv_obj_t *ta)
{
    if (!keyboard || !ta) {
        return;
    }
    lv_keyboard_set_textarea(keyboard, ta);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
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
    if (code == LV_EVENT_READY && ta_username) {
        user_profile_save(lv_textarea_get_text(ta_username));
        main_screen_update_greeting();
    }
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        hide_keyboard();
    }
}

static void refresh_username_field(void)
{
    if (!ta_username) {
        return;
    }
    char name[USER_PROFILE_NAME_MAX];
    user_profile_get_stored(name, sizeof(name));
    lv_textarea_set_text(ta_username, name);
}

static void on_brightness_changed(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    lv_obj_t *slider = lv_event_get_target(e);
    const int v = lv_slider_get_value(slider);
    const uint8_t pct = (uint8_t)(v < 10 ? 10 : (v > 100 ? 100 : v));
    display_backlight_set_percent(pct);
    update_brightness_label(pct);
}

static void on_brightness_released(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    display_backlight_save();
}

static void on_theme_dark(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (!app_theme_is_dark()) {
        app_theme_set_dark(true);
        app_theme_save();
        app_theme_apply_all();
        update_theme_buttons();
    }
}

static void on_theme_light(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (app_theme_is_dark()) {
        app_theme_set_dark(false);
        app_theme_save();
        app_theme_apply_all();
        update_theme_buttons();
    }
}

static void on_btn_wifi(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    screen_nav_show_wifi_settings();
}

void settings_screen_init(void)
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

    label_title = lv_label_create(screen);
    lv_label_set_text(label_title, "Settings");
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_hex(c->text_primary), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 16);

    nav_create_close_btn(screen);

    lv_obj_t *hint = lv_label_create(screen);
    lv_label_set_text(hint, "Tap " LV_SYMBOL_CLOSE " to return home");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 48);

    settings_scroll = lv_obj_create(screen);
    lv_obj_set_size(settings_scroll, 760, 380);
    lv_obj_align(settings_scroll, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_set_style_bg_opa(settings_scroll, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(settings_scroll, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(settings_scroll, 0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(settings_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(settings_scroll, LV_SCROLLBAR_MODE_AUTO);

    settings_card = lv_obj_create(settings_scroll);
    lv_obj_set_width(settings_card, lv_pct(100));
    lv_obj_set_height(settings_card, LV_SIZE_CONTENT);
    lv_obj_clear_flag(settings_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(settings_card, lv_color_hex(c->bg_card), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(settings_card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(settings_card, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(settings_card, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(settings_card, 20, LV_PART_MAIN);
    lv_obj_set_flex_flow(settings_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(settings_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(settings_card, 16, LV_PART_MAIN);

    lv_obj_t *row_name = lv_obj_create(settings_card);
    lv_obj_set_width(row_name, lv_pct(100));
    lv_obj_set_height(row_name, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row_name, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row_name, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row_name, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row_name, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row_name, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row_name, 8, LV_PART_MAIN);

    lv_obj_t *lbl_name = lv_label_create(row_name);
    lv_label_set_text(lbl_name, "Your name");
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_name, lv_color_hex(c->text_muted), LV_PART_MAIN);

    ta_username = lv_textarea_create(row_name);
    lv_obj_set_width(ta_username, lv_pct(100));
    lv_obj_set_height(ta_username, 44);
    lv_textarea_set_one_line(ta_username, true);
    lv_textarea_set_max_length(ta_username, USER_PROFILE_NAME_MAX - 1);
    lv_textarea_set_placeholder_text(ta_username, USER_PROFILE_DEFAULT_NAME);
    app_theme_style_textarea(ta_username);
    lv_obj_add_event_cb(ta_username, on_ta_focus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta_username, on_keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(ta_username, on_keyboard_event, LV_EVENT_CANCEL, NULL);

    lv_obj_t *lbl_name_hint = lv_label_create(row_name);
    lv_label_set_text(lbl_name_hint, "Leave empty to use \"Haha\". Tap check on keyboard to save.");
    lv_obj_set_style_text_font(lbl_name_hint, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_name_hint, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_label_set_long_mode(lbl_name_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_name_hint, lv_pct(100));

    lv_obj_t *row_ip = lv_obj_create(settings_card);
    lv_obj_set_width(row_ip, lv_pct(100));
    lv_obj_set_height(row_ip, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row_ip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row_ip, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row_ip, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row_ip, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row_ip, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row_ip, 6, LV_PART_MAIN);

    lv_obj_t *lbl_ip = lv_label_create(row_ip);
    lv_label_set_text(lbl_ip, "Device IP");
    lv_obj_set_style_text_font(lbl_ip, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_ip, lv_color_hex(c->text_muted), LV_PART_MAIN);

    label_ip_value = lv_label_create(row_ip);
    lv_label_set_text(label_ip_value, "—");
    lv_obj_set_style_text_font(label_ip_value, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_ip_value, lv_color_hex(c->text_primary), LV_PART_MAIN);

    lv_obj_t *lbl_gw = lv_label_create(row_ip);
    lv_label_set_text(lbl_gw, "Gateway IP");
    lv_obj_set_style_text_font(lbl_gw, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_gw, lv_color_hex(c->text_muted), LV_PART_MAIN);

    label_gateway_value = lv_label_create(row_ip);
    lv_label_set_text(label_gateway_value, "—");
    lv_obj_set_style_text_font(label_gateway_value, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_gateway_value, lv_color_hex(c->text_primary), LV_PART_MAIN);

    lv_obj_t *lbl_sn = lv_label_create(row_ip);
    lv_label_set_text(lbl_sn, "Subnet mask");
    lv_obj_set_style_text_font(lbl_sn, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_sn, lv_color_hex(c->text_muted), LV_PART_MAIN);

    label_subnet_value = lv_label_create(row_ip);
    lv_label_set_text(label_subnet_value, "—");
    lv_obj_set_style_text_font(label_subnet_value, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_subnet_value, lv_color_hex(c->text_primary), LV_PART_MAIN);

    lv_obj_t *row_static = lv_obj_create(settings_card);
    lv_obj_set_width(row_static, lv_pct(100));
    lv_obj_set_height(row_static, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row_static, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row_static, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row_static, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row_static, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row_static, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row_static, 8, LV_PART_MAIN);

    lv_obj_t *lbl_static = lv_label_create(row_static);
    lv_label_set_text(lbl_static, "Static IP");
    lv_obj_set_style_text_font(lbl_static, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_static, lv_color_hex(c->text_muted), LV_PART_MAIN);

    lv_obj_t *sw_row = lv_obj_create(row_static);
    lv_obj_set_width(sw_row, lv_pct(100));
    lv_obj_set_height(sw_row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(sw_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(sw_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(sw_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sw_row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(sw_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sw_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(sw_row, 12, LV_PART_MAIN);

    lv_obj_t *lbl_sw = lv_label_create(sw_row);
    lv_label_set_text(lbl_sw, "Use static IP");
    lv_obj_set_style_text_font(lbl_sw, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_sw, lv_color_hex(c->text_secondary), LV_PART_MAIN);

    sw_static_ip = lv_switch_create(sw_row);
    lv_obj_add_state(sw_static_ip, LV_STATE_CHECKED);

#define ADD_STATIC_IP_TA(title, out_ta)                                                              \
    do {                                                                                             \
        lv_obj_t *r = lv_obj_create(row_static);                                                     \
        lv_obj_set_width(r, lv_pct(100));                                                            \
        lv_obj_set_height(r, LV_SIZE_CONTENT);                                                       \
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);                                                \
        lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, LV_PART_MAIN);                                   \
        lv_obj_set_style_border_width(r, 0, LV_PART_MAIN);                                         \
        lv_obj_set_style_pad_all(r, 0, LV_PART_MAIN);                                                \
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_COLUMN);                                                \
        lv_obj_set_style_pad_row(r, 4, LV_PART_MAIN);                                              \
        lv_obj_t *lb = lv_label_create(r);                                                         \
        lv_label_set_text(lb, title);                                                              \
        lv_obj_set_style_text_font(lb, &lv_font_montserrat_14, LV_PART_MAIN);                      \
        lv_obj_set_style_text_color(lb, lv_color_hex(c->text_muted), LV_PART_MAIN);                  \
        out_ta = lv_textarea_create(r);                                                              \
        lv_obj_set_width(out_ta, lv_pct(100));                                                       \
        lv_obj_set_height(out_ta, 40);                                                             \
        lv_textarea_set_one_line(out_ta, true);                                                    \
        lv_textarea_set_max_length(out_ta, 15);                                                      \
        app_theme_style_textarea(out_ta);                                                          \
        lv_obj_add_event_cb(out_ta, on_ta_focus, LV_EVENT_FOCUSED, NULL);                          \
    } while (0)

    ADD_STATIC_IP_TA("Static device IP", ta_static_ip);
    ADD_STATIC_IP_TA("Static gateway IP", ta_static_gw);
    ADD_STATIC_IP_TA("Static subnet mask", ta_static_sn);
    lv_textarea_set_text(ta_static_sn, "255.255.255.0");
    lv_obj_add_event_cb(ta_static_ip, on_static_ip_field_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ta_static_ip, on_static_ip_field_event, LV_EVENT_DEFOCUSED, NULL);

    btn_save_static = lv_btn_create(row_static);
    lv_obj_set_size(btn_save_static, lv_pct(100), 44);
    lv_obj_add_event_cb(btn_save_static, on_save_static_ip, LV_EVENT_CLICKED, NULL);
    lbl_save_static = lv_label_create(btn_save_static);
    lv_label_set_text(lbl_save_static, "Save IP & reconnect Wi-Fi");
    lv_obj_set_style_text_font(lbl_save_static, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(lbl_save_static);
    app_theme_style_action_btn(btn_save_static, lbl_save_static);

    label_static_status = lv_label_create(row_static);
    lv_label_set_long_mode(label_static_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_static_status, lv_pct(100));
    lv_obj_set_style_text_font(label_static_status, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_static_status, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_label_set_text(label_static_status, "Turn off static IP to use DHCP from the router.");

    lv_obj_t *row_wifi = lv_obj_create(settings_card);
    lv_obj_set_width(row_wifi, lv_pct(100));
    lv_obj_set_height(row_wifi, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row_wifi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row_wifi, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row_wifi, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row_wifi, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row_wifi, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row_wifi, 8, LV_PART_MAIN);

    lv_obj_t *lbl_wifi = lv_label_create(row_wifi);
    lv_label_set_text(lbl_wifi, "Wi-Fi");
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(c->text_muted), LV_PART_MAIN);

    label_wifi_saved = lv_label_create(row_wifi);
    lv_label_set_long_mode(label_wifi_saved, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_wifi_saved, lv_pct(100));
    lv_obj_set_style_text_font(label_wifi_saved, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_wifi_saved, lv_color_hex(c->text_secondary), LV_PART_MAIN);

    btn_wifi = lv_btn_create(row_wifi);
    lv_obj_set_size(btn_wifi, lv_pct(100), 48);
    lv_obj_add_event_cb(btn_wifi, on_btn_wifi, LV_EVENT_CLICKED, NULL);
    lbl_wifi_btn = lv_label_create(btn_wifi);
    lv_label_set_text(lbl_wifi_btn, "View / edit Wi-Fi settings");
    lv_obj_set_style_text_font(lbl_wifi_btn, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(lbl_wifi_btn);
    app_theme_style_action_btn(btn_wifi, lbl_wifi_btn);

    lv_obj_t *row_bl = lv_obj_create(settings_card);
    lv_obj_set_width(row_bl, lv_pct(100));
    lv_obj_set_height(row_bl, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row_bl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row_bl, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row_bl, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row_bl, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row_bl, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row_bl, 8, LV_PART_MAIN);

    lv_obj_t *bl_head = lv_obj_create(row_bl);
    lv_obj_set_width(bl_head, lv_pct(100));
    lv_obj_set_height(bl_head, LV_SIZE_CONTENT);
    lv_obj_clear_flag(bl_head, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(bl_head, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(bl_head, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bl_head, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(bl_head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bl_head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_bl = lv_label_create(bl_head);
    lv_label_set_text(lbl_bl, "Screen brightness");
    lv_obj_set_style_text_font(lbl_bl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_bl, lv_color_hex(c->text_muted), LV_PART_MAIN);

    label_brightness_value = lv_label_create(bl_head);
    lv_label_set_text(label_brightness_value, "80%");
    lv_obj_set_style_text_font(label_brightness_value, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_brightness_value, lv_color_hex(c->text_primary), LV_PART_MAIN);

    slider_brightness = lv_slider_create(row_bl);
    lv_obj_set_width(slider_brightness, lv_pct(100));
    lv_slider_set_range(slider_brightness, 10, 100);
    lv_slider_set_value(slider_brightness, display_backlight_get_percent(), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_brightness, on_brightness_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider_brightness, on_brightness_released, LV_EVENT_RELEASED, NULL);

    lv_obj_t *row_theme = lv_obj_create(settings_card);
    lv_obj_set_width(row_theme, lv_pct(100));
    lv_obj_set_height(row_theme, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row_theme, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row_theme, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row_theme, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row_theme, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row_theme, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row_theme, 10, LV_PART_MAIN);

    lv_obj_t *lbl_theme = lv_label_create(row_theme);
    lv_label_set_text(lbl_theme, "Appearance");
    lv_obj_set_style_text_font(lbl_theme, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_theme, lv_color_hex(c->text_muted), LV_PART_MAIN);

    lv_obj_t *theme_row = lv_obj_create(row_theme);
    lv_obj_set_width(theme_row, lv_pct(100));
    lv_obj_set_height(theme_row, 52);
    lv_obj_clear_flag(theme_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(theme_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(theme_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(theme_row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(theme_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(theme_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(theme_row, 12, LV_PART_MAIN);

    btn_theme_dark = lv_btn_create(theme_row);
    lv_obj_set_size(btn_theme_dark, 160, 48);
    lv_obj_add_event_cb(btn_theme_dark, on_theme_dark, LV_EVENT_CLICKED, NULL);
    lbl_theme_dark = lv_label_create(btn_theme_dark);
    lv_label_set_text(lbl_theme_dark, "Dark");
    lv_obj_set_style_text_font(lbl_theme_dark, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(lbl_theme_dark);

    btn_theme_light = lv_btn_create(theme_row);
    lv_obj_set_size(btn_theme_light, 160, 48);
    lv_obj_add_event_cb(btn_theme_light, on_theme_light, LV_EVENT_CLICKED, NULL);
    lbl_theme_light = lv_label_create(btn_theme_light);
    lv_label_set_text(lbl_theme_light, "Light");
    lv_obj_set_style_text_font(lbl_theme_light, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(lbl_theme_light);

    label_theme_hint = lv_label_create(row_theme);
    lv_label_set_text(label_theme_hint,
                      "Brightness may be on/off only on this board\n"
                      "unless panel PWM backlight is enabled.");
    lv_obj_set_style_text_font(label_theme_hint, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_theme_hint, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_label_set_long_mode(label_theme_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_theme_hint, lv_pct(100));

    keyboard = lv_keyboard_create(screen);
    lv_obj_set_size(keyboard, ESP_PANEL_LCD_H_RES, 200);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    app_theme_style_keyboard(keyboard);
    lv_obj_add_event_cb(keyboard, on_keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, on_keyboard_event, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

    update_theme_buttons();

    lv_obj_add_event_cb(screen, settings_screen_unloaded_cb, LV_EVENT_ALL, NULL);
    screen_ready = true;
}

bool settings_screen_is_ready(void)
{
    return screen_ready;
}

void settings_screen_destroy(void)
{
    if (!screen_ready) {
        return;
    }
    screen_ready = false;
    hide_keyboard();
    if (screen) {
        lv_obj_del(screen);
    }
    settings_clear_widget_ptrs();
}

lv_obj_t *settings_screen_get_screen(void)
{
    if (!screen_ready) {
        settings_screen_init();
    }
    return screen;
}

void settings_screen_show(void)
{
    if (!screen_ready) {
        settings_screen_init();
    }
    settings_screen_refresh_network();
    load_static_fields();
    autofill_static_from_wifi(true);
    refresh_username_field();
    hide_keyboard();
    const uint8_t pct = display_backlight_get_percent();
    lv_slider_set_value(slider_brightness, pct, LV_ANIM_OFF);
    update_brightness_label(pct);
    update_theme_buttons();
}

void settings_screen_refresh_network(void)
{
    refresh_network_labels();
    refresh_wifi_saved_label();
    load_static_fields();
    autofill_static_from_wifi(true);
}

void settings_screen_apply_theme(void)
{
    if (!screen_ready) {
        return;
    }
    const app_theme_colors_t *c = app_theme_colors();
    lv_obj_set_style_bg_color(screen, lv_color_hex(c->bg), LV_PART_MAIN);
    if (settings_card) {
        lv_obj_set_style_bg_color(settings_card, lv_color_hex(c->bg_card), LV_PART_MAIN);
    }
    lv_obj_set_style_text_color(label_title, lv_color_hex(c->text_primary), LV_PART_MAIN);
    lv_obj_set_style_text_color(label_ip_value, lv_color_hex(c->text_primary), LV_PART_MAIN);
    if (label_gateway_value) {
        lv_obj_set_style_text_color(label_gateway_value, lv_color_hex(c->text_primary), LV_PART_MAIN);
    }
    if (label_subnet_value) {
        lv_obj_set_style_text_color(label_subnet_value, lv_color_hex(c->text_primary), LV_PART_MAIN);
    }
    if (label_static_status) {
        lv_obj_set_style_text_color(label_static_status, lv_color_hex(c->text_muted), LV_PART_MAIN);
    }
    lv_obj_set_style_text_color(label_wifi_saved, lv_color_hex(c->text_secondary), LV_PART_MAIN);
    lv_obj_set_style_text_color(label_brightness_value, lv_color_hex(c->text_primary), LV_PART_MAIN);
    lv_obj_set_style_text_color(label_theme_hint, lv_color_hex(c->text_muted), LV_PART_MAIN);
    app_theme_style_action_btn(btn_wifi, lbl_wifi_btn);
    if (btn_save_static) {
        app_theme_style_action_btn(btn_save_static, lbl_save_static);
    }
    if (ta_username) {
        app_theme_style_textarea(ta_username);
    }
    if (ta_static_ip) {
        app_theme_style_textarea(ta_static_ip);
    }
    if (ta_static_gw) {
        app_theme_style_textarea(ta_static_gw);
    }
    if (ta_static_sn) {
        app_theme_style_textarea(ta_static_sn);
    }
    if (keyboard) {
        app_theme_style_keyboard(keyboard);
    }
    update_theme_buttons();
}
