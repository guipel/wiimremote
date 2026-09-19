#pragma once

#include <lvgl.h>
#include <stdio.h>
#include <stdint.h>

// UI Color Tokens
#define COLOR_BG            lv_color_hex(0x101216)
#define COLOR_SURFACE       lv_color_hex(0x1C2028)
#define COLOR_SURFACE_LIGHT lv_color_hex(0x282E3A)
#define COLOR_ACCENT        lv_color_hex(0x00E5FF)
#define COLOR_TEXT_PRIMARY  lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_MUTED    lv_color_hex(0x8E9AAB)
#define COLOR_SUCCESS       lv_color_hex(0x10B981)
#define COLOR_WARNING       lv_color_hex(0xF59E0B)

#define PROGRESS_BAR_MAX 10000

// 10x12 Pixel Alpha Bitmap for clean Padlock Icon
static const uint8_t lock_alpha_map[120] = {
    0, 0, 255, 255, 255, 255, 255, 255, 0, 0,
    0, 255, 255, 0, 0, 0, 0, 255, 255, 0,
    0, 255, 255, 0, 0, 0, 0, 255, 255, 0,
    0, 255, 255, 0, 0, 0, 0, 255, 255, 0,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 0, 0, 255, 255, 255, 255,
    255, 255, 255, 255, 0, 0, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    0, 255, 255, 255, 255, 255, 255, 255, 255, 0
};

static const lv_img_dsc_t img_lock_dsc = {
    .header = {
        .cf = LV_IMG_CF_ALPHA_8BIT,
        .always_zero = 0,
        .reserved = 0,
        .w = 10,
        .h = 12,
    },
    .data_size = sizeof(lock_alpha_map),
    .data = lock_alpha_map,
};

// Helper: Format milliseconds to mm:ss
static inline void format_time(uint32_t ms, char* buffer, size_t buf_len) {
    uint32_t total_sec = ms / 1000;
    uint32_t mins = total_sec / 60;
    uint32_t secs = total_sec % 60;
    snprintf(buffer, buf_len, "%02u:%02u", mins, secs);
}
