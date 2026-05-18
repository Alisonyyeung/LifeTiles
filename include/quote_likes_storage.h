#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIKED_QUOTES_MAX      10
#define LIKED_QUOTE_TEXT_MAX  480
#define LIKED_QUOTE_AUTHOR_MAX 120

typedef struct {
    char text[LIKED_QUOTE_TEXT_MAX];
    char author[LIKED_QUOTE_AUTHOR_MAX];
} liked_quote_t;

typedef struct {
    uint8_t count;
    liked_quote_t items[LIKED_QUOTES_MAX];
} liked_quotes_list_t;

void quote_likes_init(void);

bool quote_likes_load(liked_quotes_list_t *out);

/** @return true if this quote+author is in the liked list. */
bool quote_likes_contains(const char *text, const char *author);

/**
 * Add or remove. @p out_liked is set to the new liked state.
 * @return false if add failed (list full).
 */
bool quote_likes_toggle(const char *text, const char *author, bool *out_liked);

bool quote_likes_remove_at(uint8_t index);

#ifdef __cplusplus
}
#endif
