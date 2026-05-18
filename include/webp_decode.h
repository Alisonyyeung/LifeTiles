#pragma once

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Decode a WebP file from LittleFS (path like /images/foo.webp). */
bool webp_decode_file(const char *littlefs_path, lv_img_dsc_t *out_dsc, uint8_t **out_pixel_buf);

void webp_decode_release(uint8_t *pixel_buf);

#ifdef __cplusplus
}
#endif
