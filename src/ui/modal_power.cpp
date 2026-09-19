#include "ui/modal_power.h"
#include "ui/ui_theme.h"
#include "power_manager.h"
#include "config.h"

// UI Widgets - Power Management Modal
static lv_obj_t* modal_power_manager = nullptr;
static lv_obj_t* lbl_power_voltage = nullptr;
static lv_obj_t* slider_brightness = nullptr;
static lv_obj_t* lbl_brightness_val = nullptr;
static lv_obj_t* dd_power_dim = nullptr;
static lv_obj_t* dd_power_sleep = nullptr;
static lv_obj_t* btn_sleep_now = nullptr;

// Timeout LUTs for Dropdowns
static const uint16_t dim_seconds_lut[] = { 15, 30, 60, 120, 300, 0 };
static const uint16_t sleep_seconds_lut[] = { 30, 60, 120, 300, 600, 0 };

static void event_slider_brightness(lv_event_t* e) {
    if (!slider_brightness) return;
    int32_t raw = lv_slider_get_value(slider_brightness);
    // Snap to 10% steps: 20, 30, ..., 100
    int32_t snapped = ((raw + 5) / 10) * 10;
    if (snapped < 20) snapped = 20;
    if (snapped > 100) snapped = 100;
    lv_slider_set_value(slider_brightness, snapped, LV_ANIM_OFF);

    if (lbl_brightness_val) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", (int)snapped);
        lv_label_set_text(lbl_brightness_val, buf);
    }
    power_manager_set_brightness_pct((uint8_t)snapped);
}

static void event_dd_power_dim(lv_event_t* e) {
    if (!dd_power_dim) return;
    uint16_t idx = lv_dropdown_get_selected(dd_power_dim);
    if (idx < sizeof(dim_seconds_lut) / sizeof(dim_seconds_lut[0])) {
        power_manager_set_dim_timeout_sec(dim_seconds_lut[idx]);
    }
}

static void event_dd_power_sleep(lv_event_t* e) {
    if (!dd_power_sleep) return;
    uint16_t idx = lv_dropdown_get_selected(dd_power_sleep);
    if (idx < sizeof(sleep_seconds_lut) / sizeof(sleep_seconds_lut[0])) {
        power_manager_set_sleep_timeout_sec(sleep_seconds_lut[idx]);
    }
}

static void event_btn_sleep_now(lv_event_t* e) {
    power_manager_enter_deep_sleep();
}

static void event_btn_close_power_modal(lv_event_t* e) {
    modal_power_close();
}

void modal_power_init(lv_obj_t* parent) {
    modal_power_manager = lv_obj_create(parent ? parent : lv_scr_act());
    lv_obj_set_size(modal_power_manager, 240, 320);
    lv_obj_align(modal_power_manager, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(modal_power_manager, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(modal_power_manager, LV_OPA_80, 0);
    lv_obj_set_style_radius(modal_power_manager, 0, 0);
    lv_obj_set_style_border_side(modal_power_manager, LV_BORDER_SIDE_NONE, 0);
    lv_obj_set_style_pad_all(modal_power_manager, 10, 0);
    lv_obj_add_flag(modal_power_manager, LV_OBJ_FLAG_HIDDEN); // Hidden by default

    // Modal Card
    lv_obj_t* card = lv_obj_create(modal_power_manager);
    lv_obj_set_size(card, 220, 280);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, COLOR_SURFACE, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_color(card, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Header: Title
    lv_obj_t* lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, "Power Management");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 4);

    // Section 1: Battery Voltage Box (Height 42px, Voltage only)
    lv_obj_t* box_bat = lv_obj_create(card);
    lv_obj_set_size(box_bat, 204, 42);
    lv_obj_align(box_bat, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_set_style_bg_color(box_bat, COLOR_BG, 0);
    lv_obj_set_style_border_color(box_bat, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_width(box_bat, 1, 0);
    lv_obj_set_style_radius(box_bat, 8, 0);
    lv_obj_set_style_pad_all(box_bat, 4, 0);
    lv_obj_clear_flag(box_bat, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_sec1 = lv_label_create(box_bat);
    lv_label_set_text(lbl_sec1, "BATTERY VOLTAGE");
    lv_obj_set_style_text_font(lbl_sec1, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_sec1, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_sec1, LV_ALIGN_TOP_LEFT, 4, 1);

    lbl_power_voltage = lv_label_create(box_bat);
    lv_label_set_text(lbl_power_voltage, "-- V");
    lv_obj_set_style_text_font(lbl_power_voltage, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_power_voltage, COLOR_ACCENT, 0);
    lv_obj_align(lbl_power_voltage, LV_ALIGN_TOP_LEFT, 4, 15);

    // Section 2: Screen Brightness Box (Height 46px, 20% to 100%)
    lv_obj_t* box_bright = lv_obj_create(card);
    lv_obj_set_size(box_bright, 204, 46);
    lv_obj_align(box_bright, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_bg_color(box_bright, COLOR_BG, 0);
    lv_obj_set_style_border_color(box_bright, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_width(box_bright, 1, 0);
    lv_obj_set_style_radius(box_bright, 8, 0);
    lv_obj_set_style_pad_all(box_bright, 4, 0);
    lv_obj_clear_flag(box_bright, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_sec2 = lv_label_create(box_bright);
    lv_label_set_text(lbl_sec2, "SCREEN BRIGHTNESS");
    lv_obj_set_style_text_font(lbl_sec2, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_sec2, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_sec2, LV_ALIGN_TOP_LEFT, 4, 1);

    lbl_brightness_val = lv_label_create(box_bright);
    lv_label_set_text(lbl_brightness_val, "80%");
    lv_obj_set_style_text_font(lbl_brightness_val, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_brightness_val, COLOR_ACCENT, 0);
    lv_obj_align(lbl_brightness_val, LV_ALIGN_TOP_RIGHT, -4, 1);

    slider_brightness = lv_slider_create(box_bright);
    lv_obj_set_size(slider_brightness, 194, 6);
    lv_obj_align(slider_brightness, LV_ALIGN_BOTTOM_MID, 0, -9);
    lv_slider_set_range(slider_brightness, 20, 100);
    lv_slider_set_value(slider_brightness, 80, LV_ANIM_OFF);

    // Track style
    lv_obj_set_style_bg_color(slider_brightness, lv_color_hex(0x242A35), 0);
    lv_obj_set_style_bg_opa(slider_brightness, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(slider_brightness, lv_color_hex(0x3A4252), 0);
    lv_obj_set_style_border_width(slider_brightness, 1, 0);
    lv_obj_set_style_radius(slider_brightness, 3, 0);

    // Indicator style
    lv_obj_set_style_bg_color(slider_brightness, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider_brightness, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider_brightness, 3, LV_PART_INDICATOR);

    // Hide knob for clean cursor-less look
    lv_obj_set_style_bg_opa(slider_brightness, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_opa(slider_brightness, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_brightness, 0, LV_PART_KNOB);

    // Extended touch area
    lv_obj_add_flag(slider_brightness, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(slider_brightness, 16);
    lv_obj_clear_flag(slider_brightness, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(slider_brightness, event_slider_brightness, LV_EVENT_VALUE_CHANGED, nullptr);

    // Section 3: Screen Auto-Dim Box (Height 50px)
    lv_obj_t* box_dim = lv_obj_create(card);
    lv_obj_set_size(box_dim, 204, 50);
    lv_obj_align(box_dim, LV_ALIGN_TOP_MID, 0, 122);
    lv_obj_set_style_bg_color(box_dim, COLOR_BG, 0);
    lv_obj_set_style_border_color(box_dim, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_width(box_dim, 1, 0);
    lv_obj_set_style_radius(box_dim, 8, 0);
    lv_obj_set_style_pad_all(box_dim, 4, 0);
    lv_obj_clear_flag(box_dim, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_sec3 = lv_label_create(box_dim);
    lv_label_set_text(lbl_sec3, "SCREEN AUTO-DIM");
    lv_obj_set_style_text_font(lbl_sec3, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_sec3, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_sec3, LV_ALIGN_TOP_LEFT, 4, 1);

    dd_power_dim = lv_dropdown_create(box_dim);
    lv_obj_set_size(dd_power_dim, 194, 28);
    lv_obj_align(dd_power_dim, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_dropdown_set_options(dd_power_dim, "15 seconds\n30 seconds\n1 minute\n2 minutes\n5 minutes\nNever");
    lv_obj_set_style_text_font(dd_power_dim, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(dd_power_dim, COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(dd_power_dim, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_text_color(dd_power_dim, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_radius(dd_power_dim, 6, 0);

    lv_obj_t* list_dim = lv_dropdown_get_list(dd_power_dim);
    if (list_dim) {
        lv_obj_set_style_text_font(list_dim, &lv_font_montserrat_12, 0);
        lv_obj_set_style_bg_color(list_dim, COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(list_dim, COLOR_SURFACE_LIGHT, 0);
        lv_obj_set_style_border_width(list_dim, 1, 0);
        lv_obj_set_style_text_color(list_dim, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_radius(list_dim, 8, 0);
    }
    lv_obj_add_event_cb(dd_power_dim, event_dd_power_dim, LV_EVENT_VALUE_CHANGED, nullptr);

    // Section 4: Auto Deep Sleep Box (Height 50px)
    lv_obj_t* box_sleep = lv_obj_create(card);
    lv_obj_set_size(box_sleep, 204, 50);
    lv_obj_align(box_sleep, LV_ALIGN_TOP_MID, 0, 176);
    lv_obj_set_style_bg_color(box_sleep, COLOR_BG, 0);
    lv_obj_set_style_border_color(box_sleep, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_width(box_sleep, 1, 0);
    lv_obj_set_style_radius(box_sleep, 8, 0);
    lv_obj_set_style_pad_all(box_sleep, 4, 0);
    lv_obj_clear_flag(box_sleep, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_sec4 = lv_label_create(box_sleep);
    lv_label_set_text(lbl_sec4, "AUTO DEEP SLEEP");
    lv_obj_set_style_text_font(lbl_sec4, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_sec4, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_sec4, LV_ALIGN_TOP_LEFT, 4, 1);

    dd_power_sleep = lv_dropdown_create(box_sleep);
    lv_obj_set_size(dd_power_sleep, 194, 28);
    lv_obj_align(dd_power_sleep, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_dropdown_set_options(dd_power_sleep, "30 seconds\n1 minute\n2 minutes\n5 minutes\n10 minutes\nNever");
    lv_obj_set_style_text_font(dd_power_sleep, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(dd_power_sleep, COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(dd_power_sleep, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_text_color(dd_power_sleep, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_radius(dd_power_sleep, 6, 0);

    lv_obj_t* list_sleep = lv_dropdown_get_list(dd_power_sleep);
    if (list_sleep) {
        lv_obj_set_style_text_font(list_sleep, &lv_font_montserrat_12, 0);
        lv_obj_set_style_bg_color(list_sleep, COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(list_sleep, COLOR_SURFACE_LIGHT, 0);
        lv_obj_set_style_border_width(list_sleep, 1, 0);
        lv_obj_set_style_text_color(list_sleep, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_radius(list_sleep, 8, 0);
    }
    lv_obj_add_event_cb(dd_power_sleep, event_dd_power_sleep, LV_EVENT_VALUE_CHANGED, nullptr);

    // Bottom Action Buttons: Sleep Now & Close
    btn_sleep_now = lv_btn_create(card);
    lv_obj_set_size(btn_sleep_now, 96, 30);
    lv_obj_align(btn_sleep_now, LV_ALIGN_BOTTOM_LEFT, 4, -4);
    lv_obj_set_style_bg_color(btn_sleep_now, lv_color_hex(0x3A2222), 0);
    lv_obj_set_style_border_color(btn_sleep_now, lv_color_hex(0xEF4444), 0);
    lv_obj_set_style_border_width(btn_sleep_now, 1, 0);
    lv_obj_set_style_radius(btn_sleep_now, 8, 0);
    lv_obj_add_event_cb(btn_sleep_now, event_btn_sleep_now, LV_EVENT_RELEASED, nullptr);
    lv_obj_t* lbl_sleep_btn = lv_label_create(btn_sleep_now);
    lv_label_set_text(lbl_sleep_btn, LV_SYMBOL_POWER " Sleep");
    lv_obj_set_style_text_color(lbl_sleep_btn, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_text_font(lbl_sleep_btn, &lv_font_montserrat_10, 0);
    lv_obj_center(lbl_sleep_btn);

    lv_obj_t* btn_close = lv_btn_create(card);
    lv_obj_set_size(btn_close, 96, 30);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_obj_set_style_bg_color(btn_close, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(btn_close, 8, 0);
    lv_obj_add_event_cb(btn_close, event_btn_close_power_modal, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lbl_close_btn = lv_label_create(btn_close);
    lv_label_set_text(lbl_close_btn, "Close");
    lv_obj_set_style_text_color(lbl_close_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_close_btn, &lv_font_montserrat_10, 0);
    lv_obj_center(lbl_close_btn);
}

void modal_power_open(uint32_t bat_mv) {
    if (bat_mv > 0) {
        modal_power_update_battery(bat_mv);
    }
    if (slider_brightness) {
        uint8_t cur_bright = power_manager_get_brightness_pct();
        lv_slider_set_value(slider_brightness, cur_bright, LV_ANIM_OFF);
        if (lbl_brightness_val) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d%%", cur_bright);
            lv_label_set_text(lbl_brightness_val, buf);
        }
    }
    if (dd_power_dim) {
        uint16_t cur_sec = power_manager_get_dim_timeout_sec();
        uint16_t sel_idx = 1; // Default 30s
        for (uint16_t i = 0; i < sizeof(dim_seconds_lut) / sizeof(dim_seconds_lut[0]); ++i) {
            if (dim_seconds_lut[i] == cur_sec) {
                sel_idx = i;
                break;
            }
        }
        lv_dropdown_set_selected(dd_power_dim, sel_idx);
    }
    if (dd_power_sleep) {
        uint16_t cur_sec = power_manager_get_sleep_timeout_sec();
        uint16_t sel_idx = 2; // Default 2 min (120s)
        for (uint16_t i = 0; i < sizeof(sleep_seconds_lut) / sizeof(sleep_seconds_lut[0]); ++i) {
            if (sleep_seconds_lut[i] == cur_sec) {
                sel_idx = i;
                break;
            }
        }
        lv_dropdown_set_selected(dd_power_sleep, sel_idx);
    }
    if (modal_power_manager) {
        lv_obj_move_to_index(modal_power_manager, -1);
        lv_obj_clear_flag(modal_power_manager, LV_OBJ_FLAG_HIDDEN);
    }
}

void modal_power_close() {
    if (modal_power_manager) {
        lv_obj_add_flag(modal_power_manager, LV_OBJ_FLAG_HIDDEN);
    }
}

void modal_power_update_battery(uint32_t bat_mv) {
    if (lbl_power_voltage) {
        char volt_str[24];
        snprintf(volt_str, sizeof(volt_str), "%.2f V", bat_mv / 1000.0f);
        lv_label_set_text(lbl_power_voltage, volt_str);
    }
}

bool modal_power_is_open() {
    return (modal_power_manager && !lv_obj_has_flag(modal_power_manager, LV_OBJ_FLAG_HIDDEN));
}
