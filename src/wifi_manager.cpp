#include "wifi_manager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

#include "wifi_storage.h"

static void apply_static_ip_config(void)
{
    wifi_static_config_t st;
    if (!wifi_storage_get_static(&st) || !st.use_static) {
        return;
    }

    IPAddress ip;
    IPAddress gw;
    IPAddress sn;
    IPAddress dns;
    if (!ip.fromString(st.ip) || !gw.fromString(st.gateway) || !sn.fromString(st.subnet)) {
        Serial.println("wifi_manager: invalid static IP settings, using DHCP");
        return;
    }

    dns = gw;
    if (!WiFi.config(ip, gw, sn, dns)) {
        Serial.println("wifi_manager: WiFi.config failed");
        return;
    }
    Serial.printf("wifi_manager: static IP %s gw %s mask %s\n", st.ip, st.gateway, st.subnet);
}

static void clear_static_ip_config(void)
{
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
}

static bool wait_connected(uint32_t timeout_ms, bool use_rtos_delay)
{
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout_ms) {
        if (use_rtos_delay) {
            vTaskDelay(pdMS_TO_TICKS(250));
        } else {
            delay(250);
        }
    }
    return WiFi.status() == WL_CONNECTED;
}

static bool begin_connect(const char *ssid, const char *pass, bool use_static, bool use_rtos_delay)
{
    WiFi.disconnect(true);
    if (use_rtos_delay) {
        vTaskDelay(pdMS_TO_TICKS(100));
    } else {
        delay(100);
    }
    WiFi.mode(WIFI_STA);
    if (use_static) {
        apply_static_ip_config();
    } else {
        clear_static_ip_config();
    }
    WiFi.begin(ssid, pass);
    return wait_connected(WIFI_CONNECT_TIMEOUT_MS, use_rtos_delay);
}

static bool connect_with_provision(const char *ssid, const char *pass, bool use_rtos_delay)
{
    if (wifi_storage_static_needs_provision()) {
        Serial.println("wifi_manager: new network — DHCP first, then static IP");
        if (!begin_connect(ssid, pass, false, use_rtos_delay)) {
            return false;
        }
        if (!wifi_storage_provision_static_from_wifi()) {
            return true;
        }
        return begin_connect(ssid, pass, true, use_rtos_delay);
    }

    wifi_static_config_t st;
    wifi_storage_get_static(&st);
    if (st.use_static) {
        if (begin_connect(ssid, pass, true, use_rtos_delay)) {
            return true;
        }
        Serial.println("wifi_manager: static connect failed, reprovisioning via DHCP");
        if (!begin_connect(ssid, pass, false, use_rtos_delay)) {
            return false;
        }
        wifi_storage_provision_static_from_wifi();
        return begin_connect(ssid, pass, true, use_rtos_delay);
    }

    return begin_connect(ssid, pass, false, use_rtos_delay);
}

struct async_ctx {
    void (*on_done)(bool connected, void *user_data);
    void *user_data;
};

static void async_lv_cb(void *p)
{
    async_ctx *ctx = (async_ctx *)p;
    if (ctx && ctx->on_done) {
        ctx->on_done(WiFi.status() == WL_CONNECTED, ctx->user_data);
    }
    delete ctx;
}

static void connect_task(void *param)
{
    async_ctx *ctx = (async_ctx *)param;

    char ssid[WIFI_STORAGE_SSID_MAX];
    char pass[WIFI_STORAGE_PASS_MAX];
    if (!wifi_storage_get_ssid(ssid, sizeof(ssid))) {
        async_ctx *lvctx = new async_ctx{ctx->on_done, ctx->user_data};
        lv_async_call(async_lv_cb, lvctx);
        delete ctx;
        vTaskDelete(NULL);
        return;
    }
    wifi_storage_get_password(pass, sizeof(pass));

    Serial.printf("wifi_manager: connecting to \"%s\"...\n", ssid);
    const bool ok = connect_with_provision(ssid, pass, true);

    if (ok) {
        Serial.print("wifi_manager: connected, IP ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("wifi_manager: connection failed");
    }

    async_ctx *lvctx = new async_ctx{ctx->on_done, ctx->user_data};
    lv_async_call(async_lv_cb, lvctx);
    delete ctx;
    vTaskDelete(NULL);
}

bool wifi_manager_connect_blocking(void)
{
    char ssid[WIFI_STORAGE_SSID_MAX];
    char pass[WIFI_STORAGE_PASS_MAX];
    if (!wifi_storage_get_ssid(ssid, sizeof(ssid))) {
        return false;
    }
    wifi_storage_get_password(pass, sizeof(pass));

    Serial.printf("Connecting to Wi-Fi \"%s\"...\n", ssid);
    const bool ok = connect_with_provision(ssid, pass, false);

    if (!ok) {
        Serial.println("Wi-Fi connection failed");
        return false;
    }

    Serial.print("Wi-Fi connected, IP: ");
    Serial.println(WiFi.localIP());
    return true;
}

static void fail_async_cb(void *p)
{
    async_ctx *c = (async_ctx *)p;
    if (c && c->on_done) {
        c->on_done(false, c->user_data);
    }
    delete c;
}

void wifi_manager_connect_async(void (*on_done)(bool connected, void *user_data), void *user_data)
{
    if (!wifi_storage_is_configured()) {
        if (on_done) {
            lv_async_call(fail_async_cb, new async_ctx{on_done, user_data});
        }
        return;
    }

    async_ctx *ctx = new async_ctx{on_done, user_data};
    xTaskCreate(connect_task, "wifi_conn", 4096, ctx, 1, NULL);
}

bool wifi_manager_is_connected(void)
{
    return WiFi.status() == WL_CONNECTED;
}
