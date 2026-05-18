#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_CONNECT_TIMEOUT_MS 20000

/** Block until connected or timeout (boot). Uses wifi_storage credentials. */
bool wifi_manager_connect_blocking(void);

/**
 * Reconnect in a background task (UI). Calls `on_done` on the LVGL thread with result.
 * on_done may be NULL.
 */
void wifi_manager_connect_async(void (*on_done)(bool connected, void *user_data), void *user_data);

bool wifi_manager_is_connected(void);

#ifdef __cplusplus
}
#endif
