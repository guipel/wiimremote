#include "ui/ui_player.h"
#include "ui/ui_theme.h"
#include "config.h"

// External Queue for sending UI Commands to Network Task
extern QueueHandle_t xQueueUiCmd;

// UI Widgets - Player Tab
static lv_obj_t* s_parent = nullptr;
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
// UI Widgets - Volume & Aux Controls
static lv_obj_t* obj_vol_var_cont = nullptr;
static lv_obj_t* btn_var_mute = nullptr;
static lv_obj_t* lbl_var_mute = nullptr;
static lv_obj_t* slider_vol = nullptr;
static lv_obj_t* lbl_vol_percent = nullptr;
static lv_obj_t* btn_var_input = nullptr;
static lv_obj_t* lbl_var_input = nullptr;

// State tracking
static bool current_is_fixed = false;
static bool is_user_adjusting_volume = false;
static uint8_t current_volume = 0;
static uint8_t s_last_enqueued_vol = 0xFF;
static unsigned long s_last_vol_enqueue_ms = 0;
static bool is_user_seeking = false;
static bool current_mute_state = false;
static uint32_t current_totlen_ms = 0;
static uint32_t current_actual_curpos_ms = 0;
static int32_t s_last_bar_val = -1;
static uint32_t s_last_displayed_sec = 0xFFFFFFFF;
static lv_obj_t* obj_seek_target = nullptr;
static uint32_t s_seek_target_ms = 0;

static uint32_t current_sample_rate = 0;
static uint8_t current_bit_depth = 0;
static uint16_t current_track_num = 0;
static uint16_t current_track_total = 0;

// Input Source cycling state (0=wifi, 1=bluetooth, 2=line-in, 3=optical)
static uint8_t s_current_input_idx = 0;
static unsigned long s_last_input_switch_ms = 0;

static uint8_t linkplay_mode_to_input_idx(uint8_t mode) {
    if (mode == 41) return 1; // Bluetooth
    if (mode == 40) return 2; // Line-In
    if (mode == 43) return 3; // Optical
    return 0;                 // Wi-Fi / Network
}

static void update_input_mode_button() {
    if (!lbl_var_input) return;
    switch (s_current_input_idx) {
        case 1: // Bluetooth
            lv_label_set_text(lbl_var_input, LV_SYMBOL_BLUETOOTH);
            lv_obj_set_style_text_font(lbl_var_input, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(lbl_var_input, COLOR_ACCENT, 0);
            break;
        case 2: // Line-In
            lv_label_set_text(lbl_var_input, "AUX");
            lv_obj_set_style_text_font(lbl_var_input, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(lbl_var_input, COLOR_ACCENT, 0);
            break;
        case 3: // Optical
            lv_label_set_text(lbl_var_input, "OPT");
            lv_obj_set_style_text_font(lbl_var_input, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(lbl_var_input, COLOR_ACCENT, 0);
            break;
        default: // Wi-Fi / Network
            lv_label_set_text(lbl_var_input, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_font(lbl_var_input, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(lbl_var_input, COLOR_TEXT_PRIMARY, 0);
            break;
    }
}

static void set_bar_and_time(uint32_t cur_ms, uint32_t tot_ms, bool force) {
    uint32_t cur_sec = cur_ms / 1000;
    if (lbl_time_cur && (force || cur_sec != s_last_displayed_sec)) {
        s_last_displayed_sec = cur_sec;
        char time_cur_buf[16];
        format_time(cur_ms, time_cur_buf, sizeof(time_cur_buf));
        lv_label_set_text(lbl_time_cur, time_cur_buf);
    }

    if (bar_progress) {
        int32_t val = (tot_ms > 0)
            ? (int32_t)(((uint64_t)cur_ms * PROGRESS_BAR_MAX) / tot_ms)
            : 0;
        if (val > PROGRESS_BAR_MAX) val = PROGRESS_BAR_MAX;
        if (val < 0) val = 0;
        if (force || val != s_last_bar_val) {
            s_last_bar_val = val;
            lv_bar_set_value(bar_progress, val, LV_ANIM_OFF);
        }
    }
}

// Helper: Update audio resolution (top left) and track counter (top right)
static void update_stream_info_labels() {
    if (lbl_resolution) {
        char res_buf[32] = "";
        if (current_sample_rate > 0) {
            if (current_is_fixed && current_bit_depth == 32 && current_sample_rate == 176400) {
                snprintf(res_buf, sizeof(res_buf), "DSD");
            } else {
                uint8_t display_depth = current_bit_depth;
                if (display_depth >= 32) display_depth = 24;
                uint32_t khz = current_sample_rate / 1000;
                if (display_depth > 0) {
                    snprintf(res_buf, sizeof(res_buf), "%u/%u", (unsigned int)display_depth, (unsigned int)khz);
                } else {
                    snprintf(res_buf, sizeof(res_buf), "%u", (unsigned int)khz);
                }
            }
        }
        lv_label_set_text(lbl_resolution, res_buf);
    }

    if (lbl_track_counter) {
        char track_buf[24] = "";
        if (current_track_total > 0 && current_track_num > 0) {
            snprintf(track_buf, sizeof(track_buf), "%u/%u", (unsigned int)current_track_num, (unsigned int)current_track_total);
        }
        lv_label_set_text(lbl_track_counter, track_buf);
    }
}

// Helper: Convert touch X coordinate on progress bar to seek target in ms (aligned to whole seconds)
static uint32_t touch_x_to_seek_ms(lv_indev_t* indev, int32_t* out_cx) {
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int32_t cx = p.x;
    if (cx < 10) cx = 10;
    if (cx > 230) cx = 230;
    if (out_cx) *out_cx = cx;
    int32_t val = ((cx - 10) * PROGRESS_BAR_MAX) / 220;
    if (val < 0) val = 0;
    if (val > PROGRESS_BAR_MAX) val = PROGRESS_BAR_MAX;
    uint32_t raw_ms = (uint32_t)(((uint64_t)val * (uint64_t)current_totlen_ms) / PROGRESS_BAR_MAX);
    return (raw_ms / 1000) * 1000;
}

// Event Callback - Track Progress Bar Seeking
static void event_slider_seek(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        if (current_totlen_ms > 0) {
            is_user_seeking = true;
            lv_indev_t* indev = lv_indev_get_act();
            if (indev) {
                int32_t cx = 0;
                s_seek_target_ms = touch_x_to_seek_ms(indev, &cx);

                char preview_buf[16];
                format_time(s_seek_target_ms, preview_buf, sizeof(preview_buf));
                if (lbl_time_cur) lv_label_set_text(lbl_time_cur, preview_buf);
                s_last_displayed_sec = s_seek_target_ms / 1000;

                if (obj_seek_target) {
                    lv_obj_set_pos(obj_seek_target, cx - 7, 74);
                    lv_obj_clear_flag(obj_seek_target, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!is_user_seeking) return;
        is_user_seeking = false;

        if (code == LV_EVENT_RELEASED && current_totlen_ms > 0) {
            lv_indev_t* indev = lv_indev_get_act();
            if (indev) {
                int32_t cx = 0;
                s_seek_target_ms = touch_x_to_seek_ms(indev, &cx);
            }
        }

        if (obj_seek_target) {
            lv_obj_add_flag(obj_seek_target, LV_OBJ_FLAG_HIDDEN);
        }

        if (current_totlen_ms > 0) {
            UiCommand cmd;
            cmd.type = CMD_SEEK_POSITION;
            cmd.data.seek_ms = s_seek_target_ms;
            if (xQueueSend(xQueueUiCmd, &cmd, pdMS_TO_TICKS(20)) == pdTRUE) {
                current_actual_curpos_ms = s_seek_target_ms;
            }
            set_bar_and_time(current_actual_curpos_ms, current_totlen_ms, true);
        }
    }
}

// Event Callback - Interactive Volume Slider
static void event_slider_vol(lv_event_t* e) {
    if (current_is_fixed) return;
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        is_user_adjusting_volume = true;
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        int32_t val = lv_slider_get_value(slider_vol);
        if (val < 0) val = 0;
        if (val > 100) val = 100;
        current_volume = (uint8_t)val;

        if (lbl_vol_percent) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d%%", (int)val);
            lv_label_set_text(lbl_vol_percent, buf);
        }

        unsigned long now = millis();
        if (current_volume != s_last_enqueued_vol && (now - s_last_vol_enqueue_ms >= 60)) {
            UiCommand cmd;
            cmd.type = CMD_SET_VOL;
            cmd.data.volume = current_volume;
            if (xQueueSend(xQueueUiCmd, &cmd, pdMS_TO_TICKS(20)) == pdTRUE) {
                s_last_enqueued_vol = current_volume;
                s_last_vol_enqueue_ms = now;
            }
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        is_user_adjusting_volume = false;
        if (current_volume != s_last_enqueued_vol) {
            UiCommand cmd;
            cmd.type = CMD_SET_VOL;
            cmd.data.volume = current_volume;
            if (xQueueSend(xQueueUiCmd, &cmd, pdMS_TO_TICKS(20)) == pdTRUE) {
                s_last_enqueued_vol = current_volume;
                s_last_vol_enqueue_ms = millis();
            }
        }
    }
}

// Event Callbacks - Transport Controls
static void event_btn_prev(lv_event_t* e) {
    UiCommand cmd;
    cmd.type = CMD_PREV;
    cmd.data.seek_ms = current_actual_curpos_ms;
    if (xQueueSend(xQueueUiCmd, &cmd, pdMS_TO_TICKS(20)) != pdTRUE) return;

    current_actual_curpos_ms = 0;
    set_bar_and_time(0, current_totlen_ms, true);
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
    if (xQueueSend(xQueueUiCmd, &cmd, pdMS_TO_TICKS(20)) != pdTRUE) return;
}

static void event_btn_next(lv_event_t* e) {
    UiCommand cmd;
    cmd.type = CMD_NEXT;
    if (xQueueSend(xQueueUiCmd, &cmd, pdMS_TO_TICKS(20)) != pdTRUE) return;

    current_actual_curpos_ms = 0;
    set_bar_and_time(0, current_totlen_ms, true);
}

static void event_btn_mute(lv_event_t* e) {
    UiCommand cmd;
    cmd.type = CMD_SET_MUTE;
    cmd.data.mute = !current_mute_state;
    if (xQueueSend(xQueueUiCmd, &cmd, pdMS_TO_TICKS(20)) != pdTRUE) return;
}

static void event_btn_input(lv_event_t* e) {
    static const char* const kInputModes[4] = { "wifi", "bluetooth", "line-in", "optical" };
    s_current_input_idx = (s_current_input_idx + 1) % 4;
    s_last_input_switch_ms = millis();
    update_input_mode_button();

    UiCommand cmd;
    cmd.type = CMD_SWITCH_MODE;
    strncpy(cmd.data.input_mode, kInputModes[s_current_input_idx], sizeof(cmd.data.input_mode) - 1);
    cmd.data.input_mode[sizeof(cmd.data.input_mode) - 1] = '\0';
    if (xQueueSend(xQueueUiCmd, &cmd, pdMS_TO_TICKS(20)) != pdTRUE) return;
}

void ui_player_init(lv_obj_t* parent) {
    s_parent = parent;
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_style_pad_top(parent, 6, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Telemetry: Audio Resolution (Centered under progress bar) & Track Counter (Centered under Play/Pause)
    lbl_resolution = lv_label_create(parent);
    lv_label_set_text(lbl_resolution, "");
    lv_obj_set_style_text_font(lbl_resolution, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(lbl_resolution, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_resolution, LV_ALIGN_TOP_MID, 0, 90);

    lbl_track_counter = lv_label_create(parent);
    lv_label_set_text(lbl_track_counter, "");
    lv_obj_set_style_text_font(lbl_track_counter, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(lbl_track_counter, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_track_counter, LV_ALIGN_TOP_MID, 0, 184);

    // 2. Track Title
    lbl_title = lv_label_create(parent);
    lv_label_set_text(lbl_title, "");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_title, 224);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 6);

    // 3. Artist & Album
    lbl_artist = lv_label_create(parent);
    lv_label_set_text(lbl_artist, "");
    lv_obj_set_style_text_font(lbl_artist, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(lbl_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_artist, 224);
    lv_obj_align(lbl_artist, LV_ALIGN_TOP_MID, 0, 38);

    // 4. Progress Bar & Elapsed/Total Time
    bar_progress = lv_bar_create(parent);
    lv_obj_set_size(bar_progress, 220, 6);
    lv_obj_align(bar_progress, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_style_bg_opa(bar_progress, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar_progress, 1, 0);
    lv_obj_set_style_radius(bar_progress, 3, 0);
    lv_obj_set_style_bg_opa(bar_progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_progress, 3, LV_PART_INDICATOR);

    lv_bar_set_range(bar_progress, 0, PROGRESS_BAR_MAX);
    lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);
    lv_obj_add_flag(bar_progress, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(bar_progress, 20);
    lv_obj_clear_flag(bar_progress, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(bar_progress, event_slider_seek, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(bar_progress, event_slider_seek, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(bar_progress, event_slider_seek, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(bar_progress, event_slider_seek, LV_EVENT_PRESS_LOST, nullptr);

    // Ghost/Secondary Seek Target Indicator
    obj_seek_target = lv_obj_create(parent);
    lv_obj_set_size(obj_seek_target, 14, 14);
    lv_obj_set_style_radius(obj_seek_target, 7, 0);
    lv_obj_set_style_bg_opa(obj_seek_target, LV_OPA_70, 0);
    lv_obj_set_style_border_color(obj_seek_target, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(obj_seek_target, 1, 0);
    lv_obj_set_style_pad_all(obj_seek_target, 0, 0);
    lv_obj_clear_flag(obj_seek_target, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(obj_seek_target, LV_OBJ_FLAG_HIDDEN);

    lbl_time_cur = lv_label_create(parent);
    lv_label_set_text(lbl_time_cur, "00:00");
    lv_obj_set_style_text_font(lbl_time_cur, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_time_cur, LV_ALIGN_TOP_LEFT, 10, 90);

    lbl_time_total = lv_label_create(parent);
    lv_label_set_text(lbl_time_total, "--:--");
    lv_obj_set_style_text_font(lbl_time_total, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_time_total, LV_ALIGN_TOP_RIGHT, -10, 90);

    // 5. Transport Controls Row
    lv_obj_t* trans_cont = lv_obj_create(parent);
    lv_obj_set_size(trans_cont, 224, 60);
    lv_obj_align(trans_cont, LV_ALIGN_TOP_MID, 0, 122);
    lv_obj_set_style_bg_opa(trans_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(trans_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(trans_cont, 0, 0);
    lv_obj_clear_flag(trans_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Previous Track Button
    btn_prev = lv_btn_create(trans_cont);
    lv_obj_set_size(btn_prev, 54, 54);
    lv_obj_align(btn_prev, LV_ALIGN_LEFT_MID, 12, 0);
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
    lv_obj_set_style_border_width(btn_next, 1, 0);
    lv_obj_set_style_radius(btn_next, 27, 0);
    lv_obj_add_event_cb(btn_next, event_btn_next, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lbl_next = lv_label_create(btn_next);
    lv_label_set_text(lbl_next, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_font(lbl_next, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_next);

    // Micro-interactions / pressed feedback
    lv_obj_set_style_transform_width(btn_prev, -3, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn_prev, -3, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_prev, 2, LV_STATE_PRESSED);

    lv_obj_set_style_transform_width(btn_play_pause, -3, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn_play_pause, -3, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_play_pause, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_play_pause, 2, LV_STATE_PRESSED);

    lv_obj_set_style_transform_width(btn_next, -3, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn_next, -3, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_next, 2, LV_STATE_PRESSED);

    // 6. Volume & Aux Row Container (Mute, Slider, Input Source)
    obj_vol_var_cont = lv_obj_create(parent);
    lv_obj_set_size(obj_vol_var_cont, 224, 48);
    lv_obj_align(obj_vol_var_cont, LV_ALIGN_TOP_MID, 0, 197);
    lv_obj_set_style_bg_opa(obj_vol_var_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(obj_vol_var_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(obj_vol_var_cont, 0, 0);
    lv_obj_clear_flag(obj_vol_var_cont, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_CHAIN);

    // Variable Mute Button
    btn_var_mute = lv_btn_create(obj_vol_var_cont);
    lv_obj_set_size(btn_var_mute, 38, 38);
    lv_obj_align(btn_var_mute, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_border_width(btn_var_mute, 1, 0);
    lv_obj_set_style_radius(btn_var_mute, 8, 0);
    lv_obj_set_style_pad_all(btn_var_mute, 0, 0);
    lv_obj_clear_flag(btn_var_mute, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_var_mute, event_btn_mute, LV_EVENT_CLICKED, nullptr);

    lv_obj_set_style_transform_width(btn_var_mute, -2, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn_var_mute, -2, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_var_mute, 2, LV_STATE_PRESSED);

    lbl_var_mute = lv_label_create(btn_var_mute);
    lv_label_set_text(lbl_var_mute, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(lbl_var_mute, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_var_mute);

    // Input Source Selector Button (cycles Wi-Fi -> Bluetooth -> Line-In -> Optical)
    btn_var_input = lv_btn_create(obj_vol_var_cont);
    lv_obj_set_size(btn_var_input, 38, 38);
    lv_obj_align(btn_var_input, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_border_width(btn_var_input, 1, 0);
    lv_obj_set_style_radius(btn_var_input, 8, 0);
    lv_obj_set_style_pad_all(btn_var_input, 0, 0);
    lv_obj_clear_flag(btn_var_input, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_var_input, event_btn_input, LV_EVENT_CLICKED, nullptr);

    lv_obj_set_style_transform_width(btn_var_input, -2, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn_var_input, -2, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_var_input, 2, LV_STATE_PRESSED);

    lbl_var_input = lv_label_create(btn_var_input);
    lv_label_set_text(lbl_var_input, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(lbl_var_input, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_var_input);

    // Volume Slider
    slider_vol = lv_slider_create(obj_vol_var_cont);
    lv_obj_set_size(slider_vol, 120, 6);
    lv_obj_align(slider_vol, LV_ALIGN_CENTER, 0, 0);
    lv_slider_set_range(slider_vol, 0, 100);
    lv_slider_set_value(slider_vol, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_opa(slider_vol, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(slider_vol, 1, 0);
    lv_obj_set_style_radius(slider_vol, 3, 0);

    lv_obj_set_style_bg_opa(slider_vol, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider_vol, 3, LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(slider_vol, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_opa(slider_vol, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_vol, 0, LV_PART_KNOB);

    // Disabled state opacity (Fixed volume mode)
    lv_obj_set_style_bg_opa(slider_vol, LV_OPA_60, (lv_style_selector_t)LV_PART_INDICATOR | (lv_style_selector_t)LV_STATE_DISABLED);

    lv_obj_add_flag(slider_vol, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(slider_vol, 6);
    lv_obj_clear_flag(slider_vol, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(slider_vol, event_slider_vol, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(slider_vol, event_slider_vol, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(slider_vol, event_slider_vol, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(slider_vol, event_slider_vol, LV_EVENT_PRESS_LOST, nullptr);

    lbl_vol_percent = lv_label_create(obj_vol_var_cont);
    lv_label_set_text(lbl_vol_percent, "0%");
    lv_obj_set_style_text_font(lbl_vol_percent, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(lbl_vol_percent, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_vol_percent, 50);
    lv_obj_align(lbl_vol_percent, LV_ALIGN_CENTER, 0, 14);

    // Ensure buttons maintain top hit-test priority across their entire surface
    lv_obj_move_foreground(btn_var_mute);
    lv_obj_move_foreground(btn_var_input);
}

void ui_player_set_meta(const TrackMeta& meta) {
    if (lbl_title) {
        if (strlen(meta.title) > 0) {
            lv_point_t size;
            lv_txt_get_size(&size, meta.title, &lv_font_montserrat_24, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (size.x > 224) {
                char formatted_title[192];
                snprintf(formatted_title, sizeof(formatted_title), "%s                    ", meta.title);
                lv_label_set_text(lbl_title, formatted_title);
            } else {
                lv_label_set_text(lbl_title, meta.title);
            }
        } else {
            lv_label_set_text(lbl_title, "");
        }
    }

    if (lbl_artist) {
        char artist_album[200];
        if (strlen(meta.artist) > 0 && strlen(meta.album) > 0) {
            snprintf(artist_album, sizeof(artist_album), "%s - %s", meta.artist, meta.album);
        } else if (strlen(meta.artist) > 0) {
            snprintf(artist_album, sizeof(artist_album), "%s", meta.artist);
        } else if (strlen(meta.album) > 0) {
            snprintf(artist_album, sizeof(artist_album), "%s", meta.album);
        } else {
            artist_album[0] = '\0';
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

    update_stream_info_labels();
}

void ui_player_update_state(PlayState state, uint32_t curpos_ms, uint32_t totlen_ms) {
    // 1. Play / Pause Button Symbol Coordination
    if (lbl_play_pause) {
        if (state == PLAY_STATE_PLAYING) {
            lv_label_set_text(lbl_play_pause, LV_SYMBOL_PAUSE);
        } else if (state == PLAY_STATE_PAUSED || state == PLAY_STATE_STOPPED) {
            lv_label_set_text(lbl_play_pause, LV_SYMBOL_PLAY);
        } else if (state == PLAY_STATE_NONE) {
            lv_label_set_text(lbl_play_pause, LV_SYMBOL_PLAY);
        }
        // When state is PLAY_STATE_BUFFERING or PLAY_STATE_UNKNOWN:
        // RETAIN existing symbol! Zero flickering between Pause and Play.
    }

    // 2. Playback cleared
    if (state == PLAY_STATE_NONE) {
        ui_player_clear();
        return;
    }

    // 3. Transient Buffering / Unknown States
    if (state == PLAY_STATE_BUFFERING || state == PLAY_STATE_UNKNOWN) {
        if (totlen_ms > 0) {
            current_totlen_ms = totlen_ms;
            char time_tot_buf[16] = "--:--";
            format_time(current_totlen_ms, time_tot_buf, sizeof(time_tot_buf));
            if (lbl_time_total) lv_label_set_text(lbl_time_total, time_tot_buf);
        }
        return;
    }

    // 4. Active Playback State (PLAYING, PAUSED, STOPPED)
    current_totlen_ms = totlen_ms;
    current_actual_curpos_ms = (totlen_ms > 0 && curpos_ms > totlen_ms) ? totlen_ms : curpos_ms;

    if (bar_progress && !is_user_seeking) {
        char time_tot_buf[16] = "--:--";
        if (current_totlen_ms > 0) {
            format_time(current_totlen_ms, time_tot_buf, sizeof(time_tot_buf));
        }
        if (lbl_time_total) lv_label_set_text(lbl_time_total, time_tot_buf);

        set_bar_and_time(current_actual_curpos_ms, current_totlen_ms, false);
    }
}

void ui_player_set_volume(uint8_t volume, bool is_fixed, bool mute) {
    current_mute_state = mute;
    current_is_fixed = is_fixed;

    // Update mute button appearance (common to both fixed and variable modes)
    if (btn_var_mute && lbl_var_mute) {
        if (mute) {
            lv_obj_set_style_bg_color(btn_var_mute, lv_color_hex(0xE63946), 0);
            lv_obj_set_style_border_width(btn_var_mute, 0, 0);
            lv_obj_set_style_bg_color(btn_var_mute, lv_color_hex(0x9E1B26), LV_STATE_PRESSED);
            lv_obj_set_style_border_color(btn_var_mute, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
            lv_obj_set_style_border_width(btn_var_mute, 2, LV_STATE_PRESSED);
            lv_obj_set_style_text_color(lbl_var_mute, lv_color_hex(0xFFFFFF), 0);
        } else {
            lv_obj_set_style_bg_color(btn_var_mute, COLOR_SURFACE, 0);
            lv_obj_set_style_border_width(btn_var_mute, 1, 0);
            lv_obj_set_style_border_color(btn_var_mute, COLOR_SURFACE_LIGHT, 0);
            lv_obj_set_style_bg_color(btn_var_mute, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
            lv_obj_set_style_border_color(btn_var_mute, COLOR_ACCENT, LV_STATE_PRESSED);
            lv_obj_set_style_border_width(btn_var_mute, 2, LV_STATE_PRESSED);
            lv_obj_set_style_text_color(lbl_var_mute, COLOR_TEXT_PRIMARY, 0);
        }
    }

    if (is_fixed) {
        // FIXED VOLUME MODE: Slider disabled, fixed label
        if (slider_vol) {
            lv_obj_add_state(slider_vol, LV_STATE_DISABLED);
            lv_obj_clear_flag(slider_vol, LV_OBJ_FLAG_CLICKABLE);
            lv_slider_set_value(slider_vol, 100, LV_ANIM_OFF);
        }
        if (lbl_vol_percent) {
            lv_label_set_text(lbl_vol_percent, "Fixed");
        }
    } else {
        // VARIABLE VOLUME MODE: Slider enabled, volume percentage label
        if (slider_vol) {
            lv_obj_clear_state(slider_vol, LV_STATE_DISABLED);
            lv_obj_add_flag(slider_vol, LV_OBJ_FLAG_CLICKABLE);
        }
        if (!is_user_adjusting_volume) {
            current_volume = volume;
            s_last_enqueued_vol = volume;
            if (slider_vol) lv_slider_set_value(slider_vol, volume, LV_ANIM_OFF);
            if (lbl_vol_percent) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%u%%", (unsigned int)volume);
                lv_label_set_text(lbl_vol_percent, buf);
            }
        }
    }
}

void ui_player_set_fixed_volume_mode(bool is_fixed) {
    current_is_fixed = is_fixed;

    if (is_fixed) {
        if (slider_vol) {
            lv_obj_add_state(slider_vol, LV_STATE_DISABLED);
            lv_obj_clear_flag(slider_vol, LV_OBJ_FLAG_CLICKABLE);
            lv_slider_set_value(slider_vol, 100, LV_ANIM_OFF);
        }
        if (lbl_vol_percent) {
            lv_label_set_text(lbl_vol_percent, "Fixed");
        }
    } else {
        if (slider_vol) {
            lv_obj_clear_state(slider_vol, LV_STATE_DISABLED);
            lv_obj_add_flag(slider_vol, LV_OBJ_FLAG_CLICKABLE);
            lv_slider_set_value(slider_vol, current_volume, LV_ANIM_OFF);
        }
        if (lbl_vol_percent) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%u%%", (unsigned int)current_volume);
            lv_label_set_text(lbl_vol_percent, buf);
        }
    }
}

void ui_player_set_stream_info(uint16_t track_num, uint16_t track_total, uint8_t mode) {
    current_track_num = track_num;
    current_track_total = track_total;

    // Sync input mode indicator unless user just tapped the mode button within the last 2.5 seconds
    if (millis() - s_last_input_switch_ms > 2500) {
        uint8_t polled_idx = linkplay_mode_to_input_idx(mode);
        if (polled_idx != s_current_input_idx) {
            s_current_input_idx = polled_idx;
            update_input_mode_button();
        }
    }

    update_stream_info_labels();
}

void ui_player_clear() {
    if (lbl_title) lv_label_set_text(lbl_title, "");
    if (lbl_artist) lv_label_set_text(lbl_artist, "");
    current_totlen_ms = 0;
    current_actual_curpos_ms = 0;
    is_user_seeking = false;
    set_bar_and_time(0, 0, true);
    if (lbl_time_total) lv_label_set_text(lbl_time_total, "--:--");
    if (obj_seek_target) lv_obj_add_flag(obj_seek_target, LV_OBJ_FLAG_HIDDEN);

    current_sample_rate = 0;
    current_bit_depth = 0;
    current_track_num = 0;
    current_track_total = 0;
    update_stream_info_labels();
}

void ui_player_apply_theme() {
    if (s_parent) lv_obj_set_style_bg_color(s_parent, COLOR_BG, 0);
    if (lbl_resolution) lv_obj_set_style_text_color(lbl_resolution, COLOR_TEXT_MUTED, 0);
    if (lbl_track_counter) lv_obj_set_style_text_color(lbl_track_counter, COLOR_TEXT_MUTED, 0);
    if (lbl_title) lv_obj_set_style_text_color(lbl_title, COLOR_TEXT_PRIMARY, 0);
    if (lbl_artist) lv_obj_set_style_text_color(lbl_artist, COLOR_TEXT_MUTED, 0);
    if (bar_progress) {
        lv_obj_set_style_bg_color(bar_progress, COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(bar_progress, COLOR_SURFACE_LIGHT, 0);
        lv_obj_set_style_bg_color(bar_progress, COLOR_ACCENT, LV_PART_INDICATOR);
    }
    if (obj_seek_target) {
        lv_obj_set_style_bg_color(obj_seek_target, COLOR_ACCENT, 0);
    }
    if (lbl_time_cur) lv_obj_set_style_text_color(lbl_time_cur, COLOR_TEXT_MUTED, 0);
    if (lbl_time_total) lv_obj_set_style_text_color(lbl_time_total, COLOR_TEXT_MUTED, 0);
    if (btn_prev) {
        lv_obj_set_style_bg_color(btn_prev, COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(btn_prev, COLOR_SURFACE_LIGHT, 0);
        lv_obj_set_style_bg_color(btn_prev, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn_prev, COLOR_ACCENT, LV_STATE_PRESSED);
        lv_obj_t* lbl = lv_obj_get_child(btn_prev, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, COLOR_TEXT_PRIMARY, 0);
    }
    if (btn_play_pause) {
        lv_obj_set_style_bg_color(btn_play_pause, COLOR_ACCENT, 0);
        lv_obj_set_style_bg_color(btn_play_pause, COLOR_ACCENT, LV_STATE_PRESSED);
    }
    if (btn_next) {
        lv_obj_set_style_bg_color(btn_next, COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(btn_next, COLOR_SURFACE_LIGHT, 0);
        lv_obj_set_style_bg_color(btn_next, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn_next, COLOR_ACCENT, LV_STATE_PRESSED);
        lv_obj_t* lbl = lv_obj_get_child(btn_next, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, COLOR_TEXT_PRIMARY, 0);
    }
    if (btn_var_input) {
        lv_obj_set_style_bg_color(btn_var_input, COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(btn_var_input, COLOR_SURFACE_LIGHT, 0);
        lv_obj_set_style_bg_color(btn_var_input, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn_var_input, COLOR_ACCENT, LV_STATE_PRESSED);
    }
    if (slider_vol) {
        lv_obj_set_style_bg_color(slider_vol, COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(slider_vol, COLOR_SURFACE_LIGHT, 0);
        lv_obj_set_style_bg_color(slider_vol, COLOR_ACCENT, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider_vol, COLOR_SURFACE, LV_STATE_DISABLED);
        lv_obj_set_style_border_color(slider_vol, COLOR_SURFACE_LIGHT, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(slider_vol, COLOR_SURFACE_LIGHT, LV_PART_INDICATOR | LV_STATE_DISABLED);
    }
    if (lbl_vol_percent) {
        lv_obj_set_style_text_color(lbl_vol_percent, COLOR_TEXT_MUTED, 0);
    }
    update_input_mode_button();
    ui_player_set_volume(current_volume, current_is_fixed, current_mute_state);
}
