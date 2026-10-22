#include "ui/modal_wifi.h"
#include "ui/ui_theme.h"
#include "config.h"
#include <Preferences.h>
#include <stdio.h>

extern QueueHandle_t xQueueUiCmd;

static lv_obj_t* modal_wifi_selector = nullptr;
static lv_obj_t* wifi_view_list = nullptr;
static lv_obj_t* wifi_view_pass = nullptr;
static lv_obj_t* wifi_view_status = nullptr;
static lv_obj_t* list_wifi = nullptr;
static lv_obj_t* lbl_wifi_header_title = nullptr;
static lv_obj_t* lbl_wifi_pass_title = nullptr;
static lv_obj_t* ta_wifi_pass = nullptr;
static lv_obj_t* kb_wifi = nullptr;
static lv_obj_t* lbl_wifi_connecting_msg = nullptr;
static lv_obj_t* spinner_wifi = nullptr;
static lv_obj_t* s_cont_header = nullptr;
static lv_obj_t* s_cont_back = nullptr;
static lv_obj_t* s_lbl_back_icon = nullptr;
static lv_obj_t* s_btn_rescan = nullptr;
static lv_obj_t* s_lbl_rescan_icon = nullptr;
static lv_obj_t* s_cont_pass_hdr = nullptr;
static lv_obj_t* s_lbl_pass_back_icon = nullptr;
static lv_obj_t* s_btn_eye = nullptr;
static lv_obj_t* s_cont_status_hdr = nullptr;
static lv_obj_t* s_lbl_status_back_icon = nullptr;
static lv_obj_t* s_lbl_status_hdr_title = nullptr;

static WiFiScanList current_wifi_scan_list;
static char selected_wifi_ssid[33] = "";
static char current_connected_ssid[33] = "";
static bool current_wifi_connected = false;
static bool s_ui_wifi_scanning = false;
static lv_timer_t* timer_wifi_close = nullptr;

static char s_saved_wifi_ssid[33] = "";
static char s_saved_wifi_pass[65] = "";
static bool s_trying_saved_pass = false;

static void render_wifi_list();

static void style_flat_list_btn(lv_obj_t* btn, lv_color_t icon_color, lv_color_t text_color = COLOR_TEXT_PRIMARY) {
    lv_obj_set_style_bg_color(btn, COLOR_BG, 0);
    lv_obj_set_style_bg_color(btn, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_pad_ver(btn, 9, 0);
    lv_obj_set_style_pad_hor(btn, 8, 0);
    lv_obj_set_style_pad_column(btn, 8, 0);
    lv_obj_set_style_transform_width(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_flex_cross_place(btn, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(btn, text_color, 0);

    if (lv_obj_get_child_cnt(btn) >= 2) {
        lv_obj_t* icon_obj = lv_obj_get_child(btn, 0);
        if (icon_obj) {
            lv_obj_set_style_text_color(icon_obj, icon_color, 0);
        }
    }
}

static void load_saved_wifi_creds() {
    Preferences prefs;
    if (prefs.begin("wiimremote", true)) {
        String ssid = prefs.getString("wifi_ssid", "");
        String pass = prefs.getString("wifi_pass", "");
        strncpy(s_saved_wifi_ssid, ssid.c_str(), sizeof(s_saved_wifi_ssid) - 1);
        s_saved_wifi_ssid[sizeof(s_saved_wifi_ssid) - 1] = '\0';
        strncpy(s_saved_wifi_pass, pass.c_str(), sizeof(s_saved_wifi_pass) - 1);
        s_saved_wifi_pass[sizeof(s_saved_wifi_pass) - 1] = '\0';
        prefs.end();
    }
}

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
    s_saved_wifi_ssid[0] = '\0';
    s_saved_wifi_pass[0] = '\0';
    s_trying_saved_pass = false;
    s_ui_wifi_scanning = true;
    current_wifi_scan_list.count = 0;
    if (lbl_wifi_header_title) {
        lv_label_set_text(lbl_wifi_header_title, "Scanning networks...");
    }
    render_wifi_list();
}

static void event_btn_wifi_rescan(lv_event_t* e) {
    s_ui_wifi_scanning = true;
    if (lbl_wifi_header_title) {
        lv_label_set_text(lbl_wifi_header_title, "Scanning networks...");
    }
    if (current_wifi_scan_list.count == 0) {
        render_wifi_list();
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
    if (lbl_wifi_connecting_msg) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Connecting to\n%s...", ssid);
        lv_label_set_text(lbl_wifi_connecting_msg, buf);
        lv_obj_set_style_text_color(lbl_wifi_connecting_msg, COLOR_TEXT_PRIMARY, 0);
    }
}

static void open_password_input(const char* ssid, bool auth_failed) {
    if (lbl_wifi_pass_title) {
        char title[64];
        if (auth_failed) {
            snprintf(title, sizeof(title), "Failed: %s", ssid);
            lv_label_set_text(lbl_wifi_pass_title, title);
            lv_obj_set_style_text_color(lbl_wifi_pass_title, lv_color_hex(0xE63946), 0);
        } else {
            snprintf(title, sizeof(title), "Password: %s", ssid);
            lv_label_set_text(lbl_wifi_pass_title, title);
            lv_obj_set_style_text_color(lbl_wifi_pass_title, COLOR_ACCENT, 0);
        }
    }
    if (ta_wifi_pass) {
        lv_textarea_set_text(ta_wifi_pass, "");
        lv_textarea_set_password_mode(ta_wifi_pass, true);
        lv_textarea_set_placeholder_text(ta_wifi_pass, auth_failed ? "Enter new password" : "Password");
    }
    if (kb_wifi && ta_wifi_pass) {
        lv_keyboard_set_textarea(kb_wifi, ta_wifi_pass);
    }
    if (wifi_view_list) lv_obj_add_flag(wifi_view_list, LV_OBJ_FLAG_HIDDEN);
    if (wifi_view_status) lv_obj_add_flag(wifi_view_status, LV_OBJ_FLAG_HIDDEN);
    if (wifi_view_pass) lv_obj_clear_flag(wifi_view_pass, LV_OBJ_FLAG_HIDDEN);
}

static void event_kb_wifi_ready(lv_event_t* e) {
    s_trying_saved_pass = false;
    const char* pass = ta_wifi_pass ? lv_textarea_get_text(ta_wifi_pass) : "";
    do_wifi_connect(selected_wifi_ssid, pass);
}

static void event_wifi_item_clicked(lv_event_t* e) {
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    if (idx >= current_wifi_scan_list.count) return;

    strncpy(selected_wifi_ssid, current_wifi_scan_list.networks[idx].ssid, sizeof(selected_wifi_ssid) - 1);
    selected_wifi_ssid[sizeof(selected_wifi_ssid) - 1] = '\0';

    if (current_wifi_scan_list.networks[idx].is_open) {
        s_trying_saved_pass = false;
        do_wifi_connect(selected_wifi_ssid, "");
    } else if (strlen(s_saved_wifi_ssid) > 0 && strcmp(selected_wifi_ssid, s_saved_wifi_ssid) == 0 && strlen(s_saved_wifi_pass) > 0) {
        log_i("Selected saved network '%s', attempting connection with stored password...", selected_wifi_ssid);
        s_trying_saved_pass = true;
        do_wifi_connect(selected_wifi_ssid, s_saved_wifi_pass);
    } else {
        s_trying_saved_pass = false;
        open_password_input(selected_wifi_ssid, false);
    }
}

static void render_wifi_list() {
    if (!list_wifi) return;
    lv_obj_clean(list_wifi);
    lv_obj_scroll_to_y(list_wifi, 0, LV_ANIM_OFF);

    // Top Row: Connected network & Forget action
    if (current_wifi_connected && strlen(current_connected_ssid) > 0) {
        char forget_buf[64];
        snprintf(forget_buf, sizeof(forget_buf), "Forget %s", current_connected_ssid);
        lv_obj_t* btn_forget = lv_list_add_btn(list_wifi, LV_SYMBOL_TRASH, forget_buf);
        style_flat_list_btn(btn_forget, lv_color_hex(0xFF6B6B), lv_color_hex(0xFF6B6B));
        lv_obj_add_event_cb(btn_forget, event_btn_wifi_forget, LV_EVENT_CLICKED, nullptr);
    }

    if (current_wifi_scan_list.count == 0) {
        lv_obj_t* lbl = lv_label_create(list_wifi);
        lv_label_set_text(lbl, s_ui_wifi_scanning ? "Scanning for networks..." : "No networks found.\nTap refresh to scan.");
        lv_obj_set_width(lbl, 224);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, COLOR_TEXT_MUTED, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(lbl, 40, 0);
        lv_obj_set_style_pad_left(lbl, 8, 0);
        return;
    }

    for (uint8_t i = 0; i < current_wifi_scan_list.count; ++i) {
        const WiFiNetworkInfo& net = current_wifi_scan_list.networks[i];
        lv_color_t sig_color = lv_color_hex(0xE63946); // Red (Weak)
        if (net.rssi >= -65) {
            sig_color = COLOR_SUCCESS; // Green (Strong)
        } else if (net.rssi >= -75) {
            sig_color = COLOR_WARNING; // Amber (Fair)
        }

        bool is_curr = (current_wifi_connected && strcmp(net.ssid, current_connected_ssid) == 0);
        lv_obj_t* btn = lv_list_add_btn(list_wifi, LV_SYMBOL_WIFI, net.ssid);
        style_flat_list_btn(btn, sig_color, is_curr ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);

        if (!net.is_open) {
            lv_obj_t* img = lv_img_create(btn);
            lv_img_set_src(img, &img_lock_dsc);
            lv_obj_set_style_img_recolor(img, COLOR_TEXT_MUTED, 0);
            lv_obj_set_style_img_recolor_opa(img, 255, 0);
            lv_obj_align(img, LV_ALIGN_RIGHT_MID, -6, 0);
        }

        lv_obj_add_event_cb(btn, event_wifi_item_clicked, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }
}

void modal_wifi_init(lv_obj_t* parent) {
    modal_wifi_selector = lv_obj_create(parent ? parent : lv_scr_act());
    lv_obj_set_size(modal_wifi_selector, 240, 320);
    lv_obj_align(modal_wifi_selector, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(modal_wifi_selector, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(modal_wifi_selector, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(modal_wifi_selector, 0, 0);
    lv_obj_set_style_border_side(modal_wifi_selector, LV_BORDER_SIDE_NONE, 0);
    lv_obj_set_style_pad_all(modal_wifi_selector, 0, 0);
    lv_obj_clear_flag(modal_wifi_selector, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(modal_wifi_selector, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);

    // -------------------------------------------------------------
    // View 1: Network Selection List (240 x 320 px)
    // -------------------------------------------------------------
    wifi_view_list = lv_obj_create(modal_wifi_selector);
    lv_obj_set_size(wifi_view_list, 240, 320);
    lv_obj_align(wifi_view_list, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(wifi_view_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_view_list, 0, 0);
    lv_obj_set_style_pad_all(wifi_view_list, 0, 0);
    lv_obj_clear_flag(wifi_view_list, LV_OBJ_FLAG_SCROLLABLE);

    s_cont_header = lv_obj_create(wifi_view_list);
    lv_obj_set_size(s_cont_header, 240, 32);
    lv_obj_align(s_cont_header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_cont_header, COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(s_cont_header, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_side(s_cont_header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(s_cont_header, 1, 0);
    lv_obj_set_style_radius(s_cont_header, 0, 0);
    lv_obj_set_style_pad_all(s_cont_header, 0, 0);
    lv_obj_clear_flag(s_cont_header, LV_OBJ_FLAG_SCROLLABLE);

    s_cont_back = lv_obj_create(s_cont_header);
    lv_obj_set_size(s_cont_back, 196, 31);
    lv_obj_align(s_cont_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(s_cont_back, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_color(s_cont_back, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
    lv_obj_set_style_border_side(s_cont_back, LV_BORDER_SIDE_NONE, 0);
    lv_obj_set_style_radius(s_cont_back, 0, 0);
    lv_obj_set_style_pad_all(s_cont_back, 0, 0);
    lv_obj_clear_flag(s_cont_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cont_back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_cont_back, event_btn_close_wifi_modal, LV_EVENT_CLICKED, nullptr);

    s_lbl_back_icon = lv_label_create(s_cont_back);
    lv_label_set_text(s_lbl_back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(s_lbl_back_icon, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_back_icon, COLOR_ACCENT, 0);
    lv_obj_align(s_lbl_back_icon, LV_ALIGN_LEFT_MID, 10, 0);

    lbl_wifi_header_title = lv_label_create(s_cont_back);
    lv_label_set_text(lbl_wifi_header_title, "Wi-Fi Networks");
    lv_obj_set_style_text_font(lbl_wifi_header_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_wifi_header_title, COLOR_ACCENT, 0);
    lv_obj_align(lbl_wifi_header_title, LV_ALIGN_LEFT_MID, 32, 0);

    s_btn_rescan = lv_obj_create(s_cont_header);
    lv_obj_set_size(s_btn_rescan, 44, 31);
    lv_obj_align(s_btn_rescan, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(s_btn_rescan, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_color(s_btn_rescan, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(s_btn_rescan, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_side(s_btn_rescan, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(s_btn_rescan, 1, 0);
    lv_obj_set_style_radius(s_btn_rescan, 0, 0);
    lv_obj_set_style_pad_all(s_btn_rescan, 0, 0);
    lv_obj_clear_flag(s_btn_rescan, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_btn_rescan, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_btn_rescan, event_btn_wifi_rescan, LV_EVENT_CLICKED, nullptr);

    s_lbl_rescan_icon = lv_label_create(s_btn_rescan);
    lv_label_set_text(s_lbl_rescan_icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(s_lbl_rescan_icon, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_rescan_icon, COLOR_ACCENT, 0);
    lv_obj_center(s_lbl_rescan_icon);

    list_wifi = lv_list_create(wifi_view_list);
    lv_obj_set_size(list_wifi, 240, 288);
    lv_obj_align(list_wifi, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_bg_color(list_wifi, COLOR_BG, 0);
    lv_obj_set_style_border_side(list_wifi, LV_BORDER_SIDE_NONE, 0);
    lv_obj_set_style_radius(list_wifi, 0, 0);
    lv_obj_set_style_pad_all(list_wifi, 0, 0);
    lv_obj_set_style_pad_row(list_wifi, 0, 0);

    // -------------------------------------------------------------
    // View 2: Password Entry & Keyboard (240 x 320 px)
    // -------------------------------------------------------------
    wifi_view_pass = lv_obj_create(modal_wifi_selector);
    lv_obj_set_size(wifi_view_pass, 240, 320);
    lv_obj_align(wifi_view_pass, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(wifi_view_pass, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_view_pass, 0, 0);
    lv_obj_set_style_pad_all(wifi_view_pass, 0, 0);
    lv_obj_clear_flag(wifi_view_pass, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wifi_view_pass, LV_OBJ_FLAG_HIDDEN);

    s_cont_pass_hdr = lv_obj_create(wifi_view_pass);
    lv_obj_set_size(s_cont_pass_hdr, 240, 32);
    lv_obj_align(s_cont_pass_hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_cont_pass_hdr, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_color(s_cont_pass_hdr, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(s_cont_pass_hdr, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_side(s_cont_pass_hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(s_cont_pass_hdr, 1, 0);
    lv_obj_set_style_radius(s_cont_pass_hdr, 0, 0);
    lv_obj_set_style_pad_all(s_cont_pass_hdr, 0, 0);
    lv_obj_clear_flag(s_cont_pass_hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cont_pass_hdr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_cont_pass_hdr, event_btn_wifi_pass_back, LV_EVENT_CLICKED, nullptr);

    s_lbl_pass_back_icon = lv_label_create(s_cont_pass_hdr);
    lv_label_set_text(s_lbl_pass_back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(s_lbl_pass_back_icon, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_pass_back_icon, COLOR_ACCENT, 0);
    lv_obj_align(s_lbl_pass_back_icon, LV_ALIGN_LEFT_MID, 10, 0);

    lbl_wifi_pass_title = lv_label_create(s_cont_pass_hdr);
    lv_label_set_text(lbl_wifi_pass_title, "Enter Password");
    lv_obj_set_style_text_font(lbl_wifi_pass_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_wifi_pass_title, COLOR_ACCENT, 0);
    lv_obj_set_width(lbl_wifi_pass_title, 200);
    lv_label_set_long_mode(lbl_wifi_pass_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(lbl_wifi_pass_title, LV_ALIGN_LEFT_MID, 32, 0);

    ta_wifi_pass = lv_textarea_create(wifi_view_pass);
    lv_obj_set_size(ta_wifi_pass, 188, 36);
    lv_obj_align(ta_wifi_pass, LV_ALIGN_TOP_LEFT, 6, 36);
    lv_textarea_set_password_mode(ta_wifi_pass, true);
    lv_textarea_set_one_line(ta_wifi_pass, true);
    lv_textarea_set_placeholder_text(ta_wifi_pass, "Password");
    lv_obj_set_style_bg_color(ta_wifi_pass, COLOR_SURFACE, 0);
    lv_obj_set_style_text_color(ta_wifi_pass, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(ta_wifi_pass, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_radius(ta_wifi_pass, 4, 0);
    lv_obj_set_style_text_font(ta_wifi_pass, &lv_font_montserrat_12, 0);

    s_btn_eye = lv_btn_create(wifi_view_pass);
    lv_obj_set_size(s_btn_eye, 36, 36);
    lv_obj_align(s_btn_eye, LV_ALIGN_TOP_RIGHT, -6, 36);
    lv_obj_set_style_bg_color(s_btn_eye, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_radius(s_btn_eye, 4, 0);
    lv_obj_add_event_cb(s_btn_eye, event_btn_wifi_eye_toggle, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lbl_eye = lv_label_create(s_btn_eye);
    lv_label_set_text(lbl_eye, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_font(lbl_eye, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_eye);

    kb_wifi = lv_keyboard_create(wifi_view_pass);
    lv_obj_set_size(kb_wifi, 240, 240);
    lv_obj_align(kb_wifi, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb_wifi, ta_wifi_pass);
    lv_obj_set_style_bg_color(kb_wifi, COLOR_SURFACE, 0);
    lv_obj_set_style_radius(kb_wifi, 0, 0);
    lv_obj_add_event_cb(kb_wifi, event_kb_wifi_ready, LV_EVENT_READY, nullptr);

    // -------------------------------------------------------------
    // View 3: Connecting Status (240 x 320 px)
    // -------------------------------------------------------------
    wifi_view_status = lv_obj_create(modal_wifi_selector);
    lv_obj_set_size(wifi_view_status, 240, 320);
    lv_obj_align(wifi_view_status, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(wifi_view_status, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_view_status, 0, 0);
    lv_obj_set_style_pad_all(wifi_view_status, 0, 0);
    lv_obj_clear_flag(wifi_view_status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wifi_view_status, LV_OBJ_FLAG_HIDDEN);

    s_cont_status_hdr = lv_obj_create(wifi_view_status);
    lv_obj_set_size(s_cont_status_hdr, 240, 32);
    lv_obj_align(s_cont_status_hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_cont_status_hdr, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_color(s_cont_status_hdr, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(s_cont_status_hdr, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_side(s_cont_status_hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(s_cont_status_hdr, 1, 0);
    lv_obj_set_style_radius(s_cont_status_hdr, 0, 0);
    lv_obj_set_style_pad_all(s_cont_status_hdr, 0, 0);
    lv_obj_clear_flag(s_cont_status_hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cont_status_hdr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_cont_status_hdr, event_btn_wifi_pass_back, LV_EVENT_CLICKED, nullptr);

    s_lbl_status_back_icon = lv_label_create(s_cont_status_hdr);
    lv_label_set_text(s_lbl_status_back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(s_lbl_status_back_icon, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_status_back_icon, COLOR_ACCENT, 0);
    lv_obj_align(s_lbl_status_back_icon, LV_ALIGN_LEFT_MID, 10, 0);

    s_lbl_status_hdr_title = lv_label_create(s_cont_status_hdr);
    lv_label_set_text(s_lbl_status_hdr_title, "Back to Wi-Fi List");
    lv_obj_set_style_text_font(s_lbl_status_hdr_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_status_hdr_title, COLOR_ACCENT, 0);
    lv_obj_align(s_lbl_status_hdr_title, LV_ALIGN_LEFT_MID, 32, 0);

    spinner_wifi = lv_spinner_create(wifi_view_status, 1000, 60);
    lv_obj_set_size(spinner_wifi, 50, 50);
    lv_obj_align(spinner_wifi, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_arc_color(spinner_wifi, COLOR_ACCENT, LV_PART_INDICATOR);

    lbl_wifi_connecting_msg = lv_label_create(wifi_view_status);
    lv_label_set_text(lbl_wifi_connecting_msg, "Connecting...");
    lv_obj_set_style_text_font(lbl_wifi_connecting_msg, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_wifi_connecting_msg, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(lbl_wifi_connecting_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_wifi_connecting_msg, 220);
    lv_obj_align(lbl_wifi_connecting_msg, LV_ALIGN_CENTER, 0, 35);
}

void modal_wifi_open() {
    if (!modal_wifi_selector) return;

    load_saved_wifi_creds();
    s_trying_saved_pass = false;
    s_ui_wifi_scanning = true;

    if (timer_wifi_close) {
        lv_timer_del(timer_wifi_close);
        timer_wifi_close = nullptr;
    }

    lv_obj_move_to_index(modal_wifi_selector, -1);
    lv_obj_clear_flag(modal_wifi_selector, LV_OBJ_FLAG_HIDDEN);
    if (wifi_view_list) lv_obj_clear_flag(wifi_view_list, LV_OBJ_FLAG_HIDDEN);
    if (wifi_view_pass) lv_obj_add_flag(wifi_view_pass, LV_OBJ_FLAG_HIDDEN);
    if (wifi_view_status) lv_obj_add_flag(wifi_view_status, LV_OBJ_FLAG_HIDDEN);

    if (lbl_wifi_header_title) {
        lv_label_set_text(lbl_wifi_header_title, "Scanning networks...");
    }

    render_wifi_list();

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

    if (modal_wifi_is_open()) {
        render_wifi_list();
    }
}

void modal_wifi_set_scan_results(const WiFiScanList& list) {
    current_wifi_scan_list = list;
    s_ui_wifi_scanning = false;

    if (lbl_wifi_header_title) {
        lv_label_set_text(lbl_wifi_header_title, "Wi-Fi Networks");
    }

    render_wifi_list();
}

void modal_wifi_on_connected(const char* ip) {
    s_trying_saved_pass = false;
    load_saved_wifi_creds();

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

    if (s_trying_saved_pass) {
        s_trying_saved_pass = false;
        log_w("Connection to '%s' with saved password failed. Prompting for new password.", selected_wifi_ssid);
        open_password_input(selected_wifi_ssid, true);
        return;
    }

    if (spinner_wifi) lv_obj_add_flag(spinner_wifi, LV_OBJ_FLAG_HIDDEN);
    if (lbl_wifi_connecting_msg) {
        lv_label_set_text(lbl_wifi_connecting_msg, "Connection Failed!\nTap Back above to try again.");
        lv_obj_set_style_text_color(lbl_wifi_connecting_msg, lv_color_hex(0xE63946), 0);
    }
}

bool modal_wifi_is_open() {
    return (modal_wifi_selector && !lv_obj_has_flag(modal_wifi_selector, LV_OBJ_FLAG_HIDDEN));
}

void modal_wifi_apply_theme() {
    if (modal_wifi_selector) lv_obj_set_style_bg_color(modal_wifi_selector, COLOR_BG, 0);
    if (s_cont_header) {
        lv_obj_set_style_bg_color(s_cont_header, COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(s_cont_header, COLOR_SURFACE_LIGHT, 0);
    }
    if (s_cont_back) {
        lv_obj_set_style_bg_color(s_cont_back, COLOR_SURFACE, 0);
        lv_obj_set_style_bg_color(s_cont_back, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
    }
    if (s_lbl_back_icon) lv_obj_set_style_text_color(s_lbl_back_icon, COLOR_ACCENT, 0);
    if (lbl_wifi_header_title) lv_obj_set_style_text_color(lbl_wifi_header_title, COLOR_ACCENT, 0);
    if (s_btn_rescan) {
        lv_obj_set_style_bg_color(s_btn_rescan, COLOR_SURFACE, 0);
        lv_obj_set_style_bg_color(s_btn_rescan, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(s_btn_rescan, COLOR_SURFACE_LIGHT, 0);
    }
    if (s_lbl_rescan_icon) lv_obj_set_style_text_color(s_lbl_rescan_icon, COLOR_ACCENT, 0);
    if (list_wifi) lv_obj_set_style_bg_color(list_wifi, COLOR_BG, 0);
    if (s_cont_pass_hdr) {
        lv_obj_set_style_bg_color(s_cont_pass_hdr, COLOR_SURFACE, 0);
        lv_obj_set_style_bg_color(s_cont_pass_hdr, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(s_cont_pass_hdr, COLOR_SURFACE_LIGHT, 0);
    }
    if (s_lbl_pass_back_icon) lv_obj_set_style_text_color(s_lbl_pass_back_icon, COLOR_ACCENT, 0);
    if (lbl_wifi_pass_title) lv_obj_set_style_text_color(lbl_wifi_pass_title, COLOR_ACCENT, 0);
    if (ta_wifi_pass) {
        lv_obj_set_style_bg_color(ta_wifi_pass, COLOR_SURFACE, 0);
        lv_obj_set_style_text_color(ta_wifi_pass, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_border_color(ta_wifi_pass, COLOR_SURFACE_LIGHT, 0);
    }
    if (s_btn_eye) lv_obj_set_style_bg_color(s_btn_eye, COLOR_SURFACE_LIGHT, 0);
    if (kb_wifi) lv_obj_set_style_bg_color(kb_wifi, COLOR_SURFACE, 0);
    if (s_cont_status_hdr) {
        lv_obj_set_style_bg_color(s_cont_status_hdr, COLOR_SURFACE, 0);
        lv_obj_set_style_bg_color(s_cont_status_hdr, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(s_cont_status_hdr, COLOR_SURFACE_LIGHT, 0);
    }
    if (s_lbl_status_back_icon) lv_obj_set_style_text_color(s_lbl_status_back_icon, COLOR_ACCENT, 0);
    if (s_lbl_status_hdr_title) lv_obj_set_style_text_color(s_lbl_status_hdr_title, COLOR_ACCENT, 0);
    if (spinner_wifi) lv_obj_set_style_arc_color(spinner_wifi, COLOR_ACCENT, LV_PART_INDICATOR);
    if (lbl_wifi_connecting_msg) lv_obj_set_style_text_color(lbl_wifi_connecting_msg, COLOR_TEXT_PRIMARY, 0);
    render_wifi_list();
}
