#include <Arduino.h>

#include <freertos/task.h>
#include <lvgl.h>
#include <ESP_Panel_Library.h>
#include <ESP_IOExpander_Library.h>
#include <WiFi.h>
#include <time.h>

#include "app_theme.h"
#include "display_backlight.h"
#include "image_storage.h"
#include "image_upload_server.h"
#include "lvgl_port.h"
#include "net_mutex.h"
#include "main_screen.h"
#include "quote_likes_storage.h"
#include "quotes_api.h"
#include "screen_nav.h"
#include "weather_api.h"
#include "wifi_manager.h"
#include "wifi_services.h"
#include "user_profile.h"
#include "wifi_storage.h"

#define TP_RST 1
#define LCD_BL 2
#define LCD_RST 3
#define SD_CS 4
#define USB_SEL 5

#define I2C_MASTER_NUM 0
#define I2C_MASTER_SDA_IO 8
#define I2C_MASTER_SCL_IO 9

#define LVGL_BUF_SIZE (ESP_PANEL_LCD_H_RES * 20)

static ESP_Panel *panel = nullptr;
static ESP_IOExpander *s_io_expander = nullptr;

void setup()
{
    Serial.begin(115200);

    String lvgl_ver = "Hello LVGL! V";
    lvgl_ver += lv_version_major();
    lvgl_ver += ".";
    lvgl_ver += lv_version_minor();
    lvgl_ver += ".";
    lvgl_ver += lv_version_patch();
    Serial.println(lvgl_ver);
    Serial.println("I am ESP32_Display_Panel");

    panel = new ESP_Panel();
    lvgl_port_bind_panel(panel);

    lv_init();

    static lv_disp_draw_buf_t draw_buf;
    uint8_t *buf = static_cast<uint8_t *>(
        heap_caps_calloc(1, LVGL_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_INTERNAL));
    assert(buf);
    lv_disp_draw_buf_init(&draw_buf, buf, nullptr, LVGL_BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = ESP_PANEL_LCD_H_RES;
    disp_drv.ver_res = ESP_PANEL_LCD_V_RES;
    disp_drv.flush_cb = lvgl_port_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

#if ESP_PANEL_USE_LCD_TOUCH
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_port_tp_read;
    lv_indev_drv_register(&indev_drv);
#endif

    panel->init();
#if ESP_PANEL_LCD_BUS_TYPE != ESP_PANEL_BUS_TYPE_RGB
    panel->getLcd()->setCallback(lvgl_port_notify_flush_ready, &disp_drv);
#endif

    Serial.println("Initialize IO expander");
    ESP_IOExpander *expander = new ESP_IOExpander_CH422G(I2C_MASTER_NUM, ESP_IO_EXPANDER_I2C_CH422G_ADDRESS_000);
    expander->init();
    expander->begin();
    expander->multiPinMode(TP_RST | LCD_BL | LCD_RST | SD_CS | USB_SEL, OUTPUT);
    expander->multiDigitalWrite(TP_RST | LCD_BL | LCD_RST | SD_CS, HIGH);
    expander->digitalWrite(USB_SEL, LOW);
    s_io_expander = expander;
    panel->addIOExpander(expander);
    panel->begin();

    display_backlight_init(panel, s_io_expander, LCD_BL);
    display_backlight_load();
    app_theme_load();
    user_profile_load();
    wifi_storage_init();
    net_mutex_init();

    lvgl_port_start_task();
    image_upload_server_init();

    lvgl_port_lock(-1);
    char boot_ssid[WIFI_STORAGE_SSID_MAX];
    if (wifi_storage_get_ssid(boot_ssid, sizeof(boot_ssid))) {
        main_screen_show_wifi_connecting(boot_ssid);
    } else {
        main_screen_show_wifi_connecting("Set Wi-Fi in Settings");
    }
    lvgl_port_unlock();

    const bool wifi_connected = wifi_storage_is_configured() && wifi_manager_connect_blocking();

    lvgl_port_lock(-1);

    quotes_api_init(lvgl_port_lock, lvgl_port_unlock);
    quotes_api_start_worker();
    weather_api_init(lvgl_port_lock, lvgl_port_unlock);
    weather_api_start_worker();
    wifi_services_init();

    image_storage_init();
    quote_likes_init();
    screen_nav_init();
    app_theme_apply_all();

    if (!wifi_connected) {
        if (wifi_storage_is_configured()) {
            main_screen_set_status("No Wi-Fi");
        } else {
            main_screen_set_status("Set Wi-Fi in Settings");
        }
    }

    lvgl_port_unlock();

    if (wifi_connected) {
        lvgl_port_lock(-1);
        wifi_services_on_connected();
        lvgl_port_unlock();
    }

    Serial.println("Setup done");
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(100));
}
