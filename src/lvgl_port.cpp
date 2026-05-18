#include "lvgl_port.h"

#include <Arduino.h>
#include <ESP_Panel_Library.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "app_tasks.h"

#define LVGL_TASK_MAX_DELAY_MS  500
#define LVGL_TASK_MIN_DELAY_MS  1

static ESP_Panel *s_panel = nullptr;
static SemaphoreHandle_t s_mux = nullptr;

void lvgl_port_bind_panel(void *panel)
{
    s_panel = static_cast<ESP_Panel *>(panel);
}

void lvgl_port_lock(int timeout_ms)
{
    const TickType_t timeout_ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    xSemaphoreTakeRecursive(s_mux, timeout_ticks);
}

void lvgl_port_unlock(void)
{
    xSemaphoreGiveRecursive(s_mux);
}

#if ESP_PANEL_LCD_BUS_TYPE == ESP_PANEL_BUS_TYPE_RGB
void lvgl_port_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    s_panel->getLcd()->drawBitmap(area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
    lv_disp_flush_ready(disp);
}
#else
void lvgl_port_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    s_panel->getLcd()->drawBitmap(area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
}

bool lvgl_port_notify_flush_ready(void *user_ctx)
{
    lv_disp_drv_t *disp_driver = static_cast<lv_disp_drv_t *>(user_ctx);
    lv_disp_flush_ready(disp_driver);
    return false;
}
#endif

#if ESP_PANEL_USE_LCD_TOUCH
void lvgl_port_tp_read(lv_indev_drv_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    s_panel->getLcdTouch()->readData();

    const bool touched = s_panel->getLcdTouch()->getTouchState();
    if (!touched) {
        data->state = LV_INDEV_STATE_REL;
    } else {
        const TouchPoint point = s_panel->getLcdTouch()->getPoint();
        data->state = LV_INDEV_STATE_PR;
        data->point.x = point.x;
        data->point.y = point.y;
    }
}
#endif

static void lvgl_task(void *arg)
{
    (void)arg;
    Serial.println("Starting LVGL task (core 1)");

    uint32_t task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
    for (;;) {
        lvgl_port_lock(-1);
        task_delay_ms = lv_timer_handler();
        lvgl_port_unlock();
        if (task_delay_ms > LVGL_TASK_MAX_DELAY_MS) {
            task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < LVGL_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

void lvgl_port_start_task(void)
{
    if (!s_mux) {
        s_mux = xSemaphoreCreateRecursiveMutex();
    }
    xTaskCreatePinnedToCore(
        lvgl_task,
        "lvgl",
        APP_STACK_LVGL,
        nullptr,
        APP_PRIO_LVGL,
        nullptr,
        APP_CORE_UI);
}
