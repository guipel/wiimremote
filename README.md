# WiiM ESP32-S3 Touchscreen Remote Control

A modular, high-performance, dark-themed touchscreen remote control project tailored for **WiiM audio streamers** (WiiM Mini, WiiM Pro, WiiM Pro Plus, WiiM Amp, WiiM Ultra) and generic LinkPlay-based audio nodes.

Built for **ESP32-S3 (N16R8: 16MB Flash, 8MB Octal OPI PSRAM)** with an **ILI9341V 2.8" SPI TFT display (240x320)**, **FT6336G capacitive touch controller**, and dedicated **battery fuel gauge telemetry**.

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
| **BL** | **GPIO 45** | Backlight (LEDC PWM Channel 0, 5 kHz, 8-bit, Active HIGH) |
| **RST** | **EN / Reset** | Hardware Reset tied to system EN |

### 2. FocalTech FT6336G Capacitive Touch Controller
| Signal | ESP32-S3 GPIO | Function |
| :--- | :--- | :--- |
| **SDA** | **GPIO 16** | I2C Data |
| **SCL** | **GPIO 15** | I2C Clock |
| **INT** | **GPIO 17** | Interrupt (Active LOW, Deep Sleep Wake Source) |
| **RST** | **GPIO 18** | Reset (Active LOW) |
| **I2C Addr** | `0x38` | Default FocalTech 7-bit address (Port 1, 400 kHz) |

### 3. Battery Level Telemetry (ADC1_CH8)
| Signal | ESP32-S3 GPIO | Function |
| :--- | :--- | :--- |
| **BAT_ADC** | **GPIO 9** | ADC1 Channel 8 with 200k / 200k voltage divider (2.0x ratio) |

* **Voltage Thresholds**:
  * **Charging / USB Power**: $\ge 4100\text{ mV}$ (tested ~4.12V float voltage)
  * **Full**: $\ge 3950\text{ mV}$ (~75% – 100%)
  * **High**: $\ge 3800\text{ mV}$ (~50% – 75%)
  * **Medium**: $\ge 3650\text{ mV}$ (~25% – 50%)
  * **Low**: $< 3500\text{ mV}$ (~10% – 25%)

### 4. Reserved Onboard Pins (Isolated / Protected)
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
    [ LovyanGFX ]             [ LVGL 8.4 ]        [ WiFi & SSDP ]            [ HTTP / UPnP ]
   • ILI9341V DMA            • 3 Tabs             • UDP 239.255.255.250     • getPlayerStatus
   • FT6336G (66 Hz)         • 3 Modals           • /24 Subnet Scan         • getMetaInfo
   • Backlight PWM           • Playback Coord.    • Touchscreen Provision   • MCUKeyShortClick
   • Async Double Buff       • PSRAM Draw Buffers • In-RAM Reconnect        • LRCLIB REST API
                             • Theme & Tokens                               • ADC Battery Telemetry
                                   ^                                              |
                                   |              xQueueUiState                   |
                                   +==============================================+
                                   |  (PlayerState, TrackMeta, Presets, Battery)  |
                                   +==============================================+
                                   |               xQueueUiCmd                    |
                                   |==============================================+
                                   |  (Play/Pause, Next/Prev, Vol, Mode, Presets) |
                                   +--------------------------------------------->+
```

### 1. UI Task (Core 1)
- Dedicated exclusively to LovyanGFX display rendering, touch polling (66 Hz / 15ms period), and LVGL 8.4 execution.
- Allocates dual draw buffers in 8MB Octal PSRAM (`MALLOC_CAP_SPIRAM`) with asynchronous SPI DMA transfers (`display_flush_cb`).
- **Zero network requests** in this thread ensures zero UI lag or dropped frames.

### 2. Network Task (Core 0)
- Wi-Fi connection manager with automatic non-blocking in-RAM retry.
- Dual discovery engine:
  - **SSDP M-SEARCH** broadcast to `239.255.255.250:1900` (`urn:schemas-upnp-org:device:MediaRenderer:1` and `ssdp:all`).
  - **Subnet scanner fallback** probing port 80 across the active `/24` subnet.
- Fast LinkPlay HTTP API polling (`command=getPlayerStatus`) at 1000ms intervals with a strict 1500ms timeout limit.
- LRCLIB REST client for on-demand lyrics retrieval with regex synced tag parsing.
- Volume throttling (150ms) to prevent HTTP request flooding while dragging the volume slider.
- Battery fuel gauge sampling at 5-second intervals via ADC1 Channel 8.
- NVS persistence via ESP32 `Preferences` for retaining the selected streamer, Wi-Fi credentials, brightness, and sleep timeouts.

### 3. Unified Playback Coordinator
- Normalizes LinkPlay transient states (`"load"`, `"none"` during track transitions, and negative `curpos` pre-buffer countdowns) into `PLAY_STATE_BUFFERING`.
- Eliminates widget flapping: holds Play/Pause symbols without toggling during buffering, freezes elapsed timers at `00:00`, and prevents progress bar jumping.

---

## 📁 Modular UI Architecture

The user interface is organized into single-responsibility modules:

```
include/ui/
├── ui_theme.h         # Color tokens, fonts, padlock bitmap asset, format_time()
├── ui_header.h        # Top status bar (Wi-Fi RSSI, active device, battery meter)
├── ui_player.h        # Now Playing view, transport controls, seek slider, adaptive volume
├── ui_lyrics.h        # LRCLIB lyrics view container and on-demand activation
├── ui_presets.h       # 12-preset button grid with auto-navigation
├── modal_device.h     # Discovered WiiM streamer list modal with rescan
├── modal_wifi.h       # Wi-Fi network scanner, touch keyboard, credentials modal
└── modal_power.h      # Battery voltage, brightness slider, auto-dim, auto-sleep, deep sleep

src/ui/
├── ui_core.cpp        # ui_init(), event loop router (ui_process_events), timers
├── ui_header.cpp      # Header layout & event dispatching
├── ui_player.cpp      # Player tab, adaptive volume logic, playback coordinator
├── ui_lyrics.cpp      # Lyrics container, auto-scroll, formatted error handling
├── ui_presets.cpp     # 12-preset grid and recall routing
├── modal_device.cpp   # Device discovery UI and active selection
├── modal_wifi.cpp     # Wi-Fi scanning list, LVGL keyboard, connection flow
└── modal_power.cpp    # Brightness LEDC control, sleep trigger, timeout selection
```

---

## 📁 Modular Network Architecture

The network subsystem is decoupled into single-responsibility services:

```
include/net/
├── net_utils.h         # Stateless string transcoding (UTF-8, hex, URL, XML, LRC)
├── wifi_service.h      # Wi-Fi station lifecycle, reconnect loop, synchronous scan
├── discovery_service.h # SSDP M-SEARCH broadcast, candidate verification, device storage
├── linkplay_client.h   # TLS socket session, player polling, buffering state, presets, UPnP
└── lyrics_client.h     # LRCLIB REST client, track title sanitation, error categorization

src/net/
├── net_utils.cpp       # Pure stateless utility functions
├── wifi_service.cpp    # ESP32 Wi-Fi station management & non-blocking reconnect
├── discovery_service.cpp # SSDP UDP listener, HTTP/HTTPS verification, thread-safe list
├── linkplay_client.cpp # LinkPlay HTTP/HTTPS client & UPnP SOAP transport control
└── lyrics_client.cpp   # LRCLIB API client with regex timing tag sanitation
```

---

## 📱 Features & UI Navigation

The interface features a **Top Header Bar**, a **3-Tab Navigation View**, and **3 Interactive Modals**:

```
+-------------------------------------------------------------+
|  [ -52dBm]           WiiM Pro Plus           [4.02V  ]   |  <-- Top Header Bar
+-------------------------------------------------------------+
|                                                             |
|                       [ TAB CONTENT ]                       |
|                                                             |
|       [ Tab 1: Lyrics ]  [ Tab 2: Player ]  [ Tab 3: Presets ]      |
|                                                             |
+-------------------------------------------------------------+
|     Lyrics      |       Player       |       Presets        |  <-- Bottom Navigation
+-------------------------------------------------------------+
```

### Top Header Bar
1. **Wi-Fi Signal**: Live RSSI indicator (e.g. ` -52dBm`). **Tap** opens the **Wi-Fi Manager Modal**.
2. **Active Streamer**: Displays the friendly name of the active WiiM streamer. **Tap** opens the **Device Selector Modal**.
3. **Battery Fuel Gauge**: Dynamic battery icon and voltage telemetry. **Tap** opens the **Power Management Modal**.

### Tab Navigation (3 Tabs)
1. **Lyrics Tab (Left)**:
   - Synchronized / plain-text lyrics retrieved on demand from the LRCLIB REST API.
   - Automatically strips `[mm:ss.xx]` timing tags from synced LRC records if plain text is unavailable.
   - Distinct error feedback: differentiates between track not found (HTTP 404) and connection/server failures.
2. **Now Playing Tab (Center)**:
   - High-contrast track title rendered in **Montserrat 24** typography with circular scrolling for long titles.
   - Artist and album metadata.
   - Centered audio resolution/bitrate label (e.g., `FLAC • 192 kHz / 24-bit`) positioned between progress bar and transport controls.
   - Interactive progress bar with elapsed and total duration timestamps.
   - Centered transport controls: Previous, Play/Pause/Resume, Next.
   - Centered track counter (`X/Y`) directly beneath the Play/Pause button.
    - **Unified Volume Controls**:
      - Persistent row containing the instant mute button, 120 px volume slider, percentage/status label, and shuffle/aux button across all modes.
      - **Variable Volume**: Interactive slider updating output volume in real-time with numeric percentage label (`0%`–`100%`).
      - **Fixed Volume**: Disables the volume slider, updates the status label to `"Fixed"`, and preserves mute toggle functionality via the mute button (crimson red `#E63946` when muted).
    - **Input Source Switching**: Cycle through Wi-Fi, Line-In, Optical, and Bluetooth.
3. **Presets Tab (Right)**:
   - 3x4 grid of **12 preset tiles** dynamically synced from the WiiM device (`getPresetInfo`).
   - One-touch preset recall with automatic navigation back to the Player tab.

### Interactive Modals (3 Modals)
1. **Device Selector Modal** (Tap Streamer Name):
   - Lists all WiiM streamers discovered via SSDP and subnet scanning.
   - Shows device name, IP address, and active checkmark.
   - **Rescan Network** button to trigger discovery on demand.
   - Persists the selected device to NVS memory.
2. **Wi-Fi Manager Modal** (Tap Wi-Fi Icon):
   - Touchscreen network scanner displaying available SSIDs and signal strength.
   - Full on-screen QWERTY touch keyboard for password entry.
   - Live connection status spinner with automatic modal close on success.
3. **Power Management Modal** (Tap Battery Icon):
   - Live battery voltage readout ($mV$) and battery fuel gauge.
   - 20%–100% screen brightness slider matching the volume bar aesthetic.
   - Configurable auto-dimming timeout (dims to 10% brightness on inactivity).
   - Configurable auto-sleep timeout and manual **Sleep** button.
   - ESP32-S3 deep sleep with capacitive touch-to-wake interrupt support.

---

## 📡 LinkPlay & External API Reference

### LinkPlay / WiiM HTTP API
| Operation | HTTP Command | Description |
| :--- | :--- | :--- |
| **Device Identification** | `GET /httpapi.asp?command=getStatus` | Discovers device name, UUID, and MAC |
| **Playback Telemetry** | `GET /httpapi.asp?command=getPlayerStatus` | Polling play state, position, duration, and volume |
| **Track Metadata** | `GET /httpapi.asp?command=getMetaInfo` | Retrieves title, artist, album, format, and bitrate |
| **Presets List** | `GET /httpapi.asp?command=getPresetInfo` | Retrieves 12 preset names and mappings |
| **Play / Pause Toggle** | `GET /httpapi.asp?command=setPlayerCmd:onepause` | Toggles playback state |
| **Resume Playback** | `GET /httpapi.asp?command=setPlayerCmd:resume` | Resumes playback from pause without restarting track |
| **Next Track** | `GET /httpapi.asp?command=setPlayerCmd:next` | Skips to next track in queue |
| **Previous Track** | `GET /httpapi.asp?command=setPlayerCmd:prev` | Skips to previous track |
| **Seek Position** | `GET /httpapi.asp?command=setPlayerCmd:seek:<SECS>` | Seeks to timestamp in seconds |
| **Set Volume** | `GET /httpapi.asp?command=setPlayerCmd:vol:<VAL>` | Adjusts volume (0..100) |
| **Set Mute** | `GET /httpapi.asp?command=setPlayerCmd:mute:<1\|0>` | Toggles mute state |
| **Input Source Switch** | `GET /httpapi.asp?command=setPlayerCmd:switchmode:<MODE>` | Switches input (`wifi`, `line-in`, `optical`, `bluetooth`) |
| **Trigger Preset** | `GET /httpapi.asp?command=MCUKeyShortClick:<INDEX>` | Recalls preset slot (1..12) |

### External Lyrics API (LRCLIB)
| Operation | Endpoint | Description |
| :--- | :--- | :--- |
| **Fetch Lyrics** | `GET https://lrclib.net/api/get?track_name=...&artist_name=...` | Retrieves plain and synced LRC lyrics on demand |

---

## 🚀 Getting Started

### 1. Prerequisites
- [PlatformIO IDE](https://platformio.org/) (VSCode extension or CLI).
- ESP32-S3 board with 16MB Flash and 8MB Octal PSRAM (N16R8).
- ILI9341V 2.8" SPI TFT (240x320) + FT6336G capacitive touch screen.

### 2. Wi-Fi Configuration
You can configure Wi-Fi directly on the device using the touchscreen keyboard on first boot!

Alternatively, create an `include/secrets.h` file (or edit `include/config.h`) for fallback credentials:
```cpp
#pragma once
#define WIFI_SSID       "YourWiFiNetworkName"
#define WIFI_PASSWORD   "YourWiFiPassword"
```

### 3. Build & Flash
Open a terminal in the project directory and run:
```bash
# Build the project
pio run

# Flash to the ESP32-S3
pio run --target upload

# Monitor serial output
pio run --target monitor
```

---

## 📄 License
This project is licensed under the MIT License.
