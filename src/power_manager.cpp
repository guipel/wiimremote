#include "power_manager.h"
#include "display_driver.h"
#include "config.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sleep.h>

static unsigned long s_last_activity_ms = 0;
static bool s_is_dimmed = false;

static uint16_t s_dim_timeout_sec = 30;
static uint32_t s_dim_timeout_ms = 30000;

static uint16_t s_sleep_timeout_sec = 120; // Default 2 minutes
static uint32_t s_sleep_timeout_ms = 120000;

static uint8_t s_brightness_pct = 80; // Default 80% (20% to 100% in 10% steps)

static bool s_prefs_loaded = false;

static void load_power_preferences() {
    if (s_prefs_loaded) return;
    s_prefs_loaded = true;
    Preferences prefs;
    if (prefs.begin("wiimremote", true)) {
        s_dim_timeout_sec = prefs.getUShort("dim_sec", 30);
        s_dim_timeout_ms = (uint32_t)s_dim_timeout_sec * 1000;

        s_sleep_timeout_sec = prefs.getUShort("sleep_sec", 120);
        s_sleep_timeout_ms = (uint32_t)s_sleep_timeout_sec * 1000;

        s_brightness_pct = prefs.getUChar("bright_pct", 80);
        if (s_brightness_pct < 20) s_brightness_pct = 20;
        if (s_brightness_pct > 100) s_brightness_pct = 100;

        prefs.end();
        log_i("Power Manager loaded: dim=%u s, sleep=%u s, bright=%u%%", s_dim_timeout_sec, s_sleep_timeout_sec, s_brightness_pct);
    }
}

void power_manager_init() {
    load_power_preferences();
    s_last_activity_ms = millis();
    display_set_backlight((uint8_t)(s_brightness_pct * 255 / 100));
}

void power_manager_notify_activity() {
    s_last_activity_ms = millis();
    if (s_is_dimmed) {
        s_is_dimmed = false;
        display_set_backlight((uint8_t)(s_brightness_pct * 255 / 100));
    }
}

void power_manager_check_inactivity() {
    if (s_last_activity_ms == 0) {
        s_last_activity_ms = millis();
        return;
    }

    unsigned long idle_ms = millis() - s_last_activity_ms;

    // 1. Check Deep Sleep timeout
    if (s_sleep_timeout_ms > 0 && idle_ms > s_sleep_timeout_ms) {
        power_manager_enter_deep_sleep();
        return;
    }

    // 2. Check Screen Dim timeout (dim strictly to 10% brightness)
    if (s_dim_timeout_ms > 0 && !s_is_dimmed && idle_ms > s_dim_timeout_ms) {
        s_is_dimmed = true;
        display_set_backlight(25); // 10% brightness (25/255)
    }
}

void power_manager_set_dim_timeout_sec(uint16_t seconds) {
    s_dim_timeout_sec = seconds;
    s_dim_timeout_ms = (uint32_t)seconds * 1000;
    Preferences prefs;
    if (prefs.begin("wiimremote", false)) {
        prefs.putUShort("dim_sec", seconds);
        prefs.end();
        log_i("Saved screen dim timeout: %u sec", seconds);
    }
    if (seconds == 0 && s_is_dimmed) {
        s_is_dimmed = false;
        display_set_backlight((uint8_t)(s_brightness_pct * 255 / 100));
    }
}

uint16_t power_manager_get_dim_timeout_sec() {
    load_power_preferences();
    return s_dim_timeout_sec;
}

void power_manager_set_brightness_pct(uint8_t pct) {
    if (pct < 20) pct = 20;
    if (pct > 100) pct = 100;
    s_brightness_pct = pct;
    Preferences prefs;
    if (prefs.begin("wiimremote", false)) {
        prefs.putUChar("bright_pct", pct);
        prefs.end();
        log_i("Saved brightness: %u%%", pct);
    }
    if (!s_is_dimmed) {
        display_set_backlight((uint8_t)(pct * 255 / 100));
    }
}

uint8_t power_manager_get_brightness_pct() {
    load_power_preferences();
    return s_brightness_pct;
}

void power_manager_set_sleep_timeout_sec(uint16_t seconds) {
    s_sleep_timeout_sec = seconds;
    s_sleep_timeout_ms = (uint32_t)seconds * 1000;
    Preferences prefs;
    if (prefs.begin("wiimremote", false)) {
        prefs.putUShort("sleep_sec", seconds);
        prefs.end();
        log_i("Saved deep sleep timeout: %u sec", seconds);
    }
}

uint16_t power_manager_get_sleep_timeout_sec() {
    load_power_preferences();
    return s_sleep_timeout_sec;
}

void power_manager_enter_deep_sleep() {
    log_i("Entering Deep Sleep (Touch-to-Wake on GPIO %d)...", TOUCH_INT);
    display_set_backlight(0);
    WiFi.disconnect(true);
    delay(100);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)TOUCH_INT, 0);
    esp_deep_sleep_start();
}
