#include "net_https.h"

#include <cstring>

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include "net_mutex.h"

#define NET_SSL_MIN_BLOCK_DEFAULT  NET_SSL_MIN_INTERNAL_BLOCK
#define NET_SSL_SETTLE_MS            300
#define NET_SSL_MAX_ATTEMPTS         4
#define NET_SSL_HEAP_WAIT_MS         6000
#define NET_SSL_RX_CHUNK             4096

#include "image_screen.h"
#include "lvgl_port.h"
#include "weather_screen.h"

static void log_heap(const char *tag)
{
    Serial.printf(
        "%s: internal largest=%u free=%u | psram largest=%u free=%u\n",
        tag,
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

void net_prepare_https(size_t min_internal_block)
{
    log_heap("net_prepare (before release)");
    lvgl_port_lock(-1);
    image_screen_release_heavy_memory();
    weather_screen_release_heavy_memory();
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(80));
    log_heap("net_prepare (after release)");
    if (!net_https_wait_heap(min_internal_block, NET_SSL_HEAP_WAIT_MS)) {
        Serial.println("net_prepare: TLS heap still low");
    }
}

bool net_https_wait_heap(size_t min_internal_block, uint32_t wait_ms)
{
    const uint32_t deadline = millis() + wait_ms;
    while ((int32_t)(deadline - millis()) > 0) {
        const size_t block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (block >= min_internal_block) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    Serial.printf("net_https: largest internal block=%u free=%u (need %u)\n",
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                  (unsigned)ESP.getFreeHeap(), (unsigned)min_internal_block);
    return false;
}

static void configure_tls_client(WiFiClientSecure &client, uint32_t timeout_ms)
{
    client.setInsecure();
    client.setTimeout(timeout_ms / 1000);
#if defined(WIFI_CLIENT_SECURE_HAS_BUFFER_SIZE)
    client.setBufferSizes(4096, 512);
#endif
}

/** Read HTTP body into PSRAM (avoids large Arduino String on internal heap). */
static char *read_body_to_spiram(HTTPClient &http, uint32_t timeout_ms, size_t *out_len)
{
    *out_len = 0;
    const int content_len = http.getSize();
    if (content_len <= 0 || content_len >= (int)NET_HTTPS_BODY_MAX) {
        return nullptr;
    }

    char *body =
        static_cast<char *>(heap_caps_malloc((size_t)content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!body) {
        body = static_cast<char *>(
            heap_caps_malloc((size_t)content_len + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (!body) {
        return nullptr;
    }

    WiFiClient *stream = http.getStreamPtr();
    if (!stream) {
        heap_caps_free(body);
        return nullptr;
    }

    size_t total = 0;
    const uint32_t deadline = millis() + timeout_ms;
    while (total < (size_t)content_len && (int32_t)(deadline - millis()) > 0) {
        const size_t want = min((size_t)content_len - total, (size_t)NET_SSL_RX_CHUNK);
        const int n = stream->readBytes(body + total, want);
        if (n > 0) {
            total += (size_t)n;
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    if (total == 0) {
        heap_caps_free(body);
        return nullptr;
    }

    body[total] = '\0';
    *out_len = total;
    return body;
}

/** Copy a decoded response (from http.getString()) into PSRAM and release internal String. */
static char *copy_string_to_spiram(const String &src, size_t *out_len)
{
    *out_len = 0;
    const size_t len = src.length();
    if (len == 0 || len >= NET_HTTPS_BODY_MAX) {
        return nullptr;
    }

    char *body = static_cast<char *>(heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!body) {
        Serial.println("net_https: PSRAM body alloc failed");
        return nullptr;
    }

    memcpy(body, src.c_str(), len);
    body[len] = '\0';
    *out_len = len;
    return body;
}

bool net_https_get_ex(const char *url, char **out_body, size_t *out_len, uint32_t timeout_ms,
                      size_t min_internal_block)
{
    if (out_body) {
        *out_body = nullptr;
    }
    if (out_len) {
        *out_len = 0;
    }
    if (!out_body || !out_len) {
        return false;
    }

    if (!net_mutex_lock(pdMS_TO_TICKS(timeout_ms))) {
        Serial.println("net_https: mutex timeout");
        return false;
    }

    bool ok = false;

    for (int attempt = 0; attempt < NET_SSL_MAX_ATTEMPTS && !ok; ++attempt) {
        if (attempt > 0) {
            Serial.printf("net_https: attempt %d/%d\n", attempt + 1, NET_SSL_MAX_ATTEMPTS);
            vTaskDelay(pdMS_TO_TICKS(350U * (uint32_t)attempt));
        }

        if (!net_https_wait_heap(min_internal_block, NET_SSL_HEAP_WAIT_MS)) {
            continue;
        }

        WiFiClientSecure client;
        configure_tls_client(client, timeout_ms);

        HTTPClient http;
        http.setReuse(false);
        http.setTimeout(timeout_ms);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

        if (!http.begin(client, url)) {
            Serial.printf("net_https: begin failed %s\n", url);
            client.stop();
            continue;
        }

        http.addHeader("User-Agent", "MyScreen/1.0 (ESP32)");
        http.addHeader("Accept", "application/json");
        http.addHeader("Connection", "close");

        const int code = http.GET();
        char *body = nullptr;
        size_t body_len = 0;
        String decoded;

        if (code == HTTP_CODE_OK) {
            body = read_body_to_spiram(http, timeout_ms, &body_len);
            if (!body) {
                /* Chunked or unknown length — fall back (uses internal RAM briefly). */
                decoded = http.getString();
            }
        } else {
            Serial.printf("net_https: HTTP %d for %s\n", code, url);
        }

        http.end();
        client.stop();
        vTaskDelay(pdMS_TO_TICKS(NET_SSL_SETTLE_MS));

        if (code == HTTP_CODE_OK && !body && decoded.length() > 0) {
            body = copy_string_to_spiram(decoded, &body_len);
            decoded = String();
        }

        if (body && body_len > 0) {
            *out_body = body;
            *out_len = body_len;
            ok = true;
        } else if (body) {
            heap_caps_free(body);
        }
    }

    net_mutex_unlock();

    lvgl_port_lock(-1);
    weather_screen_restore_background_memory();
    lvgl_port_unlock();

    if (ok) {
        net_https_wait_heap(min_internal_block, 2000);
    }

    return ok;
}

bool net_https_get(const char *url, char **out_body, size_t *out_len, uint32_t timeout_ms)
{
    return net_https_get_ex(url, out_body, out_len, timeout_ms, NET_SSL_MIN_BLOCK_DEFAULT);
}
