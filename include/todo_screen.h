#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void todo_screen_create(lv_obj_t *parent);
void todo_screen_destroy(void);
bool todo_screen_is_ready(void);
void todo_screen_show(void);
lv_obj_t *todo_screen_get_root(void);
void todo_screen_refresh(void);
void todo_screen_apply_theme(void);
void todo_screen_request_refresh(void);
void todo_screen_dispatch_pending(void);

#ifdef __cplusplus
}
#endif
