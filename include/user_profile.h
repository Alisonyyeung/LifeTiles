#pragma once

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USER_PROFILE_NAME_MAX 32
#define USER_PROFILE_DEFAULT_NAME "Haha"

void user_profile_load(void);
void user_profile_save(const char *name);

/** Stored name (may be empty). */
bool user_profile_get_stored(char *buf, size_t buf_len);

/** Display name: stored value or USER_PROFILE_DEFAULT_NAME if unset. */
const char *user_profile_get_display_name(void);

/** e.g. "Good morning, Haha" — uses local time when `timeinfo` is set. */
void user_profile_format_greeting(char *buf, size_t buf_len, const struct tm *timeinfo);

/** True for morning/afternoon (sun); false for good night (moon). Default true if time unknown. */
bool user_profile_greeting_show_sun(const struct tm *timeinfo);

#ifdef __cplusplus
}
#endif
