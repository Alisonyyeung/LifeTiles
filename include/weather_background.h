#pragma once

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void weather_background_init(void);
void weather_background_apply(lv_obj_t *img_obj, int weather_code, bool is_day);

#ifdef __cplusplus
}
#endif
