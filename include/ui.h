#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "model.h"

// Initialize UI widgets and layout
void ui_init();

// Process incoming state updates from Core 0 and update widgets
void ui_process_events();

// UI State setters
void ui_set_wifi_status(bool connected, int8_t rssi, const char* ip, const char* ssid = nullptr);
void ui_set_devices(const DeviceList& list);
void ui_set_player_state(const PlayerState& state);
void ui_set_track_meta(const TrackMeta& meta);
void ui_set_presets(const PresetList& presets);
void ui_set_scanning(bool is_scanning);
void ui_set_wifi_scan_results(const WiFiScanList& list);
void ui_open_wifi_modal();
void ui_wifi_on_connected(const char* ip);
void ui_wifi_on_connect_failed();
