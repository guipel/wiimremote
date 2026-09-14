#include "ui.h"
#include "config.h"
#include "display_driver.h"

// Color Palette
#define COLOR_BG            lv_color_hex(0x101216)
#define COLOR_SURFACE       lv_color_hex(0x1C2028)
#define COLOR_SURFACE_LIGHT lv_color_hex(0x282E3A)
#define COLOR_ACCENT        lv_color_hex(0x00E5FF)
#define COLOR_TEXT_PRIMARY  lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_MUTED    lv_color_hex(0x8E9AAB)
#define COLOR_SUCCESS       lv_color_hex(0x10B981)
#define COLOR_WARNING       lv_color_hex(0xF59E0B)

// UI Widgets - Top Header
static lv_obj_t* header_cont = nullptr;
static lv_obj_t* lbl_wifi = nullptr;
static lv_obj_t* btn_device_select = nullptr;
static lv_obj_t* lbl_active_device = nullptr;

// UI Widgets - Tabview
static lv_obj_t* tabview = nullptr;
static lv_obj_t* tab_player = nullptr;
static lv_obj_t* tab_presets = nullptr;


// UI Widgets - Player Tab
static lv_obj_t* lbl_resolution = nullptr;
static lv_obj_t* lbl_track_counter = nullptr;
static lv_obj_t* lbl_title = nullptr;
static lv_obj_t* lbl_artist = nullptr;
static lv_obj_t* bar_progress = nullptr;
static lv_obj_t* lbl_time_cur = nullptr;
static lv_obj_t* lbl_time_total = nullptr;
static lv_obj_t* btn_prev = nullptr;
static lv_obj_t* btn_play_pause = nullptr;
static lv_obj_t* lbl_play_pause = nullptr;
static lv_obj_t* btn_next = nullptr;
static lv_obj_t* btn_mute = nullptr;
static lv_obj_t* lbl_mute = nullptr;
static lv_obj_t* img_lock = nullptr;
static lv_obj_t* lbl_x = nullptr;

// UI Widgets - Variable Volume Controls
static lv_obj_t* obj_vol_var_cont = nullptr;
static lv_obj_t* btn_var_mute = nullptr;
static lv_obj_t* lbl_var_mute = nullptr;
static lv_obj_t* lbl_var_x = nullptr;
static lv_obj_t* slider_vol = nullptr;
static lv_obj_t* lbl_vol_percent = nullptr;
static bool is_user_adjusting_volume = false;
static uint8_t current_volume = 0;

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

// UI Widgets - Presets Tab
static lv_obj_t* preset_btns[MAX_PRESETS] = { nullptr };
static lv_obj_t* preset_lbls[MAX_PRESETS] = { nullptr };

// UI Widgets - Device Selector Modal
static lv_obj_t* modal_device_selector = nullptr;
static lv_obj_t* list_devices = nullptr;
static lv_obj_t* lbl_scan_status = nullptr;
static lv_obj_t* btn_rescan = nullptr;

// UI Widgets - Wi-Fi Configuration Modal
static lv_obj_t* btn_wifi_select = nullptr;
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

// Local state tracking to prevent UI flicker
static bool is_user_dragging_slider = false;
static bool current_mute_state = false;
static PlayState current_play_state = PLAY_STATE_UNKNOWN;
static DeviceList current_device_list;
#define PROGRESS_BAR_MAX 10000
static uint32_t current_totlen_ms = 0;
static uint32_t current_actual_curpos_ms = 0;
static uint32_t last_progress_tick_ms = 0;
static bool is_user_seeking = false;
static lv_obj_t* obj_seek_target = nullptr;
static uint32_t pending_seek_target_ms = 0;
static bool has_pending_seek = false;
static unsigned long pending_seek_timestamp = 0;

static char current_vendor[32] = "WiiM";
static uint32_t current_sample_rate = 0;
static uint8_t current_bit_depth = 0;
static uint16_t current_track_num = 0;
static uint16_t current_track_total = 0;

// Helper: Format milliseconds to mm:ss
static void format_time(uint32_t ms, char* buffer, size_t buf_len) {
    uint32_t total_sec = ms / 1000;
    uint32_t mins = total_sec / 60;
    uint32_t secs = total_sec % 60;
    snprintf(buffer, buf_len, "%02u:%02u", mins, secs);
}

// Helper: Update audio resolution (top left) and track counter (top right)
static void update_stream_info_labels() {
    if (lbl_resolution) {
        char res_buf[32] = "";
        if (current_sample_rate > 0) {
            uint8_t display_depth = current_bit_depth;
            if (display_depth >= 32) display_depth = 24;
            uint32_t khz = current_sample_rate / 1000;
            if (display_depth > 0) {
                snprintf(res_buf, sizeof(res_buf), "%u/%u", display_depth, khz);
            } else {
                snprintf(res_buf, sizeof(res_buf), "%u", khz);
            }
        }
        lv_label_set_text(lbl_resolution, res_buf);
    }

    if (lbl_track_counter) {
        char track_buf[24] = "";
        if (current_track_total > 0 && current_track_num > 0) {
            snprintf(track_buf, sizeof(track_buf), "%u/%u", current_track_num, current_track_total);
        }
        lv_label_set_text(lbl_track_counter, track_buf);
    }
}

// Event Callback - Track Progress Bar Seeking
static void event_slider_seek(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        is_user_seeking = true;

        if (current_totlen_ms > 0) {
            lv_indev_t* indev = lv_indev_get_act();
            if (indev) {
                lv_point_t p;
                lv_indev_get_point(indev, &p);
                int32_t cx = p.x;
                if (cx < 10) cx = 10;
                if (cx > 230) cx = 230;
                int32_t val = ((cx - 10) * PROGRESS_BAR_MAX) / 220;
                if (val < 0) val = 0;
                if (val > PROGRESS_BAR_MAX) val = PROGRESS_BAR_MAX;

                uint32_t seek_ms = (uint32_t)(((uint64_t)val * (uint64_t)current_totlen_ms) / PROGRESS_BAR_MAX);
                char preview_buf[16];
                format_time(seek_ms, preview_buf, sizeof(preview_buf));
                if (lbl_time_cur) lv_label_set_text(lbl_time_cur, preview_buf);

                if (obj_seek_target) {
                    lv_obj_set_pos(obj_seek_target, cx - 7, 74);
                    lv_obj_clear_flag(obj_seek_target, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    } else if (code == LV_EVENT_RELEASED) {
        is_user_seeking = false;
        if (current_totlen_ms > 0) {
            lv_indev_t* indev = lv_indev_get_act();
            if (indev) {
                lv_point_t p;
                lv_indev_get_point(indev, &p);
                int32_t cx = p.x;
                if (cx < 10) cx = 10;
                if (cx > 230) cx = 230;
                int32_t val = ((cx - 10) * PROGRESS_BAR_MAX) / 220;
                if (val < 0) val = 0;
                if (val > PROGRESS_BAR_MAX) val = PROGRESS_BAR_MAX;

                uint32_t seek_ms = (uint32_t)(((uint64_t)val * (uint64_t)current_totlen_ms) / PROGRESS_BAR_MAX);
                pending_seek_target_ms = seek_ms;
                has_pending_seek = true;
                pending_seek_timestamp = millis();

                if (obj_seek_target) {
                    lv_obj_set_pos(obj_seek_target, cx - 7, 74);
                    lv_obj_clear_flag(obj_seek_target, LV_OBJ_FLAG_HIDDEN);
                }

                char cur_buf[16];
                format_time(seek_ms, cur_buf, sizeof(cur_buf));
                if (lbl_time_cur) lv_label_set_text(lbl_time_cur, cur_buf);

                UiCommand cmd;
                cmd.type = CMD_SEEK_POSITION;
                cmd.data.seek_ms = seek_ms;
                xQueueSend(xQueueUiCmd, &cmd, 0);
            }
        }
    }
}

// Event Callback - Interactive Volume Slider
static void event_slider_vol(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        is_user_adjusting_volume = true;
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        int32_t val = lv_slider_get_value(slider_vol);
        if (val < 0) val = 0;
        if (val > 100) val = 100;
        current_volume = (uint8_t)val;

        if (lbl_vol_percent) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d%%", (int)val);
            lv_label_set_text(lbl_vol_percent, buf);
        }

        UiCommand cmd;
        cmd.type = CMD_SET_VOL;
        cmd.data.volume = (uint8_t)val;
        xQueueSend(xQueueUiCmd, &cmd, 0);
    } else if (code == LV_EVENT_RELEASED) {
        is_user_adjusting_volume = false;
        int32_t val = lv_slider_get_value(slider_vol);
        if (val < 0) val = 0;
        if (val > 100) val = 100;
        current_volume = (uint8_t)val;

        if (lbl_vol_percent) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d%%", (int)val);
            lv_label_set_text(lbl_vol_percent, buf);
        }

        UiCommand cmd;
        cmd.type = CMD_SET_VOL;
        cmd.data.volume = (uint8_t)val;
        xQueueSend(xQueueUiCmd, &cmd, 0);
    }
}

static void timer_progress_cb(lv_timer_t* timer) {
    uint32_t now = millis();
    uint32_t delta = (last_progress_tick_ms == 0) ? 50 : (now - last_progress_tick_ms);
    last_progress_tick_ms = now;

    if (current_play_state == PLAY_STATE_PLAYING && !is_user_seeking && !has_pending_seek && current_totlen_ms > 0) {
        current_actual_curpos_ms += delta;
        if (current_actual_curpos_ms > current_totlen_ms) {
            current_actual_curpos_ms = current_totlen_ms;
        }

        if (bar_progress) {
            uint32_t val = (uint32_t)(((uint64_t)current_actual_curpos_ms * PROGRESS_BAR_MAX) / current_totlen_ms);
            if (val > PROGRESS_BAR_MAX) val = PROGRESS_BAR_MAX;
            lv_bar_set_value(bar_progress, (int32_t)val, LV_ANIM_OFF);
        }

        if (lbl_time_cur) {
            char time_cur_buf[16];
            format_time(current_actual_curpos_ms, time_cur_buf, sizeof(time_cur_buf));
            lv_label_set_text(lbl_time_cur, time_cur_buf);
        }
    }
}

// Event Callbacks - Transport Controls
static void event_btn_prev(lv_event_t* e) {
    UiCommand cmd;
    cmd.type = CMD_PREV;
    cmd.data.seek_ms = current_actual_curpos_ms;
    xQueueSend(xQueueUiCmd, &cmd, 0);

    // If restarting mid-song (>5s), snap local position to 0:00 so a subsequent tap skips track
    if (current_actual_curpos_ms > 5000) {
        current_actual_curpos_ms = 0;
        if (bar_progress) lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);
        if (lbl_time_cur) lv_label_set_text(lbl_time_cur, "00:00");
    }
}

static unsigned long last_play_pause_ts = 0;
static void event_btn_play_pause(lv_event_t* e) {
    unsigned long now = millis();
    if (now - last_play_pause_ts < 250) {
        return;
    }
    last_play_pause_ts = now;

    UiCommand cmd;
    cmd.type = CMD_PLAY_PAUSE;
    xQueueSend(xQueueUiCmd, &cmd, 0);
}

static void event_btn_next(lv_event_t* e) {
    UiCommand cmd;
    cmd.type = CMD_NEXT;
    xQueueSend(xQueueUiCmd, &cmd, 0);
}

static void event_btn_mute(lv_event_t* e) {
    UiCommand cmd;
    cmd.type = CMD_SET_MUTE;
    cmd.data.mute = !current_mute_state;
    xQueueSend(xQueueUiCmd, &cmd, 0);
}

// Event Callbacks - Presets
static void event_btn_preset(lv_event_t* e) {
    uintptr_t index = (uintptr_t)lv_event_get_user_data(e);
    UiCommand cmd;
    cmd.type = CMD_TRIGGER_PRESET;
    cmd.data.preset_index = (uint8_t)index;
    xQueueSend(xQueueUiCmd, &cmd, 0);

    // Switch back to player tab for immediate playback feedback
    lv_tabview_set_act(tabview, 0, LV_ANIM_ON);
}

// Event Callbacks - Modal Device Selector
static void event_btn_open_modal(lv_event_t* e) {
    if (lbl_scan_status) {
        char status[32];
        if (current_device_list.count == 1) {
            snprintf(status, sizeof(status), "1 streamer found");
        } else {
            snprintf(status, sizeof(status), "%d streamers found", current_device_list.count);
        }
        lv_label_set_text(lbl_scan_status, status);
        lv_obj_set_style_text_color(lbl_scan_status, COLOR_TEXT_MUTED, 0);
    }
    lv_obj_clear_flag(modal_device_selector, LV_OBJ_FLAG_HIDDEN);
}

static void event_btn_close_modal(lv_event_t* e) {
    lv_obj_add_flag(modal_device_selector, LV_OBJ_FLAG_HIDDEN);
}

static void event_btn_rescan(lv_event_t* e) {
    UiCommand cmd;
    cmd.type = CMD_TRIGGER_RESCAN;
    xQueueSend(xQueueUiCmd, &cmd, 0);
    lv_label_set_text(lbl_scan_status, "Scanning for streamers...");
    lv_obj_set_style_text_color(lbl_scan_status, COLOR_ACCENT, 0);
}

static void event_device_item_clicked(lv_event_t* e) {
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    if (idx < current_device_list.count) {
        UiCommand cmd;
        cmd.type = CMD_SELECT_DEVICE;
        strncpy(cmd.data.device_ip, current_device_list.devices[idx].ip, sizeof(cmd.data.device_ip) - 1);
        cmd.data.device_ip[sizeof(cmd.data.device_ip) - 1] = '\0';
        xQueueSend(xQueueUiCmd, &cmd, 0);
    }
    lv_obj_add_flag(modal_device_selector, LV_OBJ_FLAG_HIDDEN);
}

static void event_btn_open_wifi_modal(lv_event_t* e) {
    ui_open_wifi_modal();
}

// Build Header Bar
static void build_header(lv_obj_t* parent) {
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

    // Wi-Fi Button & Action Chevron (Top Left)
    btn_wifi_select = lv_btn_create(header_cont);
    lv_obj_set_size(btn_wifi_select, 48, 22);
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

    // Active Device Button (Flat & right-aligned)
    btn_device_select = lv_btn_create(header_cont);
    lv_obj_set_size(btn_device_select, 180, 22);
    lv_obj_align(btn_device_select, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_opa(btn_device_select, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_device_select, 0, 0);
    lv_obj_set_style_border_width(btn_device_select, 0, 0);
    lv_obj_set_style_pad_all(btn_device_select, 0, 0);
    lv_obj_add_event_cb(btn_device_select, event_btn_open_modal, LV_EVENT_CLICKED, nullptr);

    lbl_active_device = lv_label_create(btn_device_select);
    lv_label_set_text(lbl_active_device, "Searching... " LV_SYMBOL_DOWN);
    lv_label_set_long_mode(lbl_active_device, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_active_device, 174);
    lv_obj_set_style_text_color(lbl_active_device, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_active_device, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(lbl_active_device, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(lbl_active_device, LV_ALIGN_RIGHT_MID, 0, 0);
}

// Build Now Playing Tab
static void build_player_tab(lv_obj_t* parent) {
    lv_obj_set_style_bg_color(parent, COLOR_BG, 0);
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);


    // 1. Bottom Telemetry Row: Audio Resolution (Left) & Track Counter (Right)
    lbl_resolution = lv_label_create(parent);
    lv_label_set_text(lbl_resolution, "");
    lv_obj_set_style_text_font(lbl_resolution, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_resolution, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_resolution, LV_ALIGN_BOTTOM_LEFT, 4, -2);

    lbl_track_counter = lv_label_create(parent);
    lv_label_set_text(lbl_track_counter, "");
    lv_obj_set_style_text_font(lbl_track_counter, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_track_counter, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_track_counter, LV_ALIGN_BOTTOM_RIGHT, -4, -2);

    // 2. Track Title
    lbl_title = lv_label_create(parent);
    lv_label_set_text(lbl_title, "No Track Playing");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(lbl_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_title, 224);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 14);

    // 3. Artist & Album
    lbl_artist = lv_label_create(parent);
    lv_label_set_text(lbl_artist, "Ready for stream");
    lv_obj_set_style_text_font(lbl_artist, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_artist, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_align(lbl_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_artist, 224);
    lv_obj_align(lbl_artist, LV_ALIGN_TOP_MID, 0, 42);

    // 4. Progress Bar & Elapsed/Total Time
    bar_progress = lv_bar_create(parent);
    lv_obj_set_size(bar_progress, 220, 6);
    lv_obj_align(bar_progress, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_style_bg_color(bar_progress, lv_color_hex(0x242A35), 0);
    lv_obj_set_style_bg_opa(bar_progress, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bar_progress, lv_color_hex(0x3A4252), 0);
    lv_obj_set_style_border_width(bar_progress, 1, 0);
    lv_obj_set_style_radius(bar_progress, 3, 0);
    lv_obj_set_style_bg_color(bar_progress, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_progress, 3, LV_PART_INDICATOR);

    lv_bar_set_range(bar_progress, 0, PROGRESS_BAR_MAX);
    lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);
    lv_obj_add_flag(bar_progress, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(bar_progress, 20);
    lv_obj_clear_flag(bar_progress, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(bar_progress, event_slider_seek, LV_EVENT_ALL, nullptr);

    // Ghost/Secondary Seek Target Indicator (semi-transparent accent circle)
    obj_seek_target = lv_obj_create(parent);
    lv_obj_set_size(obj_seek_target, 14, 14);
    lv_obj_set_style_radius(obj_seek_target, 7, 0);
    lv_obj_set_style_bg_color(obj_seek_target, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(obj_seek_target, LV_OPA_70, 0);
    lv_obj_set_style_border_color(obj_seek_target, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(obj_seek_target, 1, 0);
    lv_obj_set_style_pad_all(obj_seek_target, 0, 0);
    lv_obj_clear_flag(obj_seek_target, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(obj_seek_target, LV_OBJ_FLAG_HIDDEN);

    lbl_time_cur = lv_label_create(parent);
    lv_label_set_text(lbl_time_cur, "00:00");
    lv_obj_set_style_text_font(lbl_time_cur, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_time_cur, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_time_cur, LV_ALIGN_TOP_LEFT, 10, 90);

    lbl_time_total = lv_label_create(parent);
    lv_label_set_text(lbl_time_total, "--:--");
    lv_obj_set_style_text_font(lbl_time_total, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_time_total, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_time_total, LV_ALIGN_TOP_RIGHT, -10, 90);

    // 5. Transport Controls Row
    lv_obj_t* trans_cont = lv_obj_create(parent);
    lv_obj_set_size(trans_cont, 224, 60);
    lv_obj_align(trans_cont, LV_ALIGN_TOP_MID, 0, 118);
    lv_obj_set_style_bg_opa(trans_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(trans_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(trans_cont, 0, 0);
    lv_obj_clear_flag(trans_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Previous Track Button
    btn_prev = lv_btn_create(trans_cont);
    lv_obj_set_size(btn_prev, 54, 54);
    lv_obj_align(btn_prev, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_set_style_bg_color(btn_prev, lv_color_hex(0x222732), 0);
    lv_obj_set_style_border_color(btn_prev, lv_color_hex(0x3A4252), 0);
    lv_obj_set_style_border_width(btn_prev, 1, 0);
    lv_obj_set_style_radius(btn_prev, 27, 0);
    lv_obj_add_event_cb(btn_prev, event_btn_prev, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lbl_prev = lv_label_create(btn_prev);
    lv_label_set_text(lbl_prev, LV_SYMBOL_PREV);
    lv_obj_set_style_text_font(lbl_prev, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_prev);

    // Play / Pause Button (Hero element)
    btn_play_pause = lv_btn_create(trans_cont);
    lv_obj_set_size(btn_play_pause, 54, 54);
    lv_obj_align(btn_play_pause, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(btn_play_pause, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(btn_play_pause, 27, 0);
    lv_obj_add_event_cb(btn_play_pause, event_btn_play_pause, LV_EVENT_CLICKED, nullptr);
    lbl_play_pause = lv_label_create(btn_play_pause);
    lv_label_set_text(lbl_play_pause, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(lbl_play_pause, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_play_pause, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_play_pause);

    // Next Track Button
    btn_next = lv_btn_create(trans_cont);
    lv_obj_set_size(btn_next, 54, 54);
    lv_obj_align(btn_next, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_style_bg_color(btn_next, lv_color_hex(0x222732), 0);
    lv_obj_set_style_border_color(btn_next, lv_color_hex(0x3A4252), 0);
    lv_obj_set_style_border_width(btn_next, 1, 0);
    lv_obj_set_style_radius(btn_next, 27, 0);
    lv_obj_add_event_cb(btn_next, event_btn_next, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lbl_next = lv_label_create(btn_next);
    lv_label_set_text(lbl_next, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_font(lbl_next, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_next);

    // 1. Previous button pressed feedback (Micro-compression, bright slate bg, glowing cyan border)
    lv_obj_set_style_transform_width(btn_prev, -3, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn_prev, -3, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn_prev, lv_color_hex(0x3B4455), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_prev, COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_prev, 2, LV_STATE_PRESSED);

    // 2. Play / Pause button pressed feedback (Micro-compression, deep electric cyan bg, crisp white perimeter ring)
    lv_obj_set_style_transform_width(btn_play_pause, -3, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn_play_pause, -3, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn_play_pause, lv_color_hex(0x0088A8), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_play_pause, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_play_pause, 2, LV_STATE_PRESSED);

    // 3. Next button pressed feedback (Micro-compression, bright slate bg, glowing cyan border)
    lv_obj_set_style_transform_width(btn_next, -3, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn_next, -3, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn_next, lv_color_hex(0x3B4455), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_next, COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_next, 2, LV_STATE_PRESSED);

    // 6. Compact Volume / Mute Button (Centered, speaker + padlock, matching WiiM app)
    btn_mute = lv_btn_create(parent);
    lv_obj_set_size(btn_mute, 68, 40);
    lv_obj_align(btn_mute, LV_ALIGN_TOP_MID, 0, 186);
    lv_obj_set_style_bg_color(btn_mute, lv_color_hex(0x222732), 0);
    lv_obj_set_style_border_width(btn_mute, 1, 0);
    lv_obj_set_style_border_color(btn_mute, lv_color_hex(0x3A4252), 0);
    lv_obj_set_style_radius(btn_mute, 10, 0);
    lv_obj_set_style_pad_all(btn_mute, 0, 0);
    lv_obj_clear_flag(btn_mute, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_mute, event_btn_mute, LV_EVENT_CLICKED, nullptr);

    // Pressed feedback for Mute button
    lv_obj_set_style_transform_width(btn_mute, -2, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn_mute, -2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn_mute, lv_color_hex(0x3B4455), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_mute, COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_mute, 2, LV_STATE_PRESSED);

    lbl_mute = lv_label_create(btn_mute);
    lv_label_set_text(lbl_mute, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(lbl_mute, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_mute, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(lbl_mute, LV_ALIGN_LEFT_MID, 12, 0);

    img_lock = lv_img_create(btn_mute);
    lv_img_set_src(img_lock, &img_lock_dsc);
    lv_obj_set_style_img_recolor(img_lock, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_img_recolor_opa(img_lock, 255, 0);
    lv_obj_align(img_lock, LV_ALIGN_LEFT_MID, 38, 0);

    lbl_x = lv_label_create(btn_mute);
    lv_label_set_text(lbl_x, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(lbl_x, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_x, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_x, LV_ALIGN_LEFT_MID, 38, 0);
    lv_obj_add_flag(lbl_x, LV_OBJ_FLAG_HIDDEN);

    // 6B. Variable Volume Row Container (Y = 186, width 224, height 40)
    obj_vol_var_cont = lv_obj_create(parent);
    lv_obj_set_size(obj_vol_var_cont, 224, 40);
    lv_obj_align(obj_vol_var_cont, LV_ALIGN_TOP_MID, 0, 186);
    lv_obj_set_style_bg_opa(obj_vol_var_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(obj_vol_var_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(obj_vol_var_cont, 0, 0);
    lv_obj_clear_flag(obj_vol_var_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj_vol_var_cont, LV_OBJ_FLAG_HIDDEN); // Hidden by default

    // Variable Mute Button (Left, 38x38)
    btn_var_mute = lv_btn_create(obj_vol_var_cont);
    lv_obj_set_size(btn_var_mute, 38, 38);
    lv_obj_align(btn_var_mute, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_var_mute, lv_color_hex(0x222732), 0);
    lv_obj_set_style_border_width(btn_var_mute, 1, 0);
    lv_obj_set_style_border_color(btn_var_mute, lv_color_hex(0x3A4252), 0);
    lv_obj_set_style_radius(btn_var_mute, 8, 0);
    lv_obj_set_style_pad_all(btn_var_mute, 0, 0);
    lv_obj_clear_flag(btn_var_mute, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_var_mute, event_btn_mute, LV_EVENT_CLICKED, nullptr);

    // Pressed feedback for Variable Mute button
    lv_obj_set_style_transform_width(btn_var_mute, -2, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn_var_mute, -2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn_var_mute, lv_color_hex(0x3B4455), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_var_mute, COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_var_mute, 2, LV_STATE_PRESSED);

    lbl_var_mute = lv_label_create(btn_var_mute);
    lv_label_set_text(lbl_var_mute, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(lbl_var_mute, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_var_mute, COLOR_TEXT_PRIMARY, 0);
    lv_obj_center(lbl_var_mute);

    lbl_var_x = lv_label_create(btn_var_mute);
    lv_label_set_text(lbl_var_x, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(lbl_var_x, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_var_x, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_var_x, LV_ALIGN_CENTER, 8, -6);
    lv_obj_add_flag(lbl_var_x, LV_OBJ_FLAG_HIDDEN);

    // Interactive Volume Slider (Center, 130x8)
    slider_vol = lv_slider_create(obj_vol_var_cont);
    lv_obj_set_size(slider_vol, 130, 8);
    lv_obj_align(slider_vol, LV_ALIGN_LEFT_MID, 46, 0);
    lv_slider_set_range(slider_vol, 0, 100);
    lv_slider_set_value(slider_vol, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_vol, lv_color_hex(0x242A35), 0);
    lv_obj_set_style_bg_opa(slider_vol, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(slider_vol, lv_color_hex(0x3A4252), 0);
    lv_obj_set_style_border_width(slider_vol, 1, 0);
    lv_obj_set_style_radius(slider_vol, 4, 0);

    // Indicator (Filled cyan bar)
    lv_obj_set_style_bg_color(slider_vol, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider_vol, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider_vol, 4, LV_PART_INDICATOR);

    // Knob / Thumb
    lv_obj_set_style_bg_color(slider_vol, COLOR_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider_vol, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider_vol, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_border_width(slider_vol, 2, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_vol, 4, LV_PART_KNOB);
    lv_obj_set_style_radius(slider_vol, 8, LV_PART_KNOB);

    // Generous touch hit area
    lv_obj_set_ext_click_area(slider_vol, 14);
    lv_obj_clear_flag(slider_vol, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(slider_vol, event_slider_vol, LV_EVENT_ALL, nullptr);

    // Live Percentage Label (Right, 42px)
    lbl_vol_percent = lv_label_create(obj_vol_var_cont);
    lv_label_set_text(lbl_vol_percent, "0%");
    lv_obj_set_style_text_font(lbl_vol_percent, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_vol_percent, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(lbl_vol_percent, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(lbl_vol_percent, 42);
    lv_obj_align(lbl_vol_percent, LV_ALIGN_RIGHT_MID, 0, 0);
}

// Build Presets Drawer / List Tab (12 Presets)
static void build_presets_tab(lv_obj_t* parent) {
    lv_obj_set_style_bg_color(parent, COLOR_BG, 0);
    lv_obj_set_style_pad_all(parent, 6, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 6, 0);

    for (uint8_t i = 0; i < MAX_PRESETS; ++i) {
        preset_btns[i] = lv_btn_create(parent);
        lv_obj_set_size(preset_btns[i], 224, 44);
        lv_obj_set_style_bg_color(preset_btns[i], COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(preset_btns[i], COLOR_SURFACE_LIGHT, 0);
        lv_obj_set_style_border_width(preset_btns[i], 1, 0);
        lv_obj_set_style_radius(preset_btns[i], 8, 0);
        lv_obj_set_style_pad_all(preset_btns[i], 4, 0);
        lv_obj_set_style_bg_color(preset_btns[i], COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
        lv_obj_clear_flag(preset_btns[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(preset_btns[i], event_btn_preset, LV_EVENT_CLICKED, (void*)(uintptr_t)(i + 1));

        // Preset Index Badge (Circle Pill)
        lv_obj_t* badge = lv_obj_create(preset_btns[i]);
        lv_obj_set_size(badge, 26, 26);
        lv_obj_align(badge, LV_ALIGN_LEFT_MID, 2, 0);
        lv_obj_set_style_bg_color(badge, COLOR_SURFACE_LIGHT, 0);
        lv_obj_set_style_radius(badge, 13, 0);
        lv_obj_set_style_border_side(badge, LV_BORDER_SIDE_NONE, 0);
        lv_obj_set_style_pad_all(badge, 0, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl_idx = lv_label_create(badge);
        char idx_str[8];
        snprintf(idx_str, sizeof(idx_str), "%d", i + 1);
        lv_label_set_text(lbl_idx, idx_str);
        lv_obj_set_style_text_font(lbl_idx, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl_idx, COLOR_ACCENT, 0);
        lv_obj_center(lbl_idx);

        // Preset Title (Full readable name)
        preset_lbls[i] = lv_label_create(preset_btns[i]);
        char default_name[32];
        snprintf(default_name, sizeof(default_name), "Preset %d", i + 1);
        lv_label_set_text(preset_lbls[i], default_name);
        lv_label_set_long_mode(preset_lbls[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(preset_lbls[i], 148);
        lv_obj_set_style_text_font(preset_lbls[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(preset_lbls[i], COLOR_TEXT_PRIMARY, 0);
        lv_obj_align(preset_lbls[i], LV_ALIGN_LEFT_MID, 36, 0);

        // Right Play Arrow Indicator
        lv_obj_t* icon_play = lv_label_create(preset_btns[i]);
        lv_label_set_text(icon_play, LV_SYMBOL_PLAY);
        lv_obj_set_style_text_font(icon_play, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(icon_play, COLOR_TEXT_MUTED, 0);
        lv_obj_align(icon_play, LV_ALIGN_RIGHT_MID, -6, 0);
    }
}

// Build Device Selection Modal Overlay
static void build_device_modal() {
    modal_device_selector = lv_obj_create(lv_scr_act());
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

// Wi-Fi Configuration Modal Callbacks
static void event_btn_close_wifi_modal(lv_event_t* e) {
    if (timer_wifi_close) {
        lv_timer_del(timer_wifi_close);
        timer_wifi_close = nullptr;
    }
    if (modal_wifi_selector) {
        lv_obj_add_flag(modal_wifi_selector, LV_OBJ_FLAG_HIDDEN);
    }
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

// Build Wi-Fi Configuration Modal
static void build_wifi_modal() {
    modal_wifi_selector = lv_obj_create(lv_scr_act());
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

void ui_init() {
    lv_obj_set_style_bg_color(lv_scr_act(), COLOR_BG, 0);

    // 1. Build persistent header
    build_header(lv_scr_act());

    // 2. Build bottom-oriented Tabview (Player & Presets)
    tabview = lv_tabview_create(lv_scr_act(), LV_DIR_BOTTOM, 28);
    lv_obj_set_size(tabview, 240, 294);
    lv_obj_align(tabview, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(tabview, COLOR_BG, 0);

    // Style the Tabview Navigation Bar
    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_set_style_bg_color(tab_btns, COLOR_SURFACE, 0);
    lv_obj_set_style_pad_all(tab_btns, 0, 0);
    lv_obj_set_style_pad_gap(tab_btns, 0, 0);
    lv_obj_set_style_border_side(tab_btns, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(tab_btns, lv_color_hex(0x3A4252), 0);
    lv_obj_set_style_border_width(tab_btns, 1, 0);

    // Tab buttons (LV_PART_ITEMS) styling matching the app theme
    lv_obj_set_style_text_font(tab_btns, &lv_font_montserrat_12, LV_PART_ITEMS);
    lv_obj_set_style_text_color(tab_btns, COLOR_TEXT_MUTED, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(tab_btns, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_border_side(tab_btns, LV_BORDER_SIDE_NONE, LV_PART_ITEMS);

    // Active/Checked Tab styling (Cyan theme instead of default blue)
    lv_obj_set_style_text_color(tab_btns, COLOR_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x222732), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(tab_btns, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(tab_btns, LV_BORDER_SIDE_TOP, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(tab_btns, COLOR_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tab_btns, 2, LV_PART_ITEMS | LV_STATE_CHECKED);

    // Add Tabs
    tab_player = lv_tabview_add_tab(tabview, LV_SYMBOL_AUDIO " Player");
    tab_presets = lv_tabview_add_tab(tabview, LV_SYMBOL_LIST " Presets");

    build_player_tab(tab_player);
    build_presets_tab(tab_presets);

    // 3. Build modal pickers
    build_device_modal();
    build_wifi_modal();

    // 4. Progress Interpolation Timer (50ms = 20 FPS updates)
    lv_timer_create(timer_progress_cb, 50, nullptr);
}

void ui_set_wifi_status(bool connected, int8_t rssi, const char* ip, const char* ssid) {
    current_wifi_connected = connected;
    if (ssid && strlen(ssid) > 0) {
        strncpy(current_connected_ssid, ssid, sizeof(current_connected_ssid) - 1);
        current_connected_ssid[sizeof(current_connected_ssid) - 1] = '\0';
    } else if (!connected) {
        current_connected_ssid[0] = '\0';
    }

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

    // Update the connected banner in wifi_view_list if initialized
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

void ui_set_devices(const DeviceList& list) {
    current_device_list = list;

    // 1. Update Header Button with active device name
    bool activeFound = false;
    for (uint8_t i = 0; i < list.count; ++i) {
        if (list.devices[i].is_active) {
            char title[64];
            snprintf(title, sizeof(title), "%s " LV_SYMBOL_DOWN, list.devices[i].name);
            lv_label_set_text(lbl_active_device, title);
            activeFound = true;
            break;
        }
    }
    if (!activeFound && list.count > 0) {
        char title[64];
        snprintf(title, sizeof(title), "%s " LV_SYMBOL_DOWN, list.devices[0].name);
        lv_label_set_text(lbl_active_device, title);
    } else if (list.count == 0) {
        lv_label_set_text(lbl_active_device, "No Devices " LV_SYMBOL_DOWN);
    }

    // 2. Rebuild list inside Device Selector Modal
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

    // 3. Update Scan Status in Modal
    if (lbl_scan_status) {
        char status[32];
        if (list.count == 1) {
            snprintf(status, sizeof(status), "1 streamer found");
        } else {
            snprintf(status, sizeof(status), "%d streamers found", list.count);
        }
        lv_label_set_text(lbl_scan_status, status);
        lv_obj_set_style_text_color(lbl_scan_status, COLOR_TEXT_MUTED, 0);
    }
}

void ui_set_player_state(const PlayerState& state) {
    current_play_state = state.state;
    current_mute_state = state.mute;

    // 1. Play / Pause Button Symbol
    if (lbl_play_pause) {
        if (state.state == PLAY_STATE_PLAYING) {
            lv_label_set_text(lbl_play_pause, LV_SYMBOL_PAUSE);
        } else {
            lv_label_set_text(lbl_play_pause, LV_SYMBOL_PLAY);
        }
    }

    // 2. Adaptive Volume Controls (Fixed Mode vs Variable Mode)
    if (state.is_fixed_volume) {
        // FIXED VOLUME MODE
        if (obj_vol_var_cont) lv_obj_add_flag(obj_vol_var_cont, LV_OBJ_FLAG_HIDDEN);
        if (btn_mute) lv_obj_clear_flag(btn_mute, LV_OBJ_FLAG_HIDDEN);

        if (btn_mute && lbl_mute) {
            if (state.mute) {
                // Muted: Vibrant Red with Speaker + X (no border)
                lv_obj_set_style_bg_color(btn_mute, lv_color_hex(0xE63946), 0);
                lv_obj_set_style_border_width(btn_mute, 0, 0);
                lv_obj_set_style_bg_color(btn_mute, lv_color_hex(0x9E1B26), LV_STATE_PRESSED);
                lv_obj_set_style_border_color(btn_mute, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
                lv_obj_set_style_border_width(btn_mute, 2, LV_STATE_PRESSED);
                lv_label_set_text(lbl_mute, LV_SYMBOL_VOLUME_MAX);
                lv_obj_set_style_text_color(lbl_mute, lv_color_hex(0xFFFFFF), 0);
                lv_obj_align(lbl_mute, LV_ALIGN_LEFT_MID, 12, 0);
                if (img_lock) lv_obj_add_flag(img_lock, LV_OBJ_FLAG_HIDDEN);
                if (lbl_x) {
                    lv_obj_clear_flag(lbl_x, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_align(lbl_x, LV_ALIGN_LEFT_MID, 38, 0);
                }
            } else {
                // Unmuted: Charcoal with Speaker + Padlock
                lv_obj_set_style_bg_color(btn_mute, lv_color_hex(0x222732), 0);
                lv_obj_set_style_border_width(btn_mute, 1, 0);
                lv_obj_set_style_border_color(btn_mute, lv_color_hex(0x3A4252), 0);
                lv_obj_set_style_bg_color(btn_mute, lv_color_hex(0x3B4455), LV_STATE_PRESSED);
                lv_obj_set_style_border_color(btn_mute, COLOR_ACCENT, LV_STATE_PRESSED);
                lv_obj_set_style_border_width(btn_mute, 2, LV_STATE_PRESSED);
                lv_label_set_text(lbl_mute, LV_SYMBOL_VOLUME_MAX);
                lv_obj_set_style_text_color(lbl_mute, COLOR_TEXT_PRIMARY, 0);
                lv_obj_align(lbl_mute, LV_ALIGN_LEFT_MID, 12, 0);
                if (lbl_x) lv_obj_add_flag(lbl_x, LV_OBJ_FLAG_HIDDEN);
                if (img_lock) {
                    lv_obj_clear_flag(img_lock, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_align(img_lock, LV_ALIGN_LEFT_MID, 38, 0);
                }
            }
        }
    } else {
        // VARIABLE VOLUME MODE
        if (btn_mute) lv_obj_add_flag(btn_mute, LV_OBJ_FLAG_HIDDEN);
        if (obj_vol_var_cont) lv_obj_clear_flag(obj_vol_var_cont, LV_OBJ_FLAG_HIDDEN);

        // Update Slider and Percentage (only if user is not currently dragging it)
        if (!is_user_adjusting_volume) {
            current_volume = state.volume;
            if (slider_vol) lv_slider_set_value(slider_vol, state.volume, LV_ANIM_OFF);
            if (lbl_vol_percent) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d%%", state.volume);
                lv_label_set_text(lbl_vol_percent, buf);
            }
        }

        // Update Variable Mute Button
        if (btn_var_mute && lbl_var_mute) {
            if (state.mute) {
                lv_obj_set_style_bg_color(btn_var_mute, lv_color_hex(0xE63946), 0);
                lv_obj_set_style_border_width(btn_var_mute, 0, 0);
                lv_obj_set_style_text_color(lbl_var_mute, lv_color_hex(0xFFFFFF), 0);
                if (lbl_var_x) lv_obj_clear_flag(lbl_var_x, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_set_style_bg_color(btn_var_mute, lv_color_hex(0x222732), 0);
                lv_obj_set_style_border_width(btn_var_mute, 1, 0);
                lv_obj_set_style_border_color(btn_var_mute, lv_color_hex(0x3A4252), 0);
                lv_obj_set_style_text_color(lbl_var_mute, COLOR_TEXT_PRIMARY, 0);
                if (lbl_var_x) lv_obj_add_flag(lbl_var_x, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // 3. Progress Slider & Times
    // Atomic Update: Only update total duration, current position, and progress bar TOGETHER
    // when both values are confirmed valid and belong to an active playing or paused state.
    // During track transitions or buffering, leave the current display untouched until confirmed.
    bool is_valid_playback = (state.state == PLAY_STATE_PLAYING || state.state == PLAY_STATE_PAUSED);
    if (is_valid_playback && state.totlen_ms > 0 && state.curpos_ms <= state.totlen_ms) {
        current_totlen_ms = state.totlen_ms;
        current_actual_curpos_ms = state.curpos_ms;
        last_progress_tick_ms = millis();

        // Check if streamer has buffered and caught up with the seek target
        if (has_pending_seek) {
            int32_t diff = (int32_t)state.curpos_ms - (int32_t)pending_seek_target_ms;
            if (abs(diff) <= 3000 || (millis() - pending_seek_timestamp > 5000)) {
                has_pending_seek = false;
                if (obj_seek_target) lv_obj_add_flag(obj_seek_target, LV_OBJ_FLAG_HIDDEN);
            }
        }

        if (bar_progress && !is_user_seeking) {
            char time_tot_buf[16] = "--:--";
            format_time(state.totlen_ms, time_tot_buf, sizeof(time_tot_buf));
            if (lbl_time_total) lv_label_set_text(lbl_time_total, time_tot_buf);

            if (!has_pending_seek) {
                char time_cur_buf[16] = "00:00";
                format_time(state.curpos_ms, time_cur_buf, sizeof(time_cur_buf));
                uint32_t val = (uint32_t)(((uint64_t)state.curpos_ms * PROGRESS_BAR_MAX) / state.totlen_ms);
                if (val > PROGRESS_BAR_MAX) val = PROGRESS_BAR_MAX;
                lv_bar_set_value(bar_progress, (int32_t)val, LV_ANIM_OFF);
                if (lbl_time_cur) lv_label_set_text(lbl_time_cur, time_cur_buf);
            }
        }
    }

    // 4. Stream Info Details Line
    if (strlen(state.vendor) > 0) {
        strncpy(current_vendor, state.vendor, sizeof(current_vendor) - 1);
    }
    if (state.stream.sample_rate > 0) {
        current_sample_rate = state.stream.sample_rate;
        current_bit_depth = state.stream.bit_depth;
    }
    if (state.track_total > 0) {
        current_track_num = state.track_num;
        current_track_total = state.track_total;
    }
    update_stream_info_labels();
}

void ui_set_track_meta(const TrackMeta& meta) {
    if (lbl_title) {
        if (strlen(meta.title) > 0) {
            lv_point_t size;
            lv_txt_get_size(&size, meta.title, &lv_font_montserrat_22, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (size.x > 224) {
                char formatted_title[192];
                snprintf(formatted_title, sizeof(formatted_title), "%s                    ", meta.title);
                lv_label_set_text(lbl_title, formatted_title);
            } else {
                lv_label_set_text(lbl_title, meta.title);
            }
        } else {
            lv_label_set_text(lbl_title, "No Title");
        }
    }

    if (lbl_artist) {
        char artist_album[192];
        if (strlen(meta.artist) > 0 && strlen(meta.album) > 0) {
            snprintf(artist_album, sizeof(artist_album), "%s - %s", meta.artist, meta.album);
        } else if (strlen(meta.artist) > 0) {
            snprintf(artist_album, sizeof(artist_album), "%s", meta.artist);
        } else if (strlen(meta.album) > 0) {
            snprintf(artist_album, sizeof(artist_album), "%s", meta.album);
        } else {
            snprintf(artist_album, sizeof(artist_album), "Ready");
        }

        lv_point_t size;
        lv_txt_get_size(&size, artist_album, &lv_font_montserrat_16, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x > 224) {
            char formatted_artist[256];
            snprintf(formatted_artist, sizeof(formatted_artist), "%s                    ", artist_album);
            lv_label_set_text(lbl_artist, formatted_artist);
        } else {
            lv_label_set_text(lbl_artist, artist_album);
        }
    }

    if (meta.sample_rate > 0) {
        current_sample_rate = meta.sample_rate;
        current_bit_depth = meta.bit_depth;
    } else {
        current_sample_rate = 0;
        current_bit_depth = 0;
    }

    has_pending_seek = false;
    if (obj_seek_target) lv_obj_add_flag(obj_seek_target, LV_OBJ_FLAG_HIDDEN);

    update_stream_info_labels();
}

void ui_set_presets(const PresetList& presets) {
    for (uint8_t i = 0; i < presets.count && i < MAX_PRESETS; ++i) {
        if (preset_lbls[i]) {
            lv_label_set_text(preset_lbls[i], presets.items[i].name);
        }
    }
}

void ui_set_scanning(bool is_scanning) {
    if (lbl_scan_status) {
        if (is_scanning) {
            lv_label_set_text(lbl_scan_status, "Scanning for streamers...");
            lv_obj_set_style_text_color(lbl_scan_status, COLOR_ACCENT, 0);
        } else {
            char status[32];
            if (current_device_list.count == 1) {
                snprintf(status, sizeof(status), "1 streamer found");
            } else {
                snprintf(status, sizeof(status), "%d streamers found", current_device_list.count);
            }
            lv_label_set_text(lbl_scan_status, status);
            lv_obj_set_style_text_color(lbl_scan_status, COLOR_TEXT_MUTED, 0);
        }
    }
}


static void timer_close_wifi_modal_cb(lv_timer_t* timer) {
    if (modal_wifi_selector) {
        lv_obj_add_flag(modal_wifi_selector, LV_OBJ_FLAG_HIDDEN);
    }
    timer_wifi_close = nullptr;
}

void ui_open_wifi_modal() {
    if (!modal_wifi_selector) return;

    if (timer_wifi_close) {
        lv_timer_del(timer_wifi_close);
        timer_wifi_close = nullptr;
    }

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

void ui_set_wifi_scan_results(const WiFiScanList& list) {
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

void ui_wifi_on_connected(const char* ip) {
    if (!modal_wifi_selector || lv_obj_has_flag(modal_wifi_selector, LV_OBJ_FLAG_HIDDEN)) return;

    // Only proceed if user was actively on the connecting screen
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

void ui_wifi_on_connect_failed() {
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

void ui_process_events() {
    UiEvent evt;
    while (xQueueReceive(xQueueUiState, &evt, 0) == pdTRUE) {
        switch (evt.type) {
            case UI_EVT_WIFI_STATUS:
                ui_set_wifi_status(evt.data.wifi.connected, evt.data.wifi.rssi, evt.data.wifi.ip, evt.data.wifi.ssid);
                break;
            case UI_EVT_WIFI_CONNECT_SUCCESS:
                ui_wifi_on_connected(evt.data.wifi.ip);
                break;
            case UI_EVT_WIFI_SCAN_RESULT:
                if (evt.data.wifi_scan) {
                    ui_set_wifi_scan_results(*evt.data.wifi_scan);
                }
                break;
            case UI_EVT_WIFI_SETUP_REQUIRED:
                ui_open_wifi_modal();
                break;
            case UI_EVT_WIFI_CONNECT_FAILED:
                ui_wifi_on_connect_failed();
                break;
            case UI_EVT_DEVICES_UPDATED:
                if (evt.data.devices) {
                    ui_set_devices(*evt.data.devices);
                }
                break;
            case UI_EVT_PLAYER_STATE:
                ui_set_player_state(evt.data.player);
                break;
            case UI_EVT_META_UPDATED:
                if (evt.data.meta) {
                    ui_set_track_meta(*evt.data.meta);
                }
                break;
            case UI_EVT_PRESETS_UPDATED:
                if (evt.data.presets) {
                    ui_set_presets(*evt.data.presets);
                }
                break;
            case UI_EVT_SCAN_STATUS:
                ui_set_scanning(evt.data.scan.is_scanning);
                break;
        }
        ui_event_free(&evt);
    }
}

