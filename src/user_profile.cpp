#include "user_profile.h"

#include <Arduino.h>
#include <Preferences.h>
#include <ctype.h>
#include <string.h>

#define PREFS_NS           "myscreen"
#define PREFS_KEY_USERNAME "username"

static char s_stored_name[USER_PROFILE_NAME_MAX];

static void trim_name(char *s)
{
    if (!s) {
        return;
    }
    char *start = s;
    while (*start && isspace((unsigned char)*start)) {
        ++start;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

static const char *greeting_for_hour(int hour)
{
    if (hour >= 5 && hour < 12) {
        return "Good morning";
    }
    if (hour >= 12 && hour < 18) {
        return "Good afternoon";
    }
    return "Good night";
}

void user_profile_load(void)
{
    s_stored_name[0] = '\0';

    Preferences prefs;
    if (!prefs.begin(PREFS_NS, true)) {
        return;
    }
    const size_t len = prefs.getString(PREFS_KEY_USERNAME, s_stored_name, sizeof(s_stored_name));
    prefs.end();

    if (len >= sizeof(s_stored_name)) {
        s_stored_name[sizeof(s_stored_name) - 1] = '\0';
    }
    trim_name(s_stored_name);
}

void user_profile_save(const char *name)
{
    char tmp[USER_PROFILE_NAME_MAX];
    if (!name) {
        tmp[0] = '\0';
    } else {
        strncpy(tmp, name, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        trim_name(tmp);
    }

    strncpy(s_stored_name, tmp, sizeof(s_stored_name) - 1);
    s_stored_name[sizeof(s_stored_name) - 1] = '\0';

    Preferences prefs;
    if (!prefs.begin(PREFS_NS, false)) {
        return;
    }
    prefs.putString(PREFS_KEY_USERNAME, s_stored_name);
    prefs.end();
}

bool user_profile_get_stored(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) {
        return false;
    }
    strncpy(buf, s_stored_name, buf_len - 1);
    buf[buf_len - 1] = '\0';
    return true;
}

const char *user_profile_get_display_name(void)
{
    return s_stored_name[0] != '\0' ? s_stored_name : USER_PROFILE_DEFAULT_NAME;
}

bool user_profile_greeting_show_sun(const struct tm *timeinfo)
{
    if (!timeinfo) {
        return true;
    }
    const int hour = timeinfo->tm_hour;
    return hour >= 5 && hour < 18;
}

void user_profile_format_greeting(char *buf, size_t buf_len, const struct tm *timeinfo)
{
    if (!buf || buf_len == 0) {
        return;
    }

    if (!timeinfo) {
        snprintf(buf, buf_len, "Hello, %s", user_profile_get_display_name());
        return;
    }

    snprintf(buf, buf_len, "%s, %s", greeting_for_hour(timeinfo->tm_hour),
             user_profile_get_display_name());
}
