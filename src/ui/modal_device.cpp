#include "ui/modal_device.h"
#include "ui/ui_theme.h"
#include "config.h"

// External Queue for sending UI Commands to Network Task
extern QueueHandle_t xQueueUiCmd;

// UI Widgets - Device Selector Modal
static lv_obj_t* modal_device_selector = nullptr;
static lv_obj_t* list_devices = nullptr;
static lv_obj_t* lbl_scan_status = nullptr;
static lv_obj_t* btn_rescan = nullptr;

static DeviceList current_device_list;
static OnDeviceSelectedCb s_on_device_selected = nullptr;

static void format_scan_status(char* buf, size_t len, uint8_t count) {
    if (count == 1) snprintf(buf, len, "1 streamer found");
    else snprintf(buf, len, "%d streamers found", count);
}

static void event_btn_close_modal(lv_event_t* e) {
    modal_device_close();
}

static void event_btn_rescan(lv_event_t* e) {
    UiCommand cmd;
    cmd.type = CMD_TRIGGER_RESCAN;
    xQueueSend(xQueueUiCmd, &cmd, 0);
    if (lbl_scan_status) {
        lv_label_set_text(lbl_scan_status, "Scanning for streamers...");
        lv_obj_set_style_text_color(lbl_scan_status, COLOR_ACCENT, 0);
    }
}

static void event_device_item_clicked(lv_event_t* e) {
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    if (idx < current_device_list.count) {
        UiCommand cmd;
        cmd.type = CMD_SELECT_DEVICE;
        strncpy(cmd.data.device_ip, current_device_list.devices[idx].ip, sizeof(cmd.data.device_ip) - 1);
        cmd.data.device_ip[sizeof(cmd.data.device_ip) - 1] = '\0';
        xQueueSend(xQueueUiCmd, &cmd, 0);

        if (s_on_device_selected) {
            s_on_device_selected(current_device_list.devices[idx]);
        }
    }
    modal_device_close();
}

void modal_device_init(lv_obj_t* parent) {
    modal_device_selector = lv_obj_create(parent ? parent : lv_scr_act());
    lv_obj_set_size(modal_device_selector, 240, 320);
    lv_obj_align(modal_device_selector, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(modal_device_selector, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(modal_device_selector, LV_OPA_80, 0);
    lv_obj_set_style_radius(modal_device_selector, 0, 0);
    lv_obj_set_style_border_side(modal_device_selector, LV_BORDER_SIDE_NONE, 0);
    lv_obj_set_style_pad_all(modal_device_selector, 10, 0);
    lv_obj_add_flag(modal_device_selector, LV_OBJ_FLAG_HIDDEN); // Hidden by default

    // Modal Card
    lv_obj_t* card = lv_obj_create(modal_device_selector);
    lv_obj_set_size(card, 220, 280);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, COLOR_SURFACE, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_color(card, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 8, 0);

    // Title
    lv_obj_t* lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, "Select WiiM Streamer");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 0);

    // Scan Status
    lbl_scan_status = lv_label_create(card);
    lv_label_set_text(lbl_scan_status, "Ready");
    lv_obj_set_style_text_font(lbl_scan_status, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_scan_status, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_scan_status, LV_ALIGN_TOP_MID, 0, 20);

    // Device List
    list_devices = lv_list_create(card);
    lv_obj_set_size(list_devices, 204, 170);
    lv_obj_align(list_devices, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_bg_color(list_devices, COLOR_BG, 0);
    lv_obj_set_style_border_side(list_devices, LV_BORDER_SIDE_NONE, 0);
    lv_obj_set_style_radius(list_devices, 8, 0);

    // Bottom Action Buttons: Rescan & Close
    btn_rescan = lv_btn_create(card);
    lv_obj_set_size(btn_rescan, 96, 32);
    lv_obj_align(btn_rescan, LV_ALIGN_BOTTOM_LEFT, 2, 0);
    lv_obj_set_style_bg_color(btn_rescan, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_radius(btn_rescan, 8, 0);
    lv_obj_add_event_cb(btn_rescan, event_btn_rescan, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lbl_rescan_btn = lv_label_create(btn_rescan);
    lv_label_set_text(lbl_rescan_btn, LV_SYMBOL_REFRESH " Rescan");
    lv_obj_set_style_text_font(lbl_rescan_btn, &lv_font_montserrat_10, 0);
    lv_obj_center(lbl_rescan_btn);

    lv_obj_t* btn_close = lv_btn_create(card);
    lv_obj_set_size(btn_close, 96, 32);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_RIGHT, -2, 0);
    lv_obj_set_style_bg_color(btn_close, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(btn_close, 8, 0);
    lv_obj_add_event_cb(btn_close, event_btn_close_modal, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lbl_close_btn = lv_label_create(btn_close);
    lv_label_set_text(lbl_close_btn, "Close");
    lv_obj_set_style_text_color(lbl_close_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_close_btn, &lv_font_montserrat_10, 0);
    lv_obj_center(lbl_close_btn);
}

void modal_device_open() {
    if (lbl_scan_status) {
        char status[32];
        format_scan_status(status, sizeof(status), current_device_list.count);
        lv_label_set_text(lbl_scan_status, status);
        lv_obj_set_style_text_color(lbl_scan_status, COLOR_TEXT_MUTED, 0);
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

    if (list_devices) {
        lv_obj_clean(list_devices);
        for (uint8_t i = 0; i < list.count; ++i) {
            char item_text[96];
            if (list.devices[i].is_active) {
                snprintf(item_text, sizeof(item_text), LV_SYMBOL_OK " %s\n   %s", list.devices[i].name, list.devices[i].ip);
            } else {
                snprintf(item_text, sizeof(item_text), "   %s\n   %s", list.devices[i].name, list.devices[i].ip);
            }

            lv_obj_t* btn = lv_list_add_btn(list_devices, nullptr, item_text);
            lv_obj_set_style_bg_color(btn, list.devices[i].is_active ? COLOR_SURFACE_LIGHT : COLOR_SURFACE, 0);
            lv_obj_set_style_text_color(btn, list.devices[i].is_active ? COLOR_ACCENT : COLOR_TEXT_PRIMARY, 0);
            lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, 0);
            lv_obj_set_style_radius(btn, 6, 0);
            lv_obj_set_style_pad_bottom(btn, 4, 0);

            lv_obj_add_event_cb(btn, event_device_item_clicked, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
        }
    }

    if (lbl_scan_status) {
        char status[32];
        format_scan_status(status, sizeof(status), list.count);
        lv_label_set_text(lbl_scan_status, status);
        lv_obj_set_style_text_color(lbl_scan_status, COLOR_TEXT_MUTED, 0);
    }
}

void modal_device_set_scanning(bool is_scanning) {
    if (lbl_scan_status) {
        if (is_scanning) {
            lv_label_set_text(lbl_scan_status, "Scanning for streamers...");
            lv_obj_set_style_text_color(lbl_scan_status, COLOR_ACCENT, 0);
        } else {
            char status[32];
            format_scan_status(status, sizeof(status), current_device_list.count);
            lv_label_set_text(lbl_scan_status, status);
            lv_obj_set_style_text_color(lbl_scan_status, COLOR_TEXT_MUTED, 0);
        }
    }
}

void modal_device_set_on_selected(OnDeviceSelectedCb cb) {
    s_on_device_selected = cb;
}

bool modal_device_is_open() {
    return (modal_device_selector && !lv_obj_has_flag(modal_device_selector, LV_OBJ_FLAG_HIDDEN));
}
