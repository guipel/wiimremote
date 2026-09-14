# WiiM ESP32-S3 Touchscreen Remote Control

A modular, high-performance, dark-themed touchscreen remote control project tailored for **WiiM audio streamers** (WiiM Mini, WiiM Pro, WiiM Pro Plus, WiiM Amp, WiiM Ultra) and generic LinkPlay-based audio nodes.

Built for **ESP32-S3 (N16R8: 16MB Flash, 8MB Octal OPI PSRAM)** with an **ILI9341V 2.8" SPI TFT display (240x320)** and **FT6336G capacitive touch controller**.

---

## 🛠 Hardware Specifications & Pinout

### 1. ILI9341V 2.8" SPI Display (240x320)
| Signal | ESP32-S3 GPIO | Function |
| :--- | :--- | :--- |
| **MOSI** | **GPIO 11** | SPI Data Out |
| **MISO** | **GPIO 13** | SPI Data In |
| **SCLK** | **GPIO 12** | SPI Clock |
| **CS** | **GPIO 10** | SPI Chip Select |
| **DC** | **GPIO 46** | Data / Command Select |
| **BL** | **GPIO 45** | Backlight (PWM / LEDC controlled, Active HIGH) |
| **RST** | **EN / Reset** | Hardware Reset tied to system EN |

### 2. FocalTech FT6336G Capacitive Touch Controller
| Signal | ESP32-S3 GPIO | Function |
| :--- | :--- | :--- |
| **SDA** | **GPIO 16** | I2C Data |
| **SCL** | **GPIO 15** | I2C Clock |
| **INT** | **GPIO 17** | Interrupt (Active LOW) |
| **RST** | **GPIO 18** | Reset (Active LOW) |
| **I2C Addr** | `0x38` | Default FocalTech 7-bit address |

### 3. Reserved Onboard Pins (Isolated / Protected)
| Peripheral | Pin(s) | Description |
| :--- | :--- | :--- |
| **PA_EN** | **GPIO 1** | **Driven HIGH** at boot to keep onboard amplifier muted |
| **Codec I2S** | **GPIO 4, 5, 6, 7, 8** | High-impedance pulldown, no bus collision |
| **Status RGB** | **GPIO 42** | Status indicator LED |

---

## 🏗 Architecture & Multitasking Design

The system runs on **FreeRTOS** leveraging the ESP32-S3 dual cores to achieve complete decoupling between network operations and the UI:

```
                  +------------------------------------------------------+
                  |                   FreeRTOS System                    |
                  +------------------------------------------------------+
                                      |              |
                      +---------------+              +---------------+
                      |                                              |
               [ Core 1: UI Task ]                           [ Core 0: Net Task ]
             (Priority 3, ~60 FPS)                          (Priority 2, Background)
                      |                                              |
         +------------+------------+                    +------------+------------+
         |                         |                    |                         |
    [ LovyanGFX ]             [ LVGL 8.3 ]        [ WiFi & SSDP ]            [ HTTP API ]
   • ILI9341V DMA            • Now Playing        • UDP 239.255.255.250     • getPlayerStatus
   • FT6336G Touch           • 12 Presets         • /24 Subnet Scan         • getMetaInfo
   • Backlight PWM           • Device Picker      • Non-blocking retry      • MCUKeyShortClick
                             • PSRAM Buffers                                • NVS Persistence
                                   ^                                              |
                                   |              xQueueUiState                   |
                                   +==============================================+
                                   |  (PlayerState, TrackMeta, Presets, Devices)  |
                                   +==============================================+
                                   |               xQueueUiCmd                    |
                                   |==============================================+
                                   |  (Play/Pause, Next/Prev, Vol, Presets, Scan) |
                                   +--------------------------------------------->+
```

1. **UI Task (Core 1)**:
   - Dedicated exclusively to LovyanGFX display rendering, touch input polling, and LVGL execution.
   - Allocates dual draw buffers in 8MB Octal PSRAM (`MALLOC_CAP_SPIRAM`).
   - **Zero network requests** in this thread ensures zero UI lag or dropped frames.
2. **Network Task (Core 0)**:
   - Wi-Fi connection manager with automatic non-blocking retry.
   - Dual discovery engine:
     - **SSDP M-SEARCH** broadcast to `239.255.255.250:1900` (`urn:schemas-upnp-org:device:MediaRenderer:1` and `ssdp:all`).
     - **Subnet scanner fallback** probing port 80 across the active `/24` subnet.
   - Verification via `GET http://<IP>/httpapi.asp?command=getStatus`.
   - Polling `command=getPlayerStatus` at 1000ms intervals.
   - Fast HTTP command dispatch with a strict timeout limit of 1200ms.
   - Volume throttling (150ms) to prevent HTTP request flooding while dragging the volume slider.
   - NVS persistence via ESP32 `Preferences` for retaining the selected streamer across power cycles.

---

## 🚀 Getting Started

### 1. Prerequisites
- [PlatformIO IDE](https://platformio.org/) (VSCode extension or CLI).
- ESP32-S3 board with 16MB Flash and 8MB Octal PSRAM (N16R8).

### 2. Wi-Fi Configuration
Edit [include/config.h](file:///include/config.h) to configure your Wi-Fi credentials:
```cpp
#define WIFI_SSID       "YourWiFiNetworkName"
#define WIFI_PASSWORD   "YourWiFiPassword"
```

### 3. Build & Flash
Open terminal in the project directory and run:
```bash
# Build the project
pio run

# Flash to the ESP32-S3
pio run --target upload

# Monitor serial output
pio run --target monitor
```

---

## 📱 Features & UI Navigation

1. **Header Bar**:
   - **Wi-Fi Signal**: Live RSSI indicator (e.g. ` -52dBm`).
   - **Active Streamer Selector**: Displays the name of the currently controlled WiiM unit. Tapping opens the **Device Selector Modal**.
2. **Now Playing Tab**:
   - High-contrast track title (auto-scroll for long titles).
   - Artist and album metadata.
   - Audio format and sample rate badge (e.g., `FLAC • 192 kHz / 24-bit`).
   - Interactive progress bar with elapsed and total duration timestamps.
   - Transport controls: Previous, Play/Pause toggle, Next.
   - Volume slider (0-100%) and instant Mute toggle button.
3. **Presets Tab**:
   - Clean 3x4 grid of **12 preset tiles**.
   - Preset names dynamically synced from the WiiM device (`command=getPresetInfo`).
   - One-touch preset recall triggering `command=MCUKeyShortClick:<INDEX>`.
4. **Device Selector Modal**:
   - Lists all WiiM streamers discovered on the local network.
   - Shows device name, IP address, and active status checkmark.
   - **Rescan Network** button to trigger SSDP and subnet discovery on demand.
   - Auto-saves the selected streamer to non-volatile memory (NVS).

---

## 📡 LinkPlay / WiiM HTTP API Reference

| Operation | HTTP Command |
| :--- | :--- |
| **Device Identification** | `GET /httpapi.asp?command=getStatus` |
| **Playback Telemetry** | `GET /httpapi.asp?command=getPlayerStatus` |
| **Track Metadata** | `GET /httpapi.asp?command=getMetaInfo` |
| **Presets List** | `GET /httpapi.asp?command=getPresetInfo` |
| **Play / Pause Toggle** | `GET /httpapi.asp?command=setPlayerCmd:onepause` |
| **Next Track** | `GET /httpapi.asp?command=setPlayerCmd:next` |
| **Previous Track** | `GET /httpapi.asp?command=setPlayerCmd:prev` |
| **Set Volume** | `GET /httpapi.asp?command=setPlayerCmd:vol:<VAL>` (0..100) |
| **Set Mute** | `GET /httpapi.asp?command=setPlayerCmd:mute:<1\|0>` |
| **Trigger Preset** | `GET /httpapi.asp?command=MCUKeyShortClick:<INDEX>` (1..12) |
