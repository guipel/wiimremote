#pragma once

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

// Initialize persistent top Header bar
void ui_header_init(lv_obj_t* parent);

// Update Wi-Fi signal indicator
void ui_header_set_wifi_status(bool connected, int8_t rssi);

// Update active device name label
void ui_header_set_active_device(const char* name);

// Update battery meter icon and color based on voltage
void ui_header_update_battery(uint32_t bat_mv);
