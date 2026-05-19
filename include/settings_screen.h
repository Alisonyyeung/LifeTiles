#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void settings_screen_init(void);
void settings_screen_destroy(void);
bool settings_screen_is_ready(void);
void settings_screen_show(void);
lv_obj_t *settings_screen_get_screen(void);
void settings_screen_apply_theme(void);
void settings_screen_refresh_network(void);

#ifdef __cplusplus
}
#endif
