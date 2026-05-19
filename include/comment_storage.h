#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Max UTF-8 bytes stored for the comment board message. */
#define COMMENT_STORAGE_MAX_BYTES 512

typedef enum {
    COMMENT_STYLE_DIALOGUE = 0,
    COMMENT_STYLE_FESTIVE,
    COMMENT_STYLE_LOVE,
    COMMENT_STYLE_WARNING,
} comment_style_t;

typedef enum {
    COMMENT_FONT_SIZE_20 = 0,
    COMMENT_FONT_SIZE_24,
    COMMENT_FONT_SIZE_30,
    COMMENT_FONT_SIZE_36,
} comment_font_size_t;

typedef enum {
    COMMENT_SCROLL_STILL = 0,
    COMMENT_SCROLL_MARQUEE,
} comment_scroll_t;

typedef enum {
    COMMENT_FESTIVE_GOLD = 0,
    COMMENT_FESTIVE_RED,
    COMMENT_FESTIVE_GREEN,
    COMMENT_FESTIVE_BLUE,
    COMMENT_FESTIVE_RAINBOW,
} comment_festive_color_t;

typedef struct {
    comment_style_t style;
    comment_font_size_t font_size;
    comment_scroll_t scroll;
    comment_festive_color_t festive_color;
    bool show_to;
} comment_display_t;

void comment_display_defaults(comment_display_t *d);

bool comment_storage_save(const char *utf8);
bool comment_storage_save_all(const char *utf8, const comment_display_t *display);
bool comment_storage_load(char *buf, size_t buf_len);
bool comment_storage_load_ex(char *buf, size_t buf_len, time_t *received_at);
bool comment_storage_load_display(comment_display_t *display);
bool comment_storage_has_message(void);
/** Remove saved message and timestamp; display meta is kept. */
bool comment_storage_clear(void);

#ifdef __cplusplus
}
#endif
