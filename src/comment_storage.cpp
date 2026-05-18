#include "comment_storage.h"

#include "comment_text_normalize.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <stdio.h>
#include <string.h>

#define COMMENT_PATH       "/comment.txt"
#define COMMENT_TIME_PATH  "/comment_at.txt"
#define COMMENT_META_PATH  "/comment_meta.json"

static StaticJsonDocument<256> s_meta_doc;

void comment_display_defaults(comment_display_t *d)
{
    if (!d) {
        return;
    }
    d->style = COMMENT_STYLE_DIALOGUE;
    d->font_size = COMMENT_FONT_SIZE_24;
    d->scroll = COMMENT_SCROLL_STILL;
    d->festive_color = COMMENT_FESTIVE_GOLD;
    d->show_to = true;
}

static bool utf8_valid(const char *s, size_t max_len)
{
    if (!s) {
        return false;
    }
    size_t i = 0;
    while (s[i] != '\0' && i < max_len) {
        const uint8_t c = (uint8_t)s[i];
        if (c < 0x20 && c != '\n' && c != '\t' && c != '\r') {
            return false;
        }
        if (c < 0x80) {
            ++i;
            continue;
        }
        uint8_t need = 0;
        if ((c & 0xE0) == 0xC0) {
            need = 1;
        } else if ((c & 0xF0) == 0xE0) {
            need = 2;
        } else if ((c & 0xF8) == 0xF0) {
            need = 3;
        } else {
            return false;
        }
        if (i + need >= max_len || s[i + need] == '\0') {
            return false;
        }
        for (uint8_t j = 1; j <= need; ++j) {
            if (((uint8_t)s[i + j] & 0xC0) != 0x80) {
                return false;
            }
        }
        i += 1u + need;
    }
    return s[i] == '\0';
}

static bool write_received_time(time_t t)
{
    File f = LittleFS.open(COMMENT_TIME_PATH, "w");
    if (!f) {
        return false;
    }
    char buf[16];
    const int n = snprintf(buf, sizeof(buf), "%ld", (long)t);
    const size_t written = f.write((const uint8_t *)buf, (size_t)n);
    f.close();
    return written == (size_t)n;
}

static bool read_received_time(time_t *out)
{
    if (!out) {
        return false;
    }
    *out = 0;
    if (!LittleFS.exists(COMMENT_TIME_PATH)) {
        return false;
    }
    File f = LittleFS.open(COMMENT_TIME_PATH, "r");
    if (!f) {
        return false;
    }
    char buf[16];
    const size_t n = f.readBytes(buf, sizeof(buf) - 1);
    f.close();
    if (n == 0) {
        return false;
    }
    buf[n] = '\0';
    *out = (time_t)strtol(buf, NULL, 10);
    return *out > 0;
}

static const char *style_to_str(comment_style_t s)
{
    switch (s) {
    case COMMENT_STYLE_FESTIVE:
        return "festive";
    case COMMENT_STYLE_LOVE:
        return "love";
    case COMMENT_STYLE_WARNING:
        return "warning";
    default:
        return "dialogue";
    }
}

static comment_style_t style_from_str(const char *s)
{
    if (!s) {
        return COMMENT_STYLE_DIALOGUE;
    }
    if (strcmp(s, "festive") == 0) {
        return COMMENT_STYLE_FESTIVE;
    }
    if (strcmp(s, "love") == 0) {
        return COMMENT_STYLE_LOVE;
    }
    if (strcmp(s, "warning") == 0) {
        return COMMENT_STYLE_WARNING;
    }
    return COMMENT_STYLE_DIALOGUE;
}

static int font_size_to_px(comment_font_size_t s)
{
    switch (s) {
    case COMMENT_FONT_SIZE_20:
        return 20;
    case COMMENT_FONT_SIZE_30:
        return 30;
    case COMMENT_FONT_SIZE_36:
        return 36;
    default:
        return 24;
    }
}

static comment_font_size_t font_size_from_px(int px)
{
    if (px <= 20) {
        return COMMENT_FONT_SIZE_20;
    }
    if (px >= 36) {
        return COMMENT_FONT_SIZE_36;
    }
    if (px >= 30) {
        return COMMENT_FONT_SIZE_30;
    }
    return COMMENT_FONT_SIZE_24;
}

static comment_font_size_t font_size_from_legacy(const char *s)
{
    if (!s) {
        return COMMENT_FONT_SIZE_24;
    }
    if (strcmp(s, "sans20") == 0 || strcmp(s, "20") == 0) {
        return COMMENT_FONT_SIZE_20;
    }
    if (strcmp(s, "sans30") == 0 || strcmp(s, "30") == 0) {
        return COMMENT_FONT_SIZE_30;
    }
    if (strcmp(s, "sans36") == 0 || strcmp(s, "36") == 0) {
        return COMMENT_FONT_SIZE_36;
    }
    return COMMENT_FONT_SIZE_24;
}

static bool write_display_meta(const comment_display_t *display)
{
    if (!display) {
        return false;
    }
    s_meta_doc.clear();
    s_meta_doc["style"] = style_to_str(display->style);
    s_meta_doc["font_size"] = font_size_to_px(display->font_size);
    s_meta_doc["scroll"] = display->scroll == COMMENT_SCROLL_MARQUEE ? "marquee" : "still";
    s_meta_doc["festive_color"] = (int)display->festive_color;
    s_meta_doc["show_to"] = display->show_to;

    File f = LittleFS.open(COMMENT_META_PATH, "w");
    if (!f) {
        return false;
    }
    const bool ok = serializeJson(s_meta_doc, f) > 0;
    f.close();
    return ok;
}

static bool read_display_meta(comment_display_t *display)
{
    if (!display) {
        return false;
    }
    comment_display_defaults(display);
    if (!LittleFS.exists(COMMENT_META_PATH)) {
        return true;
    }
    File f = LittleFS.open(COMMENT_META_PATH, "r");
    if (!f) {
        return false;
    }
    s_meta_doc.clear();
    const DeserializationError err = deserializeJson(s_meta_doc, f);
    f.close();
    if (err) {
        return false;
    }

    display->style = style_from_str(s_meta_doc["style"] | "dialogue");
    if (s_meta_doc["font_size"].is<int>()) {
        display->font_size = font_size_from_px(s_meta_doc["font_size"].as<int>());
    } else {
        display->font_size = font_size_from_legacy(s_meta_doc["font"] | "cjk");
    }
    const char *scroll = s_meta_doc["scroll"] | "still";
    display->scroll = (scroll && strcmp(scroll, "marquee") == 0) ? COMMENT_SCROLL_MARQUEE : COMMENT_SCROLL_STILL;
    int fc = s_meta_doc["festive_color"] | 0;
    if (fc < 0 || fc > COMMENT_FESTIVE_RAINBOW) {
        fc = 0;
    }
    display->festive_color = (comment_festive_color_t)fc;
    display->show_to = s_meta_doc["show_to"] | true;
    return true;
}

bool comment_storage_load_display(comment_display_t *display)
{
    return read_display_meta(display);
}

bool comment_storage_has_message(void)
{
    return LittleFS.exists(COMMENT_PATH);
}

bool comment_storage_load_ex(char *buf, size_t buf_len, time_t *received_at)
{
    if (received_at) {
        read_received_time(received_at);
    }
    return comment_storage_load(buf, buf_len);
}

bool comment_storage_load(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) {
        return false;
    }
    buf[0] = '\0';
    if (!LittleFS.exists(COMMENT_PATH)) {
        return false;
    }
    File f = LittleFS.open(COMMENT_PATH, "r");
    if (!f) {
        return false;
    }
    const size_t n = f.readBytes(buf, buf_len - 1);
    f.close();
    buf[n] = '\0';
    return n > 0;
}

bool comment_storage_save(const char *utf8)
{
    comment_display_t d;
    comment_display_defaults(&d);
    read_display_meta(&d);
    return comment_storage_save_all(utf8, &d);
}

bool comment_storage_save_all(const char *utf8, const comment_display_t *display)
{
    if (!utf8) {
        return false;
    }
    const size_t len = strlen(utf8);
    if (len == 0 || len > COMMENT_STORAGE_MAX_BYTES) {
        return false;
    }
    if (!utf8_valid(utf8, len + 1)) {
        return false;
    }

    char normalized[COMMENT_STORAGE_MAX_BYTES + 1];
    strncpy(normalized, utf8, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';
    if (!comment_text_normalize(normalized, sizeof(normalized))) {
        return false;
    }

    File f = LittleFS.open(COMMENT_PATH, "w");
    if (!f) {
        return false;
    }
    const size_t norm_len = strlen(normalized);
    const size_t written = f.write((const uint8_t *)normalized, norm_len);
    f.close();
    if (written != norm_len) {
        return false;
    }

    if (display) {
        write_display_meta(display);
    }

    time_t now = time(nullptr);
    if (now < 100000) {
        now = 0;
    }
    if (now > 0) {
        write_received_time(now);
    }
    return true;
}
