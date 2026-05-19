#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void image_screen_create(lv_obj_t *parent);
void image_screen_destroy(void);
bool image_screen_is_ready(void);
void image_screen_show_next(void);
/** Show the file at image_storage_current_index() (e.g. after web selection). */
void image_screen_show_current(void);
/** Queue show_current for the main loop (safe from the web server; does not block). */
void image_screen_request_show_current(void);
/** Queue rescan + show_current for the main loop (safe from the web server). */
void image_screen_request_refresh(void);
/** Apply pending screen updates; call from loop() after server.handleClient(). */
void image_screen_dispatch_pending(void);
void image_screen_refresh(void);
void image_screen_set_playback(bool play);
/** Frees slideshow/seq viewer (~768KB PSRAM + decode buffers). Call under lvgl_port_lock. */
void image_screen_release_heavy_memory(void);
bool image_screen_is_gif_active(void);
/** Call before LittleFS.remove on a media basename so open viewers (e.g. .seq) close their File. */
void image_screen_close_file_if_displayed(const char *basename);
void image_screen_apply_theme(void);

#ifdef __cplusplus
}
#endif
