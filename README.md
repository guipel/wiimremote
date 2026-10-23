<img width="980" height="1307" alt="image" src="https://github.com/user-attachments/assets/5f1846cc-07bb-46b5-bad2-643902deeefa" />



# WiiM ESP32-S3 Touchscreen Remote Control

A modular, high-performance, multi-theme touchscreen remote control project tailored for **WiiM audio streamers** (WiiM Mini, WiiM Pro, WiiM Pro Plus, WiiM Amp, WiiM Ultra) and generic LinkPlay-based audio nodes.

Built for **ESP32-S3 (N16R8: 16MB Flash, 8MB Octal OPI PSRAM)** with an **ILI9341V 2.8" SPI TFT display (240x320)**, **FT6336G capacitive touch controller**, and dedicated **battery fuel gauge telemetry**.

---

## 🚀 Installation & Getting Started

> 📖 **First time building this remote?** Follow the step-by-step **[Beginner's Installation & Setup Guide (`INSTALL.md`)](INSTALL.md)**—no soldering, no coding, and no software installation required!

### 1. Recommended Hardware
- **Touchscreen Board**: [Hosyond 2.8" ESP32-S3 Capacitive Touch Display (ILI9341V 240x320 + FT6336G, N16R8)](https://www.amazon.com/dp/B0FKG7WRWV)
- **Rechargeable Battery**: [3.7V 3000mAh Li-Po Battery (JST 1.25mm 2-Pin)](https://www.amazon.com/dp/B0DK5BBKM5) *(Always verify `+` Red / `-` Black polarity before plugging in—see [`INSTALL.md`](INSTALL.md))*

### 2. Option A: Zero-Install Web Browser Flasher (Recommended for All Users)
You can flash the pre-built release firmware directly from **Google Chrome** or **Microsoft Edge** without installing any development tools:
1. Download **[`wiimremote-factory.bin`](https://github.com/guipel/wiimremote/releases/latest/download/wiimremote-factory.bin)** from the **[Latest GitHub Release](https://github.com/guipel/wiimremote/releases/latest)**.
2. Connect the ESP32-S3 display board to your computer via a **USB-C data cable**.
3. Open **[Espressif Web Flasher (`esptool-js`)](https://espressif.github.io/esptool-js/)** in Chrome or Edge and click **`Connect`** (under *Program*).
4. Set **Flash Address** to **`0x0`**, choose **`wiimremote-factory.bin`**, leave **Flash Mode / Frequency / Size** set to **`keep`**, and click **`Program`**.
5. Wait ~15 seconds until the log finishes with **`Hash of data verified. Leaving... Hard resetting via RTS pin...`** (this means flashing is 100% done; note that flashing `wiimremote-factory.bin` at `0x0` performs a full factory flash that resets saved settings).
6. **Restart the board** using either method:
   - **Way 1 (On the Board)**: Press the physical **`RST`** button on the back edge of the board.
   - **Way 2 (On the Website — for boards in a sealed case)**: Click **`Disconnect`** at the top of the page, scroll down to the **Console** section, click **`Start`** (select the same USB port), and the board will immediately reboot (or click **`Reset`** under Console).

### 3. Option B: Building from Source (Developers)
If you want to customize the code or build from source using [PlatformIO](https://platformio.org/):

```bash
# Build firmware (also generates firmware/wiimremote-factory.bin)
pio run

# Flash directly to the connected ESP32-S3
pio run --target upload

# Monitor serial output
pio run --target monitor
```

*(Optional)* Wi-Fi is configured directly on the touchscreen on first boot, or you can pre-populate fallback credentials in `include/secrets.h`:
```cpp
#pragma once
#define WIFI_SSID       "YourWiFiNetworkName"
#define WIFI_PASSWORD   "YourWiFiPassword"
```

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
   • ILI9341V DMA            • 4 Tabs             • UDP 239.255.255.250     • getPlayerStatus
   • FT6336G (66 Hz)         • 3 Full-Screen      • NVS Fast-Path Verify    • getMetaInfo
   • Backlight PWM             Views (32px Hdr)   • MediaRenderer:1 &       • MCUKeyShortClick
   • Internal DMA Buffs      • 6 Live Themes        MediaServer:1 SSDP      • CreateQueue & PlayQueue
   • ADC Battery Telemetry   • PSRAM LVGL Heap    • Sync Network Scan       • UPnP ContentDirectory
                             • Extended UTF-8     • Auto Reconnect          • LRCLIB REST API
                                   ^                                              |
                                   |              xQueueUiState                   |
                                   +==============================================+
                                   |  (PlayerState, TrackMeta, Presets, Browser)  |
                                   +==============================================+
                                   |               xQueueUiCmd                    |
                                   |==============================================+
                                   | (Play/Pause, Seek, Vol, Presets, UPnP Queue) |
                                   +--------------------------------------------->+
```

### 1. UI Task (Core 1)
- Dedicated exclusively to LovyanGFX display rendering, touch polling (66 Hz / 15ms period), LVGL 8.4 execution, and 5-second ADC battery telemetry.
- Allocates dual draw buffers in **internal DMA-capable SRAM** (`MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`) for zero-copy SPI DMA transfers (`display_flush_cb`), while placing the 256 KB LVGL object heap in 8MB Octal PSRAM (`MALLOC_CAP_SPIRAM`).
- **Zero network requests** in this thread ensures zero UI lag or dropped frames.

### 2. Network Task (Core 0)
- Wi-Fi connection manager with automatic non-blocking reconnect and synchronous Core 0 SSID scanning.
- Dual-phase discovery engine:
  - **Fast-path NVS verification**: Immediately probes previously saved streamer IPs on boot for sub-second reconnection and prunes unreachable entries on manual rescan.
  - **SSDP M-SEARCH** broadcast to `239.255.255.250:1900` discovering both WiiM/LinkPlay `MediaRenderer:1` streamers and UPnP/DLNA `MediaServer:1` servers (e.g., Asset UPnP, MinimServer, Plex, Synology).
- Fast LinkPlay HTTP/HTTPS API polling (`command=getPlayerStatus`) at 1000ms intervals with persistent connection reuse and UPnP SOAP fallback.
- UPnP `ContentDirectory:1` SOAP client for browsing local media server hierarchies, resolving embedded track metadata via `BrowseMetadata`, and dispatching multi-track `<PlayList>` XML queues directly to the WiiM streamer (`CreateQueue` & `PlayQueueWithIndex`).
- LRCLIB REST client for on-demand lyrics retrieval with synced tag stripping.
- Volume throttling (150ms) to prevent HTTP request flooding while dragging the volume slider.
- NVS persistence via ESP32 `Preferences` for retaining saved streamers, fixed-volume lock states, Wi-Fi credentials, brightness, sleep timeouts, and selected UI color theme.

### 3. Unified Playback Coordinator
- Normalizes LinkPlay transient states (`"load"`, `"none"` during track transitions, and negative `curpos` pre-buffer countdowns) into `PLAY_STATE_BUFFERING`.
- Eliminates widget flapping: holds Play/Pause symbols without toggling during buffering, freezes elapsed timers at `00:00`, and prevents progress bar jumping.

---

## 📁 Modular UI Architecture

The user interface is organized into single-responsibility modules:

```
include/ui/
├── themes_config.h    # Declarative 6-theme color palette config (RGB565-tuned)
├── ui_theme.h         # Dynamic theme getters, color macros, ui_style_flat_list_btn(), format_time()
├── ui_header.h        # Top 32px status bar (Wi-Fi RSSI, active device, battery meter)
├── ui_player.h        # Now Playing view, transport controls, ghost seek target, adaptive volume
├── ui_lyrics.h        # LRCLIB lyrics view container and on-demand activation
├── ui_presets.h       # 12-preset list view with auto-navigation
├── ui_browser.h       # UPnP / DLNA MediaServer folder & track browser tab
├── modal_device.h     # Full-screen discovered WiiM streamer list with rescan
├── modal_wifi.h       # Full-screen Wi-Fi network scanner, touch keyboard, credentials view
└── modal_power.h      # Full-screen Power & Display view (brightness, auto-dim, sleep, theme)

src/ui/
├── ui_core.cpp        # ui_init(), single-source theme dispatcher (ui_apply_theme), padlock asset, event router
├── ui_header.cpp      # 32px header layout & event dispatching
├── ui_player.cpp      # Player tab, ghost seek indicator, adaptive volume, input mode cycling
├── ui_lyrics.cpp      # Lyrics container, auto-scroll, formatted error handling
├── ui_presets.cpp     # 12-preset flat list and recall routing
├── ui_browser.cpp     # UPnP server list, breadcrumb header, folder/track list navigation
├── modal_device.cpp   # Device discovery full-screen view and active selection
├── modal_wifi.cpp     # Wi-Fi scanning list, LVGL keyboard, connection flow
├── modal_power.cpp    # Brightness LEDC control, sleep trigger, timeout & live theme cycling
└── fonts/             # Extended Latin-1 + LVGL symbol Montserrat fonts (10..24px)
```

---

## 📁 Modular Network Architecture

The network subsystem is decoupled into single-responsibility services:

```
include/net/
├── net_utils.h         # Stateless string transcoding (UTF-8, hex, URL, XML, LRC)
├── wifi_service.h      # Wi-Fi station lifecycle, reconnect loop, Core 0 scan
├── discovery_service.h # SSDP M-SEARCH broadcast (MediaRenderer & MediaServer), device storage
├── linkplay_client.h   # TLS socket session, player polling, buffering state, presets, UPnP
├── upnp_client.h       # UPnP ContentDirectory SOAP browser & LinkPlay PlayList queue builder
└── lyrics_client.h     # LRCLIB REST client, track title sanitation, error categorization

src/net/
├── net_utils.cpp       # Pure stateless utility functions
├── wifi_service.cpp    # ESP32 Wi-Fi station management, non-blocking reconnect & Core 0 scan
├── discovery_service.cpp # SSDP UDP listener, MediaRenderer/MediaServer verification, device lists
├── linkplay_client.cpp # LinkPlay HTTP/HTTPS client & UPnP SOAP transport control
├── upnp_client.cpp     # UPnP BrowseDirectChildren, BrowseMetadata, CreateQueue & PlayQueueWithIndex
└── lyrics_client.cpp   # LRCLIB API client with timing tag sanitation
```

---

## 📱 Features & UI Navigation

The interface features a **32px Top Header Bar**, a **4-Tab Navigation View**, and **3 Full-Screen Settings Views**:

```
+-------------------------------------------------------------+
|  []                  WiiM Pro Plus                   [  ] |  <-- 32px Top Header Bar
+-------------------------------------------------------------+
|                                                             |
|                       [ TAB CONTENT ]                       |
|                                                             |
|  [ Tab 1: Lyrics ] [ Tab 2: Player ] [ Tab 3: Presets ] [ Tab 4: Browser ]
|                                                             |
+-------------------------------------------------------------+
|   Lyrics    |    Player     |    Presets    |    Browser    |  <-- Bottom Navigation
+-------------------------------------------------------------+
```

### Top Header Bar (32px)
1. **Wi-Fi Signal**: Live RSSI color-coded indicator. **Tap** opens the **Wi-Fi Networks** screen.
2. **Active Streamer**: Displays the friendly name of the active WiiM streamer. **Tap** opens the **Select WiiM Device** screen.
3. **Battery Fuel Gauge**: Dynamic battery icon. **Tap** opens the **Power & Display** screen.

### Tab Navigation (4 Tabs)
1. **Lyrics Tab**:
   - Synchronized / plain-text lyrics retrieved on demand from the LRCLIB REST API.
   - Automatically strips `[mm:ss.xx]` timing tags from synced LRC records if plain text is unavailable.
   - Distinct error feedback: differentiates between track not found (HTTP 404) and connection/server failures.
2. **Now Playing Tab (Default)**:
   - High-contrast track title rendered in extended UTF-8 **Montserrat 24** typography with circular scrolling for long titles.
   - Artist and album metadata.
   - Compact audio resolution telemetry (`bits/sample_rate_kHz`, e.g., `24/96`) in the transport row.
   - Interactive progress bar with **Ghost Seek Target Indicator** (shows requested seek position while streamer buffers to the target timestamp) and elapsed/total duration timestamps.
   - Centered transport controls: Previous, Play/Pause/Resume, Next.
   - Centered track counter (`X/Y`) directly beneath the Play/Pause button (auto-hidden when `0/0`).
   - **Unified Volume & Input Source Row**:
     - Persistent row containing the instant mute button, 120 px volume slider, percentage/`"Fixed"` indicator, and Input Source cycle button (`` Wi-Fi $\rightarrow$ `蓝牙` Bluetooth $\rightarrow$ `AUX` Line-In $\rightarrow$ `OPT` Optical).
     - **Variable Volume**: Interactive slider updating output volume in real-time with numeric percentage label (`0%`–`100%`).
     - **Fixed Volume**: Disables the volume slider, displays `"Fixed"` beneath the bar, and preserves mute toggle functionality.
3. **Presets Tab**:
   - Edge-to-edge scrollable list of **12 presets** dynamically synced from the WiiM device (`getPresetInfo`), with unassigned slots visually dimmed.
   - One-touch preset recall with automatic navigation back to the Player tab.
4. **UPnP / DLNA Media Browser Tab**:
   - Automatically discovers local UPnP `MediaServer:1` servers (Asset UPnP, MinimServer, Plex, Synology, etc.) via SSDP.
   - Navigates arbitrary folder hierarchies via UPnP `ContentDirectory:1` (`BrowseDirectChildren`) with automatic 128-item page navigation (`Previous Page` / `Next Page`), breadcrumb page memory, and a persistent 32px back/refresh header.
   - Tapping any track builds a native LinkPlay `<PlayList>` queue (`CreateQueue` & `PlayQueueWithIndex`) containing the full album/folder starting at the selected track—enabling gapless album playback, accurate `X/Y` track counters, Next/Previous track skipping, and true embedded `TITLE` metadata lookup via UPnP `BrowseMetadata`.

### Full-Screen Settings Views (32px Unified Header)
1. **Select WiiM Device** (Tap Streamer Name):
   - Lists all WiiM streamers discovered via NVS cache and SSDP broadcast.
   - Shows device name and IP address, with active streamer indicator.
   - Dedicated refresh button in the right header to trigger SSDP discovery on demand.
2. **Wi-Fi Networks** (Tap Wi-Fi Icon):
   - Non-blocking touchscreen network scanner displaying available SSIDs with RSSI-colored signal icons and a top `"Forget <SSID>"` action when connected.
   - Full on-screen QWERTY touch keyboard for password entry with automatic saved-password reconnect.
   - Live connection status spinner with automatic close on success.
3. **Power & Display** (Tap Battery Icon):
   - Live battery voltage readout (`V`) inside the 32px top header bar.
   - **Live 180° Orientation Selector (`Rotation: Standard (0°) / Flipped (180°)`)**: Flips both the ILI9341V display and FT6336G touch coordinates immediately and persists the orientation in NVS.
   - Configurable auto-dimming timeout (dims to ~10% brightness (`PWM 25/255`) on inactivity; first tap wakes display without triggering accidental UI actions).
   - Configurable auto-sleep timeout and manual **Deep Sleep Now** action.
   - **Live Color Theme Selector (`Theme: <Name>`)**: Cycles instantaneously across 6 RGB565-tuned hi-fi color palettes isolated in [`include/ui/themes_config.h`](include/ui/themes_config.h) (`Cyber Cyan`, `Tube Amber`, `Naim Emerald`, `McIntosh Blue`, `Velvet Rose`, `Studio Crimson`) and persists the selection in NVS.
   - ESP32-S3 deep sleep with capacitive touch-to-wake interrupt support (`FT6336G` monitor mode + `PA_EN` GPIO hold).

---

## 📡 LinkPlay, UPnP & External API Reference

### LinkPlay / WiiM HTTP & UPnP APIs
| Operation | Endpoint / Action | Description |
| :--- | :--- | :--- |
| **Device Identification** | `GET /httpapi.asp?command=getStatusEx` | Discovers device name, UUID, and MAC |
| **Playback Telemetry** | `GET /httpapi.asp?command=getPlayerStatus` | Polls play state, position, duration, mode, and volume |
| **Track Metadata** | `GET /httpapi.asp?command=getMetaInfo` | Retrieves title, artist, album, sample rate, and bit depth |
| **Presets List** | `GET /httpapi.asp?command=getPresetInfo` | Retrieves 12 preset names and mappings |
| **Pause Playback** | `GET /httpapi.asp?command=setPlayerCmd:pause` | Pauses active playback |
| **Resume / Start Playback** | `GET /httpapi.asp?command=setPlayerCmd:resume` / `play` | Resumes playback from pause or starts stopped queue |
| **Next Track** | `GET /httpapi.asp?command=setPlayerCmd:next` | Skips to next track in queue |
| **Previous Track** | `GET /httpapi.asp?command=setPlayerCmd:prev` | Skips to previous track |
| **Seek Position** | `POST AVTransport:1#Seek (REL_TIME)` | Seeks to target `HH:MM:SS` timestamp via UPnP AVTransport |
| **Set Volume** | `GET /httpapi.asp?command=setPlayerCmd:vol:<VAL>` | Adjusts volume (0..100) |
| **Set Mute** | `GET /httpapi.asp?command=setPlayerCmd:mute:<1\|0>` | Toggles mute state |
| **Input Source Switch** | `GET /httpapi.asp?command=setPlayerCmd:switchmode:<MODE>` | Switches input (`wifi`, `bluetooth`, `line-in`, `optical`) |
| **Trigger Preset** | `GET /httpapi.asp?command=MCUKeyShortClick:<INDEX>` | Recalls preset slot (1..12) |
| **UPnP Browse Folder** | `POST ContentDirectory:1#Browse (BrowseDirectChildren)` | Lists containers and audio items on a DLNA/UPnP MediaServer |
| **UPnP Track Metadata** | `POST ContentDirectory:1#Browse (BrowseMetadata)` | Resolves embedded track title/artist/album tags by `ObjectID` |
| **Load Album Queue** | `POST PlayQueue:1#CreateQueue` & `PlayQueueWithIndex` | Pushes multi-track `<PlayList>` XML queue and starts playback |

### External Lyrics API (LRCLIB)
| Operation | Endpoint | Description |
| :--- | :--- | :--- |
| **Fetch Lyrics** | `GET https://lrclib.net/api/get?track_name=...&artist_name=...` | Retrieves plain and synced LRC lyrics on demand |

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
  * **Full**: $\ge 3950\text{ mV}$ (`LV_SYMBOL_BATTERY_FULL`)
  * **High**: $\ge 3800\text{ mV}$ (`LV_SYMBOL_BATTERY_3`)
  * **Medium**: $\ge 3650\text{ mV}$ (`LV_SYMBOL_BATTERY_2`)
  * **Low**: $\ge 3500\text{ mV}$ (`LV_SYMBOL_BATTERY_1`)
  * **Empty**: $< 3500\text{ mV}$ (`LV_SYMBOL_BATTERY_EMPTY`)
  * **Critical Protection**: $\le 3350\text{ mV}$ (3 consecutive samples trigger automatic deep sleep to protect the Li-Po cell)

### 4. Reserved Onboard Pins (Isolated / Protected)
| Peripheral | Pin(s) | Description |
| :--- | :--- | :--- |
| **PA_EN** | **GPIO 1** | **Driven HIGH** at boot and held during deep sleep to keep onboard amplifier muted |
| **Codec I2S** | **GPIO 4, 5, 6, 7, 8** | High-impedance pulldown, no bus collision |
| **Status RGB** | **GPIO 42** | Status indicator LED |

---

## 📄 License
This project is licensed under the MIT License.
