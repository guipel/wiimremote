#pragma once

#include <lvgl.h>
#include "model.h"

typedef void (*OnDeviceSelectedCb)(const WiiMDevice& dev);

// Initialize Device Selector Modal overlay
void modal_device_init(lv_obj_t* parent = nullptr);

// Open the device selector modal
void modal_device_open();

// Close the device selector modal
void modal_device_close();

// Update discovered devices list
void modal_device_set_list(const DeviceList& list);

// Update scanning status indicator
void modal_device_set_scanning(bool is_scanning);

// Register callback for when a device is selected by user
void modal_device_set_on_selected(OnDeviceSelectedCb cb);

// Check if device modal is currently visible
bool modal_device_is_open();
