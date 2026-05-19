#include "region_config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREFS_NS            "myscreen"
#define PREFS_KEY_REG_NAME  "reg_name"
#define PREFS_KEY_REG_LAT   "reg_lat"
#define PREFS_KEY_REG_LON   "reg_lon"
#define PREFS_KEY_REG_TZIANA "reg_tziana"
#define PREFS_KEY_REG_TZPOSIX "reg_tzposix"
#define PREFS_KEY_REG_PRESET "reg_preset"

typedef struct {
    const char *name;
    float lat;
    float lon;
    const char *tz_iana;
    const char *tz_posix;
    const char *abbrev;
} region_preset_def_t;

static const region_preset_def_t k_presets[] = {
    {"Hong Kong", 22.32f, 114.17f, "Asia/Hong_Kong", "HKT-8", "HKT"},
    {"Tokyo", 35.68f, 139.69f, "Asia/Tokyo", "JST-9", "JST"},
    {"Singapore", 1.35f, 103.82f, "Asia/Singapore", "SGT-8", "SGT"},
    {"London", 51.51f, -0.13f, "Europe/London", "GMT0BST,M3.5.0/1,M10.5.0", "GMT"},
    {"New York", 40.71f, -74.01f, "America/New_York", "EST5EDT,M3.2.0,M11.1.0", "ET"},
    {"Los Angeles", 34.05f, -118.24f, "America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0", "PT"},
    {"Sydney", -33.87f, 151.21f, "Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3", "AEST"},
};

static region_config_t s_cfg;
static bool s_loaded = false;

static void copy_field(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    strncpy(dst, src ? src : "", dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static void apply_preset_def(const region_preset_def_t *p)
{
    if (!p) {
        return;
    }
    copy_field(s_cfg.name, sizeof(s_cfg.name), p->name);
    s_cfg.latitude = p->lat;
    s_cfg.longitude = p->lon;
    copy_field(s_cfg.tz_iana, sizeof(s_cfg.tz_iana), p->tz_iana);
    copy_field(s_cfg.tz_posix, sizeof(s_cfg.tz_posix), p->tz_posix);
}

static void urlencode(const char *in, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!in) {
        return;
    }

    size_t o = 0;
    for (size_t i = 0; in[i] != '\0' && o + 1 < out_len; ++i) {
        const char c = in[i];
        if (c == '/' && o + 3 < out_len) {
            out[o++] = '%';
            out[o++] = '2';
            out[o++] = 'F';
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                   c == '-' || c == '_' || c == '.') {
            out[o++] = c;
        } else if (c == ' ' && o + 1 < out_len) {
            out[o++] = '+';
        }
    }
    out[o] = '\0';
}

void region_config_init(void)
{
    region_config_load();
    region_config_apply_timezone();
}

void region_config_load(void)
{
    apply_preset_def(&k_presets[0]);

    Preferences prefs;
    if (!prefs.begin(PREFS_NS, true)) {
        s_loaded = true;
        return;
    }

    const int preset = prefs.getInt(PREFS_KEY_REG_PRESET, 0);
    if (preset >= 0 && preset < (int)(sizeof(k_presets) / sizeof(k_presets[0]))) {
        apply_preset_def(&k_presets[preset]);
    }

    const String name = prefs.getString(PREFS_KEY_REG_NAME, s_cfg.name);
    copy_field(s_cfg.name, sizeof(s_cfg.name), name.c_str());
    s_cfg.latitude = prefs.getFloat(PREFS_KEY_REG_LAT, s_cfg.latitude);
    s_cfg.longitude = prefs.getFloat(PREFS_KEY_REG_LON, s_cfg.longitude);
    const String tziana = prefs.getString(PREFS_KEY_REG_TZIANA, s_cfg.tz_iana);
    copy_field(s_cfg.tz_iana, sizeof(s_cfg.tz_iana), tziana.c_str());
    const String tzposix = prefs.getString(PREFS_KEY_REG_TZPOSIX, s_cfg.tz_posix);
    copy_field(s_cfg.tz_posix, sizeof(s_cfg.tz_posix), tzposix.c_str());

    prefs.end();
    s_loaded = true;
}

const region_config_t *region_config_get(void)
{
    return &s_cfg;
}

int region_config_preset_count(void)
{
    return (int)(sizeof(k_presets) / sizeof(k_presets[0]));
}

bool region_config_apply_preset(int preset_index)
{
    if (preset_index < 0 || preset_index >= region_config_preset_count()) {
        return false;
    }
    apply_preset_def(&k_presets[preset_index]);
    return true;
}

int region_config_match_preset_index(const region_config_t *cfg)
{
    if (!cfg) {
        return REGION_PRESET_CUSTOM;
    }
    for (int i = 0; i < region_config_preset_count(); ++i) {
        const region_preset_def_t *p = &k_presets[i];
        if (strcmp(cfg->name, p->name) == 0 && cfg->latitude == p->lat && cfg->longitude == p->lon &&
            strcmp(cfg->tz_iana, p->tz_iana) == 0) {
            return i;
        }
    }
    return REGION_PRESET_CUSTOM;
}

bool region_config_save(const region_config_t *cfg)
{
    if (!cfg) {
        return false;
    }
    s_cfg = *cfg;
    s_loaded = true;

    Preferences prefs;
    if (!prefs.begin(PREFS_NS, false)) {
        return false;
    }

    const int preset = region_config_match_preset_index(cfg);
    prefs.putInt(PREFS_KEY_REG_PRESET, preset);
    prefs.putString(PREFS_KEY_REG_NAME, s_cfg.name);
    prefs.putFloat(PREFS_KEY_REG_LAT, s_cfg.latitude);
    prefs.putFloat(PREFS_KEY_REG_LON, s_cfg.longitude);
    prefs.putString(PREFS_KEY_REG_TZIANA, s_cfg.tz_iana);
    prefs.putString(PREFS_KEY_REG_TZPOSIX, s_cfg.tz_posix);
    prefs.end();
    return true;
}

void region_config_apply_timezone(void)
{
    if (s_cfg.tz_posix[0] == '\0') {
        return;
    }
    setenv("TZ", s_cfg.tz_posix, 1);
    tzset();
}

const char *region_config_clock_abbrev(void)
{
    const int idx = region_config_match_preset_index(&s_cfg);
    if (idx >= 0 && idx < region_config_preset_count()) {
        return k_presets[idx].abbrev;
    }
    if (s_cfg.tz_posix[0] != '\0') {
        return s_cfg.tz_posix;
    }
    return "TZ";
}

bool region_config_build_weather_url(char *buf, size_t buf_len)
{
    if (!buf || buf_len < 128) {
        return false;
    }

    char tz_enc[REGION_TZ_IANA_MAX * 3];
    urlencode(s_cfg.tz_iana, tz_enc, sizeof(tz_enc));

    const int n = snprintf(
        buf, buf_len,
        "https://api.open-meteo.com/v1/forecast?"
        "latitude=%.2f&longitude=%.2f"
        "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
        "&hourly=temperature_2m,weather_code,is_day"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min"
        "&timezone=%s&forecast_days=7",
        s_cfg.latitude, s_cfg.longitude, tz_enc);

    return n > 0 && (size_t)n < buf_len;
}
