#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void wifi_settings_screen_init(void);
void wifi_settings_screen_show(void);
lv_obj_t *wifi_settings_screen_get_screen(void);
void wifi_settings_screen_apply_theme(void);

#ifdef __cplusplus
}
#endif
