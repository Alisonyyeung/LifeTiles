#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Remove emoji variation selectors and skin-tone modifiers in-place.
 * @return true if @p buf still has printable content.
 */
bool comment_text_normalize(char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
