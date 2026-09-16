#pragma once

#include <Arduino.h>

void power_manager_init();
void power_manager_notify_activity();
void power_manager_check_inactivity();

void power_manager_set_dim_timeout_sec(uint16_t seconds);
uint16_t power_manager_get_dim_timeout_sec();

void power_manager_set_sleep_timeout_sec(uint16_t seconds);
uint16_t power_manager_get_sleep_timeout_sec();

void power_manager_set_brightness_pct(uint8_t pct);
uint8_t power_manager_get_brightness_pct();

void power_manager_enter_deep_sleep();
