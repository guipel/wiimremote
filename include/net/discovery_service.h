#pragma once

#include <Arduino.h>
#include "model.h"

typedef void (*ActiveDeviceChangedCb)(const WiiMDevice& dev);

void discovery_service_init(ActiveDeviceChangedCb onActiveChanged);
void discovery_service_start_ssdp_listener();
void discovery_service_send_ssdp_query();
void discovery_service_process_ssdp();
void discovery_service_trigger_rescan();
void discovery_service_select_device(const char* ip);
bool discovery_service_get_active_device(WiiMDevice* dev);
bool discovery_service_has_active_device();
void discovery_service_set_active_fixed_volume(bool is_fixed);
void discovery_service_broadcast_list();
uint8_t discovery_service_get_count();
