#include "wifi_storage.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <IPAddress.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <string.h>

#include "wifi_config.h"

#define WIFI_HIST_PATH    "/wifi_hist.json"
#define WIFI_HIST_JSON_CAP 2048
#define WIFI_HIST_FILE_MAX 4096

#define PREFS_NS          "myscreen"
#define PREFS_KEY_SSID    "wifi_ssid"
#define PREFS_KEY_PASS    "wifi_pass"
#define PREFS_KEY_OK      "wifi_ok"
#define PREFS_KEY_ST_EN   "wifi_st_en"
#define PREFS_KEY_ST_IP   "wifi_st_ip"
#define PREFS_KEY_ST_GW   "wifi_st_gw"
#define PREFS_KEY_ST_SN   "wifi_st_sn"
#define PREFS_KEY_ST_PV   "wifi_st_pv"

static bool s_configured = false;
static bool s_static_needs_provision = false;
static char s_ssid[WIFI_STORAGE_SSID_MAX];
static char s_pass[WIFI_STORAGE_PASS_MAX];
static wifi_static_config_t s_static;

static bool is_placeholder_ssid(const char *ssid)
{
    return !ssid || ssid[0] == '\0' || strcmp(ssid, "YOUR_WIFI_SSID") == 0;
}

static void copy_ip_field(char *dst, size_t len, const char *src)
{
    if (!dst || len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, len - 1);
    dst[len - 1] = '\0';
}

static void load_static_from_prefs(Preferences &prefs)
{
    s_static.use_static = prefs.getBool(PREFS_KEY_ST_EN, false);
    copy_ip_field(s_static.ip, sizeof(s_static.ip), prefs.getString(PREFS_KEY_ST_IP, "").c_str());
    copy_ip_field(s_static.gateway, sizeof(s_static.gateway), prefs.getString(PREFS_KEY_ST_GW, "").c_str());
    copy_ip_field(s_static.subnet, sizeof(s_static.subnet), prefs.getString(PREFS_KEY_ST_SN, "255.255.255.0").c_str());
}

static void load_from_prefs(void)
{
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, true)) {
        return;
    }
    s_configured = prefs.getBool(PREFS_KEY_OK, false);
    if (s_configured) {
        prefs.getString(PREFS_KEY_SSID, s_ssid, sizeof(s_ssid));
        prefs.getString(PREFS_KEY_PASS, s_pass, sizeof(s_pass));
        if (s_ssid[0] == '\0') {
            s_configured = false;
        }
    }
    load_static_from_prefs(prefs);
    s_static_needs_provision = prefs.getBool(PREFS_KEY_ST_PV, false);
    prefs.end();
}

static void suggest_device_ip(char *out, size_t out_len, const IPAddress &gw, const IPAddress &mask,
                              const IPAddress &dhcp)
{
    IPAddress ip = dhcp;
    const bool dhcp_ok =
        ip != IPAddress(0, 0, 0, 0) && (uint32_t)(ip & mask) == (uint32_t)(gw & mask) && ip != gw;

    if (!dhcp_ok) {
        ip = gw;
        uint8_t host = 200;
        if (host == gw[3]) {
            host = 150;
        }
        ip[3] = host;
    }

    snprintf(out, out_len, "%s", ip.toString().c_str());
}

static void persist_provision_flag(void)
{
    Preferences prefs;
    if (prefs.begin(PREFS_NS, false)) {
        prefs.putBool(PREFS_KEY_ST_PV, s_static_needs_provision);
        prefs.end();
    }
}

static void migrate_from_header_if_needed(void)
{
    if (s_configured) {
        return;
    }
    if (is_placeholder_ssid(WIFI_SSID)) {
        return;
    }
    strncpy(s_ssid, WIFI_SSID, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
    strncpy(s_pass, WIFI_PASSWORD, sizeof(s_pass) - 1);
    s_pass[sizeof(s_pass) - 1] = '\0';
    s_configured = true;

    Preferences prefs;
    if (prefs.begin(PREFS_NS, false)) {
        prefs.putString(PREFS_KEY_SSID, s_ssid);
        prefs.putString(PREFS_KEY_PASS, s_pass);
        prefs.putBool(PREFS_KEY_OK, true);
        prefs.end();
    }
    Serial.println("wifi_storage: migrated credentials from wifi_config.h to NVS");
}

bool wifi_storage_ipv4_valid(const char *s)
{
    if (!s || s[0] == '\0' || strlen(s) >= WIFI_IP_STR_MAX) {
        return false;
    }
    IPAddress addr;
    return addr.fromString(s);
}

static bool ensure_littlefs_mounted(void)
{
    if (LittleFS.begin(false)) {
        return true;
    }
    return LittleFS.begin(true);
}

static bool history_read_file(wifi_history_list_t *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    if (!ensure_littlefs_mounted()) {
        return false;
    }

    if (!LittleFS.exists(WIFI_HIST_PATH)) {
        return true;
    }

    File f = LittleFS.open(WIFI_HIST_PATH, "r");
    if (!f) {
        return false;
    }

    StaticJsonDocument<WIFI_HIST_JSON_CAP> doc;
    const DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        return false;
    }

    JsonArray arr = doc["networks"].as<JsonArray>();
    if (arr.isNull()) {
        return true;
    }

    uint8_t n = 0;
    for (JsonObject o : arr) {
        if (n >= WIFI_HISTORY_MAX) {
            break;
        }
        const char *ssid = o["ssid"] | "";
        const char *pass = o["password"] | "";
        if (!ssid || ssid[0] == '\0' || strlen(ssid) >= WIFI_STORAGE_SSID_MAX) {
            continue;
        }
        if (!pass) {
            pass = "";
        }
        if (strlen(pass) >= WIFI_STORAGE_PASS_MAX) {
            continue;
        }
        strncpy(out->entries[n].ssid, ssid, sizeof(out->entries[n].ssid) - 1);
        strncpy(out->entries[n].password, pass, sizeof(out->entries[n].password) - 1);
        ++n;
    }
    out->count = n;
    return true;
}

static bool history_write_file(const wifi_history_list_t *list)
{
    if (!list || !ensure_littlefs_mounted()) {
        return false;
    }

    StaticJsonDocument<WIFI_HIST_JSON_CAP> doc;
    JsonArray arr = doc.createNestedArray("networks");
    for (uint8_t i = 0; i < list->count && i < WIFI_HISTORY_MAX; ++i) {
        JsonObject o = arr.createNestedObject();
        o["ssid"] = list->entries[i].ssid;
        o["password"] = list->entries[i].password;
    }

    File f = LittleFS.open(WIFI_HIST_PATH, "w");
    if (!f) {
        return false;
    }
    const bool ok = serializeJson(doc, f) > 0;
    f.close();
    return ok;
}

static void history_migrate_active_network(void)
{
    if (!wifi_storage_is_configured() || !ensure_littlefs_mounted()) {
        return;
    }
    wifi_history_list_t list;
    if (!history_read_file(&list)) {
        return;
    }
    for (uint8_t i = 0; i < list.count; ++i) {
        if (strcmp(list.entries[i].ssid, s_ssid) == 0) {
            return;
        }
    }
    wifi_storage_history_upsert(s_ssid, s_pass);
}

bool wifi_storage_history_load(wifi_history_list_t *out)
{
    const bool ok = history_read_file(out);
    if (ok && out && out->count == 0) {
        history_migrate_active_network();
        return history_read_file(out);
    }
    return ok;
}

bool wifi_storage_history_get(size_t index, char *ssid_out, size_t ssid_len, char *pass_out,
                              size_t pass_len)
{
    wifi_history_list_t list;
    if (!history_read_file(&list) || index >= list.count) {
        return false;
    }
    if (ssid_out && ssid_len > 0) {
        snprintf(ssid_out, ssid_len, "%s", list.entries[index].ssid);
    }
    if (pass_out && pass_len > 0) {
        snprintf(pass_out, pass_len, "%s", list.entries[index].password);
    }
    return true;
}

bool wifi_storage_history_upsert(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0' || strlen(ssid) >= WIFI_STORAGE_SSID_MAX) {
        return false;
    }
    if (!password) {
        password = "";
    }
    if (strlen(password) >= WIFI_STORAGE_PASS_MAX) {
        return false;
    }

    wifi_history_list_t list;
    if (!history_read_file(&list)) {
        memset(&list, 0, sizeof(list));
    }

    int existing = -1;
    for (uint8_t i = 0; i < list.count; ++i) {
        if (strcmp(list.entries[i].ssid, ssid) == 0) {
            existing = (int)i;
            break;
        }
    }

    wifi_history_entry_t entry;
    strncpy(entry.ssid, ssid, sizeof(entry.ssid) - 1);
    strncpy(entry.password, password, sizeof(entry.password) - 1);

    if (existing >= 0) {
        for (int i = existing; i > 0; --i) {
            list.entries[i] = list.entries[i - 1];
        }
        list.entries[0] = entry;
    } else {
        const uint8_t n = list.count < WIFI_HISTORY_MAX ? list.count : WIFI_HISTORY_MAX - 1;
        for (int i = (int)n; i > 0; --i) {
            list.entries[i] = list.entries[i - 1];
        }
        list.entries[0] = entry;
        if (list.count < WIFI_HISTORY_MAX) {
            ++list.count;
        } else {
            list.count = WIFI_HISTORY_MAX;
        }
    }

    return history_write_file(&list);
}

bool wifi_storage_history_record_connected(void)
{
    if (!wifi_storage_is_configured()) {
        return false;
    }
    return wifi_storage_history_upsert(s_ssid, s_pass);
}

bool wifi_storage_history_remove_ssid(const char *ssid)
{
    if (!ssid || ssid[0] == '\0') {
        return false;
    }

    wifi_history_list_t list;
    if (!history_read_file(&list)) {
        return false;
    }

    int found = -1;
    for (uint8_t i = 0; i < list.count; ++i) {
        if (strcmp(list.entries[i].ssid, ssid) == 0) {
            found = (int)i;
            break;
        }
    }
    if (found < 0) {
        return true;
    }

    for (uint8_t i = (uint8_t)found; i + 1 < list.count; ++i) {
        list.entries[i] = list.entries[i + 1];
    }
    if (list.count > 0) {
        --list.count;
    }
    memset(&list.entries[list.count], 0, sizeof(list.entries[0]));
    return history_write_file(&list);
}

void wifi_storage_init(void)
{
    s_configured = false;
    s_ssid[0] = '\0';
    s_pass[0] = '\0';
    memset(&s_static, 0, sizeof(s_static));
    strncpy(s_static.subnet, "255.255.255.0", sizeof(s_static.subnet) - 1);
    load_from_prefs();
    migrate_from_header_if_needed();
}

bool wifi_storage_is_configured(void)
{
    return s_configured && s_ssid[0] != '\0';
}

bool wifi_storage_get_ssid(char *out, size_t out_len)
{
    if (!out || out_len == 0 || !wifi_storage_is_configured()) {
        return false;
    }
    snprintf(out, out_len, "%s", s_ssid);
    return true;
}

bool wifi_storage_get_password(char *out, size_t out_len)
{
    if (!out || out_len == 0 || !wifi_storage_is_configured()) {
        return false;
    }
    snprintf(out, out_len, "%s", s_pass);
    return true;
}

bool wifi_storage_save(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0' || strlen(ssid) >= WIFI_STORAGE_SSID_MAX) {
        return false;
    }
    if (!password) {
        password = "";
    }
    if (strlen(password) >= WIFI_STORAGE_PASS_MAX) {
        return false;
    }

    const bool ssid_changed = !s_configured || strcmp(s_ssid, ssid) != 0;

    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
    strncpy(s_pass, password, sizeof(s_pass) - 1);
    s_pass[sizeof(s_pass) - 1] = '\0';
    s_configured = true;

    Preferences prefs;
    if (!prefs.begin(PREFS_NS, false)) {
        return false;
    }
    prefs.putString(PREFS_KEY_SSID, s_ssid);
    prefs.putString(PREFS_KEY_PASS, s_pass);
    prefs.putBool(PREFS_KEY_OK, true);
    prefs.end();

    if (ssid_changed) {
        wifi_storage_mark_static_needs_provision();
    }
    return true;
}

bool wifi_storage_static_needs_provision(void)
{
    return s_static_needs_provision;
}

void wifi_storage_mark_static_needs_provision(void)
{
    s_static_needs_provision = true;
    persist_provision_flag();
    Serial.println("wifi_storage: static IP will be learned on next connect");
}

bool wifi_storage_provision_static_from_wifi(void)
{
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    const IPAddress gw = WiFi.gatewayIP();
    const IPAddress sn = WiFi.subnetMask();
    const IPAddress dhcp = WiFi.localIP();
    if (gw == IPAddress(0, 0, 0, 0) || sn == IPAddress(0, 0, 0, 0)) {
        Serial.println("wifi_storage: cannot provision static IP (no gateway)");
        return false;
    }

    wifi_static_config_t st;
    memset(&st, 0, sizeof(st));
    st.use_static = true;
    suggest_device_ip(st.ip, sizeof(st.ip), gw, sn, dhcp);
    snprintf(st.gateway, sizeof(st.gateway), "%s", gw.toString().c_str());
    snprintf(st.subnet, sizeof(st.subnet), "%s", sn.toString().c_str());

    s_static = st;
    s_static_needs_provision = false;

    Preferences prefs;
    if (!prefs.begin(PREFS_NS, false)) {
        return false;
    }
    prefs.putBool(PREFS_KEY_ST_EN, true);
    prefs.putString(PREFS_KEY_ST_IP, st.ip);
    prefs.putString(PREFS_KEY_ST_GW, st.gateway);
    prefs.putString(PREFS_KEY_ST_SN, st.subnet);
    prefs.putBool(PREFS_KEY_ST_PV, false);
    prefs.end();

    Serial.printf("wifi_storage: provisioned static IP %s gw %s mask %s\n", st.ip, st.gateway,
                  st.subnet);
    return true;
}

bool wifi_storage_get_static(wifi_static_config_t *out)
{
    if (!out) {
        return false;
    }
    *out = s_static;
    return true;
}

bool wifi_storage_save_static(const wifi_static_config_t *cfg)
{
    if (!cfg) {
        return false;
    }

    if (cfg->use_static) {
        if (!wifi_storage_ipv4_valid(cfg->ip) || !wifi_storage_ipv4_valid(cfg->gateway) ||
            !wifi_storage_ipv4_valid(cfg->subnet)) {
            return false;
        }
    }

    s_static = *cfg;
    s_static_needs_provision = false;

    Preferences prefs;
    if (!prefs.begin(PREFS_NS, false)) {
        return false;
    }
    prefs.putBool(PREFS_KEY_ST_EN, s_static.use_static);
    prefs.putString(PREFS_KEY_ST_IP, s_static.ip);
    prefs.putString(PREFS_KEY_ST_GW, s_static.gateway);
    prefs.putString(PREFS_KEY_ST_SN, s_static.subnet);
    prefs.putBool(PREFS_KEY_ST_PV, false);
    prefs.end();
    return true;
}
