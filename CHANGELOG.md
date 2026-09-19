# Changelog

All notable changes to the WiiM ESP32-S3 Touchscreen Remote project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
