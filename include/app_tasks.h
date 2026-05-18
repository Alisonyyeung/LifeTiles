#pragma once

/**
 * FreeRTOS layout for MyScreen (ESP32-S3 dual-core):
 *   Core 0 — network I/O (HTTP server, weather, quotes)
 *   Core 1 — LVGL UI
 *
 * Arduino loop() stays idle; do not run LVGL or WebServer there.
 */

#include <freertos/FreeRTOS.h>

#define APP_CORE_NET  0
#define APP_CORE_UI   1

#define APP_PRIO_HTTP     3
#define APP_PRIO_LVGL     2
#define APP_PRIO_QUOTES   2
#define APP_PRIO_WEATHER  1

#define APP_STACK_HTTP     (10 * 1024)
#define APP_STACK_LVGL     (16 * 1024)
#define APP_STACK_WEATHER  (16 * 1024)
#define APP_STACK_QUOTES   (12 * 1024)
