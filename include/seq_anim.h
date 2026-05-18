#pragma once

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create an animated image from a .seq file (see tools/gif_to_seq.py).
 * littlefs_path: e.g. "/images/wall.seq"
 * Returns lv_img* with user_data pointing to internal state; free via lv_obj_del (cleanup on DELETE).
 */
lv_obj_t *seq_anim_create(lv_obj_t *parent, const char *littlefs_path);

bool seq_anim_is_instance(const lv_obj_t *img);
void seq_anim_set_playing(lv_obj_t *img, bool play);

/** Logical content size for "Fit screen" (may differ from lv_img dsc when letterboxed). */
bool seq_anim_get_fit_size(const lv_obj_t *img, uint16_t *out_w, uint16_t *out_h);

/** Persist zoom across frame changes (LVGL zoom, 256 = 100%). */
void seq_anim_set_view_zoom(lv_obj_t *img, uint16_t zoom);

#ifdef __cplusplus
}
#endif
