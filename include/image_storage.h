#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IMAGE_FS_LETTER          'S'
#define IMAGE_FOLDER             "/images"
#define IMAGE_LVGL_PATH_PREFIX   "S:" IMAGE_FOLDER "/"

bool image_storage_init(void);
int image_storage_count(void);
int image_storage_current_index(void);
bool image_storage_basename_at(int index, char *out, size_t out_len);
bool image_storage_get_current_basename(char *out, size_t out_len);
bool image_storage_set_current_index(int index);
bool image_storage_set_current_by_name(const char *basename);
bool image_storage_next(char *out_path, size_t out_len);
bool image_storage_path_at(int index, char *out_path, size_t out_len);
void image_storage_reset_index(void);
void image_storage_rescan(void);
bool image_storage_delete_file(const char *basename);
bool image_storage_rename_file(const char *from_basename, const char *to_basename);
size_t image_storage_total_bytes(void);
size_t image_storage_free_bytes(void);

#ifdef __cplusplus
}
#endif
