#pragma once

#include <lvgl.h>
#include "model.h"

// Initialize Lyrics Tab content
void ui_lyrics_init(lv_obj_t* parent);

// Update lyrics content from LRCLIB response
void ui_lyrics_set_content(const LyricsInfo& info);

// Notify lyrics component that track metadata changed
void ui_lyrics_on_track_changed(const TrackMeta& meta, bool is_lyrics_tab_active);

// Trigger on-demand lyrics fetch when user switches to Lyrics tab
void ui_lyrics_on_tab_activated();

// Clear lyrics view
void ui_lyrics_clear();
