#include "ui/ui_player.h"
#include "ui/ui_theme.h"
#include "config.h"

// External Queue for sending UI Commands to Network Task
extern QueueHandle_t xQueueUiCmd;

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

// UI Widgets - Variable Volume Controls
static lv_obj_t* obj_vol_var_cont = nullptr;
static lv_obj_t* btn_var_mute = nullptr;
static lv_obj_t* lbl_var_mute = nullptr;
static lv_obj_t* slider_vol = nullptr;
static lv_obj_t* lbl_vol_percent = nullptr;
static lv_obj_t* btn_var_input = nullptr;
static lv_obj_t* lbl_var_input = nullptr;

// State tracking
static bool is_user_adjusting_volume = false;
static uint8_t current_volume = 0;
static bool is_user_seeking = false;
static bool current_mute_state = false;
static PlayState current_play_state = PLAY_STATE_UNKNOWN;
static uint32_t current_totlen_ms = 0;
static uint32_t current_actual_curpos_ms = 0;
static uint32_t last_progress_tick_ms = 0;
static lv_obj_t* obj_seek_target = nullptr;
static uint32_t pending_seek_target_ms = 0;
static bool has_pending_seek = false;
static unsigned long pending_seek_timestamp = 0;

static uint32_t current_sample_rate = 0;
static uint8_t current_bit_depth = 0;
static uint16_t current_track_num = 0;
static uint16_t current_track_total = 0;

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

// Helper: Convert touch X coordinate on progress bar to seek target in ms
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
    return (uint32_t)(((uint64_t)val * (uint64_t)current_totlen_ms) / PROGRESS_BAR_MAX);
}

// Event Callback - Track Progress Bar Seeking
static void event_slider_seek(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        is_user_seeking = true;

        if (current_totlen_ms > 0) {
            lv_indev_t* indev = lv_indev_get_act();
            if (indev) {
                int32_t cx = 0;
                uint32_t seek_ms = touch_x_to_seek_ms(indev, &cx);

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
                int32_t cx = 0;
                uint32_t seek_ms = touch_x_to_seek_ms(indev, &cx);
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
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        is_user_adjusting_volume = false;
    }
}

// Event Callbacks - Transport Controls
static void event_btn_prev(lv_event_t* e) {
    UiCommand cmd;
    cmd.type = CMD_PREV;
    cmd.data.seek_ms = current_actual_curpos_ms;
    xQueueSend(xQueueUiCmd, &cmd, 0);

    // Enter buffering state: freeze progress, retain current play/pause icon
    current_play_state = PLAY_STATE_BUFFERING;
    current_actual_curpos_ms = 0;
    if (bar_progress) lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);
    if (lbl_time_cur) lv_label_set_text(lbl_time_cur, "00:00");
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

    // Enter buffering state: freeze progress, retain current play/pause icon
    current_play_state = PLAY_STATE_BUFFERING;
    current_actual_curpos_ms = 0;
    if (bar_progress) lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);
    if (lbl_time_cur) lv_label_set_text(lbl_time_cur, "00:00");
}

static void event_btn_mute(lv_event_t* e) {
    UiCommand cmd;
    cmd.type = CMD_SET_MUTE;
    cmd.data.mute = !current_mute_state;
    xQueueSend(xQueueUiCmd, &cmd, 0);
}

static void event_btn_input(lv_event_t* e) {
    // Placeholder for Input Source selection / cycle action
}

void ui_player_init(lv_obj_t* parent) {
    lv_obj_set_style_bg_color(parent, COLOR_BG, 0);
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Telemetry: Audio Resolution (Centered under progress bar) & Track Counter (Centered under Play/Pause)
    lbl_resolution = lv_label_create(parent);
    lv_label_set_text(lbl_resolution, "");
    lv_obj_set_style_text_font(lbl_resolution, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_resolution, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_align(lbl_resolution, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_resolution, LV_ALIGN_TOP_MID, 0, 90);

    lbl_track_counter = lv_label_create(parent);
    lv_label_set_text(lbl_track_counter, "");
    lv_obj_set_style_text_font(lbl_track_counter, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_track_counter, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_align(lbl_track_counter, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_track_counter, LV_ALIGN_TOP_MID, 0, 184);

    // 2. Track Title
    lbl_title = lv_label_create(parent);
    lv_label_set_text(lbl_title, "");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_title, 224);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 6);

    // 3. Artist & Album
    lbl_artist = lv_label_create(parent);
    lv_label_set_text(lbl_artist, "");
    lv_obj_set_style_text_font(lbl_artist, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_artist, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_align(lbl_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_artist, 224);
    lv_obj_align(lbl_artist, LV_ALIGN_TOP_MID, 0, 38);

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
    lv_obj_add_event_cb(bar_progress, event_slider_seek, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(bar_progress, event_slider_seek, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(bar_progress, event_slider_seek, LV_EVENT_RELEASED, nullptr);

    // Ghost/Secondary Seek Target Indicator
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
    lv_obj_align(trans_cont, LV_ALIGN_TOP_MID, 0, 122);
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

    // Micro-interactions / pressed feedback
    lv_obj_set_style_transform_width(btn_prev, -3, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn_prev, -3, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn_prev, lv_color_hex(0x3B4455), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_prev, COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_prev, 2, LV_STATE_PRESSED);

    lv_obj_set_style_transform_width(btn_play_pause, -3, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn_play_pause, -3, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn_play_pause, lv_color_hex(0x0088A8), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_play_pause, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_play_pause, 2, LV_STATE_PRESSED);

    lv_obj_set_style_transform_width(btn_next, -3, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn_next, -3, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn_next, lv_color_hex(0x3B4455), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_next, COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_next, 2, LV_STATE_PRESSED);

    // 6. Compact Volume / Mute Button (Fixed Mode)
    btn_mute = lv_btn_create(parent);
    lv_obj_set_size(btn_mute, 68, 40);
    lv_obj_align(btn_mute, LV_ALIGN_TOP_MID, 0, 197);
    lv_obj_set_style_bg_color(btn_mute, lv_color_hex(0x222732), 0);
    lv_obj_set_style_border_width(btn_mute, 1, 0);
    lv_obj_set_style_border_color(btn_mute, lv_color_hex(0x3A4252), 0);
    lv_obj_set_style_radius(btn_mute, 10, 0);
    lv_obj_set_style_pad_all(btn_mute, 0, 0);
    lv_obj_clear_flag(btn_mute, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_mute, event_btn_mute, LV_EVENT_CLICKED, nullptr);

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

    // 6B. Variable Volume Row Container
    obj_vol_var_cont = lv_obj_create(parent);
    lv_obj_set_size(obj_vol_var_cont, 224, 48);
    lv_obj_align(obj_vol_var_cont, LV_ALIGN_TOP_MID, 0, 197);
    lv_obj_set_style_bg_opa(obj_vol_var_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(obj_vol_var_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(obj_vol_var_cont, 0, 0);
    lv_obj_clear_flag(obj_vol_var_cont, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_flag(obj_vol_var_cont, LV_OBJ_FLAG_HIDDEN);

    // Variable Mute Button
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

    // Variable Input Button
    btn_var_input = lv_btn_create(obj_vol_var_cont);
    lv_obj_set_size(btn_var_input, 38, 38);
    lv_obj_align(btn_var_input, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_var_input, lv_color_hex(0x222732), 0);
    lv_obj_set_style_border_width(btn_var_input, 1, 0);
    lv_obj_set_style_border_color(btn_var_input, lv_color_hex(0x3A4252), 0);
    lv_obj_set_style_radius(btn_var_input, 8, 0);
    lv_obj_set_style_pad_all(btn_var_input, 0, 0);
    lv_obj_clear_flag(btn_var_input, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_var_input, event_btn_input, LV_EVENT_CLICKED, nullptr);

    lv_obj_set_style_transform_width(btn_var_input, -2, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn_var_input, -2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn_var_input, lv_color_hex(0x3B4455), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_var_input, COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_var_input, 2, LV_STATE_PRESSED);

    lbl_var_input = lv_label_create(btn_var_input);
    lv_label_set_text(lbl_var_input, LV_SYMBOL_SHUFFLE);
    lv_obj_set_style_text_font(lbl_var_input, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_var_input, COLOR_TEXT_PRIMARY, 0);
    lv_obj_center(lbl_var_input);

    // Volume Slider
    slider_vol = lv_slider_create(obj_vol_var_cont);
    lv_obj_set_size(slider_vol, 126, 6);
    lv_obj_align(slider_vol, LV_ALIGN_CENTER, 0, 0);
    lv_slider_set_range(slider_vol, 0, 100);
    lv_slider_set_value(slider_vol, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_vol, lv_color_hex(0x242A35), 0);
    lv_obj_set_style_bg_opa(slider_vol, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(slider_vol, lv_color_hex(0x3A4252), 0);
    lv_obj_set_style_border_width(slider_vol, 1, 0);
    lv_obj_set_style_radius(slider_vol, 3, 0);

    lv_obj_set_style_bg_color(slider_vol, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider_vol, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider_vol, 3, LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(slider_vol, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_opa(slider_vol, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_vol, 0, LV_PART_KNOB);

    lv_obj_add_flag(slider_vol, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(slider_vol, 20);
    lv_obj_clear_flag(slider_vol, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(slider_vol, event_slider_vol, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(slider_vol, event_slider_vol, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(slider_vol, event_slider_vol, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(slider_vol, event_slider_vol, LV_EVENT_PRESS_LOST, nullptr);

    lbl_vol_percent = lv_label_create(obj_vol_var_cont);
    lv_label_set_text(lbl_vol_percent, "0%");
    lv_obj_set_style_text_font(lbl_vol_percent, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_vol_percent, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_align(lbl_vol_percent, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_vol_percent, 50);
    lv_obj_align(lbl_vol_percent, LV_ALIGN_CENTER, 0, 14);
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
        char artist_album[192];
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

    has_pending_seek = false;
    if (obj_seek_target) lv_obj_add_flag(obj_seek_target, LV_OBJ_FLAG_HIDDEN);

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
        // When state is PLAY_STATE_BUFFERING, PLAY_STATE_LOADING, or PLAY_STATE_UNKNOWN:
        // RETAIN existing symbol! Zero flickering between Pause and Play.
    }

    current_play_state = state;

    // 2. Playback cleared
    if (state == PLAY_STATE_NONE) {
        ui_player_clear();
        return;
    }

    // 3. Transient Buffering / Loading States
    if (state == PLAY_STATE_BUFFERING || state == PLAY_STATE_LOADING || state == PLAY_STATE_UNKNOWN) {
        if (totlen_ms > 0) {
            current_totlen_ms = totlen_ms;
            char time_tot_buf[16] = "--:--";
            format_time(current_totlen_ms, time_tot_buf, sizeof(time_tot_buf));
            if (lbl_time_total) lv_label_set_text(lbl_time_total, time_tot_buf);
        }
        if (curpos_ms == 0 && !has_pending_seek) {
            current_actual_curpos_ms = 0;
            if (bar_progress) lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);
            if (lbl_time_cur) lv_label_set_text(lbl_time_cur, "00:00");
        }
        return;
    }

    // 4. Active Playback State (PLAYING, PAUSED, STOPPED)
    current_totlen_ms = totlen_ms;
    current_actual_curpos_ms = (totlen_ms > 0 && curpos_ms > totlen_ms) ? totlen_ms : curpos_ms;
    last_progress_tick_ms = millis();

    // Check if streamer caught up with pending seek target
    if (has_pending_seek) {
        int32_t diff = (int32_t)curpos_ms - (int32_t)pending_seek_target_ms;
        if (abs(diff) <= 3000 || (millis() - pending_seek_timestamp > 5000)) {
            has_pending_seek = false;
            if (obj_seek_target) lv_obj_add_flag(obj_seek_target, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (bar_progress && !is_user_seeking) {
        char time_tot_buf[16] = "--:--";
        if (current_totlen_ms > 0) {
            format_time(current_totlen_ms, time_tot_buf, sizeof(time_tot_buf));
        }
        if (lbl_time_total) lv_label_set_text(lbl_time_total, time_tot_buf);

        if (!has_pending_seek) {
            char time_cur_buf[16] = "00:00";
            format_time(current_actual_curpos_ms, time_cur_buf, sizeof(time_cur_buf));
            if (lbl_time_cur) lv_label_set_text(lbl_time_cur, time_cur_buf);

            uint32_t val = (current_totlen_ms > 0)
                ? (uint32_t)(((uint64_t)current_actual_curpos_ms * PROGRESS_BAR_MAX) / current_totlen_ms)
                : 0;
            if (val > PROGRESS_BAR_MAX) val = PROGRESS_BAR_MAX;
            lv_bar_set_value(bar_progress, (int32_t)val, LV_ANIM_OFF);
        }
    }
}

void ui_player_set_volume(uint8_t volume, bool is_fixed, bool mute) {
    current_mute_state = mute;

    if (is_fixed) {
        // FIXED VOLUME MODE
        if (obj_vol_var_cont) lv_obj_add_flag(obj_vol_var_cont, LV_OBJ_FLAG_HIDDEN);
        if (btn_mute) lv_obj_clear_flag(btn_mute, LV_OBJ_FLAG_HIDDEN);

        if (btn_mute && lbl_mute) {
            if (mute) {
                lv_obj_set_style_bg_color(btn_mute, lv_color_hex(0xE63946), 0);
                lv_obj_set_style_border_width(btn_mute, 0, 0);
                lv_obj_set_style_bg_color(btn_mute, lv_color_hex(0x9E1B26), LV_STATE_PRESSED);
                lv_obj_set_style_border_color(btn_mute, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
                lv_obj_set_style_border_width(btn_mute, 2, LV_STATE_PRESSED);
                lv_label_set_text(lbl_mute, LV_SYMBOL_VOLUME_MAX);
                lv_obj_set_style_text_color(lbl_mute, lv_color_hex(0xFFFFFF), 0);
                lv_obj_align(lbl_mute, LV_ALIGN_LEFT_MID, 12, 0);
                if (img_lock) {
                    lv_obj_set_style_img_recolor(img_lock, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_align(img_lock, LV_ALIGN_LEFT_MID, 38, 0);
                }
            } else {
                lv_obj_set_style_bg_color(btn_mute, lv_color_hex(0x222732), 0);
                lv_obj_set_style_border_width(btn_mute, 1, 0);
                lv_obj_set_style_border_color(btn_mute, lv_color_hex(0x3A4252), 0);
                lv_obj_set_style_bg_color(btn_mute, lv_color_hex(0x3B4455), LV_STATE_PRESSED);
                lv_obj_set_style_border_color(btn_mute, COLOR_ACCENT, LV_STATE_PRESSED);
                lv_obj_set_style_border_width(btn_mute, 2, LV_STATE_PRESSED);
                lv_label_set_text(lbl_mute, LV_SYMBOL_VOLUME_MAX);
                lv_obj_set_style_text_color(lbl_mute, COLOR_TEXT_PRIMARY, 0);
                lv_obj_align(lbl_mute, LV_ALIGN_LEFT_MID, 12, 0);
                if (img_lock) {
                    lv_obj_set_style_img_recolor(img_lock, COLOR_TEXT_PRIMARY, 0);
                    lv_obj_align(img_lock, LV_ALIGN_LEFT_MID, 38, 0);
                }
            }
        }
    } else {
        // VARIABLE VOLUME MODE
        if (btn_mute) lv_obj_add_flag(btn_mute, LV_OBJ_FLAG_HIDDEN);
        if (obj_vol_var_cont) lv_obj_clear_flag(obj_vol_var_cont, LV_OBJ_FLAG_HIDDEN);

        if (!is_user_adjusting_volume) {
            current_volume = volume;
            if (slider_vol) lv_slider_set_value(slider_vol, volume, LV_ANIM_OFF);
            if (lbl_vol_percent) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d%%", volume);
                lv_label_set_text(lbl_vol_percent, buf);
            }
        }

        if (btn_var_mute && lbl_var_mute) {
            if (mute) {
                lv_obj_set_style_bg_color(btn_var_mute, lv_color_hex(0xE63946), 0);
                lv_obj_set_style_border_width(btn_var_mute, 0, 0);
                lv_obj_set_style_text_color(lbl_var_mute, lv_color_hex(0xFFFFFF), 0);
            } else {
                lv_obj_set_style_bg_color(btn_var_mute, lv_color_hex(0x222732), 0);
                lv_obj_set_style_border_width(btn_var_mute, 1, 0);
                lv_obj_set_style_border_color(btn_var_mute, lv_color_hex(0x3A4252), 0);
                lv_obj_set_style_text_color(lbl_var_mute, COLOR_TEXT_PRIMARY, 0);
            }
        }
    }
}

void ui_player_set_fixed_volume_mode(bool is_fixed) {
    if (is_fixed) {
        if (btn_mute) lv_obj_clear_flag(btn_mute, LV_OBJ_FLAG_HIDDEN);
        if (obj_vol_var_cont) lv_obj_add_flag(obj_vol_var_cont, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (btn_mute) lv_obj_add_flag(btn_mute, LV_OBJ_FLAG_HIDDEN);
        if (obj_vol_var_cont) lv_obj_clear_flag(obj_vol_var_cont, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_player_set_stream_info(const StreamInfo& stream, uint16_t track_num, uint16_t track_total) {
    if (stream.sample_rate > 0) {
        current_sample_rate = stream.sample_rate;
        current_bit_depth = stream.bit_depth;
    }
    if (track_total > 0) {
        current_track_num = track_num;
        current_track_total = track_total;
    }
    update_stream_info_labels();
}

void ui_player_tick_progress(uint32_t delta_ms) {
    if (current_play_state == PLAY_STATE_PLAYING && !is_user_seeking && !has_pending_seek && current_totlen_ms > 0) {
        current_actual_curpos_ms += delta_ms;
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

void ui_player_clear() {
    if (lbl_title) lv_label_set_text(lbl_title, "");
    if (lbl_artist) lv_label_set_text(lbl_artist, "");
    if (bar_progress) lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);
    if (lbl_time_cur) lv_label_set_text(lbl_time_cur, "00:00");
    if (lbl_time_total) lv_label_set_text(lbl_time_total, "--:--");
    current_totlen_ms = 0;
    current_actual_curpos_ms = 0;
    has_pending_seek = false;
    if (obj_seek_target) lv_obj_add_flag(obj_seek_target, LV_OBJ_FLAG_HIDDEN);

    current_sample_rate = 0;
    current_bit_depth = 0;
    current_track_num = 0;
    current_track_total = 0;
    update_stream_info_labels();
}
