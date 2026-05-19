#include "wifi_services.h"

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "image_upload_server.h"
#include "lvgl_port.h"
#include "main_screen.h"
#include "quotes_api.h"
#include "region_config.h"
#include "weather_api.h"
#include "wifi_storage.h"

#define NTP_SERVER_1   "pool.ntp.org"
#define NTP_SERVER_2   "time.google.com"

static bool sync_region_time(void)
{
    configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);
    region_config_apply_timezone();

    struct tm timeinfo;
    for (int i = 0; i < 12; ++i) {
        if (getLocalTime(&timeinfo, 1000)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return false;
}

static void connected_services_task(void *arg)
{
    (void)arg;

    wifi_storage_history_record_connected();

    const bool time_ok = sync_region_time();

    lvgl_port_lock(-1);
    main_screen_set_status(time_ok ? region_config_clock_abbrev() : "NTP pending");
    lvgl_port_unlock();

    image_upload_server_restart();
    quotes_api_request(QUOTE_FETCH_TODAY);
    weather_api_request_refresh();

    vTaskDelete(NULL);
}

void wifi_services_init(void)
{
}

void wifi_services_on_connected(void)
{
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    lvgl_port_lock(-1);
    main_screen_set_status("Syncing time...");
    lvgl_port_unlock();

    xTaskCreate(connected_services_task, "wifi_svc", 8192, nullptr, 1, nullptr);
}
