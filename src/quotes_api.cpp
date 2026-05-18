#include "quotes_api.h"

#include <Arduino.h>

#include "app_tasks.h"
#include "net_https.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

#include "main_screen.h"

#define QUOTE_API_BASE         "https://api.quotable.io"
#define QUOTE_API_RANDOM_URL   QUOTE_API_BASE "/random"
/* First /random of each HKT calendar day; persisted in NVS until the next day. */
#define QUOTE_API_TODAY_URL    QUOTE_API_RANDOM_URL

#define PREFS_NS             "myscreen"
#define PREFS_KEY_QUOTE_YEAR "qt_year"
#define PREFS_KEY_QUOTE_YDAY "qt_yday"
#define PREFS_KEY_QUOTE_TEXT "qt_text"
#define PREFS_KEY_QUOTE_AUTH "qt_auth"
#define QUOTE_HTTP_TIMEOUT_MS  20000
#define QUOTE_JSON_CAPACITY    4096
#define QUOTE_QUEUE_LEN        4
#define QUOTE_TEXT_MAX         480
#define QUOTE_AUTHOR_MAX       120

static quotes_lvgl_lock_fn s_lvgl_lock;
static quotes_lvgl_unlock_fn s_lvgl_unlock;
static quotes_fetch_done_fn s_done_handler = nullptr;
static QueueHandle_t s_quote_queue;

static char s_today_quote[QUOTE_TEXT_MAX];
static char s_today_author[QUOTE_AUTHOR_MAX];
static int s_today_year = -1;
static int s_today_yday = -1;
static bool s_today_cached = false;

static void lvgl_lock(void)
{
    if (s_lvgl_lock) {
        s_lvgl_lock(-1);
    }
}

static void lvgl_unlock(void)
{
    if (s_lvgl_unlock) {
        s_lvgl_unlock();
    }
}

static bool wifi_ready(void)
{
    return WiFi.status() == WL_CONNECTED;
}

static bool hk_calendar_day(int *year, int *yday)
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) {
        return false;
    }
    *year = timeinfo.tm_year;
    *yday = timeinfo.tm_yday;
    return true;
}

static bool today_cache_is_valid(void)
{
    if (!s_today_cached) {
        return false;
    }
    int year = -1;
    int yday = -1;
    if (!hk_calendar_day(&year, &yday)) {
        return false;
    }
    return year == s_today_year && yday == s_today_yday;
}

static void today_cache_load_from_flash(void)
{
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, true)) {
        s_today_cached = false;
        return;
    }

    const int year = prefs.getInt(PREFS_KEY_QUOTE_YEAR, -1);
    const int yday = prefs.getInt(PREFS_KEY_QUOTE_YDAY, -1);
    const String text = prefs.getString(PREFS_KEY_QUOTE_TEXT, "");
    const String auth = prefs.getString(PREFS_KEY_QUOTE_AUTH, "");
    prefs.end();

    if (year < 0 || yday < 0 || text.length() == 0) {
        s_today_cached = false;
        return;
    }

    strncpy(s_today_quote, text.c_str(), sizeof(s_today_quote) - 1);
    s_today_quote[sizeof(s_today_quote) - 1] = '\0';
    strncpy(s_today_author, auth.c_str(), sizeof(s_today_author) - 1);
    s_today_author[sizeof(s_today_author) - 1] = '\0';
    s_today_year = year;
    s_today_yday = yday;
    s_today_cached = true;

    Serial.println("Quote: loaded today from flash");
}

static void today_cache_save(const char *quote, const char *author)
{
    strncpy(s_today_quote, quote ? quote : "", sizeof(s_today_quote) - 1);
    s_today_quote[sizeof(s_today_quote) - 1] = '\0';
    strncpy(s_today_author, author ? author : "", sizeof(s_today_author) - 1);
    s_today_author[sizeof(s_today_author) - 1] = '\0';

    int year = -1;
    int yday = -1;
    if (!hk_calendar_day(&year, &yday)) {
        s_today_cached = false;
        return;
    }

    s_today_year = year;
    s_today_yday = yday;
    s_today_cached = true;

    Preferences prefs;
    if (!prefs.begin(PREFS_NS, false)) {
        Serial.println("Quote: flash save failed (prefs)");
        return;
    }
    prefs.putInt(PREFS_KEY_QUOTE_YEAR, s_today_year);
    prefs.putInt(PREFS_KEY_QUOTE_YDAY, s_today_yday);
    prefs.putString(PREFS_KEY_QUOTE_TEXT, s_today_quote);
    prefs.putString(PREFS_KEY_QUOTE_AUTH, s_today_author);
    prefs.end();

    Serial.println("Quote: saved today to flash");
}

static bool today_cache_show(void)
{
    if (!today_cache_is_valid()) {
        return false;
    }

    lvgl_lock();
    main_screen_set_quote(s_today_quote, s_today_author);
    lvgl_unlock();
    return true;
}

static bool parse_quote_payload(const char *payload, size_t payload_len, String &content, String &author)
{
    DynamicJsonDocument doc(QUOTE_JSON_CAPACITY);
    const DeserializationError err = deserializeJson(doc, payload, payload_len);
    if (err) {
        Serial.printf("Quote JSON error: %s\n", err.c_str());
        return false;
    }

    if (!doc.is<JsonObject>()) {
        return false;
    }

    JsonObject root = doc.as<JsonObject>();
    content = root["content"] | "";
    author = root["author"] | "";

    return content.length() > 0;
}

static void notify_done(quote_fetch_kind_t kind, bool success)
{
    if (s_done_handler) {
        s_done_handler(kind, success);
    }
}

static bool http_fetch_quote(const char *url, String &content, String &author)
{
    char *body = nullptr;
    size_t body_len = 0;
    if (!net_https_get(url, &body, &body_len, QUOTE_HTTP_TIMEOUT_MS)) {
        return false;
    }

    if (body_len < 4) {
        Serial.println("Quote: response too short");
        heap_caps_free(body);
        return false;
    }

    const bool ok = parse_quote_payload(body, body_len, content, author);
    Serial.printf("Quote: OK (%u bytes) from %s\n", (unsigned)body_len, url);
    heap_caps_free(body);
    if (!ok) {
        Serial.println("Quote: unexpected JSON shape");
    }
    return ok;
}

static bool fetch_random_quote(String &content, String &author)
{
    return http_fetch_quote(QUOTE_API_RANDOM_URL, content, author);
}

static bool fetch_today_quote(String &content, String &author)
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) {
        Serial.println("Quote: local time not available for today");
        return false;
    }

    Serial.println("Quote: fetching today from Quotable");
    return http_fetch_quote(QUOTE_API_TODAY_URL, content, author);
}

static void quote_worker_task(void *arg)
{
    (void)arg;
    Serial.println("Starting quotes task (core 0)");
    quote_fetch_kind_t kind;

    while (true) {
        if (xQueueReceive(s_quote_queue, &kind, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!wifi_ready()) {
            lvgl_lock();
            main_screen_set_quote("Wi-Fi not connected.", "");
            lvgl_unlock();
            notify_done(kind, false);
            continue;
        }

        if (kind == QUOTE_FETCH_TODAY && today_cache_show()) {
            notify_done(kind, true);
            continue;
        }

        lvgl_lock();
        main_screen_set_quote_loading();
        lvgl_unlock();

        String content;
        String author;
        bool ok = false;

        if (kind == QUOTE_FETCH_RANDOM) {
            ok = fetch_random_quote(content, author);
        } else {
            ok = fetch_today_quote(content, author);
            if (ok) {
                today_cache_save(content.c_str(), author.c_str());
            }
        }

        lvgl_lock();
        if (ok) {
            main_screen_set_quote(content.c_str(), author.c_str());
        } else {
            main_screen_set_quote("Could not load quote.", "Check Wi-Fi / try again");
        }
        lvgl_unlock();

        notify_done(kind, ok);
    }
}

void quotes_api_set_done_handler(quotes_fetch_done_fn handler)
{
    s_done_handler = handler;
}

void quotes_api_init(quotes_lvgl_lock_fn lock, quotes_lvgl_unlock_fn unlock)
{
    s_lvgl_lock = lock;
    s_lvgl_unlock = unlock;
    s_quote_queue = xQueueCreate(QUOTE_QUEUE_LEN, sizeof(quote_fetch_kind_t));
    today_cache_load_from_flash();
}

void quotes_api_start_worker(void)
{
    xTaskCreatePinnedToCore(
        quote_worker_task,
        "quotes",
        APP_STACK_QUOTES,
        nullptr,
        APP_PRIO_QUOTES,
        nullptr,
        APP_CORE_NET);
}

void quotes_api_request(quote_fetch_kind_t kind)
{
    if (!s_quote_queue) {
        return;
    }
    xQueueSend(s_quote_queue, &kind, 0);
}
