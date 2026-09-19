#pragma once

#include <Arduino.h>
#include "model.h"

void linkplay_client_init();
void linkplay_client_reset_session();
void linkplay_client_reset_track_cache();

bool linkplay_client_send_cmd(const char* targetIp, const String& cmd);
void linkplay_client_poll_active(const char* targetIp, bool is_fixed_vol, PlayerState& outState);
bool linkplay_client_fetch_meta(const char* targetIp);
void linkplay_client_fetch_presets(const char* targetIp);
bool linkplay_client_fetch_config(const char* targetIp, bool& outIsFixedVol);
void linkplay_client_seek(const char* targetIp, uint32_t seek_ms);

const String& linkplay_client_get_last_title();
const String& linkplay_client_get_last_artist();
const PlayerState& linkplay_client_get_last_player_state();
