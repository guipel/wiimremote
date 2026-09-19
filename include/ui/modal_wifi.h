#pragma once

#include <lvgl.h>
#include "model.h"

// Initialize Wi-Fi Configuration Modal overlay
void modal_wifi_init(lv_obj_t* parent = nullptr);

// Open the Wi-Fi configuration modal and start network scan
void modal_wifi_open();

// Close the Wi-Fi configuration modal
void modal_wifi_close();

// Update connection status (connected banner)
void modal_wifi_update_status(bool connected, const char* ssid);

// Populate scanned Wi-Fi networks
void modal_wifi_set_scan_results(const WiFiScanList& list);

// Handle successful Wi-Fi connection animation and auto-close
void modal_wifi_on_connected(const char* ip);

// Handle Wi-Fi connection failure
void modal_wifi_on_connect_failed();

// Check if Wi-Fi modal is currently visible
bool modal_wifi_is_open();
