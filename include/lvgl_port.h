#pragma once

#include <ESP_Panel_Library.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Must be called once before lvgl_port_start_task(). */
void lvgl_port_bind_panel(void *panel);

void lvgl_port_start_task(void);

void lvgl_port_lock(int timeout_ms);
void lvgl_port_unlock(void);

void lvgl_port_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);

#if ESP_PANEL_USE_LCD_TOUCH
void lvgl_port_tp_read(lv_indev_drv_t *indev, lv_indev_data_t *data);
#endif

#if ESP_PANEL_LCD_BUS_TYPE != ESP_PANEL_BUS_TYPE_RGB
bool lvgl_port_notify_flush_ready(void *user_ctx);
#endif

#ifdef __cplusplus
}
#endif
