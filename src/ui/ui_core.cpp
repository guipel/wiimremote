#include "ui.h"
#include "ui/ui_theme.h"
#include "ui/ui_header.h"
#include "ui/ui_player.h"
#include "ui/ui_lyrics.h"
#include "ui/ui_presets.h"
#include "ui/modal_device.h"
#include "ui/modal_wifi.h"
#include "ui/modal_power.h"
#include "config.h"

// External Queue handles
extern QueueHandle_t xQueueUiState;

// Tabview & Tabs
static lv_obj_t* tabview = nullptr;
static lv_obj_t* tab_lyrics = nullptr;
static lv_obj_t* tab_player = nullptr;
static lv_obj_t* tab_presets = nullptr;

// Battery monitoring state
static float s_battery_filtered_mv = 0.0f;
static uint32_t last_progress_tick_ms = 0;

static void timer_progress_cb(lv_timer_t* timer) {
    uint32_t now = millis();
    uint32_t delta = (last_progress_tick_ms == 0) ? 50 : (now - last_progress_tick_ms);
    last_progress_tick_ms = now;

    ui_player_tick_progress(delta);
}

static void timer_battery_cb(lv_timer_t* timer) {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += analogReadMilliVolts(PIN_BAT_ADC);
    }
    uint32_t pin_mv = sum / 16;
    uint32_t raw_bat_mv = (uint32_t)(pin_mv * BAT_DIVIDER_RATIO);

    if (s_battery_filtered_mv < 100.0f) {
        s_battery_filtered_mv = (float)raw_bat_mv;
    } else {
        s_battery_filtered_mv = 0.2f * (float)raw_bat_mv + 0.8f * s_battery_filtered_mv;
    }

    uint32_t bat_mv = (uint32_t)s_battery_filtered_mv;
    ui_header_update_battery(bat_mv);
    modal_power_update_battery(bat_mv);
}

static void event_tabview_changed(lv_event_t* e) {
    if (tabview && lv_tabview_get_tab_act(tabview) == 0) {
        ui_lyrics_on_tab_activated();
    }
}

void ui_init() {
    lv_obj_set_style_bg_color(lv_scr_act(), COLOR_BG, 0);

    // 1. Build top persistent header
    ui_header_init(lv_scr_act());

    // 2. Build bottom-oriented Tabview
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

    // Tab buttons (LV_PART_ITEMS) styling
    lv_obj_set_style_text_font(tab_btns, &lv_font_montserrat_12, LV_PART_ITEMS);
    lv_obj_set_style_text_color(tab_btns, COLOR_TEXT_MUTED, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(tab_btns, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_border_side(tab_btns, LV_BORDER_SIDE_NONE, LV_PART_ITEMS);

    // Active/Checked Tab styling
    lv_obj_set_style_text_color(tab_btns, COLOR_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x222732), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(tab_btns, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(tab_btns, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(tab_btns, COLOR_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tab_btns, 2, LV_PART_ITEMS | LV_STATE_CHECKED);

    // Add Tabs: Lyrics (Left), Player (Center - Default), Presets (Right)
    tab_lyrics = lv_tabview_add_tab(tabview, LV_SYMBOL_FILE " Lyrics");
    tab_player = lv_tabview_add_tab(tabview, LV_SYMBOL_AUDIO " Player");
    tab_presets = lv_tabview_add_tab(tabview, LV_SYMBOL_LIST " Presets");

    ui_lyrics_init(tab_lyrics);
    ui_player_init(tab_player);
    ui_presets_init(tab_presets);

    // Set Player as default active tab (Center)
    lv_tabview_set_act(tabview, 1, LV_ANIM_OFF);

    // Hook tab navigation event for on-demand lyrics loading
    lv_obj_add_event_cb(tabview, event_tabview_changed, LV_EVENT_VALUE_CHANGED, nullptr);

    // Hook preset selection to navigate back to Player tab
    ui_presets_set_on_triggered([](uint8_t index) {
        if (tabview) {
            lv_tabview_set_act(tabview, 1, LV_ANIM_ON);
        }
    });

    // 3. Build modal pickers
    modal_device_init(lv_scr_act());
    modal_device_set_on_selected([](const WiiMDevice& dev) {
        ui_header_set_active_device(dev.name);
        ui_player_clear();
        ui_lyrics_clear();
        if (dev.is_fixed_volume) {
            ui_player_set_fixed_volume_mode(true);
        }
    });

    modal_wifi_init(lv_scr_act());
    modal_power_init(lv_scr_act());

    // 4. Progress Interpolation Timer (50ms = 20 FPS updates)
    lv_timer_create(timer_progress_cb, 50, nullptr);

    // 5. Battery Monitoring Timer (5000ms period, immediate initial sample)
    lv_timer_t* timer_bat = lv_timer_create(timer_battery_cb, BAT_SAMPLE_INTERVAL_MS, nullptr);
    timer_battery_cb(timer_bat);
}

void ui_set_wifi_status(bool connected, int8_t rssi, const char* ip, const char* ssid) {
    ui_header_set_wifi_status(connected, rssi);
    modal_wifi_update_status(connected, ssid);
}

void ui_set_devices(const DeviceList& list) {
    // 1. Update Header with active device
    bool activeFound = false;
    for (uint8_t i = 0; i < list.count; ++i) {
        if (list.devices[i].is_active) {
            ui_header_set_active_device(list.devices[i].name);
            if (list.devices[i].is_fixed_volume) {
                ui_player_set_fixed_volume_mode(true);
            }
            activeFound = true;
            break;
        }
    }
    if (!activeFound && list.count > 0) {
        ui_header_set_active_device(list.devices[0].name);
    } else if (list.count == 0) {
        ui_header_set_active_device(nullptr);
    }

    // 2. Rebuild list inside Device Selector Modal
    modal_device_set_list(list);
}

void ui_set_player_state(const PlayerState& state) {
    ui_player_update_state(state.state, state.curpos_ms, state.totlen_ms);
    ui_player_set_volume(state.volume, state.is_fixed_volume, state.mute);
    ui_player_set_stream_info(state.stream, state.track_num, state.track_total);
}

void ui_set_track_meta(const TrackMeta& meta) {
    ui_player_set_meta(meta);
    bool is_lyrics_active = (tabview && lv_tabview_get_tab_act(tabview) == 0);
    ui_lyrics_on_track_changed(meta, is_lyrics_active);
}

void ui_set_presets(const PresetList& presets) {
    ui_presets_set_list(presets);
}

void ui_set_scanning(bool is_scanning) {
    modal_device_set_scanning(is_scanning);
}

void ui_open_wifi_modal() {
    modal_wifi_open();
}

void ui_set_wifi_scan_results(const WiFiScanList& list) {
    modal_wifi_set_scan_results(list);
}

void ui_wifi_on_connected(const char* ip) {
    modal_wifi_on_connected(ip);
}

void ui_wifi_on_connect_failed() {
    modal_wifi_on_connect_failed();
}

void ui_set_lyrics(const LyricsInfo& info) {
    ui_lyrics_set_content(info);
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
            case UI_EVT_LYRICS_UPDATED:
                if (evt.data.lyrics) {
                    ui_set_lyrics(*evt.data.lyrics);
                }
                break;
        }
        ui_event_free(&evt);
    }
}
