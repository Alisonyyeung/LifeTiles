#include "net_https.h"

#include <cstring>

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include "net_mutex.h"

#define NET_SSL_MIN_BLOCK_DEFAULT  (18 * 1024)
#define NET_SSL_SETTLE_MS            300
#define NET_SSL_MAX_ATTEMPTS         4
#define NET_SSL_HEAP_WAIT_MS         6000
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
    client.setBufferSizes(8192, 512);
#endif
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
            /* getString() decodes chunked transfer; raw getStreamPtr() does not. */
            decoded = http.getString();
        } else {
            Serial.printf("net_https: HTTP %d for %s\n", code, url);
        }

        http.end();
        client.stop();
        vTaskDelay(pdMS_TO_TICKS(NET_SSL_SETTLE_MS));

        if (code == HTTP_CODE_OK && decoded.length() > 0) {
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

    if (ok) {
        net_https_wait_heap(min_internal_block, 2000);
    }

    return ok;
}

bool net_https_get(const char *url, char **out_body, size_t *out_len, uint32_t timeout_ms)
{
    return net_https_get_ex(url, out_body, out_len, timeout_ms, NET_SSL_MIN_BLOCK_DEFAULT);
}
