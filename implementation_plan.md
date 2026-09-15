# Implementation Plan — WiiM Remote Optimization

Apply all actionable findings from the deep evaluation. Grouped by component, ordered by dependency.

## User Review Required

> [!IMPORTANT]
> **1.3 — NVS Device Persistence**: The plan persists up to `MAX_DISCOVERED_DEVICES` (12) device IPs/UUIDs/names to NVS using indexed keys (`dev_ip_0`, `dev_uuid_0`, `dev_name_0`, ..., `dev_count`). On boot, these are probed first before SSDP. Is this the behavior you want, or would you prefer a simpler approach (e.g., persist only the last 4 successfully-connected devices)?

> [!IMPORTANT]
> **5.1 — LVGL Memory to PSRAM**: Moving LVGL's allocator to PSRAM frees ~48 KB of internal DRAM but adds a small latency penalty (~10-20ns per allocation vs internal SRAM). On ESP32-S3 with Octal PSRAM this is negligible for object management, but I want to confirm you're comfortable with this tradeoff.

> [!IMPORTANT]
> **6.1 — Debug Level**: Reducing `CORE_DEBUG_LEVEL` to `1` (errors only) removes all `log_i()` and `log_w()` output from Serial. This saves CPU but you lose runtime visibility. Would you prefer level `1` (errors only) or level `2` (errors + warnings)?

## Open Questions

- **Draw buffer increase (3.3)**: The plan increases from 40 rows to 80 rows (19.2 KB → 38.4 KB per buffer, 76.8 KB total DMA). This should fit in internal SRAM, but if allocation fails, it falls back to PSRAM (which you already handle). Should I add a boot-time log of available DMA memory to verify headroom?

---

## Proposed Changes

### Component 1: Dead Code Removal

---

#### [MODIFY] [network_manager.h](file:///c:/Users/guipe/OneDrive/Projects/wiimremote/include/network_manager.h)

Remove dead subnet scanner method and state variables:

```diff
-    // Subnet Scanner Fallback
-    void runSubnetScanStep();
-
     // LinkPlay HTTP API (Persistent TLS Keep-Alive)
```

```diff
-    // Subnet scan state
-    bool _isScanning;
-    int _scanCurrentHost;
-    unsigned long _lastScanStepTime;
-
     // Active device
```

Add NVS device persistence methods and config poll interval constant:

```diff
     void loadSavedDevice();
     void saveActiveDevice(const WiiMDevice& dev);
+    void loadSavedDeviceList();
+    void saveSavedDeviceList();
```

---

#### [MODIFY] [network_manager.cpp](file:///c:/Users/guipe/OneDrive/Projects/wiimremote/src/network_manager.cpp)

**1.1 — Remove `runSubnetScanStep()`** (lines 602-606) and its call in `runTaskLoop()` (line 1385).

**1.3 — Replace hardcoded IPs with NVS-persisted device list:**

Remove `known_candidate_ips[]` array (lines 468-473).

Add two new methods:

```cpp
void NetworkManager::loadSavedDeviceList() {
    uint8_t count = prefs.getUChar("dev_count", 0);
    if (count > MAX_DISCOVERED_DEVICES) count = MAX_DISCOVERED_DEVICES;

    for (uint8_t i = 0; i < count; ++i) {
        char keyIp[12], keyUuid[14], keyName[14];
        snprintf(keyIp, sizeof(keyIp), "dev_ip_%u", i);
        snprintf(keyUuid, sizeof(keyUuid), "dev_uuid_%u", i);
        snprintf(keyName, sizeof(keyName), "dev_name_%u", i);

        String ip = prefs.getString(keyIp, "");
        String uuid = prefs.getString(keyUuid, "");
        String name = prefs.getString(keyName, "");

        if (ip.length() > 0) {
            WiiMDevice dev;
            memset(&dev, 0, sizeof(dev));
            strncpy(dev.ip, ip.c_str(), sizeof(dev.ip) - 1);
            strncpy(dev.uuid, uuid.c_str(), sizeof(dev.uuid) - 1);
            strncpy(dev.name, name.c_str(), sizeof(dev.name) - 1);
            dev.is_active = false;
            _deviceList.devices[_deviceList.count++] = dev;
        }
    }
    log_i("Loaded %u saved devices from NVS", _deviceList.count);
}

void NetworkManager::saveSavedDeviceList() {
    portENTER_CRITICAL(&_deviceMux);
    DeviceList snapshot = _deviceList;
    portEXIT_CRITICAL(&_deviceMux);

    prefs.putUChar("dev_count", snapshot.count);
    for (uint8_t i = 0; i < snapshot.count; ++i) {
        char keyIp[12], keyUuid[14], keyName[14];
        snprintf(keyIp, sizeof(keyIp), "dev_ip_%u", i);
        snprintf(keyUuid, sizeof(keyUuid), "dev_uuid_%u", i);
        snprintf(keyName, sizeof(keyName), "dev_name_%u", i);
        prefs.putString(keyIp, snapshot.devices[i].ip);
        prefs.putString(keyUuid, snapshot.devices[i].uuid);
        prefs.putString(keyName, snapshot.devices[i].name);
    }
    // Clean up stale keys beyond current count
    for (uint8_t i = snapshot.count; i < MAX_DISCOVERED_DEVICES; ++i) {
        char keyIp[12], keyUuid[14], keyName[14];
        snprintf(keyIp, sizeof(keyIp), "dev_ip_%u", i);
        snprintf(keyUuid, sizeof(keyUuid), "dev_uuid_%u", i);
        snprintf(keyName, sizeof(keyName), "dev_name_%u", i);
        prefs.remove(keyIp);
        prefs.remove(keyUuid);
        prefs.remove(keyName);
    }
}
```

Modify `init()` to call `loadSavedDeviceList()` after `loadSavedDevice()`.

Modify `triggerRescan()` to probe NVS-persisted IPs instead of hardcoded ones:

```diff
-    // 1. Immediately probe known candidate IPs first
-    for (size_t i = 0; i < sizeof(known_candidate_ips) / sizeof(known_candidate_ips[0]); ++i) {
-        WiiMDevice dev;
-        if (queryDeviceStatus(known_candidate_ips[i], &dev)) {
-            addOrUpdateDevice(dev);
-        }
-        vTaskDelay(pdMS_TO_TICKS(20));
-    }
+    // 1. Probe previously-discovered IPs from NVS (fast path)
+    portENTER_CRITICAL(&_deviceMux);
+    DeviceList savedSnapshot = _deviceList;
+    portEXIT_CRITICAL(&_deviceMux);
+    for (uint8_t i = 0; i < savedSnapshot.count; ++i) {
+        WiiMDevice dev;
+        if (queryDeviceStatus(savedSnapshot.devices[i].ip, &dev)) {
+            addOrUpdateDevice(dev);
+        }
+        vTaskDelay(pdMS_TO_TICKS(20));
+    }
```

Modify `addOrUpdateDevice()` to persist the list when it changes:

```diff
     if (changed) {
+        saveSavedDeviceList();
         DeviceList* pList = (DeviceList*)malloc(sizeof(DeviceList));
```

**1.4 — Remove `ssdp:all` query** from `sendSSDPQuery()` (lines 524-543):

```diff
-    // SSDP M-SEARCH for all devices
-    const char* ssdp_msearch_all =
-        "M-SEARCH * HTTP/1.1\r\n"
-        "HOST: 239.255.255.250:1900\r\n"
-        "MAN: \"ssdp:discover\"\r\n"
-        "MX: 2\r\n"
-        "ST: ssdp:all\r\n\r\n";
 
     IPAddress mcastIP(239, 255, 255, 250);
     ...
-    udpSSDP.beginPacket(mcastIP, SSDP_PORT);
-    udpSSDP.write((const uint8_t*)ssdp_msearch_all, strlen(ssdp_msearch_all));
-    udpSSDP.endPacket();
```

Remove constructor initialization of dead variables:

```diff
       _volumePending(false),
-      _isScanning(false),
-      _scanCurrentHost(1),
-      _lastScanStepTime(0),
       _hasActiveDevice(false),
```

---

### Component 2: Network Performance

---

#### [MODIFY] [network_manager.cpp](file:///c:/Users/guipe/OneDrive/Projects/wiimremote/src/network_manager.cpp)

**2.1 — Replace String concatenation with `snprintf()` in `executeApiGet()`** (lines 153-204):

```diff
 int NetworkManager::executeApiGet(const String& cmd, String& outPayload) {
     if (!_hasActiveDevice || !_wifiConnected) return -1;
 
-    String targetIp = _activeDevice.ip;
-    String path = "/httpapi.asp?command=" + cmd;
-    String fullUrl = "https://" + targetIp + path;
+    const char* targetIp = _activeDevice.ip;
+    char path[128];
+    snprintf(path, sizeof(path), "/httpapi.asp?command=%s", cmd.c_str());
+    char fullUrl[160];
+    snprintf(fullUrl, sizeof(fullUrl), "https://%s%s", targetIp, path);
 
-    if (_persistentIp != targetIp || !_persistentHttpConfigured) {
+    if (strcmp(_persistentIp.c_str(), targetIp) != 0 || !_persistentHttpConfigured) {
         resetPersistentHttp();
         ...
-        _persistentIp = targetIp;
+        _persistentIp = String(targetIp);
         ...
     }
 
     if (!_persistentHttp.connected()) {
-        if (!_persistentHttp.begin(_persistentClient, fullUrl)) {
+        if (!_persistentHttp.begin(_persistentClient, String(fullUrl))) {
             ...
         }
     } else {
-        _persistentHttp.setURL(path);
+        _persistentHttp.setURL(String(path));
     }
```

Also in the reconnect path (lines 183-195), apply the same `snprintf` pattern instead of rebuilding `String` objects.

**2.1 (cont) — Replace String concatenation in `processIncomingCommands()`:**

```diff
-    sendHttpCommand(String("setPlayerCmd:vol:") + String(_pendingVolume));
+    char volCmd[32];
+    snprintf(volCmd, sizeof(volCmd), "setPlayerCmd:vol:%u", _pendingVolume);
+    sendHttpCommand(String(volCmd));
```

```diff
-    sendHttpCommand(String("setPlayerCmd:mute:") + (cmd.data.mute ? "1" : "0"));
+    sendHttpCommand(cmd.data.mute ? "setPlayerCmd:mute:1" : "setPlayerCmd:mute:0");
```

```diff
-    sendHttpCommand(String("MCUKeyShortClick:") + String(cmd.data.preset_index));
+    char presetCmd[32];
+    snprintf(presetCmd, sizeof(presetCmd), "MCUKeyShortClick:%u", cmd.data.preset_index);
+    sendHttpCommand(String(presetCmd));
```

**2.2 — Reduce `fetchDeviceConfig()` interval to 30s:**

```diff
-        if (now - _lastConfigPoll >= 3000) {
+        if (now - _lastConfigPoll >= 30000) {
```

**2.3 — Cache `WiFi.localIP().toString()` in `handleWiFi()`:**

At the top of the connected branch, compute once and reuse:

```diff
     if (status == WL_CONNECTED) {
         if (!_wifiConnected) {
             _wifiConnected = true;
+            String localIpStr = WiFi.localIP().toString();
+            int8_t rssi = WiFi.RSSI();
+            String ssidStr = WiFi.SSID();
             ...
-            log_i("Wi-Fi Connected! IP: %s, RSSI: %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
+            log_i("Wi-Fi Connected! IP: %s, RSSI: %d dBm", localIpStr.c_str(), rssi);
             ...
-            strncpy(evtSuccess.data.wifi.ip, WiFi.localIP().toString().c_str(), ...);
+            strncpy(evtSuccess.data.wifi.ip, localIpStr.c_str(), ...);
             ...
-            evt.data.wifi.rssi = WiFi.RSSI();
-            strncpy(evt.data.wifi.ip, WiFi.localIP().toString().c_str(), ...);
-            strncpy(evt.data.wifi.ssid, WiFi.SSID().c_str(), ...);
+            evt.data.wifi.rssi = rssi;
+            strncpy(evt.data.wifi.ip, localIpStr.c_str(), ...);
+            strncpy(evt.data.wifi.ssid, ssidStr.c_str(), ...);
```

Same pattern for the periodic RSSI refresh branch (lines 419-430).

**2.6 — Add yield in connected branch of `runTaskLoop()`:**

```diff
         if (_deviceList.count == 0 && (now - _lastSSDPBroadcast >= SSDP_DISCOVERY_INTERVAL_MS)) {
             sendSSDPQuery();
         }
+
+        vTaskDelay(pdMS_TO_TICKS(1)); // Yield to Wi-Fi/lwIP stack on Core 0
     } else {
         vTaskDelay(pdMS_TO_TICKS(50));
     }
```

**6.2 — Restore WiFi sleep mode after scan:**

At the end of `startWiFiScan()`, after `WiFi.scanDelete()`:

```diff
     WiFi.scanDelete();
+    WiFi.setSleep(WIFI_PS_MIN_MODEM);
 }
```

**6.3 — Cache NVS reads in `handleWiFi()` reconnect path:**

Add two member variables to the class for caching:

```diff
     // In network_manager.h, private section:
+    String _cachedSavedSsid;
+    String _cachedSavedPass;
+    bool _cachedSetupDone;
```

Populate them once in `init()` and after successful connection. Replace the NVS reads in `handleWiFi()`:

```diff
-            bool setupDone = prefs.getBool("user_setup_done", false);
-            String savedSsid = setupDone ? prefs.getString("wifi_ssid", "") : "";
-            if (savedSsid.length() > 0 && now - _lastWiFiCheck >= WIFI_RECONNECT_INTERVAL_MS) {
+            if (_cachedSavedSsid.length() > 0 && now - _lastWiFiCheck >= WIFI_RECONNECT_INTERVAL_MS) {
                 _lastWiFiCheck = now;
                 log_i("Reconnecting to Wi-Fi...");
                 WiFi.reconnect();
```

---

### Component 3: UI Performance

---

#### [MODIFY] [ui.cpp](file:///c:/Users/guipe/OneDrive/Projects/wiimremote/src/ui.cpp)

**3.1 — Replace `LV_EVENT_ALL` with specific events:**

For the progress bar (line 521):
```diff
-    lv_obj_add_event_cb(bar_progress, event_slider_seek, LV_EVENT_ALL, nullptr);
+    lv_obj_add_event_cb(bar_progress, event_slider_seek, LV_EVENT_PRESSED, nullptr);
+    lv_obj_add_event_cb(bar_progress, event_slider_seek, LV_EVENT_PRESSING, nullptr);
+    lv_obj_add_event_cb(bar_progress, event_slider_seek, LV_EVENT_RELEASED, nullptr);
```

For the volume slider (line 749):
```diff
-    lv_obj_add_event_cb(slider_vol, event_slider_vol, LV_EVENT_ALL, nullptr);
+    lv_obj_add_event_cb(slider_vol, event_slider_vol, LV_EVENT_PRESSED, nullptr);
+    lv_obj_add_event_cb(slider_vol, event_slider_vol, LV_EVENT_VALUE_CHANGED, nullptr);
+    lv_obj_add_event_cb(slider_vol, event_slider_vol, LV_EVENT_RELEASED, nullptr);
+    lv_obj_add_event_cb(slider_vol, event_slider_vol, LV_EVENT_PRESS_LOST, nullptr);
```

**4.2 — Extract scan status formatting helper:**

```cpp
static void format_scan_status(char* buf, size_t len, uint8_t count) {
    if (count == 1) snprintf(buf, len, "1 streamer found");
    else snprintf(buf, len, "%d streamers found", count);
}
```

Replace the three duplicate formatting blocks in `event_btn_open_modal()`, `ui_set_devices()`, and `ui_set_scanning()`.

**4.3 — Extract seek coordinate-to-ms helper:**

```cpp
static uint32_t touch_x_to_seek_ms(lv_indev_t* indev, int32_t* out_cx) {
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int32_t cx = p.x;
    if (cx < 10) cx = 10;
    if (cx > 230) cx = 230;
    if (out_cx) *out_cx = cx;
    int32_t val = ((cx - 10) * PROGRESS_BAR_MAX) / 220;
    if (val < 0) val = 0;
    if (val > PROGRESS_BAR_MAX) val = PROGRESS_BAR_MAX;
    return (uint32_t)(((uint64_t)val * (uint64_t)current_totlen_ms) / PROGRESS_BAR_MAX);
}
```

Simplify `event_slider_seek()` to use it in both branches.

**4.4 — Simplify volume slider RELEASED handler:**

```diff
     } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
         is_user_adjusting_volume = false;
-        int32_t val = lv_slider_get_value(slider_vol);
-        if (val < 0) val = 0;
-        if (val > 100) val = 100;
-        current_volume = (uint8_t)val;
-
-        if (lbl_vol_percent) {
-            char buf[8];
-            snprintf(buf, sizeof(buf), "%d%%", (int)val);
-            lv_label_set_text(lbl_vol_percent, buf);
-        }
-
-        UiCommand cmd;
-        cmd.type = CMD_SET_VOL;
-        cmd.data.volume = (uint8_t)val;
-        xQueueSend(xQueueUiCmd, &cmd, 0);
     }
```

---

#### [MODIFY] [display_driver.cpp](file:///c:/Users/guipe/OneDrive/Projects/wiimremote/src/display_driver.cpp)

**3.2 — Add DMA completion wait:**

```diff
 static void display_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
     uint32_t w = (area->x2 - area->x1 + 1);
     uint32_t h = (area->y2 - area->y1 + 1);
     gfx.pushImageDMA(area->x1, area->y1, w, h, (const uint16_t *)&color_p->full);
+    gfx.waitDMA();
     lv_disp_flush_ready(disp);
 }
```

**3.3 — Increase draw buffer to 80 rows:**

```diff
-static const uint32_t DRAW_BUF_PIXELS = SCREEN_WIDTH * 40;
+static const uint32_t DRAW_BUF_PIXELS = SCREEN_WIDTH * 80;
```

---

### Component 4: LVGL Configuration

---

#### [MODIFY] [lv_conf.h](file:///c:/Users/guipe/OneDrive/Projects/wiimremote/include/lv_conf.h)

**1.5 — Disable unused widgets:**

```diff
-#define LV_USE_CHECKBOX   1
+#define LV_USE_CHECKBOX   0
 #define LV_USE_DROPDOWN   1
```

```diff
-#define LV_USE_TABLE      1
+#define LV_USE_TABLE      0
```

```diff
-#define LV_USE_ANIMIMG    1
+#define LV_USE_ANIMIMG    0
```

```diff
-#define LV_USE_IMGBTN     1
+#define LV_USE_IMGBTN     0
```

```diff
-#define LV_USE_LED        1
+#define LV_USE_LED        0
```

```diff
-#define LV_USE_GRID 1
+#define LV_USE_GRID 0
```

**1.6 — Disable unused font:**

```diff
-#define LV_FONT_MONTSERRAT_24 1
+#define LV_FONT_MONTSERRAT_24 0
```

**5.1 — Move LVGL memory pool to PSRAM:**

```diff
-#define LV_MEM_CUSTOM 0
-#if LV_MEM_CUSTOM == 0
-    #define LV_MEM_SIZE (48U * 1024U)
-    #define LV_MEM_ADR 0
-#else
-    #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
-    #define LV_MEM_CUSTOM_ALLOC   malloc
-    #define LV_MEM_CUSTOM_FREE    free
-    #define LV_MEM_CUSTOM_REALLOC realloc
-#endif
+#define LV_MEM_CUSTOM 1
+#define LV_MEM_CUSTOM_INCLUDE <esp_heap_caps.h>
+#define LV_MEM_CUSTOM_ALLOC(size)    heap_caps_malloc(size, MALLOC_CAP_SPIRAM)
+#define LV_MEM_CUSTOM_FREE(p)        heap_caps_free(p)
+#define LV_MEM_CUSTOM_REALLOC(p, s)  heap_caps_realloc(p, s, MALLOC_CAP_SPIRAM)
```

---

### Component 5: Build Configuration

---

#### [MODIFY] [platformio.ini](file:///c:/Users/guipe/OneDrive/Projects/wiimremote/platformio.ini)

**6.1 — Reduce debug level:**

```diff
-    -DCORE_DEBUG_LEVEL=3
+    -DCORE_DEBUG_LEVEL=1
```

---

### Component 6: Stack Monitoring (Temporary)

---

#### [MODIFY] [network_manager.cpp](file:///c:/Users/guipe/OneDrive/Projects/wiimremote/src/network_manager.cpp)

**5.2 — Add periodic stack high water mark logging** in `runTaskLoop()`:

```cpp
// Temporary: Log stack usage every 60s for tuning NET_TASK_STACK_SIZE
static unsigned long lastStackLog = 0;
if (millis() - lastStackLog > 60000) {
    lastStackLog = millis();
    log_i("NetTask stack HWM: %u words free", uxTaskGetStackHighWaterMark(NULL));
}
```

> [!NOTE]
> This is temporary instrumentation. After a few sessions of normal use, read the logged value and adjust `NET_TASK_STACK_SIZE` accordingly (keep ~2 KB margin above peak). Then remove this logging block.

---

## Files Summary

| File | Changes |
|:---|:---|
| [`network_manager.h`](file:///c:/Users/guipe/OneDrive/Projects/wiimremote/include/network_manager.h) | Remove dead subnet scanner decls, add NVS device list methods + cached WiFi credentials |
| [`network_manager.cpp`](file:///c:/Users/guipe/OneDrive/Projects/wiimremote/src/network_manager.cpp) | Dead code removal, NVS persistence, String→snprintf, config poll 30s, WiFi caching, yield, WiFi sleep restore, stack monitoring |
| [`ui.cpp`](file:///c:/Users/guipe/OneDrive/Projects/wiimremote/src/ui.cpp) | LV_EVENT_ALL fix, helper extractions, volume slider simplification |
| [`display_driver.cpp`](file:///c:/Users/guipe/OneDrive/Projects/wiimremote/src/display_driver.cpp) | DMA wait, larger draw buffers |
| [`lv_conf.h`](file:///c:/Users/guipe/OneDrive/Projects/wiimremote/include/lv_conf.h) | Disable unused widgets/fonts, LVGL mem to PSRAM |
| [`platformio.ini`](file:///c:/Users/guipe/OneDrive/Projects/wiimremote/platformio.ini) | Debug level reduction |

## Verification Plan

### Automated Tests
- `pio run` — Compile must succeed with zero errors
- Verify binary size reduction (expect ~30-50 KB Flash savings from disabled widgets/fonts)

### Manual Verification
- Flash to device, verify:
  1. Boot log shows PSRAM in use for LVGL, loaded saved device list
  2. Player tab renders and animates correctly (progress bar, transport controls)
  3. Volume slider responds without double-sending on release
  4. Device discovery works (SSDP finds WiiM devices, persists them to NVS)
  5. Reboot: previously discovered devices appear immediately (probed from NVS)
  6. WiFi modal: scan, connect, forget all work
  7. Lyrics tab loads and scrolls smoothly
  8. No screen tearing during tab transitions or lyrics scrolling
  9. Stack HWM log output shows sufficient margin
