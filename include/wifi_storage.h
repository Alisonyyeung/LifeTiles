#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_STORAGE_SSID_MAX  32
#define WIFI_STORAGE_PASS_MAX  64
#define WIFI_IP_STR_MAX        16
#define WIFI_HISTORY_MAX       3

typedef struct {
    char ssid[WIFI_STORAGE_SSID_MAX];
    char password[WIFI_STORAGE_PASS_MAX];
} wifi_history_entry_t;

typedef struct {
    uint8_t count;
    wifi_history_entry_t entries[WIFI_HISTORY_MAX];
} wifi_history_list_t;

typedef struct {
    bool use_static;
    char ip[WIFI_IP_STR_MAX];
    char gateway[WIFI_IP_STR_MAX];
    char subnet[WIFI_IP_STR_MAX];
} wifi_static_config_t;

void wifi_storage_init(void);
bool wifi_storage_is_configured(void);
bool wifi_storage_get_ssid(char *out, size_t out_len);
bool wifi_storage_get_password(char *out, size_t out_len);
bool wifi_storage_save(const char *ssid, const char *password);

bool wifi_storage_get_static(wifi_static_config_t *out);
bool wifi_storage_save_static(const wifi_static_config_t *cfg);
bool wifi_storage_ipv4_valid(const char *s);

/** True after SSID change until static IP is learned from a DHCP connection. */
bool wifi_storage_static_needs_provision(void);
void wifi_storage_mark_static_needs_provision(void);

/**
 * After a successful DHCP connection: suggest device IP (same subnet as gateway),
 * save gateway/subnet, enable static IP. Returns false if Wi-Fi is not connected.
 */
bool wifi_storage_provision_static_from_wifi(void);

bool wifi_storage_history_load(wifi_history_list_t *out);
bool wifi_storage_history_get(size_t index, char *ssid_out, size_t ssid_len, char *pass_out,
                              size_t pass_len);
/** Add or move network to front of history (persisted, max 3). */
bool wifi_storage_history_upsert(const char *ssid, const char *password);
/** Save active credentials into history after a successful connection. */
bool wifi_storage_history_record_connected(void);
bool wifi_storage_history_remove_ssid(const char *ssid);

#ifdef __cplusplus
}
#endif
