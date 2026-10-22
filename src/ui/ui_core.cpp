#include "ui.h"
#include "ui/ui_theme.h"
#include "ui/ui_header.h"
#include "ui/ui_player.h"
#include "ui/ui_lyrics.h"
#include "ui/ui_presets.h"
#include "ui/ui_browser.h"
#include "ui/modal_device.h"
#include "ui/modal_wifi.h"
#include "ui/modal_power.h"
#include "power_manager.h"
#include "config.h"

// External Queue handles
extern QueueHandle_t xQueueUiState;

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

const lv_img_dsc_t img_lock_dsc = {
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

static uint8_t s_active_theme_idx = 0;

uint8_t ui_theme_get_idx() {
    return s_active_theme_idx;
}

void ui_theme_set_idx(uint8_t idx) {
    s_active_theme_idx = (idx < kThemeCount) ? idx : 0;
}

const ThemePaletteConfig& ui_theme_get() {
    return kThemes[s_active_theme_idx < kThemeCount ? s_active_theme_idx : 0];
}

// Tabview & Tabs
static lv_obj_t* tabview = nullptr;
static lv_obj_t* tab_lyrics = nullptr;
static lv_obj_t* tab_player = nullptr;
static lv_obj_t* tab_presets = nullptr;
static lv_obj_t* tab_browser = nullptr;

// Battery monitoring state
static float s_battery_filtered_mv = 0.0f;
static uint8_t s_crit_battery_count = 0;
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

    // Critical low-battery cutoff protection (3 consecutive samples <= 3350mV)
    if (bat_mv > 1000 && bat_mv <= BAT_VOLT_CRIT_MV) {
        s_crit_battery_count++;
        if (s_crit_battery_count >= 3) {
            log_w("CRITICAL: Battery voltage (%u mV) <= %u mV! Entering protective deep sleep...", bat_mv, BAT_VOLT_CRIT_MV);
            power_manager_enter_deep_sleep();
        }
    } else {
        s_crit_battery_count = 0;
    }
}

static void event_tabview_changed(lv_event_t* e) {
    if (!tabview) return;
    uint16_t act = lv_tabview_get_tab_act(tabview);
    if (act == 0) {
        ui_lyrics_on_tab_activated();
    } else if (act == 3) {
        ui_browser_on_tab_activated();
    }
}

void ui_apply_theme() {
    lv_obj_set_style_bg_color(lv_scr_act(), COLOR_BG, 0);

    if (tabview) {
        lv_obj_set_style_bg_color(tabview, COLOR_BG, 0);
        lv_obj_t* tab_btns = lv_tabview_get_tab_btns(tabview);
        if (tab_btns) {
            lv_obj_set_style_bg_color(tab_btns, COLOR_SURFACE, 0);
            lv_obj_set_style_border_color(tab_btns, COLOR_SURFACE_LIGHT, 0);
            lv_obj_set_style_text_color(tab_btns, COLOR_TEXT_MUTED, LV_PART_ITEMS);
            const lv_style_selector_t sel_checked = (lv_style_selector_t)LV_PART_ITEMS | (lv_style_selector_t)LV_STATE_CHECKED;
            lv_obj_set_style_text_color(tab_btns, COLOR_ACCENT, sel_checked);
            lv_obj_set_style_bg_color(tab_btns, COLOR_SURFACE_LIGHT, sel_checked);
            lv_obj_set_style_border_color(tab_btns, COLOR_ACCENT, sel_checked);
        }
    }

    ui_header_apply_theme();
    ui_lyrics_apply_theme();
    ui_player_apply_theme();
    ui_presets_apply_theme();
    ui_browser_apply_theme();
    modal_device_apply_theme();
    modal_wifi_apply_theme();
    modal_power_apply_theme();
}

void ui_init() {
    ui_theme_set_idx(power_manager_get_theme_idx());

    // 1. Build top persistent header
    ui_header_init(lv_scr_act());

    // 2. Build bottom-oriented Tabview
    tabview = lv_tabview_create(lv_scr_act(), LV_DIR_BOTTOM, 28);
    lv_obj_set_size(tabview, 240, 288);
    lv_obj_align(tabview, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Style the Tabview Navigation Bar layout & geometry
    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_set_style_pad_all(tab_btns, 0, 0);
    lv_obj_set_style_pad_gap(tab_btns, 0, 0);
    lv_obj_set_style_border_side(tab_btns, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(tab_btns, 1, 0);

    // Tab buttons (LV_PART_ITEMS) layout & typography
    lv_obj_set_style_text_font(tab_btns, &lv_font_montserrat_10, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(tab_btns, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_border_side(tab_btns, LV_BORDER_SIDE_NONE, LV_PART_ITEMS);

    // Active/Checked Tab border geometry
    const lv_style_selector_t sel_checked = (lv_style_selector_t)LV_PART_ITEMS | (lv_style_selector_t)LV_STATE_CHECKED;
    lv_obj_set_style_bg_opa(tab_btns, LV_OPA_COVER, sel_checked);
    lv_obj_set_style_border_side(tab_btns, LV_BORDER_SIDE_BOTTOM, sel_checked);
    lv_obj_set_style_border_width(tab_btns, 2, sel_checked);

    // Add Tabs: Lyrics, Player (Default), Presets, UPnP
    tab_lyrics = lv_tabview_add_tab(tabview, LV_SYMBOL_FILE " Lyrics");
    tab_player = lv_tabview_add_tab(tabview, LV_SYMBOL_AUDIO " Player");
    tab_presets = lv_tabview_add_tab(tabview, LV_SYMBOL_LIST " Presets");
    tab_browser = lv_tabview_add_tab(tabview, LV_SYMBOL_DIRECTORY " UPnP");

    ui_lyrics_init(tab_lyrics);
    ui_player_init(tab_player);
    ui_presets_init(tab_presets);
    ui_browser_init(tab_browser);

    // Set Player as default active tab
    lv_tabview_set_act(tabview, 1, LV_ANIM_OFF);

    // Hook tab navigation event for on-demand lyrics & UPnP loading
    lv_obj_add_event_cb(tabview, event_tabview_changed, LV_EVENT_VALUE_CHANGED, nullptr);

    // Hook preset selection to navigate back to Player tab
    ui_presets_set_on_triggered([](uint8_t index) {
        if (tabview) {
            lv_tabview_set_act(tabview, 1, LV_ANIM_ON);
        }
    });

    // Hook UPnP track playback to navigate back to Player tab
    ui_browser_set_on_track_played([]() {
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
        ui_player_set_fixed_volume_mode(dev.is_fixed_volume);
    });

    modal_wifi_init(lv_scr_act());
    modal_power_init(lv_scr_act());

    // 4. Apply initial theme colors across all screens & modals (single source of truth)
    ui_apply_theme();

    // 5. Progress Interpolation Timer (50ms = 20 FPS updates)
    lv_timer_create(timer_progress_cb, 50, nullptr);

    // 6. Battery Monitoring Timer (5000ms period, immediate initial sample)
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
            ui_player_set_fixed_volume_mode(list.devices[i].is_fixed_volume);
            activeFound = true;
            break;
        }
    }
    if (!activeFound && list.count > 0) {
        ui_header_set_active_device(list.devices[0].name);
        ui_player_set_fixed_volume_mode(list.devices[0].is_fixed_volume);
    } else if (list.count == 0) {
        ui_header_set_active_device(nullptr);
    }

    // 2. Rebuild list inside Device Selector Modal
    modal_device_set_list(list);
}

void ui_set_player_state(const PlayerState& state) {
    ui_player_update_state(state.state, state.curpos_ms, state.totlen_ms);
    ui_player_set_volume(state.volume, state.is_fixed_volume, state.mute);
    ui_player_set_stream_info(state.stream, state.track_num, state.track_total, state.mode);
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
            case UI_EVT_UPNP_SERVERS_UPDATED:
                if (evt.data.upnp_servers) {
                    ui_browser_set_servers(*evt.data.upnp_servers);
                }
                break;
            case UI_EVT_UPNP_BROWSE_RESULT:
                if (evt.data.upnp_browse) {
                    ui_browser_set_browse_result(*evt.data.upnp_browse);
                }
                break;
        }
        ui_event_free(&evt);
    }
}
