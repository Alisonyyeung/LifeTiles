#include "quote_likes_storage.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#define LIKES_PATH     "/liked_quotes.json"
#define LIKES_JSON_CAP 8192
#define LIKES_FILE_MAX 8192

static SemaphoreHandle_t s_mutex = nullptr;
static StaticJsonDocument<LIKES_JSON_CAP> s_json_doc;
static char s_file_buf[LIKES_FILE_MAX];

static void lock(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_mutex);
}

static void copy_field(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static bool text_ok(const char *s)
{
    return s && s[0] != '\0' && strlen(s) < LIKED_QUOTE_TEXT_MAX;
}

static int find_index(const liked_quotes_list_t *list, const char *text, const char *author)
{
    if (!list || !text) {
        return -1;
    }
    for (uint8_t i = 0; i < list->count; ++i) {
        if (strcmp(list->items[i].text, text) != 0) {
            continue;
        }
        const char *a = author ? author : "";
        const char *b = list->items[i].author;
        if (strcmp(a, b) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static bool read_file_locked(liked_quotes_list_t *out)
{
    memset(out, 0, sizeof(*out));

    if (!LittleFS.exists(LIKES_PATH)) {
        return true;
    }

    File f = LittleFS.open(LIKES_PATH, "r");
    if (!f) {
        return false;
    }

    const size_t n = f.read((uint8_t *)s_file_buf, sizeof(s_file_buf) - 1);
    f.close();
    if (n == 0) {
        return true;
    }
    s_file_buf[n] = '\0';

    s_json_doc.clear();
    const DeserializationError err = deserializeJson(s_json_doc, s_file_buf);
    if (err) {
        return false;
    }

    JsonArray arr = s_json_doc["quotes"].as<JsonArray>();
    if (arr.isNull()) {
        return true;
    }

    uint8_t c = 0;
    for (JsonObject o : arr) {
        if (c >= LIKED_QUOTES_MAX) {
            break;
        }
        const char *text = o["text"] | "";
        const char *author = o["author"] | "";
        if (!text_ok(text)) {
            continue;
        }
        copy_field(out->items[c].text, sizeof(out->items[c].text), text);
        copy_field(out->items[c].author, sizeof(out->items[c].author), author);
        ++c;
    }
    out->count = c;
    return true;
}

static bool write_file_locked(const liked_quotes_list_t *list)
{
    s_json_doc.clear();
    JsonArray arr = s_json_doc.createNestedArray("quotes");
    if (!list) {
        return false;
    }
    for (uint8_t i = 0; i < list->count; ++i) {
        JsonObject o = arr.createNestedObject();
        o["text"] = list->items[i].text;
        o["author"] = list->items[i].author;
    }

    const size_t len = serializeJson(s_json_doc, s_file_buf, sizeof(s_file_buf));
    if (len == 0 || len >= sizeof(s_file_buf)) {
        return false;
    }

    File f = LittleFS.open(LIKES_PATH, "w");
    if (!f) {
        return false;
    }
    const size_t written = f.write((const uint8_t *)s_file_buf, len);
    f.close();
    return written == len;
}

void quote_likes_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
}

bool quote_likes_load(liked_quotes_list_t *out)
{
    if (!out) {
        return false;
    }
    lock();
    const bool ok = read_file_locked(out);
    unlock();
    return ok;
}

bool quote_likes_contains(const char *text, const char *author)
{
    if (!text_ok(text)) {
        return false;
    }
    liked_quotes_list_t list;
    lock();
    const bool ok = read_file_locked(&list);
    const int idx = ok ? find_index(&list, text, author) : -1;
    unlock();
    return idx >= 0;
}

bool quote_likes_toggle(const char *text, const char *author, bool *out_liked)
{
    if (out_liked) {
        *out_liked = false;
    }
    if (!text_ok(text)) {
        return false;
    }

    lock();
    liked_quotes_list_t list;
    if (!read_file_locked(&list)) {
        unlock();
        return false;
    }

    const int idx = find_index(&list, text, author);
    if (idx >= 0) {
        for (uint8_t i = (uint8_t)idx; i + 1 < list.count; ++i) {
            list.items[i] = list.items[i + 1];
        }
        if (list.count > 0) {
            --list.count;
        }
        if (out_liked) {
            *out_liked = false;
        }
    } else {
        if (list.count >= LIKED_QUOTES_MAX) {
            unlock();
            return false;
        }
        copy_field(list.items[list.count].text, sizeof(list.items[list.count].text), text);
        copy_field(list.items[list.count].author, sizeof(list.items[list.count].author), author);
        ++list.count;
        if (out_liked) {
            *out_liked = true;
        }
    }

    const bool ok = write_file_locked(&list);
    unlock();
    return ok;
}

bool quote_likes_remove_at(uint8_t index)
{
    lock();
    liked_quotes_list_t list;
    if (!read_file_locked(&list) || index >= list.count) {
        unlock();
        return false;
    }
    for (uint8_t i = index; i + 1 < list.count; ++i) {
        list.items[i] = list.items[i + 1];
    }
    --list.count;
    const bool ok = write_file_locked(&list);
    unlock();
    return ok;
}
