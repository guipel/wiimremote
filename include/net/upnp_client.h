#pragma once

#include <Arduino.h>
#include "model.h"

void upnp_client_init();
void upnp_client_reset_ignored();
void upnp_client_on_ssdp_location(const char* locationUrl);
void upnp_client_broadcast_servers();
void upnp_client_browse(uint8_t serverIdx, const char* objectId, const char* folderTitle);
bool upnp_client_play_track(const char* wiimIp, uint8_t trackIdx, bool isFixedVolume);
