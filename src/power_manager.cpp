#include "power_manager.h"
#include "display_driver.h"
#include "config.h"
#include <Preferences.h>
#include <esp_sleep.h>

static unsigned long s_last_activity_ms = 0;
static bool s_is_dimmed = false;

static uint16_t s_dim_timeout_sec = 30;
static uint32_t s_dim_timeout_ms = 30000;

static uint16_t s_sleep_timeout_sec = 120; // Default 2 minutes
static uint32_t s_sleep_timeout_ms = 120000;

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
        prefs.end();
        log_i("Power Manager loaded: dim=%u s, sleep=%u s", s_dim_timeout_sec, s_sleep_timeout_sec);
    }
}

void power_manager_init() {
    load_power_preferences();
    s_last_activity_ms = millis();
}

void power_manager_notify_activity() {
    s_last_activity_ms = millis();
    if (s_is_dimmed) {
        s_is_dimmed = false;
        display_set_backlight(BL_DEFAULT_BRIGHT);
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

    // 2. Check Screen Dim timeout
    if (s_dim_timeout_ms > 0 && !s_is_dimmed && idle_ms > s_dim_timeout_ms) {
        s_is_dimmed = true;
        display_set_backlight(38); // 15% brightness
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
        display_set_backlight(BL_DEFAULT_BRIGHT);
    }
}

uint16_t power_manager_get_dim_timeout_sec() {
    load_power_preferences();
    return s_dim_timeout_sec;
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
    esp_sleep_enable_ext0_wakeup((gpio_num_t)TOUCH_INT, 0);
    esp_deep_sleep_start();
}
