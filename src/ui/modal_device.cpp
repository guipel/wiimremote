#include "ui/modal_device.h"
#include "ui/ui_theme.h"
#include "config.h"
#include <stdio.h>

extern QueueHandle_t xQueueUiCmd;

static lv_obj_t* modal_device_selector = nullptr;
static lv_obj_t* s_cont_header = nullptr;
static lv_obj_t* s_cont_back = nullptr;
static lv_obj_t* s_lbl_back_icon = nullptr;
static lv_obj_t* s_btn_rescan = nullptr;
static lv_obj_t* s_lbl_rescan_icon = nullptr;
static lv_obj_t* list_devices = nullptr;
static lv_obj_t* lbl_header_title = nullptr;

static DeviceList current_device_list;
static OnDeviceSelectedCb s_on_device_selected = nullptr;

static void show_status_message(const char* msg) {
    if (!list_devices) return;
    lv_obj_clean(list_devices);
    lv_obj_scroll_to_y(list_devices, 0, LV_ANIM_OFF);

    lv_obj_t* lbl = lv_label_create(list_devices);
    lv_label_set_text(lbl, msg);
    lv_obj_set_width(lbl, 224);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(lbl, 40, 0);
    lv_obj_set_style_pad_left(lbl, 8, 0);
}

static void event_btn_close_modal(lv_event_t* e) {
    modal_device_close();
}

static void event_btn_rescan(lv_event_t* e) {
    UiCommand cmd;
    cmd.type = CMD_TRIGGER_RESCAN;
    if (xQueueSend(xQueueUiCmd, &cmd, pdMS_TO_TICKS(20)) == pdTRUE && lbl_header_title) {
        lv_label_set_text(lbl_header_title, "Scanning streamers...");
    }
}

static void event_device_item_clicked(lv_event_t* e) {
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    if (idx < current_device_list.count) {
        UiCommand cmd;
        cmd.type = CMD_SELECT_DEVICE;
        strncpy(cmd.data.device_ip, current_device_list.devices[idx].ip, sizeof(cmd.data.device_ip) - 1);
        cmd.data.device_ip[sizeof(cmd.data.device_ip) - 1] = '\0';
        if (xQueueSend(xQueueUiCmd, &cmd, pdMS_TO_TICKS(20)) != pdTRUE) return;

        if (s_on_device_selected) {
            s_on_device_selected(current_device_list.devices[idx]);
        }
    }
    modal_device_close();
}

void modal_device_init(lv_obj_t* parent) {
    modal_device_selector = lv_obj_create(parent ? parent : lv_scr_act());
    lv_obj_set_size(modal_device_selector, 240, 320);
    lv_obj_align(modal_device_selector, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(modal_device_selector, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(modal_device_selector, 0, 0);
    lv_obj_set_style_border_side(modal_device_selector, LV_BORDER_SIDE_NONE, 0);
    lv_obj_set_style_pad_all(modal_device_selector, 0, 0);
    lv_obj_clear_flag(modal_device_selector, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(modal_device_selector, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);

    // Header Bar (240 x 32 px)
    s_cont_header = lv_obj_create(modal_device_selector);
    lv_obj_set_size(s_cont_header, 240, 32);
    lv_obj_align(s_cont_header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_border_side(s_cont_header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(s_cont_header, 1, 0);
    lv_obj_set_style_radius(s_cont_header, 0, 0);
    lv_obj_set_style_pad_all(s_cont_header, 0, 0);
    lv_obj_clear_flag(s_cont_header, LV_OBJ_FLAG_SCROLLABLE);

    // Left Clickable Back + Title Area (196 x 31 px)
    s_cont_back = lv_obj_create(s_cont_header);
    lv_obj_set_size(s_cont_back, 196, 31);
    lv_obj_align(s_cont_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_border_side(s_cont_back, LV_BORDER_SIDE_NONE, 0);
    lv_obj_set_style_radius(s_cont_back, 0, 0);
    lv_obj_set_style_pad_all(s_cont_back, 0, 0);
    lv_obj_clear_flag(s_cont_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cont_back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_cont_back, event_btn_close_modal, LV_EVENT_CLICKED, nullptr);

    s_lbl_back_icon = lv_label_create(s_cont_back);
    lv_label_set_text(s_lbl_back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(s_lbl_back_icon, &lv_font_montserrat_12, 0);
    lv_obj_align(s_lbl_back_icon, LV_ALIGN_LEFT_MID, 10, 0);

    lbl_header_title = lv_label_create(s_cont_back);
    lv_label_set_text(lbl_header_title, "Select WiiM Streamer");
    lv_obj_set_style_text_font(lbl_header_title, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_header_title, LV_ALIGN_LEFT_MID, 32, 0);

    // Right Clickable Refresh Button (44 x 31 px)
    s_btn_rescan = lv_obj_create(s_cont_header);
    lv_obj_set_size(s_btn_rescan, 44, 31);
    lv_obj_align(s_btn_rescan, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_border_side(s_btn_rescan, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(s_btn_rescan, 1, 0);
    lv_obj_set_style_radius(s_btn_rescan, 0, 0);
    lv_obj_set_style_pad_all(s_btn_rescan, 0, 0);
    lv_obj_clear_flag(s_btn_rescan, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_btn_rescan, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_btn_rescan, event_btn_rescan, LV_EVENT_CLICKED, nullptr);

    s_lbl_rescan_icon = lv_label_create(s_btn_rescan);
    lv_label_set_text(s_lbl_rescan_icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(s_lbl_rescan_icon, &lv_font_montserrat_12, 0);
    lv_obj_center(s_lbl_rescan_icon);

    // Flat Edge-to-Edge List Container (240 x 288 px)
    list_devices = lv_list_create(modal_device_selector);
    lv_obj_set_size(list_devices, 240, 288);
    lv_obj_align(list_devices, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_border_side(list_devices, LV_BORDER_SIDE_NONE, 0);
    lv_obj_set_style_radius(list_devices, 0, 0);
    lv_obj_set_style_pad_all(list_devices, 0, 0);
    lv_obj_set_style_pad_row(list_devices, 0, 0);
}

void modal_device_apply_theme() {
    if (modal_device_selector) lv_obj_set_style_bg_color(modal_device_selector, COLOR_BG, 0);
    if (s_cont_header) {
        lv_obj_set_style_bg_color(s_cont_header, COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(s_cont_header, COLOR_SURFACE_LIGHT, 0);
    }
    if (s_cont_back) {
        lv_obj_set_style_bg_color(s_cont_back, COLOR_SURFACE, 0);
        lv_obj_set_style_bg_color(s_cont_back, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
    }
    if (s_lbl_back_icon) lv_obj_set_style_text_color(s_lbl_back_icon, COLOR_ACCENT, 0);
    if (lbl_header_title) lv_obj_set_style_text_color(lbl_header_title, COLOR_ACCENT, 0);
    if (s_btn_rescan) {
        lv_obj_set_style_bg_color(s_btn_rescan, COLOR_SURFACE, 0);
        lv_obj_set_style_bg_color(s_btn_rescan, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(s_btn_rescan, COLOR_SURFACE_LIGHT, 0);
    }
    if (s_lbl_rescan_icon) lv_obj_set_style_text_color(s_lbl_rescan_icon, COLOR_ACCENT, 0);
    if (list_devices) lv_obj_set_style_bg_color(list_devices, COLOR_BG, 0);

    modal_device_set_list(current_device_list);
}

void modal_device_open() {
    if (lbl_header_title) {
        lv_label_set_text(lbl_header_title, "Select WiiM Streamer");
    }
    if (modal_device_selector) {
        lv_obj_move_to_index(modal_device_selector, -1);
        lv_obj_clear_flag(modal_device_selector, LV_OBJ_FLAG_HIDDEN);
    }
}

void modal_device_close() {
    if (modal_device_selector) {
        lv_obj_add_flag(modal_device_selector, LV_OBJ_FLAG_HIDDEN);
    }
}

void modal_device_set_list(const DeviceList& list) {
    current_device_list = list;
    if (!list_devices) return;
    lv_obj_clean(list_devices);

    if (list.count == 0) {
        show_status_message("No WiiM streamers found.\nTap refresh to scan.");
        return;
    }

    for (uint8_t i = 0; i < list.count; ++i) {
        char item_text[104];
        snprintf(item_text, sizeof(item_text), "%s (%s)", list.devices[i].name, list.devices[i].ip);
        bool active = list.devices[i].is_active;

        lv_obj_t* btn = lv_list_add_btn(
            list_devices,
            active ? LV_SYMBOL_OK : LV_SYMBOL_AUDIO,
            item_text
        );
        ui_style_flat_list_btn(
            btn,
            active ? COLOR_ACCENT : COLOR_TEXT_MUTED,
            active ? COLOR_ACCENT : COLOR_TEXT_PRIMARY
        );
        lv_obj_add_event_cb(btn, event_device_item_clicked, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }
}

void modal_device_set_scanning(bool is_scanning) {
    if (lbl_header_title) {
        lv_label_set_text(lbl_header_title, is_scanning ? "Scanning streamers..." : "Select WiiM Streamer");
    }
}

void modal_device_set_on_selected(OnDeviceSelectedCb cb) {
    s_on_device_selected = cb;
}

bool modal_device_is_open() {
    return (modal_device_selector && !lv_obj_has_flag(modal_device_selector, LV_OBJ_FLAG_HIDDEN));
}
