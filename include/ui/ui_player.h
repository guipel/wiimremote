#pragma once

#include <lvgl.h>
#include "model.h"

// Initialize Player Tab widgets and layout
void ui_player_init(lv_obj_t* parent);

// Update track title, artist, and resolution from metadata
void ui_player_set_meta(const TrackMeta& meta);

// Unified Playback Coordinator: atomic update for play/pause state, progress, and times
void ui_player_update_state(PlayState state, uint32_t curpos_ms, uint32_t totlen_ms);

// Update volume level, mute state, and fixed vs variable controls
void ui_player_set_volume(uint8_t volume, bool is_fixed, bool mute);

// Update stream resolution and track index counter (e.g. 24/96, 5/12)
void ui_player_set_stream_info(const StreamInfo& stream, uint16_t track_num, uint16_t track_total);

// Interpolate progress bar and elapsed time (called from periodic UI timer)
void ui_player_tick_progress(uint32_t delta_ms);

// Immediately clear all playback widgets (e.g. on device switch or track cleared)
void ui_player_clear();

// Update volume mode visibility without altering other playback states
void ui_player_set_fixed_volume_mode(bool is_fixed);
