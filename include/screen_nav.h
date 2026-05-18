#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SCREEN_RESTORE_HOME = 0,
    SCREEN_RESTORE_IMAGE,
    SCREEN_RESTORE_WEATHER,
    SCREEN_RESTORE_COMMENT,
    SCREEN_RESTORE_TODO,
    SCREEN_RESTORE_SETTINGS,
} screen_restore_t;

void screen_nav_init(void);
void screen_nav_show_home(void);
void screen_nav_show_home_tile(lv_obj_t *tile);
void screen_nav_show_weather(void);
void screen_nav_show_settings(void);
void screen_nav_show_wifi_settings(void);
void screen_nav_show_comment(void);
void screen_nav_show_todo(void);
void screen_nav_show_image(void);
void screen_nav_show_menu(void);
/** Called when menu overlay closes (resume image playback, etc.). */
void screen_nav_on_menu_dismissed(void);
void screen_nav_apply_theme(void);
lv_obj_t *screen_nav_get_tileview(void);
lv_obj_t *screen_nav_get_tile_main(void);
lv_obj_t *screen_nav_get_tile_image(void);
lv_obj_t *screen_nav_get_tile_comment(void);
lv_obj_t *screen_nav_get_tile_todo(void);
bool screen_nav_is_tileview_active(void);

#ifdef __cplusplus
}
#endif
