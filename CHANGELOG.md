# Changelog

All notable changes to the WiiM ESP32-S3 Touchscreen Remote project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
