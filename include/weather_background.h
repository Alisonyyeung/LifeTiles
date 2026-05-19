#pragma once

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void weather_background_init(void);
void weather_background_apply(lv_obj_t *img_obj, int weather_code, bool is_day);
/** Drop decoded RGB565 (PSRAM) before HTTPS; s_loaded kind is kept for a fast reload. */
void weather_background_release_decoded(void);

#ifdef __cplusplus
}
#endif
