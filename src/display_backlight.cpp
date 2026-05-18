#include "display_backlight.h"

#include <Arduino.h>
#include <Preferences.h>
#include <ESP_IOExpander.h>
#include <ESP_Panel.h>
#include <ESP_PanelBacklight.h>

#define PREFS_NS       "myscreen"
#define PREFS_KEY_BL   "brightness"
#define DEFAULT_BL_PCT 80

static ESP_Panel *s_panel = NULL;
static ESP_IOExpander *s_expander = NULL;
static uint8_t s_exp_bl_pin = 0;
static uint8_t s_percent = DEFAULT_BL_PCT;

static void apply_hardware(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_percent = percent;

    if (s_panel) {
        ESP_PanelBacklight *bl = s_panel->getBacklight();
        if (bl) {
            bl->setBrightness(percent);
            return;
        }
    }

    /* ESP32-S3-Box: backlight on CH422G expander (digital on/off if no panel PWM). */
    if (s_expander) {
        s_expander->digitalWrite(s_exp_bl_pin, percent > 8 ? HIGH : LOW);
    }
}

void display_backlight_init(ESP_Panel *panel, ESP_IOExpander *expander, uint8_t expander_bl_pin)
{
    s_panel = panel;
    s_expander = expander;
    s_exp_bl_pin = expander_bl_pin;
}

void display_backlight_load(void)
{
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, true)) {
        apply_hardware(DEFAULT_BL_PCT);
        return;
    }
    const uint8_t pct = prefs.getUChar(PREFS_KEY_BL, DEFAULT_BL_PCT);
    prefs.end();
    apply_hardware(pct);
}

uint8_t display_backlight_get_percent(void)
{
    return s_percent;
}

void display_backlight_set_percent(uint8_t percent)
{
    apply_hardware(percent);
}

void display_backlight_save(void)
{
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, false)) {
        return;
    }
    prefs.putUChar(PREFS_KEY_BL, s_percent);
    prefs.end();
}
