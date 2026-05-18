#include "comment_text_normalize.h"

#include "comment_storage.h"

#include <stdint.h>
#include <string.h>

static int utf8_decode(const char *s, size_t len, size_t *idx, uint32_t *cp)
{
    if (!s || !idx || !cp || *idx >= len) {
        return 0;
    }

    const uint8_t c0 = (uint8_t)s[*idx];
    if (c0 < 0x80) {
        *cp = c0;
        (*idx)++;
        return 1;
    }
    if ((c0 & 0xE0) == 0xC0 && *idx + 1 < len) {
        *cp = ((uint32_t)(c0 & 0x1F) << 6) | ((uint8_t)s[*idx + 1] & 0x3F);
        *idx += 2;
        return 1;
    }
    if ((c0 & 0xF0) == 0xE0 && *idx + 2 < len) {
        *cp = ((uint32_t)(c0 & 0x0F) << 12) | ((uint32_t)((uint8_t)s[*idx + 1] & 0x3F) << 6) |
              ((uint8_t)s[*idx + 2] & 0x3F);
        *idx += 3;
        return 1;
    }
    if ((c0 & 0xF8) == 0xF0 && *idx + 3 < len) {
        *cp = ((uint32_t)(c0 & 0x07) << 18) | ((uint32_t)((uint8_t)s[*idx + 1] & 0x3F) << 12) |
              ((uint32_t)((uint8_t)s[*idx + 2] & 0x3F) << 6) | ((uint8_t)s[*idx + 3] & 0x3F);
        *idx += 4;
        return 1;
    }

    (*idx)++;
    return 0;
}

static int utf8_encode(uint32_t cp, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return 0;
    }
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800 && out_len >= 2) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000 && out_len >= 3) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (out_len >= 4) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

static bool strip_modifier(uint32_t cp)
{
    if (cp == 0xFE0E || cp == 0xFE0F) {
        return true;
    }
    if (cp >= 0x1F3FB && cp <= 0x1F3FF) {
        return true;
    }
    return false;
}

bool comment_text_normalize(char *buf, size_t buf_len)
{
    if (!buf || buf_len < 2) {
        return false;
    }

    char tmp[COMMENT_STORAGE_MAX_BYTES + 1];
    size_t in = 0;
    size_t out = 0;
    const size_t src_len = strlen(buf);

    while (in < src_len && out + 1 < buf_len && out < sizeof(tmp) - 1) {
        uint32_t cp = 0;
        const size_t start = in;
        if (!utf8_decode(buf, src_len, &in, &cp)) {
            continue;
        }
        if (strip_modifier(cp)) {
            continue;
        }

        char seq[4];
        const int n = utf8_encode(cp, seq, sizeof(seq));
        if (n <= 0) {
            in = start + 1;
            continue;
        }
        if (out + (size_t)n >= buf_len || out + (size_t)n >= sizeof(tmp)) {
            break;
        }
        memcpy(tmp + out, seq, (size_t)n);
        out += (size_t)n;
    }

    tmp[out] = '\0';
    strncpy(buf, tmp, buf_len - 1);
    buf[buf_len - 1] = '\0';

    for (const char *p = buf; *p; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (c >= 0x20 || c == '\n' || c == '\t') {
            return true;
        }
    }
    return false;
}
