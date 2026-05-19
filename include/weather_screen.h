#pragma once

#include <lvgl.h>
#include <stdbool.h>

#define WEATHER_FORECAST_DAYS 7
#define WEATHER_HOURLY_MAX      24

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char time_label[6];
    int weather_code;
    float temp_c;
    bool is_day;
} weather_hourly_slot_t;

void weather_screen_init(void);
void weather_screen_destroy(void);
bool weather_screen_is_ready(void);
void weather_screen_show(void);
void weather_screen_update_location_title(void);
lv_obj_t *weather_screen_get_screen(void);

void weather_screen_set_loading(void);
void weather_screen_set_error(const char *message);
void weather_screen_set_current(int weather_code, float temp_c, int humidity_pct, float wind_kmh,
                                 bool is_day);
/** Hourly rows for today (forecast page 0 only). */
void weather_screen_set_today_hourly(const weather_hourly_slot_t *slots, int count);
void weather_screen_set_forecast_day(int index, const char *day_label, int weather_code,
                                     float temp_max_c, float temp_min_c, bool is_day);
void weather_screen_set_forecast_count(int day_count);
void weather_screen_apply_theme(void);
/** Free weather JPEG/RGB buffer before TLS (see net_prepare_https). */
void weather_screen_release_heavy_memory(void);
/** Reload background after HTTPS if weather data was shown. */
void weather_screen_restore_background_memory(void);

#define WEATHER_FORECAST_LABEL_MAX 32

#ifdef __cplusplus
}
#endif
