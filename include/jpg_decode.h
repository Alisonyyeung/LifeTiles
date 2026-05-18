#pragma once

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool jpg_decode_file(const char *littlefs_path, lv_img_dsc_t *out_dsc, uint8_t **out_pixel_buf);
bool jpg_decode_memory(const uint8_t *jpeg, size_t jpeg_len, lv_img_dsc_t *out_dsc, uint8_t **out_pixel_buf);
void jpg_decode_release(uint8_t *pixel_buf);

#ifdef __cplusplus
}
#endif
