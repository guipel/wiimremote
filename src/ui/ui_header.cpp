#include "ui/ui_header.h"
#include "ui/ui_theme.h"
#include "ui/modal_wifi.h"
#include "ui/modal_device.h"
#include "ui/modal_power.h"
#include "config.h"

// UI Widgets - Top Header
static lv_obj_t* header_cont = nullptr;
static lv_obj_t* btn_wifi_select = nullptr;
static lv_obj_t* lbl_wifi = nullptr;
static lv_obj_t* btn_device_select = nullptr;
static lv_obj_t* lbl_active_device = nullptr;
static lv_obj_t* btn_battery = nullptr;
static lv_obj_t* lbl_bat_icon = nullptr;

static uint32_t s_last_bat_mv = 0;

static void event_btn_open_wifi_modal(lv_event_t* e) {
    modal_wifi_open();
}

static void event_btn_open_device_modal(lv_event_t* e) {
    modal_device_open();
}

static void event_btn_open_power_modal(lv_event_t* e) {
    modal_power_open(s_last_bat_mv);
}

void ui_header_init(lv_obj_t* parent) {
    header_cont = lv_obj_create(parent);
    lv_obj_set_size(header_cont, 240, 26);
    lv_obj_align(header_cont, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header_cont, COLOR_SURFACE, 0);
    lv_obj_set_style_border_side(header_cont, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header_cont, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_width(header_cont, 1, 0);
    lv_obj_set_style_radius(header_cont, 0, 0);
    lv_obj_set_style_pad_all(header_cont, 2, 0);
    lv_obj_clear_flag(header_cont, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Wi-Fi Button & Action Chevron (Left, ~38px)
    btn_wifi_select = lv_btn_create(header_cont);
    lv_obj_set_size(btn_wifi_select, 38, 22);
    lv_obj_align(btn_wifi_select, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_bg_opa(btn_wifi_select, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_wifi_select, 0, 0);
    lv_obj_set_style_border_width(btn_wifi_select, 0, 0);
    lv_obj_set_style_pad_all(btn_wifi_select, 0, 0);
    lv_obj_add_event_cb(btn_wifi_select, event_btn_open_wifi_modal, LV_EVENT_CLICKED, nullptr);

    lbl_wifi = lv_label_create(btn_wifi_select);
    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI " " LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(lbl_wifi, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_wifi, LV_ALIGN_LEFT_MID, 2, 0);

    // 2. Battery Button & Action Chevron (Right, ~38px)
    btn_battery = lv_btn_create(header_cont);
    lv_obj_set_size(btn_battery, 38, 22);
    lv_obj_align(btn_battery, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_opa(btn_battery, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_battery, 0, 0);
    lv_obj_set_style_border_width(btn_battery, 0, 0);
    lv_obj_set_style_pad_all(btn_battery, 0, 0);
    lv_obj_add_event_cb(btn_battery, event_btn_open_power_modal, LV_EVENT_CLICKED, nullptr);

    lbl_bat_icon = lv_label_create(btn_battery);
    lv_label_set_text(lbl_bat_icon, LV_SYMBOL_BATTERY_FULL " " LV_SYMBOL_DOWN);
    lv_obj_set_style_text_font(lbl_bat_icon, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_bat_icon, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(lbl_bat_icon, LV_ALIGN_RIGHT_MID, 0, 0);

    // 3. Active Device Button (Center, ~156px)
    btn_device_select = lv_btn_create(header_cont);
    lv_obj_set_size(btn_device_select, 156, 22);
    lv_obj_align(btn_device_select, LV_ALIGN_LEFT_MID, 42, 0);
    lv_obj_set_style_bg_opa(btn_device_select, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_device_select, 0, 0);
    lv_obj_set_style_border_width(btn_device_select, 0, 0);
    lv_obj_set_style_pad_all(btn_device_select, 0, 0);
    lv_obj_add_event_cb(btn_device_select, event_btn_open_device_modal, LV_EVENT_CLICKED, nullptr);

    lbl_active_device = lv_label_create(btn_device_select);
    lv_label_set_text(lbl_active_device, "Searching... " LV_SYMBOL_DOWN);
    lv_label_set_long_mode(lbl_active_device, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_active_device, 150);
    lv_obj_set_style_text_color(lbl_active_device, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_active_device, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(lbl_active_device, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_active_device, LV_ALIGN_CENTER, 0, 0);
}

void ui_header_set_wifi_status(bool connected, int8_t rssi) {
    if (!lbl_wifi) return;
    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI " " LV_SYMBOL_DOWN);
    if (connected) {
        if (rssi >= -65) {
            lv_obj_set_style_text_color(lbl_wifi, COLOR_SUCCESS, 0); // Green (Strong)
        } else if (rssi >= -75) {
            lv_obj_set_style_text_color(lbl_wifi, COLOR_WARNING, 0); // Amber (Fair)
        } else {
            lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0xE63946), 0); // Red (Weak)
        }
    } else {
        lv_obj_set_style_text_color(lbl_wifi, COLOR_TEXT_MUTED, 0); // Disconnected
    }
}

void ui_header_set_active_device(const char* name) {
    if (!lbl_active_device) return;
    if (name && strlen(name) > 0) {
        char title[64];
        snprintf(title, sizeof(title), "%s " LV_SYMBOL_DOWN, name);
        lv_label_set_text(lbl_active_device, title);
    } else {
        lv_label_set_text(lbl_active_device, "No Devices " LV_SYMBOL_DOWN);
    }
}

void ui_header_update_battery(uint32_t bat_mv) {
    s_last_bat_mv = bat_mv;

    const char* symbol = LV_SYMBOL_BATTERY_FULL;
    lv_color_t color = COLOR_TEXT_PRIMARY;

    if (bat_mv >= BAT_VOLT_CHARGE_MV) {
        symbol = LV_SYMBOL_CHARGE;
        color = COLOR_ACCENT;
    } else if (bat_mv >= BAT_VOLT_FULL_MV) {
        symbol = LV_SYMBOL_BATTERY_FULL;
        color = COLOR_TEXT_PRIMARY;
    } else if (bat_mv >= BAT_VOLT_HIGH_MV) {
        symbol = LV_SYMBOL_BATTERY_3;
        color = COLOR_TEXT_PRIMARY;
    } else if (bat_mv >= BAT_VOLT_MED_MV) {
        symbol = LV_SYMBOL_BATTERY_2;
        color = COLOR_TEXT_PRIMARY;
    } else if (bat_mv >= BAT_VOLT_LOW_MV) {
        symbol = LV_SYMBOL_BATTERY_1;
        color = COLOR_WARNING;
    } else {
        symbol = LV_SYMBOL_BATTERY_EMPTY;
        color = lv_color_hex(0xEF4444);
    }

    if (lbl_bat_icon) {
        char icon_text[20];
        snprintf(icon_text, sizeof(icon_text), "%s " LV_SYMBOL_DOWN, symbol);
        lv_label_set_text(lbl_bat_icon, icon_text);
        lv_obj_set_style_text_color(lbl_bat_icon, color, 0);
    }
}
