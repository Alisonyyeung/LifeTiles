#include "image_storage.h"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ctype.h>
#include <string.h>

#include "lv_fs_littlefs.h"

#define PREFS_NS_IMG     "myscreen"
#define PREFS_KEY_IMGSEL "img_sel"

extern "C" void image_screen_close_file_if_displayed(const char *basename);

#define MAX_IMAGE_FILES 32
#define MAX_NAME_LEN    64

static char s_files[MAX_IMAGE_FILES][MAX_NAME_LEN];
static int s_file_count = 0;
static int s_current_index = 0;

static bool ends_with_ci(const char *name, const char *ext)
{
    const size_t nlen = strlen(name);
    const size_t elen = strlen(ext);
    if (nlen < elen) {
        return false;
    }
    const char *tail = name + nlen - elen;
    for (size_t i = 0; i < elen; ++i) {
        if (tolower((unsigned char)tail[i]) != tolower((unsigned char)ext[i])) {
            return false;
        }
    }
    return true;
}

static bool is_supported_media(const char *name)
{
    return ends_with_ci(name, ".gif") || ends_with_ci(name, ".seq") || ends_with_ci(name, ".bmp") ||
           ends_with_ci(name, ".png") || ends_with_ci(name, ".jpg") || ends_with_ci(name, ".jpeg") ||
           ends_with_ci(name, ".webp");
}

/** Skip foo.gif when foo_scaled.gif exists (prepare_images output). */
static bool superseded_by_scaled(const char *base_name)
{
    if (strstr(base_name, "_scaled.")) {
        return false;
    }
    const char *dot = strrchr(base_name, '.');
    if (!dot) {
        return false;
    }
    const int stem_len = (int)(dot - base_name);
    char scaled_path[80];
    snprintf(scaled_path, sizeof(scaled_path), IMAGE_FOLDER "/%.*s_scaled%s", stem_len, base_name, dot);
    return LittleFS.exists(scaled_path);
}

static void scan_images_folder(void)
{
    s_file_count = 0;

    if (!LittleFS.exists(IMAGE_FOLDER)) {
        Serial.println("images: /images folder not found (upload LittleFS data)");
        return;
    }

    File dir = LittleFS.open(IMAGE_FOLDER);
    if (!dir || !dir.isDirectory()) {
        Serial.println("images: cannot open /images");
        return;
    }

    File entry = dir.openNextFile();
    while (entry && s_file_count < MAX_IMAGE_FILES) {
        if (!entry.isDirectory()) {
            const char *name = entry.name();
            const char *base = strrchr(name, '/');
            base = base ? base + 1 : name;
            if (is_supported_media(base) && !superseded_by_scaled(base)) {
                snprintf(s_files[s_file_count], MAX_NAME_LEN, "%s", base);
                s_file_count++;
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    Serial.printf("images: found %d file(s) in " IMAGE_FOLDER "\n", s_file_count);
}

static int index_of_basename(const char *basename)
{
    if (!basename || basename[0] == '\0') {
        return -1;
    }
    for (int i = 0; i < s_file_count; ++i) {
        if (strcasecmp(s_files[i], basename) == 0) {
            return i;
        }
    }
    return -1;
}

static void prefs_save_selection(void)
{
    if (s_file_count <= 0 || s_current_index < 0 || s_current_index >= s_file_count) {
        return;
    }
    Preferences prefs;
    if (prefs.begin(PREFS_NS_IMG, false)) {
        prefs.putString(PREFS_KEY_IMGSEL, s_files[s_current_index]);
        prefs.end();
    }
}

static void prefs_load_selection(void)
{
    Preferences prefs;
    if (!prefs.begin(PREFS_NS_IMG, true)) {
        return;
    }
    const String saved = prefs.getString(PREFS_KEY_IMGSEL, "");
    prefs.end();
    if (saved.length() == 0) {
        return;
    }
    const int idx = index_of_basename(saved.c_str());
    if (idx >= 0) {
        s_current_index = idx;
    }
}

static void clamp_current_index(void)
{
    if (s_file_count <= 0) {
        s_current_index = 0;
        return;
    }
    if (s_current_index < 0 || s_current_index >= s_file_count) {
        s_current_index = 0;
    }
}

bool image_storage_init(void)
{
    if (!LittleFS.begin(true)) {
        Serial.println("images: LittleFS mount failed");
        return false;
    }

    if (!LittleFS.exists(IMAGE_FOLDER)) {
        LittleFS.mkdir(IMAGE_FOLDER);
    }

    lv_fs_littlefs_init();
    scan_images_folder();
    s_current_index = 0;
    prefs_load_selection();
    clamp_current_index();
    return true;
}

void image_storage_rescan(void)
{
    char prev[MAX_NAME_LEN];
    const bool had = image_storage_get_current_basename(prev, sizeof(prev));
    scan_images_folder();
    if (had) {
        const int idx = index_of_basename(prev);
        if (idx >= 0) {
            s_current_index = idx;
        }
    }
    clamp_current_index();
}

int image_storage_current_index(void)
{
    return s_current_index;
}

bool image_storage_basename_at(int index, char *out, size_t out_len)
{
    if (index < 0 || index >= s_file_count || !out || out_len == 0) {
        return false;
    }
    snprintf(out, out_len, "%s", s_files[index]);
    return true;
}

bool image_storage_get_current_basename(char *out, size_t out_len)
{
    return image_storage_basename_at(s_current_index, out, out_len);
}

static bool safe_name_for_storage(const char *name)
{
    if (!name || name[0] == '\0' || strlen(name) >= MAX_NAME_LEN) {
        return false;
    }
    if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\')) {
        return false;
    }
    return is_supported_media(name);
}

bool image_storage_set_current_index(int index)
{
    if (index < 0 || index >= s_file_count) {
        return false;
    }
    s_current_index = index;
    prefs_save_selection();
    return true;
}

bool image_storage_set_current_by_name(const char *basename)
{
    if (!basename || !safe_name_for_storage(basename)) {
        return false;
    }
    const int idx = index_of_basename(basename);
    if (idx < 0) {
        return false;
    }
    return image_storage_set_current_index(idx);
}

bool image_storage_delete_file(const char *basename)
{
    if (!basename || !safe_name_for_storage(basename)) {
        return false;
    }
    char path[80];
    snprintf(path, sizeof(path), IMAGE_FOLDER "/%s", basename);
    if (!LittleFS.exists(path)) {
        return false;
    }
    image_screen_close_file_if_displayed(basename);
    return LittleFS.remove(path);
}

bool image_storage_rename_file(const char *from_basename, const char *to_basename)
{
    if (!from_basename || !to_basename || !safe_name_for_storage(from_basename) ||
        !safe_name_for_storage(to_basename)) {
        return false;
    }
    if (strcasecmp(from_basename, to_basename) == 0) {
        return true;
    }
    char old_path[80];
    char new_path[80];
    snprintf(old_path, sizeof(old_path), IMAGE_FOLDER "/%s", from_basename);
    snprintf(new_path, sizeof(new_path), IMAGE_FOLDER "/%s", to_basename);
    if (!LittleFS.exists(old_path) || LittleFS.exists(new_path)) {
        return false;
    }
    image_screen_close_file_if_displayed(from_basename);
    if (!LittleFS.rename(old_path, new_path)) {
        return false;
    }
    const int idx = index_of_basename(from_basename);
    if (idx >= 0) {
        snprintf(s_files[idx], MAX_NAME_LEN, "%s", to_basename);
        if (s_current_index == idx) {
            prefs_save_selection();
        }
    }
    return true;
}

size_t image_storage_total_bytes(void)
{
    return LittleFS.totalBytes();
}

size_t image_storage_free_bytes(void)
{
    const size_t total = LittleFS.totalBytes();
    const size_t used = LittleFS.usedBytes();
    return (used < total) ? (total - used) : 0;
}

int image_storage_count(void)
{
    return s_file_count;
}

static bool build_lvgl_path(int index, char *out_path, size_t out_len)
{
    if (index < 0 || index >= s_file_count || !out_path || out_len == 0) {
        return false;
    }
    snprintf(out_path, out_len, IMAGE_LVGL_PATH_PREFIX "%s", s_files[index]);
    return true;
}

bool image_storage_path_at(int index, char *out_path, size_t out_len)
{
    return build_lvgl_path(index, out_path, out_len);
}

bool image_storage_next(char *out_path, size_t out_len)
{
    if (s_file_count == 0) {
        return false;
    }
    s_current_index = (s_current_index + 1) % s_file_count;
    prefs_save_selection();
    return build_lvgl_path(s_current_index, out_path, out_len);
}

void image_storage_reset_index(void)
{
    s_current_index = 0;
}
