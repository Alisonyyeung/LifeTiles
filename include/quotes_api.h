#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    QUOTE_FETCH_RANDOM = 0,
    QUOTE_FETCH_TODAY,
} quote_fetch_kind_t;

typedef void (*quotes_lvgl_lock_fn)(int timeout_ms);
typedef void (*quotes_lvgl_unlock_fn)(void);

typedef void (*quotes_fetch_done_fn)(quote_fetch_kind_t kind, bool success);

void quotes_api_init(quotes_lvgl_lock_fn lock, quotes_lvgl_unlock_fn unlock);
void quotes_api_start_worker(void);

/** Optional: called after each fetch finishes (including cache-only today). */
void quotes_api_set_done_handler(quotes_fetch_done_fn handler);

void quotes_api_request(quote_fetch_kind_t kind);

#ifdef __cplusplus
}
#endif
