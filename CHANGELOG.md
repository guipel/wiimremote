# Changelog

All notable changes to the WiiM ESP32-S3 Touchscreen Remote project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- **UPnP / DLNA Media Server Browser & Native LinkPlay Album Queue (`505394c`)**:
  - Added a 4th navigation tab (`Browser` / `LV_SYMBOL_DIRECTORY`) implemented in `include/ui/ui_browser.h`, `src/ui/ui_browser.cpp`, `include/net/upnp_client.h`, and `src/net/upnp_client.cpp`.
  - Extended SSDP discovery (`src/net/discovery_service.cpp`) to discover local UPnP `urn:schemas-upnp-org:device:MediaServer:1` servers (e.g., Asset UPnP, MinimServer, Plex, Synology) and extract their `ContentDirectory:1` control URLs.
  - Implemented recursive folder navigation (`BrowseDirectChildren`) with PSRAM-backed item lists (`UpnpItemList`) and a persistent 32px breadcrumb/back header with a dedicated server refresh button.
  - Implemented native LinkPlay `<PlayList>` queue construction (`PlayQueue:1#AppendTracksInQueueEx` on port `49152`) when tapping any track in a folder, loading the entire album/folder queue onto the WiiM streamer starting at the selected track index so `X/Y` track counters, gapless playback, and Next/Previous buttons work natively.
  - Added standard UPnP `BrowseMetadata` queries per track in `upnp_client_play_track()` so servers using `Folders & Filename Browsing` (such as Asset UPnP) still provide the true embedded `TITLE`, `ARTIST`, and `ALBUM` tags during playback on the WiiM.
- **Declarative 6-Theme Color System & Live 180° Display Rotation (`505394c`, `da59fd3`, `fc2034e`)**:
  - Created `include/ui/themes_config.h` isolating 6 RGB565-tuned hi-fi color palettes (`Cyber Cyan`, `Tube Amber`, `Naim Emerald`, `McIntosh Blue`, `Velvet Rose`, `Studio Crimson`) away from UI logic, calibrated for the 16-bit RGB565 TFT gamma curve (`da59fd3`).
  - Renamed the **Power & Battery** screen to **Power & Display** (`src/ui/modal_power.cpp`), added a **`Theme: <Name>`** row that cycles live across all 6 themes (`ui_apply_theme()`), and added a **`Rotation: Standard (0°) / Flipped (180°)`** row (`display_set_rotation()`) that flips both the ILI9341V display and FT6336G touch coordinates live and persists `theme_idx` and `rot_180` in NVS (`"wiimremote"`).
- **Ghost Seek Target Indicator (`505394c`)**:
  - Added a translucent seek target indicator (`obj_seek_target`) on the Player progress bar (`src/ui/ui_player.cpp`) that marks the requested seek position while the streamer buffers and automatically hides once `curpos` catches up within 3 seconds.
- **Input Source Cycling & Dynamic Source Icon**:
  - Repurposed `btn_var_input` on the Player volume row (`src/ui/ui_player.cpp`) to cycle through WiiM input modes (`wifi` &rarr; `bluetooth` &rarr; `line-in` &rarr; `optical`) via `setPlayerCmd:switchmode:<mode>` (`CMD_SWITCH_MODE`).
  - Added dynamic icon updates (`LV_SYMBOL_WIFI`, `LV_SYMBOL_BLUETOOTH`, `"AUX"`, `"OPT"`) synchronized with the active LinkPlay `mode` telemetry (`10/31/32` Wi-Fi, `41` Bluetooth, `40` Line-In, `43` Optical).
- **Version-Controlled Extended UTF-8 Montserrat Fonts (`10..24px`)**:
  - Updated `scripts/generate_fonts.py` and generated `ui_font_montserrat_{10,12,14,16,20,24}.c` in `src/ui/fonts/` covering Basic Latin, Latin-1 Supplement (`0x20–0xFF`), Smart Punctuation (`0x2010–0x2026`), and FontAwesome LVGL symbols.
  - Configured `include/lv_conf.h` to alias `lv_font_montserrat_*` to the extended UTF-8 fonts so accented characters on the 24px Now Playing song title render properly and persist across `pio run -t clean`.
- **Deep Sleep & Dimmed Mode Battery Optimizations**:
  - Configured the FT6336G capacitive touch controller to automatically enter low-power Monitor mode (`0x86 = 0x01`, `0x87 = 5s`) when idle and explicitly switch to Monitor mode (`0xA5 = 0x01`) before deep sleep while preserving instant `TOUCH_INT` (`GPIO 17`) wake.
  - Configured deep-sleep GPIO hold on `PIN_PA_EN` (`GPIO 1` HIGH) and `TFT_BL` (`GPIO 45` LOW), RTC pull-up on `TOUCH_INT`, and explicit `WiFi.mode(WIFI_OFF)` shutdown without issuing LCD `DISPOFF`/`SLPIN` commands that caused a VCOM border halo (`479d062`).
  - Implemented dynamic CPU frequency scaling (full `240 MHz` while awake, stepping down to `80 MHz` when dimmed) and adaptive HTTPS polling (`1000ms` playing, `2500ms` paused/stopped, `5000ms` dimmed).
- **Critical Low-Battery Protection & Task Watchdog Timer**:
  - Added a 3-sample consecutive `<= 3350 mV` (`BAT_VOLT_CRIT_MV`) cutoff in `src/ui/ui_core.cpp` that automatically enters deep sleep to protect the Li-Po cell.
  - Subscribed both `UiTask` (Core 1) and `NetTask` (Core 0) to the 15-second ESP32 Task Watchdog Timer (`esp_task_wdt`).
- **UI/UX Refinements**:
  - Swallowed the initial wake-from-dim touch gesture in `src/display_driver.cpp` until finger release so waking the screen does not trigger underlying buttons or sliders.
  - Added backdrop tap-to-close on all three modals (`modal_device.cpp`, `modal_wifi.cpp`, `modal_power.cpp`).
  - Visually dimmed unassigned preset tiles (`LV_OPA_50`) in `src/ui/ui_presets.cpp` and hid the `0/0` track counter when no playlist queue is active.
  - Replaced `(-XX dBm)` text suffixes in the Wi-Fi scan list with RSSI color-coded signal icons.

### Changed
- **UPnP SOAP Heap Elimination, Persistent `BrowseMetadata` Socket & UI Theme Deduplication (`21a59d9`)**:
  - Replaced `xml_escape()` in `src/net/upnp_client.cpp` with direct-append `append_xml_escaped(String& out, const char* s)` and pre-reserved SOAP `<PlayList>` buffer capacity, eliminating ~35 KB of peak temporary heap allocations in internal SRAM during album queue generation.
  - Passed a persistent `WiFiClient` to `metaHttp.begin(metaClient, ...)` in `upnp_client_play_track()` so `metaHttp.setReuse(true)` reuses a single TCP socket across `BrowseMetadata` queries, and added a 1-track early exit when `BrowseDirectChildren` already supplied real track titles.
  - Consolidated initial and live theme color assignments across all UI views (`ui_header`, `ui_player`, `ui_lyrics`, `ui_presets`, `ui_browser`, `modal_device`, `modal_wifi`, `modal_power`) into `*_apply_theme()` invoked at the end of `ui_init()`.
  - Added shared `ui_style_flat_list_btn()` in `include/ui/ui_theme.h` and moved `lock_alpha_map[120]` / `img_lock_dsc` into `src/ui/ui_core.cpp`, reducing UI code by 154 lines and saving ~4.2 KB of Flash.
- **Unified Full-Screen Settings Views & 32px Header Bars (`505394c`)**:
  - Redesigned the Device Selector (`src/ui/modal_device.cpp`), Wi-Fi Manager (`src/ui/modal_wifi.cpp`), Power & Display (`src/ui/modal_power.cpp`), Presets (`src/ui/ui_presets.cpp`), and UPnP Browser (`src/ui/ui_browser.cpp`) into edge-to-edge flat lists with a standardized `32px` top header bar matching the main status header (`src/ui/ui_header.cpp`).
  - Added a `"Forget <SSID>"` top row in the Wi-Fi list (`CMD_WIFI_FORGET`) and displayed `"Fixed"` beneath the disabled volume slider when a streamer is in fixed line-out mode.
  - Extended `CMD_PREV` (`src/net/linkplay_client.cpp`) to restart the current song (`seek:0`) if playback is past 5 seconds, or skip to the previous track otherwise.
- **Synchronous Core 0 Wi-Fi Scanning & Zero-Allocation SSDP Parsing (`fc2034e`)**:
  - Simplified `wifi_service_start_scan()` (`src/net/wifi_service.cpp`) to run synchronous `WiFi.scanNetworks(false, false, false, 300)` on Core 0 (`NetTask`), waiting on the ESP-IDF `WIFI_SCAN_DONE_BIT` event group instead of `WiFi.scanComplete()`'s premature `max_ms_per_chan * 20` timeout that failed during connected home-channel interleaved scans.
  - Replaced per-packet `String` heap allocations in `discovery_service.cpp` (`ssdp_listen_window`) with zero-allocation `strstr()` parsing and added a 350ms LAN connect timeout for fast-path NVS verification.
  - Throttled LVGL progress bar (`PROGRESS_BAR_MAX = 220`) and elapsed-time label redraws in `src/ui/ui_player.cpp` to only invalidate when visual values change.
  - Pinned exact library versions (`LovyanGFX @ 1.2.28`, `lvgl @ 8.4.0`, `ArduinoJson @ 7.4.3`) and enabled `-Wall -Wextra -Wno-unused-parameter` in `platformio.ini`.
- **Unified Player Volume Controls & Fixed Line-Out Behavior**:
  - Unified the Player screen volume layout across both Variable and Fixed line-out modes into a single persistent row: mute button, 120 px volume slider, percentage/status label, and input mode button.
  - Eliminated the separate fixed-mode padlock button (`btn_mute`, `lbl_mute`, `img_lock`) in `src/ui/ui_player.cpp` to eliminate jarring layout shifts when changing devices or output modes.
  - When in fixed volume mode, the volume slider is disabled (`LV_STATE_DISABLED`, touch interactions ignored) with dimmed indicator styling, and a padlock icon is displayed beneath the slider.
  - The mute button maintains consistent mute toggle behavior across both fixed and variable modes, turning solid crimson red (`#E63946`) when muted.

### Fixed
- **Accurate 5-Stage Battery Icon & LRCLIB `503` Retry (`fc2034e`)**:
  - Removed the voltage-based `LV_SYMBOL_CHARGE` heuristic (`BAT_VOLT_CHARGE_MV`) in `src/ui/ui_header.cpp` so the top header always displays the true 5-stage Li-Po battery capacity icon (`FULL` >= 3.95V down to `EMPTY` < 3.50V) without false charging indicators.
  - Added a 1.2s retry in `lyrics_client_fetch()` (`src/net/lyrics_client.cpp`) when `lrclib.net` returns `HTTP 503 Service Unavailable` (`retry-after: 1`), differentiated HTTP 5xx server overload messages from Wi-Fi connection errors, and stopped `ui_lyrics_on_track_changed()` / `linkplay_client_fetch_meta()` from re-triggering lyrics requests at 1 Hz on UPnP tracks.
- **Boot Brightness Persistence Override**:
  - Reordered `display_driver_init()` before `power_manager_init()` in `src/main.cpp` and removed the hardcoded `BL_DEFAULT_BRIGHT` (85%) override so NVS-saved brightness is applied on boot and wake from deep sleep.
- **Deep Sleep Immediate Re-Wake Race**:
  - Added a finger-release wait loop on `PIN_TOUCH_INT` (`GPIO 17`) in `power_manager_enter_deep_sleep()` so tapping the "Sleep" button does not immediately re-wake the ESP32-S3 via `EXT0` while the finger is still lifting.
- **On-Demand Lyrics Fetch & Single-Request LRCLIB Simplification**:
  - Passed the active track `title` and `artist` directly inside `CMD_FETCH_LYRICS` (`UiCommand::data::lyrics_req`) and synchronized `s_last_known_track_title` in `linkplay_client_fetch_meta()` so metadata from `getMetaInfo` and `getPlayerStatus` never desynchronize or get discarded as stale.
  - Simplified `lyrics_client_fetch()` in `src/net/lyrics_client.cpp` to a single flat `HTTPClient::GET()` request without multi-request retries or stripped-title fallbacks that previously triggered back-to-back TLS resets and overwrote HTTP 404 responses with connection errors (`479d062`).
- **Non-WiiM UPnP SSDP Probe Storm Elimination**:
  - Added an in-RAM ignored IP cache (`s_ignored_ips`) in `src/net/discovery_service.cpp` so non-WiiM UPnP devices broadcasting SSDP `NOTIFY` packets on the LAN are not repeatedly probed over TCP/TLS every 5 seconds (`479d062`).
- **Hex String Decoder False Positives**:
  - Hardened `net_decode_hex_string()` in `src/net/net_utils.cpp` with strict UTF-8 printability and numeric-only checks so plain ASCII artist/track names composed of hex characters (`"ABBA"`, `"ACDC"`, `"CAFE"`, `"1984"`) are never corrupted.
- **Fixed-Volume Lock Clobbering on SSDP Refresh**:
  - Preserved `is_fixed_volume` across `add_or_update_device()` discovery refreshes in `src/net/discovery_service.cpp` and avoided redundant NVS flash writes when the flag has not changed.
- **Volume Bar Bouncing on Device Selection**:
  - Eliminated volume bar bouncing (empty 0% &rarr; full 100% &rarr; adjusted level) when switching from a fixed-volume device to a variable-volume device.
  - Inverted query sequence in `network_manager` so `linkplay_client_fetch_config()` fetches `getStatusEx` hardware line-out configuration prior to dispatching active player status.
  - Zeroed memory in discovery queries (`query_device_status`) and persisted per-device `is_fixed_volume` flags in NVS.
  - Retained current volume slider appearance during device transition without clobbering to 0%, smoothly applying the verified volume upon arrival.
- **Smart Saved Wi-Fi Reconnect & Failure Input Fallback**:
  - Eliminated artificial password prompts when selecting a previously configured network from the Wi-Fi scan list.
  - `modal_wifi` now caches stored credentials and immediately attempts connection with the saved password when the user selects their existing network.
  - If connecting with the saved password fails, the UI automatically transitions to the manual password entry view (`"Failed: <SSID>"`) with the on-screen keyboard focused.
  - Removed destructive `s_wifi_prefs.remove()` calls in `wifi_service_init()` fallback to protect against transient NVS read glitches wiping stored credentials on boot.

---

## [1.4.0] - 2026-09-19

### Changed & Refactored
- **Modular Network Architecture Decomposition**:
  - Decomposed the monolithic 1,550-line `src/network_manager.cpp` into single-responsibility modular services under `include/net/` and `src/net/`:
    - `net_utils.h` / `net_utils.cpp`: Stateless utilities for UTF-8 transcoding, hex decoding, URL encoding, XML tag extraction, and LRC timestamp stripping (`fad6bd9`).
    - `wifi_service.h` / `wifi_service.cpp`: Wi-Fi station lifecycle, non-blocking reconnect loop, in-RAM credential retention, synchronous scanning on Core 0, and UI event dispatching (`fad6bd9`).
    - `discovery_service.h` / `discovery_service.cpp`: SSDP M-SEARCH broadcast over UDP 1900, packet deduplication, two-tier streamer verification (HTTP port 49152 `description.xml` & HTTPS port 443 `getStatusEx`), thread-safe `DeviceList` storage (portMUX protected), and active device selection (`fad6bd9`).
    - `linkplay_client.h` / `linkplay_client.cpp`: Persistent TLS socket session, status polling (`getPlayerStatus`), buffering state mapping (`PLAY_STATE_BUFFERING`), signed negative `curpos` DAC countdown handling, metadata parsing, preset retrieval, and UPnP SOAP transport control (`fad6bd9`).
    - `lyrics_client.h` / `lyrics_client.cpp`: On-demand LRCLIB REST client, track title sanitation fallbacks, and error status differentiation (`fad6bd9`).
  - Reduced `src/network_manager.cpp` from 1,550 lines to 264 lines, focusing purely on the Core 0 task loop orchestration, `xQueueUiCmd` router, volume command throttling, and periodic polling timers (`fad6bd9`).

### Fixed
- **Wi-Fi Connected Network Banner Flicker**:
  - Eliminated artificial pre-scan disconnect (`WiFi.disconnect(false, false)`) from `wifi_service_start_scan()`. The ESP32-S3 radio now performs synchronous channel sweeps on Core 0 while remaining associated to the AP, preserving active TLS sockets and preventing the connected network banner (`✔ <SSID>`) from disappearing during network scans (`b235e0f`).

---

## [1.3.0] - 2026-09-19

### Added
- **Transient Buffering State (`PLAY_STATE_BUFFERING`)**:
  - Added `PLAY_STATE_BUFFERING` to `include/model.h` to cleanly represent LinkPlay transient states (`"load"`, `"none"` during track transitions, and negative `curpos` DAC pre-buffer countdowns) (`6dae3c3`).
- **Dedicated Lyrics Error Differentiation**:
  - Enhanced `NetworkManager::fetchLyrics()` to capture HTTP response codes, distinguishing between missing lyrics (HTTP 404 &rarr; `"No lyrics found for this track."`) and connection or server failures (HTTP < 0 or >= 500 &rarr; `"Failed to retrieve lyrics.\nPlease check your connection."`) (`322fd23`).
- **Montserrat 24 Typography**:
  - Enabled `LV_FONT_MONTSERRAT_24` in `include/lv_conf.h` and applied it to the Now Playing song title with circular scrolling width calculation (`66650a7`).

### Changed & Refactored
- **Modular UI Architecture Decomposition**:
  - Decomposed the monolithic 2,332-line `src/ui.cpp` into discrete, single-responsibility components under `include/ui/` and `src/ui/`:
    - `ui_theme.h`: Design tokens, colors, padlock bitmap asset, and `format_time()` helper (`6dae3c3`).
    - `ui_header.h` & `ui_header.cpp`: Top persistent status bar (Wi-Fi RSSI, active device label, battery gauge) (`6dae3c3`).
    - `ui_player.h` & `ui_player.cpp`: Now Playing interface, transport controls, seek slider, adaptive volume modes, unified playback coordinator (`6dae3c3`).
    - `ui_lyrics.h` & `ui_lyrics.cpp`: LRCLIB lyrics view container and on-demand activation (`6dae3c3`).
    - `ui_presets.h` & `ui_presets.cpp`: 12-preset button grid with auto-navigation to Player (`6dae3c3`).
    - `modal_device.h` & `modal_device.cpp`: Streamer discovery and selection modal (`6dae3c3`).
    - `modal_wifi.h` & `modal_wifi.cpp`: Wi-Fi network scanner, password input, and on-screen keyboard (`6dae3c3`).
    - `modal_power.h` & `modal_power.cpp`: Battery voltage, brightness slider, auto-dim, and sleep modal (`6dae3c3`).
    - `ui_core.cpp`: Public API entry points (`ui_init()`, `ui_process_events()`), event queue routing, progress and battery timers (`6dae3c3`).
- **Unified Playback Coordinator**:
  - Established centralized state synchronization in `ui_player.cpp` to eliminate widget flapping: holds Play/Pause button symbol without toggling during buffering, freezes progress counter, and prevents progress bar jumping across track transitions (`6dae3c3`).
- **Player Layout & Visual Refinements**:
  - Removed cluttered '✕' overlay from both fixed and variable mute buttons; mute status is cleanly indicated by solid crimson red (`#E63946`) background transition with white icons (`e846f30`).
  - Relocated audio resolution/bitrate label to center horizontally between the progress bar and transport controls on the same telemetry row as elapsed and total times (`e846f30`, `66650a7`).
  - Relocated track counter from bottom-right corner to center horizontally beneath the Play/Pause button (`66650a7`).
  - Vertically balanced layout: transport controls at `Y = 122`, track counter at `Y = 184`, and volume controls at `Y = 197` (`b479b36`, `a6c131f`).
  - Formatted volume percentage label with `COLOR_TEXT_MUTED`, harmonizing it with the track counter (`a6c131f`).
  - Moved active tab cyan accent line on the bottom navigation bar from the top of the tab to the bottom edge (`LV_BORDER_SIDE_BOTTOM`) (`a6c131f`).
- **Documentation Overhaul**:
  - Brought `README.md` fully up to date with the v1.3.0 release, documenting hardware telemetry (GPIO 9 battery ADC, touch-to-wake interrupt), 3-tab and 3-modal navigation hierarchy, modular UI decomposition, and API endpoints.

### Fixed
- **Mute & Input Button Touch Interception**:
  - Eliminated touch hitbox overlap where tapping the center or right half of the mute button hit the volume slider at 0% instead of muting (`d35559a`).
  - Reduced slider extended click area from 20 px to 6 px, resized slider width to 120 px (8 px safe gap), and brought mute and input buttons to foreground in Z-order for 100% surface touch priority (`d35559a`).

---

## [1.2.0] - 2026-09-18

### Added
- **Synced Lyrics Fallback**: Added regular expression parsing to automatically strip `[mm:ss.xx]` timing tags from synced LRC lyrics fetched from LRCLIB, providing full lyrics display when plain text variants are unavailable (`9f2a72a`).
- **On-Demand Lyrics Fetching**: Deferred lyrics network requests until the user actually navigates to the Lyrics tab, reducing background network traffic (`83cff2f`).
- **State-Aware Transport Commands**: Integrated intelligent transport dispatching so tapping Play while paused sends `setPlayerCmd:resume` rather than restarting the song from `00:00` (`7a132e7`, `50d8967`).

### Fixed
- **Progress Bar Bounce Elimination (`0% -> 100% -> 0%`)**:
  - Eliminated intermediate desynchronized UPnP player state dispatches inside `NetworkManager::fetchUpnpTrackDuration()` that were pairing new song durations with old playback positions (`4ae58a4`).
  - Reordered `NetworkManager::pollActiveDevice()` into a forward-flowing pipeline: track identity and duration cache invalidation now run strictly *before* duration and position calculations (`4ae58a4`).
  - Corrected signed integer parsing (`strtol`) for LinkPlay `curpos`. LinkPlay sends negative pre-buffer countdown values (`-710ms` &rarr; `0ms`) while the hardware DAC fills its audio ring buffer; parsing with `strtoul` previously underflowed to 4.29 billion and triggered the safety clamp, locking the bar at 100% until playback reached timestamp 0 (`4ae58a4`).
- **Current Time Drift Elimination (`00:00 -> 00:01 -> 00:00`)**:
  - Set `current_play_state = PLAY_STATE_UNKNOWN` on transport button taps (`event_btn_next`, `event_btn_prev`) and during negative pre-buffer countdowns (`rawCurpos < 0`). This suspends the local 50ms FreeRTOS progress timer at `00:00` without making assumptions about upcoming player state, preventing premature timer drift before sound begins (`4ae58a4`).
- **Wake & Boot Visual Flicker**:
  - Initialized track title and artist labels as empty strings rather than placeholder text to prevent visual flicker on boot or wake from deep sleep (`9f2a72a`).
  - Immediately broadcast known NVS-stored devices to the UI upon boot instead of waiting for SSDP discovery to complete (`9f2a72a`).
- **Stopped Streamer State Rendering**:
  - Updated parser to accurately render playback position and duration for stopped media without clearing or corrupting the view (`30d0554`).
  - Handled LinkPlay `none` state gracefully, preventing ghost tracks from lingering on screen when streamers are idle (`b709759`).
- **In-RAM Wi-Fi Reconnection**:
  - Replaced flash-wearing NVS calls during automatic periodic Wi-Fi reconnect attempts with in-RAM credential retention (`9f2a72a`).

### Changed & Refactored
- **Display Driver DMA Parallelization**:
  - Enabled asynchronous non-blocking SPI DMA double buffering in LovyanGFX (`display_flush_cb`), permitting the CPU to render the next frame while the SPI controller transmits pixel data in parallel (`8926092`).
- **Capacitive Touch Scan Rate**:
  - Increased touch polling frequency from 33 Hz (30ms period) to 66 Hz (15ms period) for lower latency and smoother volume and seek slider response (`8926092`).
- **Code Hygiene**:
  - Purged periodic 60s FreeRTOS task stack high-water-mark diagnostic logging to reduce serial bus contention (`8926092`).
  - Removed dead `current_vendor` variable and obsolete defensive `strcmp` checks (`6a78b42`, `9f2a72a`).

---

## [1.1.0] - 2026-09-15

### Added
- **Power Management Subsystem (`power_manager.cpp`, `power_manager.h`)**:
  - Implemented ESP32-S3 deep sleep with touch-to-wake interrupt support on the capacitive touchscreen (`2dfc8aa`).
  - Added power management configuration modal accessible via UI header (`e401a23`).
  - Added persistent screen auto-dimming configuration with a fixed 10% auto-dim brightness level (`98bbd44`, `e401a23`).
  - Added manual screen brightness slider in the power modal styled to match the volume bar (`20abb97`, `98bbd44`).
  - Added battery fuel gauge with live voltage measurement via ADC voltage divider (`99200db`).
- **Clean Sleep Wi-Fi Disconnect**:
  - Added graceful Wi-Fi teardown before entering deep sleep to prevent router connection hangs (`d13c3e2`).

### Fixed
- **Device Switching View Reset**:
  - Immediately cleared track view, progress, and metadata when switching active devices, preventing stale info from displaying when selecting standby streamers (`fe8703b`).
- **Sleep Button Event Handler**:
  - Switched sleep button event trigger to `LV_EVENT_RELEASED` to eliminate ghost clicks (`79c3c31`).

### Changed
- **Project Development Directives**:
  - Formally codified non-negotiable project architecture and pair-programming rules in `AGENTS.md` (`bba2d32`).
- **Performance Optimization**:
  - Optimized network polling intervals, UI responsiveness, and overall memory footprint (`2e95504`).

---

## [1.0.0] - 2026-09-14

### Added
- **Three-Tab User Interface Layout**:
  - Left Tab: Scrollable track lyrics integrated with the LRCLIB API (`e0c6f3a`).
  - Center Tab: Primary player interface with album art, track info, transport controls, and progress bar (`e0c6f3a`).
  - Right Tab: 12-slot WiiM preset list with one-tap preset recall (`e0c6f3a`).
- **Aesthetic Transport & Volume Sliders**:
  - Redesigned volume slider to a 6px thin bar with 3px border radius, flush knobless aesthetic matching the track progress bar (`73492bd`, `fac1acd`).
  - Centered volume slider vertically with mute and input selector buttons (`6d129ff`).
  - Positioned volume percentage label directly beneath the slider (`171dba9`).

---

## [0.9.0] - 2026-09-13

### Added
- **Dynamic Fixed vs. Variable Volume Detection**:
  - Added runtime detection of WiiM fixed-volume output mode (displaying lock icon and dedicated mute button) versus variable volume mode (displaying interactive volume slider) (`ec08d44`, `1ca7290`).
- **On-Device Wi-Fi Configuration Modal**:
  - Integrated interactive Wi-Fi scanning and configuration modal with persistent NVS storage (`1cfc970`).
  - Added "Forget Network" action banner and forced setup on first boot or missing credentials (`5a6e917`).
  - Added clean channel scanning with radio settling and automatic reconnection on modal close (`56c63aa`).

### Fixed
- **Volume String Parsing**:
  - Fixed volume string-to-integer conversion from `getPlayerStatus` and enhanced touch dragging responsiveness (`6f46079`).

---

## [0.1.0] - 2026-09-13

### Added
- **Initial Release (`3f82e5a`)**:
  - Core ESP32-S3 firmware with LovyanGFX display driver and LVGL v8 graphics library.
  - LinkPlay HTTP/UPnP API integration for device control and discovery.
  - SSDP M-SEARCH discovery for automatic detection of WiiM devices on the local subnet.
  - PlatformIO build configuration for `esp32-s3-devkitc-1`.
