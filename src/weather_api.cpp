#include "weather_api.h"

#include <Arduino.h>

#include "app_tasks.h"
#include "net_https.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <time.h>

#include "main_screen.h"
#include "weather_icons.h"
#include "weather_screen.h"

#define WEATHER_API_URL       "https://api.open-meteo.com/v1/forecast?" \
                              "latitude=22.32&longitude=114.17" \
                              "&current=temperature_2m,relative_humidity_2m,weather_code," \
                              "wind_speed_10m,is_day" \
                              "&hourly=temperature_2m,weather_code,is_day" \
                              "&daily=weather_code,temperature_2m_max,temperature_2m_min" \
                              "&timezone=Asia%2FHong_Kong&forecast_days=7&forecast_hours=24"
#define WEATHER_JSON_CAPACITY 16384

struct SpiRamAllocator {
    void *allocate(size_t size)
    {
        void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) {
            p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        return p;
    }
    void deallocate(void *pointer)
    {
        heap_caps_free(pointer);
    }
    void *reallocate(void *ptr, size_t new_size)
    {
        void *p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) {
            p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        return p;
    }
};

using WeatherJsonDocument = BasicJsonDocument<SpiRamAllocator>;
#define WEATHER_HTTP_TIMEOUT       20000
#define WEATHER_SSL_MIN_HEAP       NET_SSL_MIN_INTERNAL_BLOCK
#define WEATHER_POST_FETCH_DELAY_MS 600

static weather_lvgl_lock_fn s_lvgl_lock;
static weather_lvgl_unlock_fn s_lvgl_unlock;
static QueueHandle_t s_weather_queue;

typedef struct {
    bool valid;
    time_t fetched_at;
    int current_code;
    float current_temp;
    int humidity;
    float wind_kmh;
    bool is_day;
    int day_count;
    struct {
        char day_label[WEATHER_FORECAST_LABEL_MAX];
        int code;
        float tmax;
        float tmin;
        bool is_day;
    } forecast[WEATHER_FORECAST_DAYS];
    uint8_t today_hourly_count;
    weather_hourly_slot_t today_hourly[WEATHER_HOURLY_MAX];
} weather_cache_t;

static weather_cache_t s_cache;
static volatile bool s_fetch_in_progress = false;

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

static void cache_today_hourly_from_json(JsonObject daily, JsonObject hourly)
{
    s_cache.today_hourly_count = 0;

    JsonArray daily_times = daily["time"].as<JsonArray>();
    JsonArray ht = hourly["time"].as<JsonArray>();
    JsonArray hcode = hourly["weather_code"].as<JsonArray>();
    JsonArray htemp = hourly["temperature_2m"].as<JsonArray>();
    JsonArray hday = hourly["is_day"].as<JsonArray>();

    if (daily_times.isNull() || daily_times.size() == 0 || ht.isNull() || hcode.isNull() || htemp.isNull()) {
        Serial.println("Weather: missing hourly arrays");
        return;
    }

    const char *today_iso = daily_times[0].as<const char *>();
    if (!today_iso) {
        return;
    }

    char today_date[11] = {};
    if (sscanf(today_iso, "%10[^T]", today_date) != 1) {
        return;
    }

    for (size_t hi = 0; hi < ht.size(); ++hi) {
        const char *iso = ht[hi].as<const char *>();
        if (!iso) {
            continue;
        }

        char date_buf[11] = {};
        if (sscanf(iso, "%10[^T]", date_buf) != 1 || strncmp(date_buf, today_date, 10) != 0) {
            continue;
        }

        if (s_cache.today_hourly_count >= WEATHER_HOURLY_MAX) {
            continue;
        }

        weather_hourly_slot_t *slot = &s_cache.today_hourly[s_cache.today_hourly_count];
        int hour = 0;
        int minute = 0;
        if (strlen(iso) >= 16) {
            sscanf(iso + 11, "%d:%d", &hour, &minute);
        }
        snprintf(slot->time_label, sizeof(slot->time_label), "%02d:%02d", hour, minute);
        slot->weather_code = hcode[hi] | 0;
        slot->temp_c = htemp[hi] | 0.0f;
        slot->is_day = hday.isNull() ? true : (bool)hday[hi].as<bool>();
        s_cache.today_hourly_count++;
    }

    Serial.printf("Weather: hourly slots today=%u\n", (unsigned)s_cache.today_hourly_count);
}

static void push_cache_to_ui(void)
{
    if (!s_cache.valid) {
        return;
    }

    main_screen_set_weather_icon_ex(s_cache.current_code, s_cache.is_day);

    if (!weather_screen_is_ready()) {
        return;
    }

    weather_screen_set_current(s_cache.current_code, s_cache.current_temp, s_cache.humidity,
                               s_cache.wind_kmh, s_cache.is_day);
    for (int i = 0; i < s_cache.day_count; ++i) {
        weather_screen_set_forecast_day(i, s_cache.forecast[i].day_label, s_cache.forecast[i].code,
                                        s_cache.forecast[i].tmax, s_cache.forecast[i].tmin,
                                        s_cache.forecast[i].is_day);
    }
    for (int i = s_cache.day_count; i < WEATHER_FORECAST_DAYS; ++i) {
        weather_screen_set_forecast_day(i, "—", 0, 0.0f, 0.0f, true);
    }
    weather_screen_set_today_hourly(s_cache.today_hourly, s_cache.today_hourly_count);
    weather_screen_set_forecast_count(s_cache.day_count);
}

bool weather_api_has_cached_data(void)
{
    return s_cache.valid;
}

bool weather_api_fetch_in_progress(void)
{
    return s_fetch_in_progress;
}

void weather_api_apply_cached_to_ui(void)
{
    push_cache_to_ui();
}

static void format_forecast_day_label(const char *iso_date, int index, char *buf, size_t len)
{
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };

    int year = 0;
    int month = 0;
    int day = 0;
    if (!iso_date || sscanf(iso_date, "%d-%d-%d", &year, &month, &day) != 3) {
        snprintf(buf, len, "Day %d", index + 1);
        return;
    }

    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    mktime(&t);

    const char *mon = (month >= 1 && month <= 12) ? months[month - 1] : "";

    if (index == 0) {
        snprintf(buf, len, "Today  %d %s", day, mon);
    } else if (index == 1) {
        snprintf(buf, len, "Tomorrow  %d %s", day, mon);
    } else {
        static const char *weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        snprintf(buf, len, "%s  %d %s", weekdays[t.tm_wday], day, mon);
    }
}

static bool apply_weather_from_doc(WeatherJsonDocument &doc)
{
    JsonObject current = doc["current"].as<JsonObject>();
    if (current.isNull()) {
        Serial.println("Weather: missing current object");
        return false;
    }

    const int current_code = current["weather_code"] | 0;
    const float current_temp = current["temperature_2m"] | 0.0f;
    const int humidity = current["relative_humidity_2m"] | 0;
    const float wind_kmh = current["wind_speed_10m"] | 0.0f;
    const bool is_day = current["is_day"].isNull() ? true : (bool)current["is_day"].as<bool>();

    Serial.println("Weather: --- current (Hong Kong) ---");
    Serial.printf("  code=%d  temp=%.1f C  humidity=%d%%  wind=%.1f km/h (%s)  %s\n", current_code,
                  current_temp, humidity, wind_kmh, weather_wind_level_label(wind_kmh),
                  is_day ? "day" : "night");
    Serial.printf("  WMO: %s\n", weather_code_to_label(current_code));

    s_cache.current_code = current_code;
    s_cache.current_temp = current_temp;
    s_cache.humidity = humidity;
    s_cache.wind_kmh = wind_kmh;
    s_cache.is_day = is_day;

    JsonObject daily = doc["daily"].as<JsonObject>();
    JsonObject hourly = doc["hourly"].as<JsonObject>();
    JsonArray times = daily["time"].as<JsonArray>();
    JsonArray codes = daily["weather_code"].as<JsonArray>();
    JsonArray tmax = daily["temperature_2m_max"].as<JsonArray>();
    JsonArray tmin = daily["temperature_2m_min"].as<JsonArray>();

    if (times.isNull() || codes.isNull() || tmax.isNull() || tmin.isNull()) {
        Serial.println("Weather: missing daily forecast arrays");
        s_cache.day_count = 0;
        s_cache.today_hourly_count = 0;
        s_cache.valid = true;
        s_cache.fetched_at = time(nullptr);
        lvgl_lock();
        push_cache_to_ui();
        lvgl_unlock();
        return true;
    }

    const int day_count = min((int)times.size(), WEATHER_FORECAST_DAYS);
    s_cache.day_count = day_count;

    cache_today_hourly_from_json(daily, hourly);

    Serial.printf("Weather: --- %d-day forecast ---\n", day_count);

    for (int i = 0; i < day_count; ++i) {
        const char *iso = times[i].as<const char *>();
        const int wcode = codes[i] | 0;
        const float tmax_v = tmax[i] | 0.0f;
        const float tmin_v = tmin[i] | 0.0f;
        const bool day_icon = (i == 0) ? is_day : true;
        format_forecast_day_label(iso, i, s_cache.forecast[i].day_label, sizeof(s_cache.forecast[i].day_label));
        s_cache.forecast[i].code = wcode;
        s_cache.forecast[i].tmax = tmax_v;
        s_cache.forecast[i].tmin = tmin_v;
        s_cache.forecast[i].is_day = day_icon;
        if (i == 0) {
            Serial.printf("  [%d] date=%s  %s  hourly=%u\n", i, iso ? iso : "?",
                          s_cache.forecast[i].day_label, (unsigned)s_cache.today_hourly_count);
        } else {
            Serial.printf("  [%d] date=%s  %s\n", i, iso ? iso : "?", s_cache.forecast[i].day_label);
        }
        Serial.printf("       code=%d  %s  H=%.1f C  L=%.1f C\n", wcode, weather_code_to_label(wcode), tmax_v,
                      tmin_v);
    }

    s_cache.valid = true;
    s_cache.fetched_at = time(nullptr);

    lvgl_lock();
    push_cache_to_ui();
    lvgl_unlock();

    Serial.println("Weather: loaded OK");
    return true;
}

static bool fetch_hong_kong_weather(void)
{
    net_prepare_https(WEATHER_SSL_MIN_HEAP);

    char *body = nullptr;
    size_t body_len = 0;
    if (!net_https_get_ex(WEATHER_API_URL, &body, &body_len, WEATHER_HTTP_TIMEOUT, WEATHER_SSL_MIN_HEAP)) {
        Serial.println("Weather: HTTPS fetch failed");
        return false;
    }

    Serial.printf("Weather: received %u bytes\n", (unsigned)body_len);

    if (body_len < 4 || body[0] != '{') {
        Serial.printf("Weather: unexpected payload (first char 0x%02x)\n",
                      body_len > 0 ? (unsigned)(uint8_t)body[0] : 0);
        heap_caps_free(body);
        return false;
    }

    WeatherJsonDocument *doc = new WeatherJsonDocument(WEATHER_JSON_CAPACITY);
    if (!doc) {
        Serial.println("Weather: JSON document alloc failed");
        heap_caps_free(body);
        return false;
    }

    const DeserializationError err = deserializeJson(*doc, body, body_len);
    heap_caps_free(body);
    body = nullptr;

    if (err) {
        Serial.printf("Weather JSON error: %s (len=%u)\n", err.c_str(), (unsigned)body_len);
        delete doc;
        return false;
    }

    const bool ok = apply_weather_from_doc(*doc);
    delete doc;
    vTaskDelay(pdMS_TO_TICKS(WEATHER_POST_FETCH_DELAY_MS));
    return ok;
}

static void weather_worker_task(void *arg)
{
    (void)arg;
    Serial.println("Starting weather task (core 0)");
    uint8_t trigger;

    while (true) {
        if (xQueueReceive(s_weather_queue, &trigger, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        s_fetch_in_progress = true;

        if (!wifi_ready()) {
            lvgl_lock();
            weather_screen_set_error("Wi-Fi not connected");
            lvgl_unlock();
            s_fetch_in_progress = false;
            continue;
        }

        lvgl_lock();
        weather_screen_set_loading();
        lvgl_unlock();

        Serial.println("Weather: fetching forecast");
        if (!fetch_hong_kong_weather()) {
            if (!s_cache.valid) {
                lvgl_lock();
                weather_screen_set_error("Could not load weather");
                lvgl_unlock();
            }
        }

        s_fetch_in_progress = false;
    }
}

void weather_api_init(weather_lvgl_lock_fn lock, weather_lvgl_unlock_fn unlock)
{
    s_lvgl_lock = lock;
    s_lvgl_unlock = unlock;
    s_weather_queue = xQueueCreate(2, sizeof(uint8_t));
    memset(&s_cache, 0, sizeof(s_cache));
}

void weather_api_start_worker(void)
{
    xTaskCreatePinnedToCore(
        weather_worker_task,
        "weather",
        APP_STACK_WEATHER,
        nullptr,
        APP_PRIO_WEATHER,
        nullptr,
        APP_CORE_NET);
}

void weather_api_request_refresh(void)
{
    if (!s_weather_queue) {
        return;
    }
    if (s_fetch_in_progress) {
        return;
    }
    const uint8_t trigger = 1;
    if (xQueueSend(s_weather_queue, &trigger, 0) == pdTRUE) {
        s_fetch_in_progress = true;
    }
}
