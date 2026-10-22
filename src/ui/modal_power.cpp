#include "ui/modal_power.h"
#include "ui/ui_theme.h"
#include "ui.h"
#include "power_manager.h"
#include "config.h"
#include <stdio.h>

static lv_obj_t* modal_power_manager = nullptr;
static lv_obj_t* s_cont_header = nullptr;
static lv_obj_t* s_lbl_back_icon = nullptr;
static lv_obj_t* s_lbl_title = nullptr;
static lv_obj_t* list_power = nullptr;
static lv_obj_t* s_btn_volt = nullptr;
static lv_obj_t* s_btn_theme = nullptr;
static lv_obj_t* s_btn_rotate = nullptr;
static lv_obj_t* s_btn_bright = nullptr;
static lv_obj_t* s_btn_dim = nullptr;
static lv_obj_t* s_btn_sleep = nullptr;
static lv_obj_t* s_btn_sleep_now = nullptr;

static lv_obj_t* lbl_row_voltage = nullptr;
static lv_obj_t* lbl_row_theme = nullptr;
static lv_obj_t* lbl_row_rotate = nullptr;
static lv_obj_t* lbl_row_brightness = nullptr;
static lv_obj_t* lbl_row_dim = nullptr;
static lv_obj_t* lbl_row_sleep = nullptr;

static const uint8_t bright_pct_lut[] = { 20, 40, 60, 80, 100 };
static const uint16_t dim_seconds_lut[] = { 15, 30, 60, 120, 300, 0 };
static const char* const dim_names_lut[] = {
    "15 seconds", "30 seconds", "1 minute", "2 minutes", "5 minutes", "Never"
};
static const uint16_t sleep_seconds_lut[] = { 30, 60, 120, 300, 600, 0 };
static const char* const sleep_names_lut[] = {
    "30 seconds", "1 minute", "2 minutes", "5 minutes", "10 minutes", "Never"
};

static uint8_t s_bright_idx = 3; // Default 80%
static uint8_t s_dim_idx = 1;    // Default 30s
static uint8_t s_sleep_idx = 1;  // Default 1m

static void refresh_row_labels() {
    if (lbl_row_theme) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Theme: %s", ui_theme_get().name);
        lv_label_set_text(lbl_row_theme, buf);
    }
    if (lbl_row_rotate) {
        lv_label_set_text(lbl_row_rotate, power_manager_get_rotation_180()
            ? "Rotation: Flipped (180\xC2\xB0)"
            : "Rotation: Standard (0\xC2\xB0)");
    }
    if (lbl_row_brightness) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Brightness: %u%%", bright_pct_lut[s_bright_idx]);
        lv_label_set_text(lbl_row_brightness, buf);
    }
    if (lbl_row_dim) {
        char buf[40];
        snprintf(buf, sizeof(buf), "Auto-Dim: %s", dim_names_lut[s_dim_idx]);
        lv_label_set_text(lbl_row_dim, buf);
    }
    if (lbl_row_sleep) {
        char buf[40];
        snprintf(buf, sizeof(buf), "Auto-Sleep: %s", sleep_names_lut[s_sleep_idx]);
        lv_label_set_text(lbl_row_sleep, buf);
    }
}

static void event_btn_close_power_modal(lv_event_t* e) {
    modal_power_close();
}

static void event_row_theme(lv_event_t* e) {
    uint8_t next_idx = (ui_theme_get_idx() + 1) % kThemeCount;
    ui_theme_set_idx(next_idx);
    power_manager_set_theme_idx(next_idx);
    ui_apply_theme();
    refresh_row_labels();
}

static void event_row_rotate(lv_event_t* e) {
    power_manager_set_rotation_180(!power_manager_get_rotation_180());
    refresh_row_labels();
}

static void event_row_brightness(lv_event_t* e) {
    s_bright_idx = (s_bright_idx + 1) % (sizeof(bright_pct_lut) / sizeof(bright_pct_lut[0]));
    power_manager_set_brightness_pct(bright_pct_lut[s_bright_idx]);
    refresh_row_labels();
}

static void event_row_dim(lv_event_t* e) {
    s_dim_idx = (s_dim_idx + 1) % (sizeof(dim_seconds_lut) / sizeof(dim_seconds_lut[0]));
    power_manager_set_dim_timeout_sec(dim_seconds_lut[s_dim_idx]);
    refresh_row_labels();
}

static void event_row_sleep(lv_event_t* e) {
    s_sleep_idx = (s_sleep_idx + 1) % (sizeof(sleep_seconds_lut) / sizeof(sleep_seconds_lut[0]));
    power_manager_set_sleep_timeout_sec(sleep_seconds_lut[s_sleep_idx]);
    refresh_row_labels();
}

static void event_row_sleep_now(lv_event_t* e) {
    power_manager_enter_deep_sleep();
}

void modal_power_init(lv_obj_t* parent) {
    modal_power_manager = lv_obj_create(parent ? parent : lv_scr_act());
    lv_obj_set_size(modal_power_manager, 240, 320);
    lv_obj_align(modal_power_manager, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(modal_power_manager, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(modal_power_manager, 0, 0);
    lv_obj_set_style_border_side(modal_power_manager, LV_BORDER_SIDE_NONE, 0);
    lv_obj_set_style_pad_all(modal_power_manager, 0, 0);
    lv_obj_clear_flag(modal_power_manager, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(modal_power_manager, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);

    // Full-Width Clickable Header Bar (240 x 32 px)
    s_cont_header = lv_obj_create(modal_power_manager);
    lv_obj_set_size(s_cont_header, 240, 32);
    lv_obj_align(s_cont_header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_border_side(s_cont_header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(s_cont_header, 1, 0);
    lv_obj_set_style_radius(s_cont_header, 0, 0);
    lv_obj_set_style_pad_all(s_cont_header, 0, 0);
    lv_obj_clear_flag(s_cont_header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cont_header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_cont_header, event_btn_close_power_modal, LV_EVENT_CLICKED, nullptr);

    s_lbl_back_icon = lv_label_create(s_cont_header);
    lv_label_set_text(s_lbl_back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(s_lbl_back_icon, &lv_font_montserrat_12, 0);
    lv_obj_align(s_lbl_back_icon, LV_ALIGN_LEFT_MID, 10, 0);

    s_lbl_title = lv_label_create(s_cont_header);
    lv_label_set_text(s_lbl_title, "Power & Display");
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_montserrat_12, 0);
    lv_obj_align(s_lbl_title, LV_ALIGN_LEFT_MID, 32, 0);

    // Flat Edge-to-Edge List Container (240 x 288 px)
    list_power = lv_list_create(modal_power_manager);
    lv_obj_set_size(list_power, 240, 288);
    lv_obj_align(list_power, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_border_side(list_power, LV_BORDER_SIDE_NONE, 0);
    lv_obj_set_style_radius(list_power, 0, 0);
    lv_obj_set_style_pad_all(list_power, 0, 0);
    lv_obj_set_style_pad_row(list_power, 0, 0);

    // Row 1: Battery Voltage (status row)
    s_btn_volt = lv_list_add_btn(list_power, LV_SYMBOL_BATTERY_FULL, "Battery Voltage: -- V");
    lbl_row_voltage = lv_obj_get_child(s_btn_volt, 1);
    lv_obj_clear_flag(s_btn_volt, LV_OBJ_FLAG_CLICKABLE);

    // Row 2: Color Theme (tap to cycle across kThemes[])
    s_btn_theme = lv_list_add_btn(list_power, LV_SYMBOL_IMAGE, "Theme: Cyber Cyan");
    lbl_row_theme = lv_obj_get_child(s_btn_theme, 1);
    lv_obj_add_event_cb(s_btn_theme, event_row_theme, LV_EVENT_CLICKED, nullptr);

    // Row 3: Screen Orientation (tap to toggle 0° / 180°)
    s_btn_rotate = lv_list_add_btn(list_power, LV_SYMBOL_REFRESH, "Rotation: Standard (0\xC2\xB0)");
    lbl_row_rotate = lv_obj_get_child(s_btn_rotate, 1);
    lv_obj_add_event_cb(s_btn_rotate, event_row_rotate, LV_EVENT_CLICKED, nullptr);

    // Row 4: Screen Brightness (tap to cycle 20%..100%)
    s_btn_bright = lv_list_add_btn(list_power, LV_SYMBOL_SETTINGS, "Brightness: 80%");
    lbl_row_brightness = lv_obj_get_child(s_btn_bright, 1);
    lv_obj_add_event_cb(s_btn_bright, event_row_brightness, LV_EVENT_CLICKED, nullptr);

    // Row 5: Screen Auto-Dim (tap to cycle timeout)
    s_btn_dim = lv_list_add_btn(list_power, LV_SYMBOL_EYE_CLOSE, "Auto-Dim: 30 seconds");
    lbl_row_dim = lv_obj_get_child(s_btn_dim, 1);
    lv_obj_add_event_cb(s_btn_dim, event_row_dim, LV_EVENT_CLICKED, nullptr);

    // Row 6: Auto Deep Sleep (tap to cycle timeout)
    s_btn_sleep = lv_list_add_btn(list_power, LV_SYMBOL_LOOP, "Auto-Sleep: 1 minute");
    lbl_row_sleep = lv_obj_get_child(s_btn_sleep, 1);
    lv_obj_add_event_cb(s_btn_sleep, event_row_sleep, LV_EVENT_CLICKED, nullptr);

    // Row 7: Enter Deep Sleep Now
    s_btn_sleep_now = lv_list_add_btn(list_power, LV_SYMBOL_POWER, "Enter Deep Sleep Now");
    lv_obj_add_event_cb(s_btn_sleep_now, event_row_sleep_now, LV_EVENT_RELEASED, nullptr);

    refresh_row_labels();
}

void modal_power_apply_theme() {
    if (modal_power_manager) lv_obj_set_style_bg_color(modal_power_manager, COLOR_BG, 0);
    if (s_cont_header) {
        lv_obj_set_style_bg_color(s_cont_header, COLOR_SURFACE, 0);
        lv_obj_set_style_bg_color(s_cont_header, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(s_cont_header, COLOR_SURFACE_LIGHT, 0);
    }
    if (s_lbl_back_icon) lv_obj_set_style_text_color(s_lbl_back_icon, COLOR_ACCENT, 0);
    if (s_lbl_title) lv_obj_set_style_text_color(s_lbl_title, COLOR_ACCENT, 0);
    if (list_power) lv_obj_set_style_bg_color(list_power, COLOR_BG, 0);

    if (s_btn_volt) ui_style_flat_list_btn(s_btn_volt, COLOR_ACCENT);
    if (s_btn_theme) ui_style_flat_list_btn(s_btn_theme, COLOR_ACCENT);
    if (s_btn_rotate) ui_style_flat_list_btn(s_btn_rotate, COLOR_ACCENT);
    if (s_btn_bright) ui_style_flat_list_btn(s_btn_bright, COLOR_ACCENT);
    if (s_btn_dim) ui_style_flat_list_btn(s_btn_dim, COLOR_ACCENT);
    if (s_btn_sleep) ui_style_flat_list_btn(s_btn_sleep, COLOR_ACCENT);
    if (s_btn_sleep_now) ui_style_flat_list_btn(s_btn_sleep_now, lv_color_hex(0xFF6B6B), lv_color_hex(0xFF6B6B));
}

void modal_power_open(uint32_t bat_mv) {
    if (bat_mv > 0) {
        modal_power_update_battery(bat_mv);
    }

    uint8_t cur_bright = power_manager_get_brightness_pct();
    s_bright_idx = 3;
    uint8_t best_diff = 255;
    for (uint8_t i = 0; i < sizeof(bright_pct_lut) / sizeof(bright_pct_lut[0]); ++i) {
        uint8_t diff = (cur_bright > bright_pct_lut[i]) ? (cur_bright - bright_pct_lut[i]) : (bright_pct_lut[i] - cur_bright);
        if (diff < best_diff) {
            best_diff = diff;
            s_bright_idx = i;
        }
    }

    uint16_t cur_dim = power_manager_get_dim_timeout_sec();
    s_dim_idx = 1;
    for (uint8_t i = 0; i < sizeof(dim_seconds_lut) / sizeof(dim_seconds_lut[0]); ++i) {
        if (dim_seconds_lut[i] == cur_dim) {
            s_dim_idx = i;
            break;
        }
    }

    uint16_t cur_sleep = power_manager_get_sleep_timeout_sec();
    s_sleep_idx = 1;
    for (uint8_t i = 0; i < sizeof(sleep_seconds_lut) / sizeof(sleep_seconds_lut[0]); ++i) {
        if (sleep_seconds_lut[i] == cur_sleep) {
            s_sleep_idx = i;
            break;
        }
    }

    refresh_row_labels();

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
    if (lbl_row_voltage) {
        char volt_str[32];
        snprintf(volt_str, sizeof(volt_str), "Battery Voltage: %.2f V", bat_mv / 1000.0f);
        lv_label_set_text(lbl_row_voltage, volt_str);
    }
}

bool modal_power_is_open() {
    return (modal_power_manager && !lv_obj_has_flag(modal_power_manager, LV_OBJ_FLAG_HIDDEN));
}
