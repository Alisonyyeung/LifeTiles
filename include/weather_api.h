#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*weather_lvgl_lock_fn)(int timeout_ms);
typedef void (*weather_lvgl_unlock_fn)(void);

void weather_api_init(weather_lvgl_lock_fn lock, weather_lvgl_unlock_fn unlock);
void weather_api_start_worker(void);

/** User-initiated fetch only (e.g. pull down on weather screen). */
void weather_api_request_refresh(void);

bool weather_api_has_cached_data(void);

/** Apply last successful fetch to UI (caller must hold LVGL lock). */
void weather_api_apply_cached_to_ui(void);

#ifdef __cplusplus
}
#endif
