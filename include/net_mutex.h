#pragma once

#include <stdbool.h>
#include <freertos/FreeRTOS.h>

#ifdef __cplusplus
extern "C" {
#endif

void net_mutex_init(void);

/** Serialize HTTPS (quotes, weather) — avoids SSL heap/socket conflicts. */
bool net_mutex_lock(TickType_t timeout_ticks);
void net_mutex_unlock(void);

#ifdef __cplusplus
}
#endif
