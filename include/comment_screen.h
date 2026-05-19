#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void comment_screen_create(lv_obj_t *parent);
void comment_screen_destroy(void);
bool comment_screen_is_ready(void);
void comment_screen_show(void);
void comment_screen_set_message(const char *utf8);
void comment_screen_refresh_message(void);
/** Queue show + message update (safe from the web server). */
void comment_screen_request_show(const char *utf8);
void comment_screen_dispatch_pending(void);
lv_obj_t *comment_screen_get_root(void);
void comment_screen_apply_theme(void);

#ifdef __cplusplus
}
#endif
