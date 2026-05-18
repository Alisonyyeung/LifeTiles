#include "net_mutex.h"

#include <Arduino.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#define NET_SSL_COOLDOWN_MS  500

static SemaphoreHandle_t s_mutex = nullptr;
static TickType_t s_last_unlock_tick = 0;

void net_mutex_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
}

bool net_mutex_lock(TickType_t timeout_ticks)
{
    if (!s_mutex) {
        return false;
    }

    if (s_last_unlock_tick != 0) {
        const TickType_t now = xTaskGetTickCount();
        const TickType_t cooldown = pdMS_TO_TICKS(NET_SSL_COOLDOWN_MS);
        if (now - s_last_unlock_tick < cooldown) {
            vTaskDelay(cooldown - (now - s_last_unlock_tick));
        }
    }

    return xSemaphoreTake(s_mutex, timeout_ticks) == pdTRUE;
}

void net_mutex_unlock(void)
{
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
        s_last_unlock_tick = xTaskGetTickCount();
    }
}
