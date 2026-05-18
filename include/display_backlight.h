#pragma once

#include <stdint.h>

class ESP_IOExpander;
class ESP_Panel;

#ifdef __cplusplus
extern "C" {
#endif

void display_backlight_init(ESP_Panel *panel, ESP_IOExpander *expander, uint8_t expander_bl_pin);
void display_backlight_load(void);
uint8_t display_backlight_get_percent(void);
void display_backlight_set_percent(uint8_t percent);
void display_backlight_save(void);

#ifdef __cplusplus
}
#endif
