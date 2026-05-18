#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
#include <WString.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TODO_MAX_TASKS     10
#define TODO_TEXT_MAX      128
#define TODO_ID_MAX        12

typedef struct {
    char id[TODO_ID_MAX];
    char text[TODO_TEXT_MAX + 1];
    bool done;
} todo_task_t;

typedef struct {
    todo_task_t tasks[TODO_MAX_TASKS];
    uint8_t count;
} todo_list_t;

void todo_storage_init(void);
bool todo_storage_load(todo_list_t *out);
/** Cached list (no file/JSON parse); use on UI thread. */
bool todo_storage_get_list(todo_list_t *out);
bool todo_storage_save(const todo_list_t *list);

#ifdef __cplusplus
bool todo_storage_import_json(const char *json, size_t len);
bool todo_storage_export_json(String &out);
#endif

bool todo_storage_toggle_id(const char *id, bool *out_done);
bool todo_storage_set_done_id(const char *id, bool done);

#ifdef __cplusplus
}
#endif
