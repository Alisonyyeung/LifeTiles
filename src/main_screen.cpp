#include "main_screen.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdint.h>
#include <time.h>

#include "comment_emoji_images.h"
#include "quote_likes_storage.h"
#include "quotes_api.h"
#include "weather_api.h"
#include "weather_icon_images.h"
#include "weather_icons.h"
#include "app_theme.h"
#include "screen_nav.h"
#include "settings_screen.h"
#include "user_profile.h"
#include "menu_screen.h"
#include "weather_screen.h"
#include "wifi_settings_screen.h"

#define MAIN_CLOCK_TOP_Y        56
#define MAIN_GREETING_TIME_GAP  18
#define MAIN_GREETING_ROW_W     680
#define MAIN_GREETING_ICON_ZOOM 150
#define QUOTE_DOUBLE_CLICK_MS   400

static lv_obj_t *screen;
static lv_obj_t *clock_col;
static lv_obj_t *content_col;
static lv_obj_t *greeting_row;
static lv_obj_t *label_greeting;
static lv_obj_t *img_greeting;
static lv_obj_t *label_time;
static lv_obj_t *label_date;
static lv_obj_t *quote_row;
static lv_obj_t *label_quote;
static lv_obj_t *img_like_mark;
static lv_obj_t *label_author;
static lv_obj_t *label_status;
static lv_obj_t *btn_likes;
static lv_obj_t *img_likes_btn;
static lv_obj_t *likes_panel;
static lv_obj_t *likes_list;
static lv_obj_t *btn_settings;
static lv_obj_t *lbl_settings_icon;
static lv_obj_t *btn_weather;
static lv_obj_t *img_weather;
static lv_obj_t *btn_shuffle;
static lv_obj_t *btn_today;

static lv_timer_t *clock_timer;
static bool screen_ready = false;
static uint32_t s_last_quote_click_ms;
static char s_cur_quote[LIKED_QUOTE_TEXT_MAX];
static char s_cur_author[LIKED_QUOTE_AUTHOR_MAX];

static void update_quote_like_mark(void);
static void likes_panel_refresh(void);
static void likes_panel_show(bool show);

static lv_obj_t *create_heart_img(lv_obj_t *parent)
{
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, &comment_emoji_heart);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    return img;
}

static void update_quote_like_mark(void)
{
    if (!img_like_mark || s_cur_quote[0] == '\0') {
        if (img_like_mark) {
            lv_obj_add_flag(img_like_mark, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (quote_likes_contains(s_cur_quote, s_cur_author)) {
        lv_obj_clear_flag(img_like_mark, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(img_like_mark, LV_OBJ_FLAG_HIDDEN);
    }
}

static void async_toggle_current_like(void *user_data)
{
    LV_UNUSED(user_data);
    if (s_cur_quote[0] == '\0') {
        return;
    }

    bool liked = false;
    if (!quote_likes_toggle(s_cur_quote, s_cur_author, &liked)) {
        main_screen_set_status("Liked list full (10 max)");
        return;
    }

    main_screen_set_status("");
    update_quote_like_mark();
    if (likes_panel && !lv_obj_has_flag(likes_panel, LV_OBJ_FLAG_HIDDEN)) {
        likes_panel_refresh();
    }
}

static void on_quote_row_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_cur_quote[0] == '\0') {
        return;
    }

    const uint32_t now = lv_tick_get();
    if (now - s_last_quote_click_ms < QUOTE_DOUBLE_CLICK_MS) {
        s_last_quote_click_ms = 0;
        lv_async_call(async_toggle_current_like, NULL);
        return;
    }
    s_last_quote_click_ms = now;
}

static void async_toggle_list_like(void *user_data)
{
    const uint8_t idx = (uint8_t)(uintptr_t)user_data;
    liked_quotes_list_t list;
    if (!quote_likes_load(&list) || idx >= list.count) {
        return;
    }

    bool liked = false;
    quote_likes_toggle(list.items[idx].text, list.items[idx].author, &liked);

    update_quote_like_mark();
    likes_panel_refresh();
}

static void on_liked_row_heart(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    lv_async_call(async_toggle_list_like, (void *)(uintptr_t)idx);
}

static void on_likes_close(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    likes_panel_show(false);
}

static void on_btn_likes(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    likes_panel_refresh();
    likes_panel_show(true);
}

static void likes_panel_show(bool show)
{
    if (!likes_panel) {
        return;
    }
    if (show) {
        lv_obj_clear_flag(likes_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(likes_panel);
    } else {
        lv_obj_add_flag(likes_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void likes_panel_refresh(void)
{
    if (!likes_list) {
        return;
    }

    lv_obj_clean(likes_list);

    liked_quotes_list_t list;
    if (!quote_likes_load(&list)) {
        lv_obj_t *empty = lv_label_create(likes_list);
        lv_label_set_text(empty, "Could not load liked quotes.");
        return;
    }

    if (list.count == 0) {
        lv_obj_t *empty = lv_label_create(likes_list);
        lv_label_set_text(empty, "No liked quotes yet.\nDouble-tap a quote to like.");
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        return;
    }

    const app_theme_colors_t *c = app_theme_colors();
    for (uint8_t i = 0; i < list.count; ++i) {
        lv_obj_t *row = lv_obj_create(likes_list);
        lv_obj_set_width(row, 660);
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(row, lv_color_hex(c->bg_card), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(row, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *col = lv_obj_create(row);
        lv_obj_set_width(col, 580);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);

        lv_obj_t *q = lv_label_create(col);
        lv_obj_set_width(q, 560);
        lv_label_set_long_mode(q, LV_LABEL_LONG_WRAP);
        lv_label_set_text(q, list.items[i].text);
        lv_obj_set_style_text_font(q, &lv_font_montserrat_20, LV_PART_MAIN);
        lv_obj_set_style_text_color(q, lv_color_hex(c->text_primary), LV_PART_MAIN);

        if (list.items[i].author[0] != '\0') {
            lv_obj_t *a = lv_label_create(col);
            lv_obj_set_width(a, 560);
            lv_label_set_long_mode(a, LV_LABEL_LONG_WRAP);
            char line[160];
            snprintf(line, sizeof(line), "by %s", list.items[i].author);
            lv_label_set_text(a, line);
            lv_obj_set_style_text_font(a, &lv_font_montserrat_14, LV_PART_MAIN);
            lv_obj_set_style_text_color(a, lv_color_hex(c->text_muted), LV_PART_MAIN);
        }

        lv_obj_t *btn = lv_btn_create(row);
        lv_obj_set_size(btn, 44, 44);
        lv_obj_add_event_cb(btn, on_liked_row_heart, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *h = create_heart_img(btn);
        lv_obj_center(h);
    }
}

static const char *ordinal_suffix(int day)
{
    if (day >= 11 && day <= 13) {
        return "th";
    }
    switch (day % 10) {
    case 1: return "st";
    case 2: return "nd";
    case 3: return "rd";
    default: return "th";
    }
}

static const char *month_name(int month)
{
    static const char *names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    if (month < 1 || month > 12) {
        return "";
    }
    return names[month - 1];
}

static void format_date_string(const struct tm *timeinfo, char *buf, size_t buf_len)
{
    snprintf(buf, buf_len, "%d%s %s, %d",
             timeinfo->tm_mday,
             ordinal_suffix(timeinfo->tm_mday),
             month_name(timeinfo->tm_mon + 1),
             timeinfo->tm_year + 1900);
}

void main_screen_update_greeting(void)
{
    if (!label_greeting) {
        return;
    }

    struct tm timeinfo;
    char greet_buf[96];
    const bool have_time = getLocalTime(&timeinfo, 0);
    user_profile_format_greeting(greet_buf, sizeof(greet_buf), have_time ? &timeinfo : NULL);
    lv_label_set_text(label_greeting, greet_buf);

    if (img_greeting) {
        if (have_time) {
            const bool show_sun = user_profile_greeting_show_sun(&timeinfo);
            weather_icon_set_src_ex(img_greeting, 0, show_sun);
            lv_img_set_zoom(img_greeting, MAIN_GREETING_ICON_ZOOM);
            lv_obj_clear_flag(img_greeting, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(img_greeting, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void update_clock_labels(void)
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) {
        lv_label_set_text(label_time, "--:--");
        lv_label_set_text(label_date, "Syncing time...");
        main_screen_update_greeting();
        return;
    }

    char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    lv_label_set_text(label_time, time_buf);

    char date_buf[48];
    format_date_string(&timeinfo, date_buf, sizeof(date_buf));
    lv_label_set_text(label_date, date_buf);
    main_screen_update_greeting();
}

static void clock_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    update_clock_labels();
}

static void on_btn_shuffle(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    quotes_api_request(QUOTE_FETCH_RANDOM);
}

static void on_btn_today(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    quotes_api_request(QUOTE_FETCH_TODAY);
}

static void on_btn_weather(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    weather_screen_show();
    screen_nav_show_weather();
}

static void on_btn_settings(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    settings_screen_show();
    screen_nav_show_settings();
}

void main_screen_show_wifi_connecting(const char *ssid)
{
    lv_obj_t *boot = lv_obj_create(NULL);
    lv_obj_clear_flag(boot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(boot, lv_color_hex(0x0a0a0f), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(boot, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(boot);
    char buf[128];
    if (ssid != NULL && ssid[0] != '\0') {
        snprintf(buf, sizeof(buf), "Connecting to Wi-Fi\n\"%s\"", ssid);
    } else {
        snprintf(buf, sizeof(buf), "Connecting to Wi-Fi...");
    }
    lv_label_set_text(label, buf);
    lv_obj_set_width(label, 700);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0xC8C8D8), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    lv_scr_load(boot);
    lv_refr_now(NULL);
}

void main_screen_create(lv_obj_t *parent)
{
    const app_theme_colors_t *c = app_theme_colors();

    screen = parent;
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(c->bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    btn_settings = lv_btn_create(screen);
    lv_obj_set_size(btn_settings, 48, 48);
    lv_obj_align(btn_settings, LV_ALIGN_TOP_LEFT, 12, 8);
    lv_obj_add_event_cb(btn_settings, on_btn_settings, LV_EVENT_CLICKED, NULL);
    lbl_settings_icon = lv_label_create(btn_settings);
    lv_label_set_text(lbl_settings_icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(lbl_settings_icon, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(lbl_settings_icon);
    app_theme_style_icon_btn(btn_settings, lbl_settings_icon);

    label_status = lv_label_create(screen);
    lv_label_set_text(label_status, "");
    lv_obj_set_style_text_font(label_status, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_status, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_obj_align(label_status, LV_ALIGN_TOP_LEFT, 68, 20);

    btn_weather = lv_btn_create(screen);
    lv_obj_set_size(btn_weather, 56, 56);
    lv_obj_align(btn_weather, LV_ALIGN_TOP_RIGHT, -12, 8);
    lv_obj_add_event_cb(btn_weather, on_btn_weather, LV_EVENT_CLICKED, NULL);
    app_theme_style_icon_btn(btn_weather, NULL);

    img_weather = lv_img_create(btn_weather);
    lv_img_set_src(img_weather, &weather_img_unknown);
    lv_img_set_zoom(img_weather, 200);
    lv_obj_center(img_weather);

    clock_col = lv_obj_create(screen);
    lv_obj_set_width(clock_col, 720);
    lv_obj_set_height(clock_col, LV_SIZE_CONTENT);
    lv_obj_clear_flag(clock_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(clock_col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(clock_col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(clock_col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(clock_col, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(clock_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(clock_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(clock_col, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_align(clock_col, LV_ALIGN_TOP_MID, 0, MAIN_CLOCK_TOP_Y);

    greeting_row = lv_obj_create(clock_col);
    lv_obj_set_width(greeting_row, MAIN_GREETING_ROW_W);
    lv_obj_set_height(greeting_row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(greeting_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(greeting_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(greeting_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(greeting_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(greeting_row, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(greeting_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(greeting_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    label_greeting = lv_label_create(greeting_row);
    lv_label_set_text(label_greeting, "");
    lv_obj_set_width(label_greeting, LV_SIZE_CONTENT);
    lv_label_set_long_mode(label_greeting, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_width(label_greeting, MAIN_GREETING_ROW_W - 56, LV_PART_MAIN);
    lv_obj_set_style_text_font(label_greeting, &lv_font_montserrat_44, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_greeting, lv_color_hex(c->text_primary), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_greeting, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    img_greeting = lv_img_create(greeting_row);
    lv_img_set_src(img_greeting, &weather_img_unknown);
    lv_img_set_zoom(img_greeting, MAIN_GREETING_ICON_ZOOM);
    lv_obj_add_flag(img_greeting, LV_OBJ_FLAG_HIDDEN);

    label_time = lv_label_create(clock_col);
    lv_label_set_text(label_time, "--:--");
    lv_obj_set_style_text_font(label_time, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_time, lv_color_hex(c->text_primary), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_top(label_time, MAIN_GREETING_TIME_GAP, LV_PART_MAIN);

    label_date = lv_label_create(clock_col);
    lv_label_set_text(label_date, "");
    lv_obj_set_width(label_date, 680);
    lv_label_set_long_mode(label_date, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label_date, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_date, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_date, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    content_col = lv_obj_create(screen);
    lv_obj_set_width(content_col, 720);
    lv_obj_set_height(content_col, LV_SIZE_CONTENT);
    lv_obj_clear_flag(content_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(content_col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content_col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content_col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(content_col, 14, LV_PART_MAIN);
    lv_obj_set_flex_flow(content_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(content_col, LV_ALIGN_CENTER, 0, 52);

    quote_row = lv_obj_create(content_col);
    lv_obj_set_width(quote_row, 680);
    lv_obj_set_height(quote_row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(quote_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(quote_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(quote_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(quote_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(quote_row, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(quote_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(quote_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(quote_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(quote_row, on_quote_row_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_pad_top(quote_row, 10, LV_PART_MAIN);

    label_quote = lv_label_create(quote_row);
    lv_obj_set_width(label_quote, 640);
    lv_label_set_long_mode(label_quote, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label_quote, "Loading quote...");
    lv_obj_set_style_text_font(label_quote, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_quote, lv_color_hex(c->text_secondary), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_quote, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_clear_flag(label_quote, LV_OBJ_FLAG_CLICKABLE);

    img_like_mark = create_heart_img(quote_row);
    lv_obj_add_flag(img_like_mark, LV_OBJ_FLAG_HIDDEN);

    label_author = lv_label_create(content_col);
    lv_label_set_text(label_author, "");
    lv_obj_set_width(label_author, 680);
    lv_label_set_long_mode(label_author, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label_author, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_author, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_author, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *btn_row = lv_obj_create(screen);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, 64);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, -32);

    lv_obj_t *quote_btns = lv_obj_create(btn_row);
    lv_obj_set_size(quote_btns, LV_SIZE_CONTENT, 64);
    lv_obj_clear_flag(quote_btns, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(quote_btns, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(quote_btns, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(quote_btns, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(quote_btns, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(quote_btns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(quote_btns, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    likes_panel = lv_obj_create(screen);
    lv_obj_set_size(likes_panel, 740, 420);
    lv_obj_align(likes_panel, LV_ALIGN_CENTER, 0, 20);
    lv_obj_clear_flag(likes_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(likes_panel, lv_color_hex(c->bg_card), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(likes_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(likes_panel, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_obj_set_style_border_width(likes_panel, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(likes_panel, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_all(likes_panel, 16, LV_PART_MAIN);
    lv_obj_add_flag(likes_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *likes_hdr = lv_obj_create(likes_panel);
    lv_obj_set_width(likes_hdr, LV_PCT(100));
    lv_obj_set_height(likes_hdr, 40);
    lv_obj_clear_flag(likes_hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(likes_hdr, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(likes_hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(likes_hdr, 0, LV_PART_MAIN);

    lv_obj_t *likes_title = lv_label_create(likes_hdr);
    lv_label_set_text(likes_title, "Liked quotes");
    lv_obj_set_style_text_font(likes_title, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(likes_title, lv_color_hex(c->text_primary), LV_PART_MAIN);
    lv_obj_align(likes_title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *btn_close_likes = lv_btn_create(likes_hdr);
    lv_obj_set_size(btn_close_likes, 40, 40);
    lv_obj_align(btn_close_likes, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(btn_close_likes, on_likes_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_close = lv_label_create(btn_close_likes);
    lv_label_set_text(lbl_close, LV_SYMBOL_CLOSE);
    lv_obj_center(lbl_close);

    likes_list = lv_obj_create(likes_panel);
    lv_obj_set_width(likes_list, LV_PCT(100));
    lv_obj_set_height(likes_list, 340);
    lv_obj_align(likes_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_scroll_dir(likes_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(likes_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(likes_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(likes_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(likes_list, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(likes_list, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(likes_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(likes_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    btn_shuffle = lv_btn_create(quote_btns);
    lv_obj_set_size(btn_shuffle, 220, 52);
    lv_obj_add_event_cb(btn_shuffle, on_btn_shuffle, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_shuffle = lv_label_create(btn_shuffle);
    lv_label_set_text(lbl_shuffle, "Shuffle");
    lv_obj_set_style_text_font(lbl_shuffle, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(lbl_shuffle);

    btn_today = lv_btn_create(quote_btns);
    lv_obj_set_size(btn_today, 220, 52);
    lv_obj_add_event_cb(btn_today, on_btn_today, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_today = lv_label_create(btn_today);
    lv_label_set_text(lbl_today, "Today");
    lv_obj_set_style_text_font(lbl_today, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(lbl_today);

    btn_likes = lv_btn_create(quote_btns);
    lv_obj_set_size(btn_likes, 52, 52);
    lv_obj_add_event_cb(btn_likes, on_btn_likes, LV_EVENT_CLICKED, NULL);
    img_likes_btn = create_heart_img(btn_likes);
    lv_obj_center(img_likes_btn);
    app_theme_style_icon_btn(btn_likes, NULL);

    weather_screen_init();
    settings_screen_init();
    wifi_settings_screen_init();
    menu_screen_init();

    lv_obj_t *hint_menu = lv_label_create(screen);
    lv_label_set_text(hint_menu, "Down: menu  |  Right: comment  |  Left: images / to-do");
    lv_obj_set_style_text_font(hint_menu, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint_menu, lv_color_hex(c->text_muted), LV_PART_MAIN);
    lv_obj_align(hint_menu, LV_ALIGN_BOTTOM_MID, 0, -8);

    update_clock_labels();
    main_screen_update_greeting();
    clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    screen_ready = true;
}

void main_screen_show(void)
{
    screen_nav_show_home();
}

void main_screen_set_status(const char *text)
{
    if (label_status) {
        lv_label_set_text(label_status, text ? text : "");
    }
}

void main_screen_set_weather_icon(int weather_code)
{
    main_screen_set_weather_icon_ex(weather_code, true);
}

void main_screen_set_weather_icon_ex(int weather_code, bool is_day)
{
    weather_icon_set_src_ex(img_weather, weather_code, is_day);
}

void main_screen_set_quote_loading(void)
{
    if (!label_quote || !label_author) {
        return;
    }
    s_cur_quote[0] = '\0';
    s_cur_author[0] = '\0';
    lv_label_set_text(label_quote, "Loading quote...");
    lv_label_set_text(label_author, "");
    update_quote_like_mark();
}

void main_screen_apply_theme(void)
{
    if (!screen) {
        return;
    }
    const app_theme_colors_t *c = app_theme_colors();
    lv_obj_set_style_bg_color(screen, lv_color_hex(c->bg), LV_PART_MAIN);
    if (label_greeting) {
        lv_obj_set_style_text_color(label_greeting, lv_color_hex(c->text_primary), LV_PART_MAIN);
    }
    if (label_time) {
        lv_obj_set_style_text_color(label_time, lv_color_hex(c->text_primary), LV_PART_MAIN);
    }
    if (label_date) {
        lv_obj_set_style_text_color(label_date, lv_color_hex(c->text_muted), LV_PART_MAIN);
    }
    if (label_quote) {
        lv_obj_set_style_text_color(label_quote, lv_color_hex(c->text_secondary), LV_PART_MAIN);
    }
    if (label_author) {
        lv_obj_set_style_text_color(label_author, lv_color_hex(c->text_muted), LV_PART_MAIN);
    }
    if (label_status) {
        lv_obj_set_style_text_color(label_status, lv_color_hex(c->text_muted), LV_PART_MAIN);
    }
    app_theme_style_icon_btn(btn_settings, lbl_settings_icon);
    app_theme_style_icon_btn(btn_weather, NULL);
}

void main_screen_set_quote(const char *quote, const char *author)
{
    if (!label_quote || !label_author) {
        return;
    }

    strncpy(s_cur_quote, quote ? quote : "", sizeof(s_cur_quote) - 1);
    s_cur_quote[sizeof(s_cur_quote) - 1] = '\0';
    strncpy(s_cur_author, author ? author : "", sizeof(s_cur_author) - 1);
    s_cur_author[sizeof(s_cur_author) - 1] = '\0';

    lv_label_set_text(label_quote, s_cur_quote);

    if (s_cur_author[0] != '\0') {
        char author_line[160];
        snprintf(author_line, sizeof(author_line), "by %s", s_cur_author);
        lv_label_set_text(label_author, author_line);
    } else {
        lv_label_set_text(label_author, "");
    }

    update_quote_like_mark();
}
