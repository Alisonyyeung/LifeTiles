#include "weather_background.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>

#include "ESP_Panel_Conf.h"
#include "jpg_decode.h"
#include "weather_icons.h"
#include "webp_decode.h"

enum bg_kind {
    BG_NONE = -1,
    BG_DAY = 0,
    BG_NIGHT = 1,
    BG_RAIN = 2,
};

static const char *const DAY_PATHS[] = {
    "/weather_background/day.jpg",
    "/weather_background/day.jpeg",
    NULL,
};

static const char *const NIGHT_PATHS[] = {
    "/weather_background/night.jpeg",
    "/weather_background/night.jpg",
    NULL,
};

static const char *const RAIN_PATHS[] = {
    "/weather_background/rain.jpeg",
    "/weather_background/rain.jpg",
    "/weather_background/rain.webp",
    NULL,
};

static lv_img_dsc_t s_dsc;
static uint8_t *s_pixels = NULL;
static int s_loaded = BG_NONE;

static const char *const *paths_for_kind(int kind)
{
    switch (kind) {
    case BG_DAY:
        return DAY_PATHS;
    case BG_NIGHT:
        return NIGHT_PATHS;
    case BG_RAIN:
        return RAIN_PATHS;
    default:
        return NULL;
    }
}

static int pick_background_kind(int weather_code, bool is_day)
{
    if (weather_code_uses_rain_background(weather_code)) {
        return BG_RAIN;
    }
    return is_day ? BG_DAY : BG_NIGHT;
}

static void release_background(void)
{
    if (s_pixels) {
        jpg_decode_release(s_pixels);
    }
    s_pixels = NULL;
    s_loaded = BG_NONE;
    memset(&s_dsc, 0, sizeof(s_dsc));
}

static bool decode_background_file(const char *path)
{
    if (!path) {
        return false;
    }
    if (strstr(path, ".webp") != NULL) {
        return webp_decode_file(path, &s_dsc, &s_pixels);
    }
    return jpg_decode_file(path, &s_dsc, &s_pixels);
}

static const char *resolve_path(const char *const *candidates)
{
    if (!candidates) {
        return NULL;
    }
    for (int i = 0; candidates[i] != NULL; ++i) {
        if (LittleFS.exists(candidates[i])) {
            return candidates[i];
        }
    }
    return NULL;
}

static void log_background_folder(void)
{
    if (!LittleFS.exists("/weather_background")) {
        Serial.println("Weather BG: /weather_background missing — run: pio run -t uploadfs");
        return;
    }

    File dir = LittleFS.open("/weather_background");
    if (!dir || !dir.isDirectory()) {
        Serial.println("Weather BG: cannot list /weather_background");
        return;
    }

    Serial.println("Weather BG: files on LittleFS:");
    File entry;
    while ((entry = dir.openNextFile())) {
        Serial.printf("  %s (%u bytes)\n", entry.name(), (unsigned)entry.size());
        entry.close();
    }
    dir.close();
}

static void fit_image_to_screen(lv_obj_t *img_obj)
{
    const lv_coord_t sw = lv_disp_get_hor_res(NULL);
    const lv_coord_t sh = lv_disp_get_ver_res(NULL);
    const int iw = (int)s_dsc.header.w;
    const int ih = (int)s_dsc.header.h;

    if (iw <= 0 || ih <= 0 || sw <= 0 || sh <= 0) {
        return;
    }

    lv_img_set_src(img_obj, &s_dsc);

    uint32_t zoom_w = ((uint32_t)sw * 256u) / (uint32_t)iw;
    uint32_t zoom_h = ((uint32_t)sh * 256u) / (uint32_t)ih;
    uint32_t zoom = zoom_w > zoom_h ? zoom_w : zoom_h;
    if (zoom < 256u) {
        zoom = 256u;
    }

    lv_img_set_zoom(img_obj, (uint16_t)zoom);
    lv_obj_set_size(img_obj, (lv_coord_t)iw, (lv_coord_t)ih);
    lv_obj_align(img_obj, LV_ALIGN_CENTER, 0, 0);
}

static bool load_kind(int kind)
{
    if (kind == s_loaded) {
        return s_pixels != NULL;
    }

    release_background();

    const char *const *candidates = paths_for_kind(kind);
    const char *path = resolve_path(candidates);
    if (!path) {
        Serial.printf("Weather BG: no file for kind %d\n", kind);
        log_background_folder();
        return false;
    }

    if (!decode_background_file(path)) {
        Serial.printf("Weather BG: decode failed %s\n", path);
        return false;
    }

    s_loaded = kind;
    Serial.printf("Weather BG: loaded %s (%dx%d)\n", path, (int)s_dsc.header.w, (int)s_dsc.header.h);
    return true;
}

void weather_background_init(void)
{
    release_background();
}

void weather_background_apply(lv_obj_t *img_obj, int weather_code, bool is_day)
{
    if (!img_obj) {
        return;
    }

    const int kind = pick_background_kind(weather_code, is_day);
    if (!load_kind(kind)) {
        lv_obj_add_flag(img_obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(img_obj, LV_OBJ_FLAG_HIDDEN);
    fit_image_to_screen(img_obj);
}
