#pragma once

#include <lvgl.h>
#include <stdio.h>
#include <stdint.h>
#include "ui/themes_config.h"

uint8_t ui_theme_get_idx();
void ui_theme_set_idx(uint8_t idx);
const ThemePaletteConfig& ui_theme_get();

// UI Color Tokens (resolved dynamically from active theme in themes_config.h)
#define COLOR_BG            lv_color_hex(ui_theme_get().bg)
#define COLOR_SURFACE       lv_color_hex(ui_theme_get().surface)
#define COLOR_SURFACE_LIGHT lv_color_hex(ui_theme_get().surface_light)
#define COLOR_ACCENT        lv_color_hex(ui_theme_get().accent)
#define COLOR_TEXT_PRIMARY  lv_color_hex(ui_theme_get().text_primary)
#define COLOR_TEXT_MUTED    lv_color_hex(ui_theme_get().text_muted)
#define COLOR_SUCCESS       lv_color_hex(ui_theme_get().success)
#define COLOR_WARNING       lv_color_hex(ui_theme_get().warning)

#define PROGRESS_BAR_MAX 220

// Shared 10x12 Padlock Icon Asset (defined in ui_core.cpp)
extern const lv_img_dsc_t img_lock_dsc;

// Shared styling helper for edge-to-edge flat list buttons across screens & modals
static inline lv_obj_t* ui_style_flat_list_btn(
    lv_obj_t* btn,
    lv_color_t icon_color,
    lv_color_t text_color = COLOR_TEXT_PRIMARY,
    lv_coord_t pad_ver = 9
) {
    lv_obj_set_style_bg_color(btn, COLOR_BG, 0);
    lv_obj_set_style_bg_color(btn, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_pad_ver(btn, pad_ver, 0);
    lv_obj_set_style_pad_hor(btn, 8, 0);
    lv_obj_set_style_pad_column(btn, 8, 0);
    lv_obj_set_style_transform_width(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_flex_cross_place(btn, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(btn, text_color, 0);

    lv_obj_t* text_lbl = nullptr;
    if (lv_obj_get_child_cnt(btn) >= 2) {
        lv_obj_t* icon_obj = lv_obj_get_child(btn, 0);
        if (icon_obj) {
            lv_obj_set_style_text_color(icon_obj, icon_color, 0);
        }
        text_lbl = lv_obj_get_child(btn, 1);
    }
    return text_lbl;
}

// Helper: Format milliseconds to mm:ss
static inline void format_time(uint32_t ms, char* buffer, size_t buf_len) {
    uint32_t total_sec = ms / 1000;
    uint32_t mins = total_sec / 60;
    uint32_t secs = total_sec % 60;
    snprintf(buffer, buf_len, "%02u:%02u", (unsigned int)mins, (unsigned int)secs);
}
