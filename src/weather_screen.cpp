#include "weather_screen.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>

#include "nav_gestures.h"
#include "screen_nav.h"
#include "weather_api.h"
#include "weather_background.h"
#include "weather_icon_images.h"
#include "weather_icons.h"

#define WEATHER_PANEL_W         384
#define WEATHER_PANEL_H         388
#define WEATHER_PANEL_Y         78
#define WEATHER_PANEL_GAP       16
#define WEATHER_PANEL_L_X       8
#define WEATHER_PANEL_R_X       (WEATHER_PANEL_L_X + WEATHER_PANEL_W + WEATHER_PANEL_GAP)
#define FORECAST_PAGE_W         WEATHER_PANEL_W
#define FORECAST_VIEW_H         (WEATHER_PANEL_H - 52)
#define FORECAST_ICON_ZOOM      320
#define HOURLY_ICON_ZOOM        140
#define HOURLY_ROW_H            56
#define HOURLY_LIST_PAD_ROW     14
#define WEATHER_CARD_OPA        ((lv_opa_t)(LV_OPA_COVER * 8 / 10))
#define WEATHER_HEADER_ON_DARK     0xFFFFFF
#define WEATHER_PULL_REFRESH_PX    48
#define WEATHER_PULL_DEBOUNCE_MS   3000
/** Solid backdrop while fetching (no photo JPEG yet). */
#define WEATHER_REFRESH_BG         0xA8D4F0

typedef struct {
    uint32_t bg;
    uint32_t card;
    uint32_t text_primary;
    uint32_t text_secondary;
    uint32_t text_muted;
} weather_palette_t;

static const weather_palette_t s_palettes[2] = {
    {0xA8D4F0, 0xF0F8FC, 0x1A3048, 0x2A5070, 0x4A6880},
    {0x0C1420, 0x182838, 0xE4EEF6, 0xA8B8C8, 0x788898},
};

static bool s_day_appearance = true;
static int s_current_weather_code = 0;

static lv_obj_t *screen;
static lv_obj_t *img_background;
static lv_obj_t *label_title;
static lv_obj_t *label_hint;
static lv_obj_t *current_panel;
static lv_obj_t *forecast_panel;
static lv_obj_t *label_forecast_header;
static lv_obj_t *label_forecast_swipe;
static lv_obj_t *label_forecast_page;
static lv_obj_t *img_current_icon;
static lv_obj_t *label_current_heading;
static lv_obj_t *label_current_temp;
static lv_obj_t *label_current_desc;
static lv_obj_t *label_current_humidity;
static lv_obj_t *label_current_wind;
static lv_obj_t *forecast_viewport;
static lv_obj_t *forecast_pages[WEATHER_FORECAST_DAYS];
static lv_obj_t *forecast_summary[WEATHER_FORECAST_DAYS];
static lv_obj_t *today_hourly_list;
static lv_obj_t *forecast_page_day[WEATHER_FORECAST_DAYS];
static lv_obj_t *forecast_page_icon[WEATHER_FORECAST_DAYS];
static lv_obj_t *forecast_page_temp[WEATHER_FORECAST_DAYS];
static lv_obj_t *forecast_page_desc[WEATHER_FORECAST_DAYS];
static lv_obj_t *forecast_page_swipe_hint;

static bool screen_ready = false;
static int forecast_page_count = WEATHER_FORECAST_DAYS;
static uint32_t s_last_pull_refresh_ms = 0;

static void weather_clear_widget_ptrs(void)
{
    screen = NULL;
    img_background = NULL;
    label_title = NULL;
    label_hint = NULL;
    current_panel = NULL;
    forecast_panel = NULL;
    label_forecast_header = NULL;
    label_forecast_swipe = NULL;
    label_forecast_page = NULL;
    img_current_icon = NULL;
    label_current_heading = NULL;
    label_current_temp = NULL;
    label_current_desc = NULL;
    label_current_humidity = NULL;
    label_current_wind = NULL;
    forecast_viewport = NULL;
    today_hourly_list = NULL;
    forecast_page_swipe_hint = NULL;
    for (int i = 0; i < WEATHER_FORECAST_DAYS; ++i) {
        forecast_pages[i] = NULL;
        forecast_summary[i] = NULL;
        forecast_page_day[i] = NULL;
        forecast_page_icon[i] = NULL;
        forecast_page_temp[i] = NULL;
        forecast_page_desc[i] = NULL;
    }
}

static void weather_screen_unloaded_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_SCREEN_UNLOADED) {
        return;
    }
    weather_screen_destroy();
}

static const weather_palette_t *weather_palette(void)
{
    return &s_palettes[s_day_appearance ? 0 : 1];
}

static void style_weather_card(lv_obj_t *obj, uint32_t bg_hex)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg_hex), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, WEATHER_CARD_OPA, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 12, LV_PART_MAIN);
}

static void style_forecast_day_scroll(lv_obj_t *panel, lv_coord_t w, lv_coord_t view_h)
{
    lv_obj_set_width(panel, w);
    lv_obj_set_height(panel, view_h);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(panel, LV_SCROLL_SNAP_NONE);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
}

static lv_obj_t *create_forecast_scroll_content(lv_obj_t *page, lv_coord_t w, lv_coord_t view_h)
{
    lv_obj_t *content = lv_obj_create(page);
    lv_obj_set_width(content, w);
    lv_obj_set_height(content, LV_SIZE_CONTENT);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    return content;
}

static void style_text_on_panel(lv_obj_t *label, const weather_palette_t *p, const lv_font_t *font,
                                uint32_t color, lv_text_align_t align)
{
    if (!label) {
        return;
    }
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
}

static lv_obj_t *create_hourly_section(lv_obj_t *parent, lv_coord_t w, const weather_palette_t *p,
                                       lv_obj_t **out_list)
{
    lv_obj_t *section = lv_obj_create(parent);
    lv_obj_set_width(section, w);
    lv_obj_set_height(section, LV_SIZE_CONTENT);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(section, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(section, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(section, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(section, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(section, 6, LV_PART_MAIN);

    lv_obj_t *heading = lv_label_create(section);
    lv_label_set_text(heading, "Hourly");
    style_text_on_panel(heading, p, &lv_font_montserrat_20, p->text_primary, LV_TEXT_ALIGN_CENTER);

    lv_obj_t *list = lv_obj_create(section);
    lv_obj_set_width(list, w - 12);
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, HOURLY_LIST_PAD_ROW, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (out_list) {
        *out_list = list;
    }
    return section;
}

static void fill_hourly_list(lv_obj_t *list, const weather_hourly_slot_t *slots, int count,
                             const weather_palette_t *p, bool show_empty_message)
{
    if (!list) {
        return;
    }

    lv_obj_clean(list);

    if (!slots || count <= 0) {
        if (show_empty_message) {
            lv_obj_t *empty = lv_label_create(list);
            lv_label_set_text(empty, "No hourly data");
            style_text_on_panel(empty, p, &lv_font_montserrat_14, p->text_muted, LV_TEXT_ALIGN_CENTER);
        }
        return;
    }

    const lv_coord_t row_w = lv_obj_get_width(list) > 0 ? lv_obj_get_width(list) - 4 : FORECAST_PAGE_W - 24;

    for (int i = 0; i < count; ++i) {
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_set_size(row, row_w, HOURLY_ROW_H);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, lv_color_hex(p->card), LV_PART_MAIN);
        lv_obj_set_style_radius(row, 8, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 4, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *time_lbl = lv_label_create(row);
        lv_label_set_text(time_lbl, slots[i].time_label);
        style_text_on_panel(time_lbl, p, &lv_font_montserrat_14, p->text_primary, LV_TEXT_ALIGN_LEFT);
        lv_obj_set_width(time_lbl, 52);

        lv_obj_t *icon = lv_img_create(row);
        weather_icon_set_src_ex(icon, slots[i].weather_code, slots[i].is_day);
        lv_img_set_zoom(icon, HOURLY_ICON_ZOOM);

        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%.0f°", slots[i].temp_c);
        lv_obj_t *temp_lbl = lv_label_create(row);
        lv_label_set_text(temp_lbl, tbuf);
        style_text_on_panel(temp_lbl, p, &lv_font_montserrat_20, p->text_secondary, LV_TEXT_ALIGN_RIGHT);
        lv_obj_set_width(temp_lbl, 48);
    }
}

static bool weather_uses_day_background(int weather_code, bool is_day)
{
    return is_day && !weather_code_uses_rain_background(weather_code);
}

static bool weather_has_photo_background(void)
{
    return img_background && !lv_obj_has_flag(img_background, LV_OBJ_FLAG_HIDDEN);
}

static void apply_refresh_background(void)
{
    if (!screen_ready || !screen) {
        return;
    }
    if (img_background) {
        lv_img_set_src(img_background, NULL);
        lv_obj_add_flag(img_background, LV_OBJ_FLAG_HIDDEN);
    }
    weather_background_release_decoded();
    lv_obj_set_style_bg_color(screen, lv_color_hex(WEATHER_REFRESH_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
}

static void apply_weather_appearance(bool is_day)
{
    if (!screen_ready) {
        return;
    }

    s_day_appearance = is_day;
    const weather_palette_t *p = weather_palette();
    /* Day/rain/night photos: light cards + dark text (same as clear day). */
    const weather_palette_t *card_p = weather_has_photo_background() ? &s_palettes[0] : p;
    const bool day_bg = weather_uses_day_background(s_current_weather_code, is_day);
    const uint32_t header_color = day_bg ? p->text_primary : WEATHER_HEADER_ON_DARK;
    const uint32_t hint_color = day_bg ? p->text_muted : WEATHER_HEADER_ON_DARK;

    if (weather_has_photo_background()) {
        lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(screen, lv_color_hex(p->bg), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    }

    style_weather_card(current_panel, card_p->card);
    style_weather_card(forecast_panel, card_p->card);

    style_text_on_panel(label_title, p, &lv_font_montserrat_30, header_color, LV_TEXT_ALIGN_CENTER);
    style_text_on_panel(label_hint, p, &lv_font_montserrat_14, hint_color, LV_TEXT_ALIGN_CENTER);
    style_text_on_panel(label_current_heading, card_p, &lv_font_montserrat_20, card_p->text_muted,
                        LV_TEXT_ALIGN_CENTER);
    style_text_on_panel(label_current_temp, card_p, &lv_font_montserrat_48, card_p->text_primary,
                        LV_TEXT_ALIGN_CENTER);
    style_text_on_panel(label_current_desc, card_p, &lv_font_montserrat_20, card_p->text_muted,
                        LV_TEXT_ALIGN_CENTER);
    style_text_on_panel(label_current_humidity, card_p, &lv_font_montserrat_14, card_p->text_muted,
                        LV_TEXT_ALIGN_CENTER);
    style_text_on_panel(label_current_wind, card_p, &lv_font_montserrat_14, card_p->text_muted,
                        LV_TEXT_ALIGN_CENTER);
    style_text_on_panel(label_forecast_header, card_p, &lv_font_montserrat_20, card_p->text_muted,
                        LV_TEXT_ALIGN_LEFT);
    style_text_on_panel(label_forecast_swipe, card_p, &lv_font_montserrat_14, card_p->text_muted,
                        LV_TEXT_ALIGN_CENTER);
    style_text_on_panel(label_forecast_page, card_p, &lv_font_montserrat_14, card_p->text_muted,
                        LV_TEXT_ALIGN_RIGHT);

    for (int i = 0; i < WEATHER_FORECAST_DAYS; ++i) {
        style_text_on_panel(forecast_page_day[i], card_p, &lv_font_montserrat_20, card_p->text_primary,
                            LV_TEXT_ALIGN_CENTER);
        style_text_on_panel(forecast_page_desc[i], card_p, &lv_font_montserrat_14, card_p->text_muted,
                            LV_TEXT_ALIGN_CENTER);
        style_text_on_panel(forecast_page_temp[i], card_p, &lv_font_montserrat_20, card_p->text_secondary,
                            LV_TEXT_ALIGN_CENTER);
    }
    if (forecast_page_swipe_hint) {
        style_text_on_panel(forecast_page_swipe_hint, card_p, &lv_font_montserrat_14, card_p->text_muted,
                            LV_TEXT_ALIGN_CENTER);
    }
}

static void forecast_pull_refresh_event(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_SCROLL_END) {
        return;
    }

    lv_obj_t *page = lv_event_get_target(e);
    if (lv_obj_get_scroll_y(page) > -WEATHER_PULL_REFRESH_PX) {
        return;
    }

    const uint32_t now = millis();
    if (now - s_last_pull_refresh_ms < WEATHER_PULL_DEBOUNCE_MS) {
        lv_obj_scroll_to_y(page, 0, LV_ANIM_ON);
        return;
    }
    s_last_pull_refresh_ms = now;

    Serial.println("Weather: pull-to-refresh");
    weather_screen_set_loading();
    weather_api_request_refresh();
    lv_obj_scroll_to_y(page, 0, LV_ANIM_ON);
}

static void forecast_scroll_event(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_SCROLL_END || !forecast_viewport) {
        return;
    }
    const lv_coord_t x = lv_obj_get_scroll_x(forecast_viewport);
    const lv_coord_t page_w = lv_obj_get_width(forecast_viewport);
    if (page_w <= 0) {
        return;
    }
    int page = (int)((x + page_w / 2) / page_w);
    if (page < 0) {
        page = 0;
    }
    if (page >= forecast_page_count) {
        page = forecast_page_count - 1;
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "%d / %d", page + 1, forecast_page_count);
    lv_label_set_text(label_forecast_page, buf);
}

static void create_forecast_summary(lv_obj_t *parent, int index, const weather_palette_t *p)
{
    forecast_summary[index] = lv_obj_create(parent);
    lv_obj_set_size(forecast_summary[index], FORECAST_PAGE_W, FORECAST_VIEW_H);
    lv_obj_clear_flag(forecast_summary[index], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(forecast_summary[index], LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(forecast_summary[index], 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(forecast_summary[index], 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(forecast_summary[index], LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(forecast_summary[index], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    forecast_page_day[index] = lv_label_create(forecast_summary[index]);
    lv_label_set_text(forecast_page_day[index], "-");
    style_text_on_panel(forecast_page_day[index], p, &lv_font_montserrat_20, p->text_primary,
                        LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(forecast_page_day[index], LV_LABEL_LONG_WRAP);
    lv_obj_set_width(forecast_page_day[index], FORECAST_PAGE_W - 24);

    lv_obj_t *icon_slot = lv_obj_create(forecast_summary[index]);
    lv_obj_set_size(icon_slot, 110, 110);
    lv_obj_clear_flag(icon_slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(icon_slot, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(icon_slot, 0, LV_PART_MAIN);
    forecast_page_icon[index] = lv_img_create(icon_slot);
    lv_img_set_src(forecast_page_icon[index], &weather_img_unknown);
    lv_img_set_zoom(forecast_page_icon[index], FORECAST_ICON_ZOOM);
    lv_obj_center(forecast_page_icon[index]);

    forecast_page_desc[index] = lv_label_create(forecast_summary[index]);
    lv_label_set_text(forecast_page_desc[index], "-");
    style_text_on_panel(forecast_page_desc[index], p, &lv_font_montserrat_14, p->text_muted,
                        LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(forecast_page_desc[index], FORECAST_PAGE_W - 24);
    lv_label_set_long_mode(forecast_page_desc[index], LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_top(forecast_page_desc[index], 8, LV_PART_MAIN);

    forecast_page_temp[index] = lv_label_create(forecast_summary[index]);
    lv_label_set_text(forecast_page_temp[index], "-");
    style_text_on_panel(forecast_page_temp[index], p, &lv_font_montserrat_20, p->text_secondary,
                        LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_pad_top(forecast_page_temp[index], 10, LV_PART_MAIN);
}

static lv_obj_t *create_forecast_page(lv_obj_t *parent, int index)
{
    const weather_palette_t *p = weather_palette();
    const bool today_page = (index == 0);

    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_width(page, FORECAST_PAGE_W);
    lv_obj_set_height(page, FORECAST_VIEW_H);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SNAPABLE);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);

    if (today_page) {
        style_forecast_day_scroll(page, FORECAST_PAGE_W, FORECAST_VIEW_H);
        lv_obj_add_event_cb(page, forecast_pull_refresh_event, LV_EVENT_SCROLL_END, NULL);

        lv_obj_t *content =
            create_forecast_scroll_content(page, FORECAST_PAGE_W, FORECAST_VIEW_H);
        create_forecast_summary(content, index, p);

        forecast_page_swipe_hint = lv_label_create(forecast_summary[index]);
        lv_label_set_text(forecast_page_swipe_hint, "Swipe up for hourly");
        style_text_on_panel(forecast_page_swipe_hint, p, &lv_font_montserrat_14, p->text_muted,
                            LV_TEXT_ALIGN_CENTER);
        lv_obj_set_style_pad_top(forecast_page_swipe_hint, 8, LV_PART_MAIN);

        create_hourly_section(content, FORECAST_PAGE_W, p, &today_hourly_list);
    } else {
        lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
        create_forecast_summary(page, index, p);
    }

    return page;
}

void weather_screen_init(void)
{
    if (screen_ready) {
        return;
    }

    weather_background_init();

    const weather_palette_t *p = weather_palette();

    screen = lv_obj_create(NULL);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(p->bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);

    img_background = lv_img_create(screen);
    lv_obj_add_flag(img_background, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(img_background);

    nav_create_close_btn(screen);

    label_title = lv_label_create(screen);
    lv_label_set_text(label_title, "Hong Kong");
    style_text_on_panel(label_title, p, &lv_font_montserrat_30, p->text_primary, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 12);

    label_hint = lv_label_create(screen);
    lv_label_set_text(label_hint, "Tap " LV_SYMBOL_CLOSE " for home");
    style_text_on_panel(label_hint, p, &lv_font_montserrat_14, p->text_muted, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(label_hint, LV_ALIGN_TOP_MID, 0, 46);

    current_panel = lv_obj_create(screen);
    lv_obj_set_size(current_panel, WEATHER_PANEL_W, WEATHER_PANEL_H);
    lv_obj_set_pos(current_panel, WEATHER_PANEL_L_X, WEATHER_PANEL_Y);
    lv_obj_clear_flag(current_panel, LV_OBJ_FLAG_SCROLLABLE);
    style_weather_card(current_panel, p->card);

    label_current_heading = lv_label_create(current_panel);
    lv_label_set_text(label_current_heading, "Now");
    style_text_on_panel(label_current_heading, p, &lv_font_montserrat_20, p->text_muted, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(label_current_heading, LV_ALIGN_TOP_MID, 0, 0);

    img_current_icon = lv_img_create(current_panel);
    lv_img_set_src(img_current_icon, &weather_img_unknown);
    lv_img_set_zoom(img_current_icon, FORECAST_ICON_ZOOM);
    lv_obj_align(img_current_icon, LV_ALIGN_TOP_MID, 0, 36);

    label_current_temp = lv_label_create(current_panel);
    lv_label_set_text(label_current_temp, "-°C");
    style_text_on_panel(label_current_temp, p, &lv_font_montserrat_48, p->text_primary, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(label_current_temp, LV_ALIGN_TOP_MID, 0, 148);

    label_current_desc = lv_label_create(current_panel);
    lv_label_set_text(label_current_desc, "Loading...");
    style_text_on_panel(label_current_desc, p, &lv_font_montserrat_20, p->text_muted, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(label_current_desc, WEATHER_PANEL_W - 32);
    lv_label_set_long_mode(label_current_desc, LV_LABEL_LONG_WRAP);
    lv_obj_align(label_current_desc, LV_ALIGN_TOP_MID, 0, 218);

    label_current_humidity = lv_label_create(current_panel);
    lv_label_set_text(label_current_humidity, "");
    style_text_on_panel(label_current_humidity, p, &lv_font_montserrat_14, p->text_muted, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(label_current_humidity, LV_ALIGN_TOP_MID, 0, 278);

    label_current_wind = lv_label_create(current_panel);
    lv_label_set_text(label_current_wind, "");
    style_text_on_panel(label_current_wind, p, &lv_font_montserrat_14, p->text_muted, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(label_current_wind, LV_ALIGN_TOP_MID, 0, 312);

    forecast_panel = lv_obj_create(screen);
    lv_obj_set_size(forecast_panel, WEATHER_PANEL_W, WEATHER_PANEL_H);
    lv_obj_set_pos(forecast_panel, WEATHER_PANEL_R_X, WEATHER_PANEL_Y);
    style_weather_card(forecast_panel, p->card);
    lv_obj_clear_flag(forecast_panel, LV_OBJ_FLAG_SCROLLABLE);

    label_forecast_header = lv_label_create(forecast_panel);
    lv_label_set_text(label_forecast_header, "Forecast");
    style_text_on_panel(label_forecast_header, p, &lv_font_montserrat_20, p->text_muted, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(label_forecast_header, LV_ALIGN_TOP_LEFT, 4, 0);

    label_forecast_page = lv_label_create(forecast_panel);
    lv_label_set_text(label_forecast_page, "-");
    style_text_on_panel(label_forecast_page, p, &lv_font_montserrat_14, p->text_muted, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(label_forecast_page, LV_ALIGN_TOP_RIGHT, -4, 2);

    label_forecast_swipe = lv_label_create(forecast_panel);
    lv_label_set_text(label_forecast_swipe, "Swipe down to refresh");
    style_text_on_panel(label_forecast_swipe, p, &lv_font_montserrat_14, p->text_muted, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(label_forecast_swipe, LV_ALIGN_TOP_MID, 0, 22);

    forecast_viewport = lv_obj_create(forecast_panel);
    lv_obj_set_size(forecast_viewport, WEATHER_PANEL_W - 8, FORECAST_VIEW_H);
    lv_obj_align(forecast_viewport, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(forecast_viewport, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_bg_opa(forecast_viewport, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(forecast_viewport, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(forecast_viewport, 0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(forecast_viewport, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(forecast_viewport, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(forecast_viewport, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(forecast_viewport, LV_FLEX_FLOW_ROW);
    lv_obj_add_event_cb(forecast_viewport, forecast_scroll_event, LV_EVENT_SCROLL_END, NULL);
    for (int i = 0; i < WEATHER_FORECAST_DAYS; ++i) {
        forecast_pages[i] = create_forecast_page(forecast_viewport, i);
    }

    lv_obj_add_event_cb(screen, weather_screen_unloaded_cb, LV_EVENT_ALL, NULL);
    screen_ready = true;
}

bool weather_screen_is_ready(void)
{
    return screen_ready;
}

void weather_screen_destroy(void)
{
    if (!screen_ready) {
        return;
    }
    screen_ready = false;
    weather_background_release_decoded();
    if (screen) {
        lv_obj_del(screen);
    }
    weather_clear_widget_ptrs();
}

lv_obj_t *weather_screen_get_screen(void)
{
    if (!screen_ready) {
        weather_screen_init();
    }
    return screen;
}

void weather_screen_show(void)
{
    if (!screen_ready) {
        weather_screen_init();
    }

    if (forecast_viewport) {
        lv_obj_scroll_to_x(forecast_viewport, 0, LV_ANIM_OFF);
    }
    if (current_panel) {
        lv_obj_scroll_to_y(current_panel, 0, LV_ANIM_OFF);
    }

    if (weather_api_has_cached_data()) {
        weather_api_apply_cached_to_ui();
    } else if (weather_api_fetch_in_progress()) {
        weather_screen_set_loading();
    } else {
        weather_screen_set_loading();
        lv_label_set_text(label_current_desc, "Swipe down on forecast to refresh");
    }
}

void weather_screen_set_loading(void)
{
    if (!screen_ready) {
        return;
    }
    apply_refresh_background();

    const weather_palette_t *card_p = &s_palettes[0];
    style_weather_card(current_panel, card_p->card);
    style_weather_card(forecast_panel, card_p->card);

    lv_img_set_src(img_current_icon, &weather_img_unknown);
    lv_label_set_text(label_current_temp, "...");
    lv_label_set_text(label_current_desc, "Updating...");
    lv_label_set_text(label_current_humidity, "");
    lv_label_set_text(label_current_wind, "");
    style_text_on_panel(label_current_temp, card_p, &lv_font_montserrat_48, card_p->text_primary,
                        LV_TEXT_ALIGN_CENTER);
    style_text_on_panel(label_current_desc, card_p, &lv_font_montserrat_20, card_p->text_muted,
                        LV_TEXT_ALIGN_CENTER);

    for (int i = 0; i < WEATHER_FORECAST_DAYS; ++i) {
        lv_label_set_text(forecast_page_day[i], "");
        lv_img_set_src(forecast_page_icon[i], &weather_img_unknown);
        lv_label_set_text(forecast_page_desc[i], "");
        lv_label_set_text(forecast_page_temp[i], "");
    }
    fill_hourly_list(today_hourly_list, NULL, 0, card_p, false);
    if (forecast_pages[0]) {
        lv_obj_scroll_to_y(forecast_pages[0], 0, LV_ANIM_OFF);
    }
}

void weather_screen_set_error(const char *message)
{
    if (!screen_ready) {
        return;
    }
    lv_label_set_text(label_current_desc, message ? message : "Error");
}

void weather_screen_set_current(int weather_code, float temp_c, int humidity_pct, float wind_kmh,
                                 bool is_day)
{
    if (!screen_ready) {
        return;
    }

    s_current_weather_code = weather_code;
    weather_background_apply(img_background, weather_code, is_day);
    apply_weather_appearance(is_day);

    char buf[24];
    snprintf(buf, sizeof(buf), "%.0f°C", temp_c);
    weather_icon_set_src_ex(img_current_icon, weather_code, is_day);
    lv_label_set_text(label_current_temp, buf);
    lv_label_set_text(label_current_desc, weather_code_to_label(weather_code));

    char humid[32];
    snprintf(humid, sizeof(humid), "Humidity %d%%", humidity_pct);
    lv_label_set_text(label_current_humidity, humid);

    char wind[48];
    snprintf(wind, sizeof(wind), "%s (%.0f km/h)", weather_wind_level_label(wind_kmh), wind_kmh);
    lv_label_set_text(label_current_wind, wind);
}

void weather_screen_set_today_hourly(const weather_hourly_slot_t *slots, int count)
{
    if (!screen_ready) {
        return;
    }
    const weather_palette_t *card_p =
        weather_has_photo_background() ? &s_palettes[0] : weather_palette();
    fill_hourly_list(today_hourly_list, slots, count, card_p, true);
    if (forecast_pages[0]) {
        lv_obj_t *content = lv_obj_get_child(forecast_pages[0], 0);
        if (content) {
            lv_obj_update_layout(content);
        }
        lv_obj_scroll_to_y(forecast_pages[0], 0, LV_ANIM_OFF);
    }
}

void weather_screen_set_forecast_count(int day_count)
{
    if (!screen_ready) {
        return;
    }
    if (day_count < 1) {
        day_count = 1;
    }
    if (day_count > WEATHER_FORECAST_DAYS) {
        day_count = WEATHER_FORECAST_DAYS;
    }
    forecast_page_count = day_count;
    char buf[24];
    snprintf(buf, sizeof(buf), "1 / %d", forecast_page_count);
    lv_label_set_text(label_forecast_page, buf);
}

void weather_screen_set_forecast_day(int index, const char *day_label, int weather_code,
                                     float temp_max_c, float temp_min_c, bool is_day)
{
    if (!screen_ready || index < 0 || index >= WEATHER_FORECAST_DAYS) {
        return;
    }

    char temp_buf[40];
    snprintf(temp_buf, sizeof(temp_buf), "H %.0f°  L %.0f°", temp_max_c, temp_min_c);

    lv_label_set_text(forecast_page_day[index], day_label ? day_label : "-");
    weather_icon_set_src_ex(forecast_page_icon[index], weather_code, is_day);
    lv_label_set_text(forecast_page_desc[index], weather_code_to_label(weather_code));
    lv_label_set_text(forecast_page_temp[index], temp_buf);

    if (index == 0 && forecast_pages[0]) {
        lv_obj_scroll_to_y(forecast_pages[0], 0, LV_ANIM_OFF);
    }
}

void weather_screen_apply_theme(void)
{
    if (screen_ready) {
        weather_background_apply(img_background, s_current_weather_code, s_day_appearance);
        apply_weather_appearance(s_day_appearance);
    }
}

void weather_screen_release_heavy_memory(void)
{
    if (!screen_ready || !img_background) {
        return;
    }
    lv_img_set_src(img_background, NULL);
    weather_background_release_decoded();
}

void weather_screen_restore_background_memory(void)
{
    if (!screen_ready || !img_background) {
        return;
    }
    weather_background_apply(img_background, s_current_weather_code, s_day_appearance);
    apply_weather_appearance(s_day_appearance);
}
