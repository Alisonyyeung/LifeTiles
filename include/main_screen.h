#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void main_screen_show_wifi_connecting(const char *ssid);
void main_screen_create(lv_obj_t *parent);
void main_screen_show(void);
void main_screen_set_status(const char *text);
void main_screen_set_weather_icon(int weather_code);
void main_screen_set_weather_icon_ex(int weather_code, bool is_day);
void main_screen_set_quote_loading(void);
void main_screen_set_quote(const char *quote, const char *author);
void main_screen_update_greeting(void);
void main_screen_apply_theme(void);

#ifdef __cplusplus
}
#endif
