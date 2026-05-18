#pragma once

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Full WMO WW description (Open-Meteo). */
const char *weather_code_to_label(int code);

/** Beaufort-style label for 10 m wind speed in km/h (e.g. "Windy"). */
const char *weather_wind_level_label(float wind_kmh);

/** Rain/drizzle/thunder WMO codes — use rain background image. */
bool weather_code_uses_rain_background(int code);

/** Icon for WMO code; defaults to day variant. */
const lv_img_dsc_t *weather_code_to_image(int code);

/** Icon with day/night variant (Google Weather Icons v4). */
const lv_img_dsc_t *weather_code_to_image_ex(int code, bool is_day);

void weather_icon_set_src(lv_obj_t *img_obj, int weather_code);
void weather_icon_set_src_ex(lv_obj_t *img_obj, int weather_code, bool is_day);

#ifdef __cplusplus
}
#endif
