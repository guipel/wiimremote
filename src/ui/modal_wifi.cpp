#include "ui/modal_wifi.h"
#include "ui/ui_theme.h"
#include "config.h"

// External Queue for sending UI Commands to Network Task
extern QueueHandle_t xQueueUiCmd;

// UI Widgets - Wi-Fi Configuration Modal
static lv_obj_t* modal_wifi_selector = nullptr;
static lv_obj_t* wifi_view_list = nullptr;
static lv_obj_t* wifi_view_pass = nullptr;
static lv_obj_t* wifi_view_status = nullptr;
static lv_obj_t* list_wifi = nullptr;
static lv_obj_t* lbl_wifi_scan_status = nullptr;
static lv_obj_t* lbl_wifi_pass_title = nullptr;
static lv_obj_t* ta_wifi_pass = nullptr;
static lv_obj_t* kb_wifi = nullptr;
static lv_obj_t* lbl_wifi_connecting_msg = nullptr;
static lv_obj_t* spinner_wifi = nullptr;
static lv_obj_t* btn_wifi_status_back = nullptr;
static lv_obj_t* box_wifi_connected = nullptr;
static lv_obj_t* lbl_wifi_curr_ssid = nullptr;
static lv_obj_t* btn_wifi_forget = nullptr;

static WiFiScanList current_wifi_scan_list;
static char selected_wifi_ssid[33] = "";
static char current_connected_ssid[33] = "";
static bool current_wifi_connected = false;
static lv_timer_t* timer_wifi_close = nullptr;

static void timer_close_wifi_modal_cb(lv_timer_t* timer) {
    if (modal_wifi_selector) {
        lv_obj_add_flag(modal_wifi_selector, LV_OBJ_FLAG_HIDDEN);
    }
    timer_wifi_close = nullptr;
}

static void event_btn_close_wifi_modal(lv_event_t* e) {
    modal_wifi_close();
    UiCommand cmd;
    cmd.type = CMD_WIFI_RECONNECT;
    xQueueSend(xQueueUiCmd, &cmd, 0);
}

static void event_btn_wifi_forget(lv_event_t* e) {
    log_i("User tapped Forget Network");
    UiCommand cmd;
    cmd.type = CMD_WIFI_FORGET;
    xQueueSend(xQueueUiCmd, &cmd, 0);

    current_wifi_connected = false;
    current_connected_ssid[0] = '\0';
    if (box_wifi_connected) {
        lv_obj_add_flag(box_wifi_connected, LV_OBJ_FLAG_HIDDEN);
    }
    if (lbl_wifi_scan_status) {
        lv_label_set_text(lbl_wifi_scan_status, "Forgetting network & scanning...");
        lv_obj_set_style_text_color(lbl_wifi_scan_status, COLOR_ACCENT, 0);
    }
    if (list_wifi) {
        lv_obj_clean(list_wifi);
    }
}

static void event_btn_wifi_rescan(lv_event_t* e) {
    if (lbl_wifi_scan_status) {
        lv_label_set_text(lbl_wifi_scan_status, "Scanning for networks...");
        lv_obj_set_style_text_color(lbl_wifi_scan_status, COLOR_ACCENT, 0);
    }
    if (list_wifi) {
        lv_obj_clean(list_wifi);
    }
    UiCommand cmd;
    cmd.type = CMD_WIFI_START_SCAN;
    xQueueSend(xQueueUiCmd, &cmd, 0);
}

static void event_btn_wifi_eye_toggle(lv_event_t* e) {
    if (!ta_wifi_pass) return;
    bool cur = lv_textarea_get_password_mode(ta_wifi_pass);
    lv_textarea_set_password_mode(ta_wifi_pass, !cur);
    lv_obj_t* btn = lv_event_get_target(e);
    lv_obj_t* lbl = lv_obj_get_child(btn, 0);
    if (lbl) {
        lv_label_set_text(lbl, cur ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    }
}

static void event_btn_wifi_pass_back(lv_event_t* e) {
    if (wifi_view_pass) lv_obj_add_flag(wifi_view_pass, LV_OBJ_FLAG_HIDDEN);
    if (wifi_view_status) lv_obj_add_flag(wifi_view_status, LV_OBJ_FLAG_HIDDEN);
    if (wifi_view_list) lv_obj_clear_flag(wifi_view_list, LV_OBJ_FLAG_HIDDEN);
}

static void do_wifi_connect(const char* ssid, const char* pass) {
    if (!ssid || strlen(ssid) == 0) return;

    UiCommand cmd;
    cmd.type = CMD_WIFI_CONNECT;
    strncpy(cmd.data.wifi_connect.ssid, ssid, sizeof(cmd.data.wifi_connect.ssid) - 1);
    cmd.data.wifi_connect.ssid[sizeof(cmd.data.wifi_connect.ssid) - 1] = '\0';
    strncpy(cmd.data.wifi_connect.password, pass ? pass : "", sizeof(cmd.data.wifi_connect.password) - 1);
    cmd.data.wifi_connect.password[sizeof(cmd.data.wifi_connect.password) - 1] = '\0';
    xQueueSend(xQueueUiCmd, &cmd, 0);

    if (wifi_view_list) lv_obj_add_flag(wifi_view_list, LV_OBJ_FLAG_HIDDEN);
    if (wifi_view_pass) lv_obj_add_flag(wifi_view_pass, LV_OBJ_FLAG_HIDDEN);
    if (wifi_view_status) lv_obj_clear_flag(wifi_view_status, LV_OBJ_FLAG_HIDDEN);

    if (spinner_wifi) lv_obj_clear_flag(spinner_wifi, LV_OBJ_FLAG_HIDDEN);
    if (btn_wifi_status_back) lv_obj_add_flag(btn_wifi_status_back, LV_OBJ_FLAG_HIDDEN);
    if (lbl_wifi_connecting_msg) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Connecting to\n%s...", ssid);
        lv_label_set_text(lbl_wifi_connecting_msg, buf);
        lv_obj_set_style_text_color(lbl_wifi_connecting_msg, COLOR_TEXT_PRIMARY, 0);
    }
}

static void event_btn_wifi_pass_connect(lv_event_t* e) {
    const char* pass = ta_wifi_pass ? lv_textarea_get_text(ta_wifi_pass) : "";
    do_wifi_connect(selected_wifi_ssid, pass);
}

static void event_kb_wifi_ready(lv_event_t* e) {
    const char* pass = ta_wifi_pass ? lv_textarea_get_text(ta_wifi_pass) : "";
    do_wifi_connect(selected_wifi_ssid, pass);
}

static void event_wifi_item_clicked(lv_event_t* e) {
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    if (idx >= current_wifi_scan_list.count) return;

    strncpy(selected_wifi_ssid, current_wifi_scan_list.networks[idx].ssid, sizeof(selected_wifi_ssid) - 1);
    selected_wifi_ssid[sizeof(selected_wifi_ssid) - 1] = '\0';

    if (current_wifi_scan_list.networks[idx].is_open) {
        do_wifi_connect(selected_wifi_ssid, "");
    } else {
        if (lbl_wifi_pass_title) {
            char title[64];
            snprintf(title, sizeof(title), "Network: %s", selected_wifi_ssid);
            lv_label_set_text(lbl_wifi_pass_title, title);
        }
        if (ta_wifi_pass) {
            lv_textarea_set_text(ta_wifi_pass, "");
            lv_textarea_set_password_mode(ta_wifi_pass, true);
        }
        if (kb_wifi && ta_wifi_pass) {
            lv_keyboard_set_textarea(kb_wifi, ta_wifi_pass);
        }
        if (wifi_view_list) lv_obj_add_flag(wifi_view_list, LV_OBJ_FLAG_HIDDEN);
        if (wifi_view_status) lv_obj_add_flag(wifi_view_status, LV_OBJ_FLAG_HIDDEN);
        if (wifi_view_pass) lv_obj_clear_flag(wifi_view_pass, LV_OBJ_FLAG_HIDDEN);
    }
}

void modal_wifi_init(lv_obj_t* parent) {
    modal_wifi_selector = lv_obj_create(parent ? parent : lv_scr_act());
    lv_obj_set_size(modal_wifi_selector, 240, 320);
    lv_obj_align(modal_wifi_selector, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(modal_wifi_selector, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(modal_wifi_selector, LV_OPA_80, 0);
    lv_obj_set_style_radius(modal_wifi_selector, 0, 0);
    lv_obj_set_style_border_side(modal_wifi_selector, LV_BORDER_SIDE_NONE, 0);
    lv_obj_set_style_pad_all(modal_wifi_selector, 8, 0);
    lv_obj_add_flag(modal_wifi_selector, LV_OBJ_FLAG_HIDDEN); // Hidden by default

    // Modal Card
    lv_obj_t* card = lv_obj_create(modal_wifi_selector);
    lv_obj_set_size(card, 224, 296);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, COLOR_SURFACE, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_color(card, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // -------------------------------------------------------------
    // View 1: Network Selection List
    // -------------------------------------------------------------
    wifi_view_list = lv_obj_create(card);
    lv_obj_set_size(wifi_view_list, 212, 284);
    lv_obj_center(wifi_view_list);
    lv_obj_set_style_bg_opa(wifi_view_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_view_list, 0, 0);
    lv_obj_set_style_pad_all(wifi_view_list, 0, 0);
    lv_obj_clear_flag(wifi_view_list, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_title = lv_label_create(wifi_view_list);
    lv_label_set_text(lbl_title, "Wi-Fi Networks");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 0);

    // Connected Network banner (shown when Wi-Fi is connected)
    box_wifi_connected = lv_obj_create(wifi_view_list);
    lv_obj_set_size(box_wifi_connected, 212, 32);
    lv_obj_align(box_wifi_connected, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_bg_color(box_wifi_connected, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_width(box_wifi_connected, 0, 0);
    lv_obj_set_style_pad_all(box_wifi_connected, 4, 0);
    lv_obj_set_style_radius(box_wifi_connected, 6, 0);
    lv_obj_clear_flag(box_wifi_connected, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box_wifi_connected, LV_OBJ_FLAG_HIDDEN);

    lbl_wifi_curr_ssid = lv_label_create(box_wifi_connected);
    lv_label_set_text(lbl_wifi_curr_ssid, LV_SYMBOL_OK " Connected");
    lv_obj_set_style_text_font(lbl_wifi_curr_ssid, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_wifi_curr_ssid, COLOR_SUCCESS, 0);
    lv_obj_set_width(lbl_wifi_curr_ssid, 130);
    lv_label_set_long_mode(lbl_wifi_curr_ssid, LV_LABEL_LONG_DOT);
    lv_obj_align(lbl_wifi_curr_ssid, LV_ALIGN_LEFT_MID, 4, 0);

    btn_wifi_forget = lv_btn_create(box_wifi_connected);
    lv_obj_set_size(btn_wifi_forget, 62, 24);
    lv_obj_align(btn_wifi_forget, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_wifi_forget, lv_color_hex(0x3A2222), 0);
    lv_obj_set_style_border_color(btn_wifi_forget, lv_color_hex(0xE63946), 0);
    lv_obj_set_style_border_width(btn_wifi_forget, 1, 0);
    lv_obj_set_style_radius(btn_wifi_forget, 4, 0);
    lv_obj_set_style_pad_all(btn_wifi_forget, 0, 0);
    lv_obj_add_event_cb(btn_wifi_forget, event_btn_wifi_forget, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lbl_f = lv_label_create(btn_wifi_forget);
    lv_label_set_text(lbl_f, "Forget");
    lv_obj_set_style_text_color(lbl_f, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_text_font(lbl_f, &lv_font_montserrat_10, 0);
    lv_obj_center(lbl_f);

    lbl_wifi_scan_status = lv_label_create(wifi_view_list);
    lv_label_set_text(lbl_wifi_scan_status, "Ready");
    lv_obj_set_style_text_font(lbl_wifi_scan_status, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_wifi_scan_status, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_wifi_scan_status, LV_ALIGN_TOP_MID, 0, 56);

    list_wifi = lv_list_create(wifi_view_list);
    lv_obj_set_size(list_wifi, 212, 176);
    lv_obj_align(list_wifi, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_bg_color(list_wifi, COLOR_BG, 0);
    lv_obj_set_style_border_side(list_wifi, LV_BORDER_SIDE_NONE, 0);
    lv_obj_set_style_radius(list_wifi, 8, 0);

    lv_obj_t* btn_rescan_w = lv_btn_create(wifi_view_list);
    lv_obj_set_size(btn_rescan_w, 100, 32);
    lv_obj_align(btn_rescan_w, LV_ALIGN_BOTTOM_LEFT, 2, 0);
    lv_obj_set_style_bg_color(btn_rescan_w, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_radius(btn_rescan_w, 8, 0);
    lv_obj_add_event_cb(btn_rescan_w, event_btn_wifi_rescan, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lbl_rescan = lv_label_create(btn_rescan_w);
    lv_label_set_text(lbl_rescan, LV_SYMBOL_REFRESH " Rescan");
    lv_obj_set_style_text_font(lbl_rescan, &lv_font_montserrat_10, 0);
    lv_obj_center(lbl_rescan);

    lv_obj_t* btn_close_w = lv_btn_create(wifi_view_list);
    lv_obj_set_size(btn_close_w, 100, 32);
    lv_obj_align(btn_close_w, LV_ALIGN_BOTTOM_RIGHT, -2, 0);
    lv_obj_set_style_bg_color(btn_close_w, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(btn_close_w, 8, 0);
    lv_obj_add_event_cb(btn_close_w, event_btn_close_wifi_modal, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lbl_close = lv_label_create(btn_close_w);
    lv_label_set_text(lbl_close, "Close");
    lv_obj_set_style_text_color(lbl_close, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_close, &lv_font_montserrat_10, 0);
    lv_obj_center(lbl_close);

    // -------------------------------------------------------------
    // View 2: Password Entry & Keyboard
    // -------------------------------------------------------------
    wifi_view_pass = lv_obj_create(card);
    lv_obj_set_size(wifi_view_pass, 212, 284);
    lv_obj_center(wifi_view_pass);
    lv_obj_set_style_bg_opa(wifi_view_pass, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_view_pass, 0, 0);
    lv_obj_set_style_pad_all(wifi_view_pass, 0, 0);
    lv_obj_clear_flag(wifi_view_pass, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wifi_view_pass, LV_OBJ_FLAG_HIDDEN);

    lbl_wifi_pass_title = lv_label_create(wifi_view_pass);
    lv_label_set_text(lbl_wifi_pass_title, "Enter Password");
    lv_obj_set_style_text_font(lbl_wifi_pass_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_wifi_pass_title, COLOR_ACCENT, 0);
    lv_obj_set_width(lbl_wifi_pass_title, 210);
    lv_label_set_long_mode(lbl_wifi_pass_title, LV_LABEL_LONG_DOT);
    lv_obj_align(lbl_wifi_pass_title, LV_ALIGN_TOP_MID, 0, 0);

    // Text Area for Password
    ta_wifi_pass = lv_textarea_create(wifi_view_pass);
    lv_obj_set_size(ta_wifi_pass, 172, 34);
    lv_obj_align(ta_wifi_pass, LV_ALIGN_TOP_LEFT, 0, 20);
    lv_textarea_set_password_mode(ta_wifi_pass, true);
    lv_textarea_set_one_line(ta_wifi_pass, true);
    lv_textarea_set_placeholder_text(ta_wifi_pass, "Password");
    lv_obj_set_style_bg_color(ta_wifi_pass, COLOR_BG, 0);
    lv_obj_set_style_text_color(ta_wifi_pass, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(ta_wifi_pass, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_radius(ta_wifi_pass, 6, 0);
    lv_obj_set_style_text_font(ta_wifi_pass, &lv_font_montserrat_12, 0);

    // Eye toggle button
    lv_obj_t* btn_eye = lv_btn_create(wifi_view_pass);
    lv_obj_set_size(btn_eye, 34, 34);
    lv_obj_align(btn_eye, LV_ALIGN_TOP_RIGHT, 0, 20);
    lv_obj_set_style_bg_color(btn_eye, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_radius(btn_eye, 6, 0);
    lv_obj_add_event_cb(btn_eye, event_btn_wifi_eye_toggle, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lbl_eye = lv_label_create(btn_eye);
    lv_label_set_text(lbl_eye, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_font(lbl_eye, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_eye);

    // Keyboard
    kb_wifi = lv_keyboard_create(wifi_view_pass);
    lv_obj_set_size(kb_wifi, 212, 178);
    lv_obj_align(kb_wifi, LV_ALIGN_TOP_MID, 0, 58);
    lv_keyboard_set_textarea(kb_wifi, ta_wifi_pass);
    lv_obj_set_style_bg_color(kb_wifi, COLOR_SURFACE, 0);
    lv_obj_set_style_radius(kb_wifi, 6, 0);
    lv_obj_add_event_cb(kb_wifi, event_kb_wifi_ready, LV_EVENT_READY, nullptr);

    // Bottom Action Buttons: Back & Connect
    lv_obj_t* btn_pass_back = lv_btn_create(wifi_view_pass);
    lv_obj_set_size(btn_pass_back, 100, 32);
    lv_obj_align(btn_pass_back, LV_ALIGN_BOTTOM_LEFT, 2, 0);
    lv_obj_set_style_bg_color(btn_pass_back, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_radius(btn_pass_back, 8, 0);
    lv_obj_add_event_cb(btn_pass_back, event_btn_wifi_pass_back, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lbl_back = lv_label_create(btn_pass_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_10, 0);
    lv_obj_center(lbl_back);

    lv_obj_t* btn_pass_connect = lv_btn_create(wifi_view_pass);
    lv_obj_set_size(btn_pass_connect, 100, 32);
    lv_obj_align(btn_pass_connect, LV_ALIGN_BOTTOM_RIGHT, -2, 0);
    lv_obj_set_style_bg_color(btn_pass_connect, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(btn_pass_connect, 8, 0);
    lv_obj_add_event_cb(btn_pass_connect, event_btn_wifi_pass_connect, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lbl_conn = lv_label_create(btn_pass_connect);
    lv_label_set_text(lbl_conn, "Connect");
    lv_obj_set_style_text_color(lbl_conn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_conn, &lv_font_montserrat_10, 0);
    lv_obj_center(lbl_conn);

    // -------------------------------------------------------------
    // View 3: Connecting Status
    // -------------------------------------------------------------
    wifi_view_status = lv_obj_create(card);
    lv_obj_set_size(wifi_view_status, 212, 284);
    lv_obj_center(wifi_view_status);
    lv_obj_set_style_bg_opa(wifi_view_status, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_view_status, 0, 0);
    lv_obj_set_style_pad_all(wifi_view_status, 0, 0);
    lv_obj_clear_flag(wifi_view_status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wifi_view_status, LV_OBJ_FLAG_HIDDEN);

    spinner_wifi = lv_spinner_create(wifi_view_status, 1000, 60);
    lv_obj_set_size(spinner_wifi, 50, 50);
    lv_obj_align(spinner_wifi, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_arc_color(spinner_wifi, COLOR_ACCENT, LV_PART_INDICATOR);

    lbl_wifi_connecting_msg = lv_label_create(wifi_view_status);
    lv_label_set_text(lbl_wifi_connecting_msg, "Connecting...");
    lv_obj_set_style_text_font(lbl_wifi_connecting_msg, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_wifi_connecting_msg, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(lbl_wifi_connecting_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_wifi_connecting_msg, 200);
    lv_obj_align(lbl_wifi_connecting_msg, LV_ALIGN_CENTER, 0, 25);

    btn_wifi_status_back = lv_btn_create(wifi_view_status);
    lv_obj_set_size(btn_wifi_status_back, 120, 34);
    lv_obj_align(btn_wifi_status_back, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(btn_wifi_status_back, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_radius(btn_wifi_status_back, 8, 0);
    lv_obj_add_event_cb(btn_wifi_status_back, event_btn_wifi_pass_back, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(btn_wifi_status_back, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* lbl_st_back = lv_label_create(btn_wifi_status_back);
    lv_label_set_text(lbl_st_back, "Back to List");
    lv_obj_set_style_text_font(lbl_st_back, &lv_font_montserrat_10, 0);
    lv_obj_center(lbl_st_back);
}

void modal_wifi_open() {
    if (!modal_wifi_selector) return;

    if (timer_wifi_close) {
        lv_timer_del(timer_wifi_close);
        timer_wifi_close = nullptr;
    }

    lv_obj_move_to_index(modal_wifi_selector, -1);
    lv_obj_clear_flag(modal_wifi_selector, LV_OBJ_FLAG_HIDDEN);
    if (wifi_view_list) lv_obj_clear_flag(wifi_view_list, LV_OBJ_FLAG_HIDDEN);
    if (wifi_view_pass) lv_obj_add_flag(wifi_view_pass, LV_OBJ_FLAG_HIDDEN);
    if (wifi_view_status) lv_obj_add_flag(wifi_view_status, LV_OBJ_FLAG_HIDDEN);

    // Update banner for currently connected network
    if (box_wifi_connected && lbl_wifi_curr_ssid) {
        if (current_wifi_connected && strlen(current_connected_ssid) > 0) {
            char buf[48];
            snprintf(buf, sizeof(buf), LV_SYMBOL_OK " %s", current_connected_ssid);
            lv_label_set_text(lbl_wifi_curr_ssid, buf);
            lv_obj_clear_flag(box_wifi_connected, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(box_wifi_connected, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (lbl_wifi_scan_status) {
        lv_label_set_text(lbl_wifi_scan_status, "Scanning for networks...");
        lv_obj_set_style_text_color(lbl_wifi_scan_status, COLOR_ACCENT, 0);
    }

    if (list_wifi && current_wifi_scan_list.count == 0) {
        lv_obj_clean(list_wifi);
    }

    UiCommand cmd;
    cmd.type = CMD_WIFI_START_SCAN;
    xQueueSend(xQueueUiCmd, &cmd, 0);
}

void modal_wifi_close() {
    if (timer_wifi_close) {
        lv_timer_del(timer_wifi_close);
        timer_wifi_close = nullptr;
    }
    if (modal_wifi_selector) {
        lv_obj_add_flag(modal_wifi_selector, LV_OBJ_FLAG_HIDDEN);
    }
}

void modal_wifi_update_status(bool connected, const char* ssid) {
    current_wifi_connected = connected;
    if (ssid && strlen(ssid) > 0) {
        strncpy(current_connected_ssid, ssid, sizeof(current_connected_ssid) - 1);
        current_connected_ssid[sizeof(current_connected_ssid) - 1] = '\0';
    } else if (!connected) {
        current_connected_ssid[0] = '\0';
    }

    if (box_wifi_connected && lbl_wifi_curr_ssid) {
        if (connected && strlen(current_connected_ssid) > 0) {
            char buf[48];
            snprintf(buf, sizeof(buf), LV_SYMBOL_OK " %s", current_connected_ssid);
            lv_label_set_text(lbl_wifi_curr_ssid, buf);
            lv_obj_clear_flag(box_wifi_connected, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(box_wifi_connected, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void modal_wifi_set_scan_results(const WiFiScanList& list) {
    current_wifi_scan_list = list;
    if (!list_wifi) return;
    lv_obj_clean(list_wifi);

    if (lbl_wifi_scan_status) {
        char status[32];
        if (list.count == 0) {
            snprintf(status, sizeof(status), "No networks found");
        } else if (list.count == 1) {
            snprintf(status, sizeof(status), "1 network found");
        } else {
            snprintf(status, sizeof(status), "%d networks found", list.count);
        }
        lv_label_set_text(lbl_wifi_scan_status, status);
        lv_obj_set_style_text_color(lbl_wifi_scan_status, COLOR_TEXT_MUTED, 0);
    }

    for (uint8_t i = 0; i < list.count; ++i) {
        char item_text[64];
        snprintf(item_text, sizeof(item_text), "%s (%d dBm)", list.networks[i].ssid, list.networks[i].rssi);

        lv_obj_t* btn = lv_list_add_btn(list_wifi, LV_SYMBOL_WIFI, item_text);
        lv_obj_set_style_bg_color(btn, COLOR_SURFACE, 0);
        lv_obj_set_style_text_color(btn, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_bottom(btn, 4, 0);

        // Prevent marquee / rotating text
        uint32_t child_cnt = lv_obj_get_child_cnt(btn);
        for (uint32_t c = 0; c < child_cnt; ++c) {
            lv_obj_t* child = lv_obj_get_child(btn, c);
            if (lv_obj_check_type(child, &lv_label_class)) {
                lv_label_set_long_mode(child, LV_LABEL_LONG_DOT);
            }
        }

        // Add volume padlock icon on the right side if password-protected
        if (!list.networks[i].is_open) {
            lv_obj_t* img = lv_img_create(btn);
            lv_img_set_src(img, &img_lock_dsc);
            lv_obj_set_style_img_recolor(img, COLOR_TEXT_MUTED, 0);
            lv_obj_set_style_img_recolor_opa(img, 255, 0);
            lv_obj_align(img, LV_ALIGN_RIGHT_MID, -6, 0);
        }

        lv_obj_add_event_cb(btn, event_wifi_item_clicked, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }
}

void modal_wifi_on_connected(const char* ip) {
    if (!modal_wifi_selector || lv_obj_has_flag(modal_wifi_selector, LV_OBJ_FLAG_HIDDEN)) return;
    if (!wifi_view_status || lv_obj_has_flag(wifi_view_status, LV_OBJ_FLAG_HIDDEN)) return;

    if (spinner_wifi) lv_obj_add_flag(spinner_wifi, LV_OBJ_FLAG_HIDDEN);
    if (lbl_wifi_connecting_msg) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Connected!\nIP: %s", (ip && strlen(ip) > 0) ? ip : "OK");
        lv_label_set_text(lbl_wifi_connecting_msg, buf);
        lv_obj_set_style_text_color(lbl_wifi_connecting_msg, COLOR_SUCCESS, 0);
    }

    if (!timer_wifi_close) {
        timer_wifi_close = lv_timer_create(timer_close_wifi_modal_cb, 1200, nullptr);
        lv_timer_set_repeat_count(timer_wifi_close, 1);
    }
}

void modal_wifi_on_connect_failed() {
    if (!modal_wifi_selector || lv_obj_has_flag(modal_wifi_selector, LV_OBJ_FLAG_HIDDEN)) return;

    if (spinner_wifi) lv_obj_add_flag(spinner_wifi, LV_OBJ_FLAG_HIDDEN);
    if (lbl_wifi_connecting_msg) {
        lv_label_set_text(lbl_wifi_connecting_msg, "Connection Failed!\nCheck password and try again.");
        lv_obj_set_style_text_color(lbl_wifi_connecting_msg, lv_color_hex(0xE63946), 0);
    }
    if (btn_wifi_status_back) {
        lv_obj_clear_flag(btn_wifi_status_back, LV_OBJ_FLAG_HIDDEN);
    }
}

bool modal_wifi_is_open() {
    return (modal_wifi_selector && !lv_obj_has_flag(modal_wifi_selector, LV_OBJ_FLAG_HIDDEN));
}
