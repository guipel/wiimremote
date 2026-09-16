#pragma once

#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lvgl.h>
#include "config.h"

class LGFX_ESP32S3_Custom : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 _panel_instance;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Light_PWM     _light_instance;
    lgfx::Touch_FT5x06  _touch_instance;

public:
    LGFX_ESP32S3_Custom();
};

extern LGFX_ESP32S3_Custom gfx;

// Driver functions
void display_driver_init();
void display_set_backlight(uint8_t brightness);

