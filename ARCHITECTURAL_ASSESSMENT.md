# WiiM ESP32-S3 Touchscreen Remote: Architectural Assessment & Production Roadmap

**Target Hardware:** ESP32-S3 (N16R8: 16MB Flash, 8MB Octal PSRAM)  
**Display & Touch:** 2.8" ILI9341V IPS SPI (240x320) | FT6336G Capacitive Touch (I2C)  
**Target Device:** WiiM / LinkPlay Hi-Fi Audio Streamers  
**Date:** September 13, 2026  

---

## 1. Executive Summary: Root Causes of Lag

The remote currently feels sluggish due to three distinct layers of latency:

```
[Touch Event] 
      │
      ▼
┌─────────────────────────────────────────────────────────────┐
│ 1. Core 0 Network Transport (~350ms - 500ms)               │
│    • Full TCP SYN/ACK + TLS 1.2 handshake on EVERY request  │
│    • Commands wait behind 1-second status polling loop      │
└─────────────────────────────────────────────────────────────┘
      │
      ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. WiiM Audio Engine Transition (~100ms - 200ms)            │
│    • Linux ALSA / playback daemon buffer drain & state sync │
│    • Polling immediately (<15ms) returns stale state        │
└─────────────────────────────────────────────────────────────┘
      │
      ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. Core 1 Display & Rendering (~25ms - 40ms)                │
│    • Draw buffers in high-latency external PSRAM            │
│    • Synchronous CPU-blocking SPI write (no hardware DMA)   │
│    • Unconditional 16ms sleep in UI loop (vTaskDelay(16))   │
└─────────────────────────────────────────────────────────────┘
      │
      ▼
[Screen Updates / Button Changes]
Total turnaround: 500ms - 800ms
```

---

## 2. In-Depth Technical Breakdown

### A. Network Layer (Core 0)
1. **Per-Request TLS Teardown (Highest Latency):**
   - In `src/network_manager.cpp`, functions `pollActiveDevice()`, `sendHttpCommand()`, and `fetchTrackMeta()` each instantiate a fresh `WiFiClientSecure` stack.
   - Each HTTP request performs a new TCP 3-way handshake and an mbedTLS handshake (ephemeral ECDHE key agreement, cipher selection).
   - This consumes **250ms to 400ms of CPU time per request**.
   - **Solution:** Enable **HTTP Keep-Alive** (`http.setReuse(true)` or a persistent `WiFiClientSecure` session). Subsequent polls and commands drop to **12ms – 25ms** (a 20x speedup).

2. **Command Serialization Behind Polling:**
   - In `runTaskLoop()`, `processIncomingCommands()` runs sequentially with `pollActiveDevice()`.
   - If the user presses a button while the 1-second status poll is executing, the command must wait for the poll to finish before being dispatched.
   - **Solution:** Process and dispatch queued commands immediately before initiating background polling.

3. **Synchronous UPnP SOAP Blocking:**
   - `fetchUpnpTrackDuration()` uses a 1500ms blocking timeout inside the polling sequence. If the streamer delays the SOAP response, Core 0 stalls.
   - **Solution:** Execute track duration queries asynchronously or only upon track title changes.

---

### B. Display & Rendering Pipeline (Core 1)
1. **Draw Buffers in External PSRAM:**
   - In `src/display_driver.cpp`, `disp_draw_buf1` and `disp_draw_buf2` are allocated via `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`.
   - Octal PSRAM has significantly higher read/write latency than on-chip SRAM.
   - Each buffer is 240x40 pixels (19.2 KB). Both buffers total **38.4 KB**.
   - The ESP32-S3 has over 150 KB of free on-chip SRAM.
   - **Solution:** Move both draw buffers to **Internal SRAM with DMA capability** (`MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`).

2. **Synchronous SPI Flushing (No Hardware DMA):**
   - `display_flush_cb` currently calls `gfx.writePixels()`, a blocking CPU loop.
   - While the CPU pumps bytes over SPI, LVGL rendering is completely halted.
   - True double-buffering is ineffective because LVGL cannot render tile $N+1$ while tile $N$ is being transferred.
   - **Solution:** Use LovyanGFX asynchronous DMA (`gfx.pushImageDMA()`). The SPI peripheral transfers buffer 1 in hardware while Core 1 computes and renders buffer 2 in parallel.

3. **Fixed Frame Delay in UI Task:**
   - In `src/main.cpp`, `vTaskDelay(pdMS_TO_TICKS(16))` is executed unconditionally every loop.
   - If rendering takes 10ms, the loop time becomes 26ms (~38 FPS).
   - Any touch input occurring during that 16ms sleep sits waiting.
   - **Solution:** Dynamically scale task delay: `uint32_t delay = lv_timer_handler(); vTaskDelay(pdMS_TO_TICKS(constrain(delay, 4, 16)));`.

---

### C. Touch & Interaction Design
1. **HTTP Transfer (15ms) vs Streamer State Latency (150ms):**
   - When the remote sends `setPlayerCmd:onepause`, the HTTP call completes in 15ms.
   - However, the WiiM itself takes 100ms–200ms to stop ALSA audio buffers and flip its internal status daemon from `"play"` to `"pause"`.
   - If we poll immediately, the WiiM still reports `"play"`.
2. **Optimistic UI Updates:**
   - In modern touch remotes (Spotify, Apple TV, Sonos), touching "Pause" changes the icon immediately (0ms visual lag), while the network round-trip completes quietly in the background.
   - **Strategy:** Once Persistent TLS is implemented, network turnaround drops from ~700ms to ~150ms. We will test the physical device first—if ~150ms feels fast enough, we can keep the UI logic simple and skip optimistic state rollbacks.

---

## 3. Production Readiness Gaps

| Area | Current Implementation | Production Risk | Recommended Fix |
| :--- | :--- | :--- | :--- |
| **Power Management** | Backlight ON 100% at 220 brightness. No sleep/dim. | Remote consumes ~180mA continuously; causes heat and battery drain. | Add inactivity manager: auto-dim to 15% after 30s, screen off after 2m. Wake on capacitive touch interrupt. |
| **Dynamic Discovery** | Hardcoded candidate IPs (`192.168.50.x`). | Fails if used on other networks or if DHCP reassigns streamer IPs. | Implement NVS streamer cache (remember last 5 streamers) + SSDP multicast + mDNS. |
| **Adaptive Volume** | Only Mute button exists; volume slider removed. | Streamers with variable volume (amplifiers/speakers) cannot have volume adjusted. | Dynamically show volume slider when `!is_fixed_volume`, and compact mute/lock badge when `is_fixed_volume`. |
| **Build Optimization** | `CORE_DEBUG_LEVEL=3` in `platformio.ini`. | `ESP_LOGI` executes across ESP-IDF libraries, wasting CPU cycles and flash space. | Set `CORE_DEBUG_LEVEL=0` (or `1` for errors) in production builds. |

---

## 4. Implementation Roadmap for Tomorrow

### Phase 1: High-Impact Speed & Responsiveness (First Priority)
1. **Persistent TLS Session (HTTP Keep-Alive):**
   - Reuse the `WiFiClientSecure` connection to the active WiiM streamer.
   - Avoid teardown and re-handshake on every 1-second poll and command.
   - Target: Command latency drops from ~400ms to **<20ms**.
2. **Internal SRAM DMA Display Driver:**
   - Reallocate the two LVGL 240x40 draw buffers to `MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`.
   - Switch `display_flush_cb` to asynchronous DMA (`gfx.pushImageDMA()`).
   - Target: Render time cut in half; 60 FPS animation smoothness.
3. **Preemptive Command Dispatch:**
   - Process incoming UI queue commands before starting the 1-second polling routine.

### Phase 2: Playback Polish & Smoothness
1. **Client-Side Progress Interpolation:**
   - Advance the song timer and timeline slider smoothly every 100ms on Core 1 during playback, resyncing on the 1-second network tick.
2. **Dynamic UI Task Scheduling:**
   - Replace fixed 16ms sleep with dynamic load-aware yielding.
3. **Adaptive Volume UI:**
   - Dynamically display the volume slider if the selected streamer has variable volume, or the compact mute badge if fixed volume.

### Phase 3: Production Hardening & Power Efficiency
1. **Inactivity Power Controller:**
   - Auto-dim to 15% brightness after 30s of no touch.
   - Screen sleep after 2 minutes; instant wake-on-touch via FT6336G interrupt pin.
2. **NVS Streamer Cache:**
   - Save last discovered streamers to non-volatile storage to eliminate hardcoded IP dependencies.
3. **Release Build Tuning:**
   - Set `CORE_DEBUG_LEVEL=0` and verify zero memory fragmentation.

---

## 5. Settings Screen & On-Device Configuration

A production remote cannot rely on hardcoded Wi-Fi credentials in `config.h` or fixed accent colors.

### A. Settings UI Architecture
- **Tab Navigation**: Add a 3rd tab or dedicated gear button in the top header: `[ Player ] [ Presets ] [ Settings ]`.
- **Theme Color Palette Customizer**:
  - Palette selector with instant visual preview pills:
    - **Electric Cyan** (`#00E5FF`) [Default]
    - **Amber / Warm Gold** (`#FFB300`)
    - **Emerald Green** (`#10B981`)
    - **Purple / Neon Violet** (`#A855F7`)
    - **Crimson Red** (`#EF4444`)
    - **Minimalist White** (`#FFFFFF`)
  - Stored in NVS via `Preferences` (`theme_color`).
  - Dynamic style update: Updates the progress bar indicator, active tab borders, slider knobs, preset badges, and highlight colors without rebooting.

### B. Wi-Fi Configuration Manager
1. **On-Screen Wi-Fi Scanner**:
   - `WiFi.scanNetworks()` populates a scrollable list inside the Settings tab showing SSID names, security type (WPA2/WPA3), and signal RSSI strength.
2. **Integrated On-Screen Keyboard**:
   - Tapping a network brings up an LVGL virtual keyboard (`lv_keyboard_create`) and a masked text input box (`lv_textarea_create`) to enter the WPA passphrase.
3. **Persistent Credential Storage**:
   - Save active `ssid` and `password` to NVS.
   - If Wi-Fi fails to connect after 15 seconds, trigger a fallback "Setup Hotspot" (SoftAP `WiiM-Remote-Setup`) serving a lightweight HTML captive portal.
4. **Network Diagnostics Display**:
   - Display real-time connection telemetry: SSID, IP Address, Subnet Mask, Gateway, BSSID, RSSI (dBm), and Ping latency to the active WiiM streamer.

---

## 6. Wireless Energy & Battery Architecture

To transition this remote into a true untethered wireless device running on a LiPo or 18650 cell:

### A. Power Consumption Analysis & Budget
| Operating State | Active Subsystems | Current Draw @ 3.7V | Battery Life (1500mAh LiPo) |
| :--- | :--- | :--- | :--- |
| **Active Screen (100%)** | 240MHz ESP32-S3 + Wi-Fi TX/RX + 100% Backlight | ~190mA – 230mA | **6.5 – 7.5 hours** |
| **Active Screen (15% Dim)** | Backlight dimmed to low brightness | ~110mA – 130mA | **11 – 13 hours** |
| **Light Sleep (Screen Off)** | Backlight OFF, LCD in SLPIN, Wi-Fi in DTIM Modem Sleep | ~12mA – 18mA | **3.5 – 5 days** |
| **Deep Sleep (Standby)** | ULP / RTC only, Wi-Fi OFF, Touch INT armed | ~15µA – 25µA | **> 6 months** |

### B. Hardware Battery Fuel Gauge
- **ADC Voltage Divider**:
  - Connect battery $V_{bat}$ through a $2 \times 100\text{k}\Omega$ high-precision resistor divider to an unused ADC1 pin (e.g. GPIO 2 or GPIO 3).
  - Software calibration: Map 3.4V (0%) to 4.2V (100%) LiPo discharge curve.
- **Header Status Icon**:
  - Render an adaptive battery indicator in the top header:
    - $\ge 80\%$: `LV_SYMBOL_BATTERY_FULL` (Green)
    - $50\% - 79\%$: `LV_SYMBOL_BATTERY_3` (Green)
    - $20\% - 49\%$: `LV_SYMBOL_BATTERY_2` (Amber)
    - $< 20\%$: `LV_SYMBOL_BATTERY_1` (Red flash)
  - Critical voltage protection: Force graceful shutdown / deep sleep below 3.3V to prevent LiPo degradation.

### C. Multi-Tier Sleep & Wake-on-Touch
1. **Tier 1 (Auto-Dim)**: After 30s of inactivity, smoothly ramp backlight PWM down to 15%.
2. **Tier 2 (Display Sleep / Light Sleep)**: After 2 minutes of inactivity:
   - Turn off backlight PWM (0mA).
   - Send `0x10` (`SLPIN` Sleep In) command to the ILI9341 display controller.
   - Put ESP32-S3 into automatic light sleep with Wi-Fi modem sleep maintained.
3. **Tier 3 (Capacitive Touch Wakeup)**:
   - The FT6336G capacitive touch controller features a dedicated active-LOW interrupt pin: `TOUCH_INT` (GPIO 17).
   - Arm GPIO 17 as an external wake-up source (`gpio_wakeup_enable(GPIO_NUM_17, GPIO_INTR_LOW_LEVEL)`).
   - **User Experience**: Tapping anywhere on the dark glass immediately wakes the ESP32-S3, wakes the LCD controller (`SLPOUT`), and restores full brightness in **< 40ms**.

---

## 7. Interface Evolution, Background Art & New Features

### A. Re-Introducing Album Art Backgrounds (The Correct Architecture)
The previous attempt caused stutter because JPEG decoding ran synchronously and UART debug statements choked the serial bus. The production approach:
1. **Dedicated Low-Priority Worker Task (`ArtFetchTask`) on Core 0**:
   - Downloading and decoding artwork must **never** run on Core 1 (UI thread) and must **never** block the main status polling loop.
2. **Hardware-Accelerated SIMD JPEG Decoding**:
   - Use Espressif's official `esp_jpeg` library (optimized for ESP32-S3 vector instructions) decoding directly into an 8-bit or 16-bit PSRAM image canvas.
3. **Downscale & Auto-Darken in Memory**:
   - Downscale the cover art to $240 \times 320$ during decode.
   - Apply a 65% dark scrim/tint in memory so that white track titles, artists, and UI buttons maintain 100% crisp readability.
4. **Asynchronous LVGL Fade-In**:
   - When the image buffer is ready in PSRAM, post an event to Core 1 to update `lv_img_set_src()` with a smooth 200ms crossfade. If art fetch fails or times out, seamlessly keep the sleek minimalist dark charcoal theme.

### B. High-Value New WiiM Features
1. **Audio Input Source Selector**:
   - WiiM devices support switching audio inputs via LinkPlay HTTP API:
     - `setPlayerCmd:switchmode:wifi` (Wi-Fi Streaming / AirPlay / Spotify Connect)
     - `setPlayerCmd:switchmode:line-in` (Analog Aux / Line In)
     - `setPlayerCmd:switchmode:optical` (Optical SPDIF)
     - `setPlayerCmd:switchmode:bluetooth` (Bluetooth RX)
     - `setPlayerCmd:switchmode:coaxial` / `usb` / `hdmi` (WiiM Pro Plus / Ultra)
   - Add a sleek source badge in the top header with a drop-down selector.
2. **Multi-Room Grouping & Sync**:
   - WiiM LinkPlay API allows master-slave multi-room pairing:
     - `multiroom:join:slave_ip` and `multiroom:leave`
   - In the Device Selector modal, allow toggling "Party Mode / Group" to link multiple WiiM streamers in perfect sync.
3. **EQ Preset Selector**:
   - Query and toggle WiiM onboard hardware EQ profiles:
     - `GET /httpapi.asp?command=getEQ` & `setPlayerCmd:EQ:{id}`
     - Flat, Rock, Jazz, Classical, Bass Boost, Custom 10-Band EQ.
4. **Preset Long-Press Quick-Save**:
   - In the Presets tab, detect `LV_EVENT_LONG_PRESSED` on any preset card:
   - Prompts a confirmation dialog: *"Save current stream to Preset X?"*.
   - Dispatches `setPlayerCmd:savePreset:{x}`, allowing users to bookmark live radio stations directly from the remote without opening their phone.
5. **Sleep Timer**:
   - Dedicated sleep timer button (15m, 30m, 45m, 60m) triggering WiiM's internal sleep daemon (`setSleepTimer:{minutes}`).
6. **Liked Songs & Add to Playlist ("Heart" Button)**:
   - **Now Playing Heart Icon**: A dedicated heart button (`LV_SYMBOL_HEART` or custom vector icon) placed cleanly adjacent to the track title or within the transport control row.
   - **Like State Synchronization**: Inspects the `like` / `favorite` flag returned in `getPlayerStatus` and `getMetaInfo` to illuminate the heart in vibrant crimson red (`#EF4444`) when the current song is liked, or subtle muted gray outline when unliked.
   - **One-Tap Like/Unlike**: Tapping the heart dispatches `setPlayerCmd:favorite` / `setPlayerCmd:addtofav` (or `setPlayerCmd:delfav`), immediately saving the track to your streaming provider (Spotify, Tidal, Amazon Music, Qobuz) or WiiM Favorites list.
   - **Long-Press "Add to Playlist" Drawer**: Long-pressing the heart button opens a modal drawer querying and listing the user's custom WiiM playlists, allowing one-tap addition of the currently playing song directly from the remote without opening a smartphone.

