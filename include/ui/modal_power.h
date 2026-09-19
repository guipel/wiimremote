#pragma once

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

// Initialize Power Management Modal overlay
void modal_power_init(lv_obj_t* parent = nullptr);

// Open the power modal with current battery voltage
void modal_power_open(uint32_t bat_mv = 0);

// Close the power modal
void modal_power_close();

// Update battery voltage display in modal
void modal_power_update_battery(uint32_t bat_mv);

// Check if power modal is currently visible
bool modal_power_is_open();
