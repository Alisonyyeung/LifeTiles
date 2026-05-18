#include "todo_storage.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#define TODO_PATH       "/todos.json"
#define TODO_JSON_CAP   1536
#define TODO_FILE_MAX   3072

static todo_list_t s_cache;
static bool s_cache_valid = false;
static StaticJsonDocument<TODO_JSON_CAP> s_json_doc;
static char s_file_buf[TODO_FILE_MAX];
static SemaphoreHandle_t s_mutex = nullptr;

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

static bool text_valid(const char *s)
{
    if (!s || s[0] == '\0') {
        return false;
    }
    for (const char *p = s; *p; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c > 0x7E) {
            return false;
        }
    }
    return strlen(s) <= TODO_TEXT_MAX;
}

static bool id_valid(const char *id)
{
    if (!id || id[0] == '\0') {
        return false;
    }
    for (const char *p = id; *p; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (!isalnum(c) && c != '_' && c != '-') {
            return false;
        }
    }
    return strlen(id) < TODO_ID_MAX;
}

static bool parse_doc_to_list(const JsonArray &arr, todo_list_t *out)
{
    memset(out, 0, sizeof(*out));
    if (arr.isNull()) {
        return true;
    }
    uint8_t n = 0;
    for (JsonObject o : arr) {
        if (n >= TODO_MAX_TASKS) {
            break;
        }
        const char *id = o["id"] | "";
        const char *text = o["text"] | "";
        if (!id_valid(id) || !text_valid(text)) {
            continue;
        }
        strncpy(out->tasks[n].id, id, sizeof(out->tasks[n].id) - 1);
        strncpy(out->tasks[n].text, text, sizeof(out->tasks[n].text));
        out->tasks[n].done = o["done"] | false;
        ++n;
    }
    out->count = n;
    return true;
}

static bool list_to_doc(const todo_list_t *list)
{
    s_json_doc.clear();
    JsonArray arr = s_json_doc.createNestedArray("tasks");
    for (uint8_t i = 0; i < list->count; ++i) {
        JsonObject o = arr.createNestedObject();
        o["id"] = list->tasks[i].id;
        o["text"] = list->tasks[i].text;
        o["done"] = list->tasks[i].done;
    }
    return true;
}

/** Caller must hold s_mutex. Never use portENTER_CRITICAL around this. */
static bool read_list_from_disk(todo_list_t *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    if (!LittleFS.exists(TODO_PATH)) {
        return true;
    }

    File f = LittleFS.open(TODO_PATH, "r");
    if (!f) {
        return false;
    }

    const size_t n = f.readBytes(s_file_buf, sizeof(s_file_buf) - 1);
    f.close();
    s_file_buf[n] = '\0';

    s_json_doc.clear();
    const DeserializationError err = deserializeJson(s_json_doc, s_file_buf);
    if (err) {
        return false;
    }

    return parse_doc_to_list(s_json_doc["tasks"].as<JsonArray>(), out);
}

/** Caller must hold s_mutex. */
static bool write_list_to_disk(const todo_list_t *list)
{
    if (!list || list->count > TODO_MAX_TASKS) {
        return false;
    }
    for (uint8_t i = 0; i < list->count; ++i) {
        if (!id_valid(list->tasks[i].id) || !text_valid(list->tasks[i].text)) {
            return false;
        }
    }

    list_to_doc(list);

    File f = LittleFS.open(TODO_PATH, "w");
    if (!f) {
        return false;
    }
    const size_t n = serializeJson(s_json_doc, f);
    f.close();
    if (n == 0 && list->count > 0) {
        return false;
    }
    s_cache = *list;
    s_cache_valid = true;
    return true;
}

static void ensure_cache_loaded(void)
{
    if (s_cache_valid) {
        return;
    }
    todo_list_t tmp;
    if (read_list_from_disk(&tmp)) {
        s_cache = tmp;
    } else {
        memset(&s_cache, 0, sizeof(s_cache));
    }
    s_cache_valid = true;
}

void todo_storage_init(void)
{
    lock();
    ensure_cache_loaded();
    unlock();
}

bool todo_storage_load(todo_list_t *out)
{
    if (!out) {
        return false;
    }

    lock();
    const bool ok = read_list_from_disk(out);
    if (ok) {
        s_cache = *out;
        s_cache_valid = true;
    }
    unlock();
    return ok;
}

bool todo_storage_get_list(todo_list_t *out)
{
    if (!out) {
        return false;
    }

    lock();
    ensure_cache_loaded();
    *out = s_cache;
    unlock();
    return true;
}

bool todo_storage_save(const todo_list_t *list)
{
    lock();
    const bool ok = write_list_to_disk(list);
    unlock();
    return ok;
}

bool todo_storage_import_json(const char *json, size_t len)
{
    if (!json || len == 0 || len >= TODO_FILE_MAX) {
        return false;
    }

    lock();
    memcpy(s_file_buf, json, len);
    s_file_buf[len] = '\0';

    s_json_doc.clear();
    const DeserializationError err = deserializeJson(s_json_doc, s_file_buf);
    if (err) {
        unlock();
        return false;
    }

    const JsonArray arr = s_json_doc["tasks"].as<JsonArray>();
    todo_list_t list;
    const bool parsed = parse_doc_to_list(arr, &list);
    if (!parsed) {
        unlock();
        return false;
    }
    if (!arr.isNull() && arr.size() > 0 && list.count == 0) {
        unlock();
        return false;
    }

    const bool ok = write_list_to_disk(&list);
    unlock();
    return ok;
}

bool todo_storage_export_json(String &out)
{
    lock();
    ensure_cache_loaded();
    list_to_doc(&s_cache);
    serializeJson(s_json_doc, out);
    unlock();
    return true;
}

static int find_index_locked(const char *id)
{
    for (uint8_t i = 0; i < s_cache.count; ++i) {
        if (strcmp(s_cache.tasks[i].id, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

bool todo_storage_toggle_id(const char *id, bool *out_done)
{
    lock();
    ensure_cache_loaded();
    const int idx = find_index_locked(id);
    if (idx < 0) {
        unlock();
        return false;
    }
    s_cache.tasks[idx].done = !s_cache.tasks[idx].done;
    if (out_done) {
        *out_done = s_cache.tasks[idx].done;
    }
    const bool ok = write_list_to_disk(&s_cache);
    unlock();
    return ok;
}

bool todo_storage_set_done_id(const char *id, bool done)
{
    lock();
    ensure_cache_loaded();
    const int idx = find_index_locked(id);
    if (idx < 0) {
        unlock();
        return false;
    }
    s_cache.tasks[idx].done = done;
    const bool ok = write_list_to_disk(&s_cache);
    unlock();
    return ok;
}
