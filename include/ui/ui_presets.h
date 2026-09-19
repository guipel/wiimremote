#pragma once

#include <lvgl.h>
#include "model.h"

typedef void (*OnPresetTriggeredCb)(uint8_t index);

// Initialize Presets Tab content
void ui_presets_init(lv_obj_t* parent);

// Update preset names from device preset list
void ui_presets_set_list(const PresetList& presets);

// Set callback when a preset button is clicked (e.g. to navigate back to player tab)
void ui_presets_set_on_triggered(OnPresetTriggeredCb cb);
