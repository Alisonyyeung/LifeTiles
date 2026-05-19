#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REGION_NAME_MAX       48
#define REGION_TZ_IANA_MAX    40
#define REGION_TZ_POSIX_MAX   48
#define REGION_PRESET_CUSTOM  (-1)

typedef struct {
    char name[REGION_NAME_MAX];
    float latitude;
    float longitude;
    char tz_iana[REGION_TZ_IANA_MAX];
    char tz_posix[REGION_TZ_POSIX_MAX];
} region_config_t;

void region_config_init(void);
void region_config_load(void);
const region_config_t *region_config_get(void);

/** Number of built-in city presets (excludes Custom). */
int region_config_preset_count(void);
/** Apply preset 0 .. preset_count-1. Returns false if index out of range. */
bool region_config_apply_preset(int preset_index);
/** Index of matching preset, or REGION_PRESET_CUSTOM. */
int region_config_match_preset_index(const region_config_t *cfg);

bool region_config_save(const region_config_t *cfg);

/** Apply TZ to libc (for clock / quotes day boundary). */
void region_config_apply_timezone(void);

/** Short label for status line (e.g. HKT, JST). */
const char *region_config_clock_abbrev(void);

/** Build Open-Meteo forecast URL into buf (null-terminated). */
bool region_config_build_weather_url(char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
