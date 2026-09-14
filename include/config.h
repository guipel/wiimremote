#pragma once

#include <Arduino.h>

// =============================================================================
// Hardware Pinout Configuration
// =============================================================================

// Display (ILI9341V SPI, 240x320)
#define TFT_MOSI            11
#define TFT_MISO            13
#define TFT_SCLK            12
#define TFT_CS              10
#define TFT_DC              46
#define TFT_BL              45   // Backlight PWM (Active HIGH)
#define TFT_RST             -1   // Tied to system EN / Reset

#define SCREEN_WIDTH        240
#define SCREEN_HEIGHT       320
#define SCREEN_ROTATION     0    // 0 = Portrait (240x320), 2 = 180° Portrait

// Touch Controller: FocalTech FT6336G (Capacitive I2C)
#define TOUCH_SDA           16
#define TOUCH_SCL           15
#define TOUCH_INT           17   // Active LOW
#define TOUCH_RST           18   // Active LOW
#define TOUCH_ADDR          0x38 // FT6336 default 7-bit address
#define TOUCH_I2C_PORT      1
#define TOUCH_I2C_FREQ      400000

// Touch Axis Mapping & Inversion
#define TOUCH_INVERT_X      true
#define TOUCH_INVERT_Y      true
#define TOUCH_SWAP_XY       false

// Reserved & Protection Pins (Must not conflict)
#define PIN_PA_EN           1    // Audio Amp Enable: Set HIGH to keep onboard speaker muted
#define PIN_I2S_MCLK        4    // Reserved Codec I2S
#define PIN_I2S_BCLK        5
#define PIN_I2S_WS          6
#define PIN_I2S_DOUT        7
#define PIN_I2S_DIN         8
#define PIN_RGB_STATUS      42   // Onboard RGB Status LED

// Backlight PWM Channel
#define BL_LEDC_CHANNEL     0
#define BL_LEDC_FREQ        5000
#define BL_LEDC_RESOLUTION  8
#define BL_DEFAULT_BRIGHT   220  // 0 - 255

// =============================================================================
// Wi-Fi Credentials & Networking Defaults
// =============================================================================
#if __has_include("secrets.h")
#include "secrets.h"
#endif

// Fallback Wi-Fi credentials (override in secrets.h or via build flags)
#ifndef WIFI_SSID
#define WIFI_SSID           "YOUR_WIFI_SSID"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD       "YOUR_WIFI_PASSWORD"
#endif

#define WIFI_CONNECT_TIMEOUT_MS   15000
#define WIFI_RECONNECT_INTERVAL_MS 10000

// SSDP Discovery
#define SSDP_MULTICAST_IP   "239.255.255.250"
#define SSDP_PORT           1900
#define SSDP_DISCOVERY_INTERVAL_MS 30000

// LinkPlay / WiiM HTTP & HTTPS API
#define WIIM_HTTPS_PORT           443
#define WIIM_UPNP_PORT            49152
#define HTTP_REQUEST_TIMEOUT_MS   1500
#define STATUS_POLL_INTERVAL_MS   1000
#define VOLUME_THROTTLE_MS        150

// FreeRTOS Task Configuration
#define UI_TASK_STACK_SIZE        (16 * 1024)
#define UI_TASK_PRIORITY          3
#define UI_TASK_CORE              1

#define NET_TASK_STACK_SIZE       (32 * 1024)
#define NET_TASK_PRIORITY         2
#define NET_TASK_CORE             0

#define QUEUE_UI_CMD_LEN          16
#define QUEUE_UI_STATE_LEN        8
