#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void menu_screen_init(void);
void menu_screen_show(void);
void menu_screen_dismiss(void);
bool menu_screen_is_active(void);
void menu_screen_apply_theme(void);

#ifdef __cplusplus
}
#endif
