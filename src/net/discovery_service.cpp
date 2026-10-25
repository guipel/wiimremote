#include "net/discovery_service.h"
#include "net/linkplay_client.h"
#include "net/upnp_client.h"
#include "net/net_utils.h"
#include "net/wifi_service.h"
#include <Preferences.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include "config.h"

extern QueueHandle_t xQueueUiState;

static Preferences s_dev_prefs;
static WiFiUDP s_udp_ssdp;
static ActiveDeviceChangedCb s_on_active_changed_cb = nullptr;

static DeviceList s_device_list;
static uint8_t s_prev_saved_count = 0;
static portMUX_TYPE s_device_mux = portMUX_INITIALIZER_UNLOCKED;
static WiiMDevice s_active_device;
static bool s_has_active_device = false;

// Cache of non-WiiM UPnP IPs already probed during passive SSDP listening to prevent repeated TCP/TLS probe storms
static constexpr uint8_t MAX_IGNORED_IPS = 16;
static char s_ignored_ips[MAX_IGNORED_IPS][24];
static uint8_t s_ignored_count = 0;

static void load_saved_device() {
    String savedIp = s_dev_prefs.getString("active_ip", "");
    String savedUuid = s_dev_prefs.getString("active_uuid", "");
    String savedName = s_dev_prefs.getString("active_name", "");

    if (savedIp.length() > 0) {
        strncpy(s_active_device.ip, savedIp.c_str(), sizeof(s_active_device.ip) - 1);
        strncpy(s_active_device.uuid, savedUuid.c_str(), sizeof(s_active_device.uuid) - 1);
        strncpy(s_active_device.name, savedName.c_str(), sizeof(s_active_device.name) - 1);
        s_active_device.is_active = true;
        s_active_device.is_fixed_volume = false;
        s_has_active_device = true;
        log_i("Loaded saved active device: %s (%s)", s_active_device.name, s_active_device.ip);
    }
}

static void save_active_device(const WiiMDevice& dev) {
    s_dev_prefs.putString("active_ip", dev.ip);
    s_dev_prefs.putString("active_uuid", dev.uuid);
    s_dev_prefs.putString("active_name", dev.name);
    log_i("Saved active device to NVS: %s (%s)", dev.name, dev.ip);
}

static void clear_saved_active_device() {
    s_dev_prefs.remove("active_ip");
    s_dev_prefs.remove("active_uuid");
    s_dev_prefs.remove("active_name");
    s_dev_prefs.remove("fixed_vol");
    log_i("Cleared active device from NVS");
}

static void load_saved_device_list() {
    uint8_t count = s_dev_prefs.getUChar("dev_count", 0);
    if (count > MAX_DISCOVERED_DEVICES) count = MAX_DISCOVERED_DEVICES;

    for (uint8_t i = 0; i < count; ++i) {
        char keyIp[24], keyUuid[24], keyName[24];
        snprintf(keyIp, sizeof(keyIp), "dev_ip_%u", i);
        snprintf(keyUuid, sizeof(keyUuid), "dev_uuid_%u", i);
        snprintf(keyName, sizeof(keyName), "dev_name_%u", i);

        String ip = s_dev_prefs.getString(keyIp, "");
        String uuid = s_dev_prefs.getString(keyUuid, "");
        String name = s_dev_prefs.getString(keyName, "");

        if (ip.length() > 0) {
            WiiMDevice dev;
            memset(&dev, 0, sizeof(dev));
            strncpy(dev.ip, ip.c_str(), sizeof(dev.ip) - 1);
            strncpy(dev.uuid, uuid.c_str(), sizeof(dev.uuid) - 1);
            strncpy(dev.name, name.c_str(), sizeof(dev.name) - 1);
            dev.is_active = (s_has_active_device && strcmp(dev.ip, s_active_device.ip) == 0);
            dev.is_fixed_volume = false;
            s_device_list.devices[s_device_list.count++] = dev;
        }
    }
    s_prev_saved_count = s_device_list.count;
    log_i("Loaded %u saved devices from NVS", s_device_list.count);
}

static void save_saved_device_list() {
    portENTER_CRITICAL(&s_device_mux);
    DeviceList snapshot = s_device_list;
    portEXIT_CRITICAL(&s_device_mux);

    s_dev_prefs.putUChar("dev_count", snapshot.count);
    for (uint8_t i = 0; i < snapshot.count && i < MAX_DISCOVERED_DEVICES; ++i) {
        char keyIp[24], keyUuid[24], keyName[24];
        snprintf(keyIp, sizeof(keyIp), "dev_ip_%u", i);
        snprintf(keyUuid, sizeof(keyUuid), "dev_uuid_%u", i);
        snprintf(keyName, sizeof(keyName), "dev_name_%u", i);
        s_dev_prefs.putString(keyIp, snapshot.devices[i].ip);
        s_dev_prefs.putString(keyUuid, snapshot.devices[i].uuid);
        s_dev_prefs.putString(keyName, snapshot.devices[i].name);
    }
    // Clean up only stale keys beyond current count up to previously saved count
    for (uint8_t i = snapshot.count; i < s_prev_saved_count && i < MAX_DISCOVERED_DEVICES; ++i) {
        char keyIp[24], keyUuid[24], keyName[24];
        snprintf(keyIp, sizeof(keyIp), "dev_ip_%u", i);
        snprintf(keyUuid, sizeof(keyUuid), "dev_uuid_%u", i);
        snprintf(keyName, sizeof(keyName), "dev_name_%u", i);
        s_dev_prefs.remove(keyIp);
        s_dev_prefs.remove(keyUuid);
        s_dev_prefs.remove(keyName);
    }
    s_prev_saved_count = snapshot.count;
}

static bool query_device_status(const char* ip, WiiMDevice* outDevice) {
    if (!ip || strlen(ip) == 0) return false;

    String devName = "";
    String uuid = "";

    // 1. FAST PATH: Query plain HTTP UPnP description.xml on port 49152 (instant, no TLS overhead!)
    {
        HTTPClient http;
        String url = "http://" + String(ip) + ":" + String(WIIM_UPNP_PORT) + "/description.xml";
        http.setConnectTimeout(350);
        http.setTimeout(600);
        if (http.begin(url)) {
            int code = http.GET();
            if (code == HTTP_CODE_OK) {
                String payload = http.getString();
                http.end();
                if (payload.indexOf("MediaRenderer") >= 0 || payload.indexOf("WiiM") >= 0 || payload.indexOf("Linkplay") >= 0) {
                    devName = net_extract_xml_tag(payload, "friendlyName");
                    uuid = net_extract_xml_tag(payload, "UDN");
                    if (uuid.startsWith("uuid:")) {
                        uuid = uuid.substring(5);
                    }
                }
            } else {
                http.end();
            }
        }
    }

    // 2. FALLBACK PATH: Query HTTPS 443 with getStatusEx
    if (devName.length() == 0) {
        WiFiClientSecure secureClient;
        secureClient.setInsecure(); // Accept WiiM self-signed TLS cert
        secureClient.setTimeout(1); // 1 sec handshake timeout

        HTTPClient https;
        String url = "https://" + String(ip) + "/httpapi.asp?command=getStatusEx";
        https.setConnectTimeout(500);
        https.setTimeout(HTTP_REQUEST_TIMEOUT_MS);

        if (https.begin(secureClient, url)) {
            int code = https.GET();
            if (code == HTTP_CODE_OK) {
                String payload = https.getString();
                https.end();

                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, payload);
                if (!err) {
                    const char* nameCandidate = doc["DeviceName"] | doc["GroupName"] | doc["ssid"] | "WiiM Streamer";
                    const char* uuidCandidate = doc["uuid"] | doc["MAC"] | ip;
                    devName = String(nameCandidate);
                    uuid = String(uuidCandidate);
                }
            } else {
                https.end();
            }
        }
    }

    if (devName.length() == 0) {
        return false;
    }

    if (uuid.length() == 0) {
        uuid = String(ip);
    }

    if (outDevice) {
        memset(outDevice, 0, sizeof(WiiMDevice));
        strncpy(outDevice->name, devName.c_str(), sizeof(outDevice->name) - 1);
        strncpy(outDevice->ip, ip, sizeof(outDevice->ip) - 1);
        strncpy(outDevice->uuid, uuid.c_str(), sizeof(outDevice->uuid) - 1);
        outDevice->is_active = false;
        outDevice->is_fixed_volume = false;
    }

    log_i("Discovered WiiM Device: Name='%s', IP=%s, UUID=%s", devName.c_str(), ip, uuid.c_str());
    return true;
}

static void add_or_update_device(const WiiMDevice& dev) {
    portENTER_CRITICAL(&s_device_mux);

    bool exists = false;
    bool changed = false;
    bool preservedFixed = dev.is_fixed_volume;

    // 1. Primary identity lookup: match by stable hardware UUID
    for (uint8_t i = 0; i < s_device_list.count; ++i) {
        if (strlen(dev.uuid) > 0 && strcmp(s_device_list.devices[i].uuid, dev.uuid) == 0) {
            exists = true;
            preservedFixed = s_device_list.devices[i].is_fixed_volume;
            if (strcmp(s_device_list.devices[i].name, dev.name) != 0 ||
                strcmp(s_device_list.devices[i].ip, dev.ip) != 0) {
                strncpy(s_device_list.devices[i].name, dev.name, sizeof(s_device_list.devices[i].name) - 1);
                strncpy(s_device_list.devices[i].ip, dev.ip, sizeof(s_device_list.devices[i].ip) - 1);
                changed = true;
            }
            break;
        }
    }

    // 2. Secondary reconciliation: if IP matches a different UUID (DHCP reassigned IP to another WiiM),
    //    replace the stale entry's UUID, name, and fixed-volume state with the new device's identity.
    if (!exists) {
        for (uint8_t i = 0; i < s_device_list.count; ++i) {
            if (strcmp(s_device_list.devices[i].ip, dev.ip) == 0) {
                exists = true;
                preservedFixed = dev.is_fixed_volume;
                s_device_list.devices[i] = dev;
                changed = true;
                break;
            }
        }
    }

    // 3. New device insertion (with FIFO eviction of oldest non-active entry if table is full)
    if (!exists) {
        if (s_device_list.count >= MAX_DISCOVERED_DEVICES) {
            uint8_t evictIdx = 0;
            for (uint8_t i = 0; i < s_device_list.count; ++i) {
                if (!s_device_list.devices[i].is_active) {
                    evictIdx = i;
                    break;
                }
            }
            if (evictIdx + 1 < s_device_list.count) {
                memmove(&s_device_list.devices[evictIdx],
                        &s_device_list.devices[evictIdx + 1],
                        sizeof(WiiMDevice) * (s_device_list.count - evictIdx - 1));
            }
            s_device_list.count--;
        }
        s_device_list.devices[s_device_list.count] = dev;
        s_device_list.count++;
        changed = true;
    }

    // Auto-select if no active device, or reconcile if this matches active UUID or reassigned active IP
    bool newActive = false;
    bool activeUpdated = false;
    if (!s_has_active_device) {
        s_active_device = dev;
        s_active_device.is_active = true;
        s_active_device.is_fixed_volume = preservedFixed;
        s_has_active_device = true;
        newActive = true;
        changed = true;
    } else if (strcmp(s_active_device.uuid, dev.uuid) == 0) {
        if (strcmp(s_active_device.ip, dev.ip) != 0 || strcmp(s_active_device.name, dev.name) != 0) {
            bool prevActiveFixed = s_active_device.is_fixed_volume;
            s_active_device = dev;
            s_active_device.is_active = true;
            s_active_device.is_fixed_volume = prevActiveFixed;
            activeUpdated = true;
            changed = true;
        }
    } else if (strcmp(s_active_device.ip, dev.ip) == 0 && strcmp(s_active_device.uuid, dev.uuid) != 0) {
        // Active IP was reassigned by DHCP to a different WiiM unit
        s_active_device = dev;
        s_active_device.is_active = true;
        newActive = true;
        changed = true;
    }

    // Update is_active flag in list
    for (uint8_t i = 0; i < s_device_list.count; ++i) {
        s_device_list.devices[i].is_active = (s_has_active_device && strcmp(s_device_list.devices[i].ip, s_active_device.ip) == 0);
    }

    portEXIT_CRITICAL(&s_device_mux);

    if (newActive || activeUpdated) {
        save_active_device(s_active_device);
        if (newActive && s_on_active_changed_cb) {
            s_on_active_changed_cb(s_active_device);
        }
    }

    if (changed) {
        save_saved_device_list();
        discovery_service_broadcast_list();
    }
}

void discovery_service_init(ActiveDeviceChangedCb onActiveChanged) {
    s_on_active_changed_cb = onActiveChanged;
    s_dev_prefs.begin("wiimremote", false);

    memset(&s_active_device, 0, sizeof(s_active_device));
    memset(&s_device_list, 0, sizeof(s_device_list));
    memset(s_ignored_ips, 0, sizeof(s_ignored_ips));
    s_ignored_count = 0;

    load_saved_device();
    load_saved_device_list();

    if (s_device_list.count > 0) {
        discovery_service_broadcast_list();
    }
}

void discovery_service_start_ssdp_listener() {
    s_udp_ssdp.beginMulticast(IPAddress(239, 255, 255, 250), SSDP_PORT);
}

void discovery_service_send_upnp_server_query() {
    if (!wifi_service_is_connected()) return;

    const char* ssdp_msearch_mediaserver =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 1\r\n"
        "ST: urn:schemas-upnp-org:device:MediaServer:1\r\n\r\n";

    IPAddress mcastIP(239, 255, 255, 250);
    s_udp_ssdp.beginPacket(mcastIP, SSDP_PORT);
    s_udp_ssdp.write((const uint8_t*)ssdp_msearch_mediaserver, strlen(ssdp_msearch_mediaserver));
    s_udp_ssdp.endPacket();
}

void discovery_service_send_ssdp_query() {
    if (!wifi_service_is_connected()) return;

    // SSDP M-SEARCH for MediaRenderer
    const char* ssdp_msearch_renderer =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n\r\n";

    // SSDP M-SEARCH for LinkPlay WiFiAudio
    const char* ssdp_msearch_wiimu =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: urn:schemas-wiimu-com:device:WiFiAudio:1\r\n\r\n";

    // SSDP M-SEARCH for UPnP MediaServer
    const char* ssdp_msearch_mediaserver =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: urn:schemas-upnp-org:device:MediaServer:1\r\n\r\n";

    IPAddress mcastIP(239, 255, 255, 250);
    s_udp_ssdp.beginPacket(mcastIP, SSDP_PORT);
    s_udp_ssdp.write((const uint8_t*)ssdp_msearch_renderer, strlen(ssdp_msearch_renderer));
    s_udp_ssdp.endPacket();

    s_udp_ssdp.beginPacket(mcastIP, SSDP_PORT);
    s_udp_ssdp.write((const uint8_t*)ssdp_msearch_wiimu, strlen(ssdp_msearch_wiimu));
    s_udp_ssdp.endPacket();

    s_udp_ssdp.beginPacket(mcastIP, SSDP_PORT);
    s_udp_ssdp.write((const uint8_t*)ssdp_msearch_mediaserver, strlen(ssdp_msearch_mediaserver));
    s_udp_ssdp.endPacket();
}

void discovery_service_process_ssdp() {
    int packetSize;
    while ((packetSize = s_udp_ssdp.parsePacket()) > 0) {
        char packetBuffer[1024];
        int len = s_udp_ssdp.read(packetBuffer, sizeof(packetBuffer) - 1);
        if (len <= 0) continue;
        packetBuffer[len] = '\0';

        // Route UPnP MediaServer / ContentDirectory announcements to upnp_client
        if (strstr(packetBuffer, "MediaServer") != nullptr ||
            strstr(packetBuffer, "ContentDirectory") != nullptr) {
            const char* locHdr = strstr(packetBuffer, "LOCATION: http://");
            if (!locHdr) locHdr = strstr(packetBuffer, "location: http://");
            if (!locHdr) locHdr = strstr(packetBuffer, "Location: http://");
            if (locHdr) {
                const char* urlStart = locHdr + 10;
                const char* lineEnd = strpbrk(urlStart, "\r\n");
                if (lineEnd && (lineEnd - urlStart) < 160) {
                    char locUrl[160];
                    size_t uLen = (size_t)(lineEnd - urlStart);
                    memcpy(locUrl, urlStart, uLen);
                    locUrl[uLen] = '\0';
                    upnp_client_on_ssdp_location(locUrl);
                }
            }
            continue;
        }

        // Zero-allocation check on stack buffer before constructing String objects
        if (strstr(packetBuffer, "MediaRenderer") == nullptr &&
            strstr(packetBuffer, "Linkplay") == nullptr &&
            strstr(packetBuffer, "LinkPlay") == nullptr &&
            strstr(packetBuffer, "WiFiAudio") == nullptr &&
            strstr(packetBuffer, "WiiM") == nullptr &&
            strstr(packetBuffer, "wiim") == nullptr) {
            continue;
        }

        IPAddress remoteIP = s_udp_ssdp.remoteIP();
        String ipStr = remoteIP.toString();

        const char* locPtr = strstr(packetBuffer, "LOCATION: http://");
        if (!locPtr) locPtr = strstr(packetBuffer, "location: http://");
        if (!locPtr) locPtr = strstr(packetBuffer, "Location: http://");
        if (locPtr && strstr(locPtr, ":49152") == nullptr &&
            strstr(packetBuffer, "Linkplay") == nullptr &&
            strstr(packetBuffer, "LinkPlay") == nullptr &&
            strstr(packetBuffer, "WiFiAudio") == nullptr &&
            strstr(packetBuffer, "WiiM") == nullptr &&
            strstr(packetBuffer, "wiim") == nullptr) {
            continue;
        }
        if (locPtr) {
            const char* start = locPtr + 17;
            const char* colonPtr = strchr(start, ':');
            const char* slashPtr = strchr(start, '/');
            const char* end = (colonPtr && (!slashPtr || colonPtr < slashPtr)) ? colonPtr : slashPtr;
            if (end && end > start && (end - start) < 24) {
                char parsedIp[24];
                size_t ipLen = (size_t)(end - start);
                memcpy(parsedIp, start, ipLen);
                parsedIp[ipLen] = '\0';
                ipStr = String(parsedIp);
            }
        }

        bool alreadyKnown = false;
        portENTER_CRITICAL(&s_device_mux);
        for (uint8_t i = 0; i < s_device_list.count; ++i) {
            if (strcmp(s_device_list.devices[i].ip, ipStr.c_str()) == 0) {
                alreadyKnown = true;
                break;
            }
        }
        portEXIT_CRITICAL(&s_device_mux);

        if (alreadyKnown) continue;

        bool alreadyIgnored = false;
        for (uint8_t i = 0; i < s_ignored_count; ++i) {
            if (strcmp(s_ignored_ips[i], ipStr.c_str()) == 0) {
                alreadyIgnored = true;
                break;
            }
        }
        if (alreadyIgnored) continue;

        WiiMDevice dev;
        if (query_device_status(ipStr.c_str(), &dev)) {
            add_or_update_device(dev);
        } else if (s_ignored_count < MAX_IGNORED_IPS) {
            strncpy(s_ignored_ips[s_ignored_count], ipStr.c_str(), sizeof(s_ignored_ips[0]) - 1);
            s_ignored_ips[s_ignored_count][sizeof(s_ignored_ips[0]) - 1] = '\0';
            s_ignored_count++;
        }
    }
}

void discovery_service_trigger_rescan() {
    if (!wifi_service_is_connected()) return;
    log_i("Triggering device rescan (NVS IPs + SSDP)...");

    // Clear ignored non-WiiM IP cache on explicit rescan
    s_ignored_count = 0;

    UiEvent evt;
    evt.type = UI_EVT_SCAN_STATUS;
    evt.data.scan.is_scanning = true;
    xQueueSend(xQueueUiState, &evt, 0);

    // 1. Snapshot NVS devices and broadcast SSDP M-SEARCH immediately so responses arrive in parallel
    portENTER_CRITICAL(&s_device_mux);
    DeviceList savedSnapshot = s_device_list;
    s_device_list.count = 0;
    portEXIT_CRITICAL(&s_device_mux);

    discovery_service_send_ssdp_query();

    // 2. Probe previously-discovered IPs from NVS and prune stale/unreachable entries
    for (uint8_t i = 0; i < savedSnapshot.count; ++i) {
        esp_task_wdt_reset();
        WiiMDevice dev;
        if (query_device_status(savedSnapshot.devices[i].ip, &dev)) {
            dev.is_fixed_volume = savedSnapshot.devices[i].is_fixed_volume;
            add_or_update_device(dev);
        } else {
            log_w("Pruning unreachable NVS streamer: %s (%s)", savedSnapshot.devices[i].name, savedSnapshot.devices[i].ip);
        }
        discovery_service_process_ssdp();
        vTaskDelay(pdMS_TO_TICKS(15)); // Yield to prevent WDT timeout
    }

    // 3. Collect incoming SSDP responses (catches newly added or DHCP re-IP'd streamers)
    for (uint8_t w = 0; w < 8; ++w) {
        esp_task_wdt_reset();
        discovery_service_process_ssdp();
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // 4. Reconcile active device state after both NVS probes and SSDP collection complete
    bool activePresent = false;
    bool switchedActive = false;
    bool clearedActive = false;
    WiiMDevice newActiveDev;
    memset(&newActiveDev, 0, sizeof(newActiveDev));

    portENTER_CRITICAL(&s_device_mux);
    for (uint8_t i = 0; i < s_device_list.count; ++i) {
        if (s_device_list.devices[i].is_active ||
            (s_has_active_device &&
             ((strlen(s_active_device.uuid) > 0 && strcmp(s_device_list.devices[i].uuid, s_active_device.uuid) == 0) ||
              strcmp(s_device_list.devices[i].ip, s_active_device.ip) == 0))) {
            bool prevFixed = s_active_device.is_fixed_volume;
            s_device_list.devices[i].is_active = true;
            s_device_list.devices[i].is_fixed_volume = prevFixed;
            s_active_device = s_device_list.devices[i];
            s_has_active_device = true;
            activePresent = true;
            break;
        }
    }
    if (s_has_active_device && !activePresent) {
        if (s_device_list.count > 0) {
            s_active_device = s_device_list.devices[0];
            s_active_device.is_active = true;
            s_device_list.devices[0].is_active = true;
            newActiveDev = s_active_device;
            switchedActive = true;
        } else {
            s_has_active_device = false;
            memset(&s_active_device, 0, sizeof(s_active_device));
            clearedActive = true;
        }
    }
    portEXIT_CRITICAL(&s_device_mux);

    save_saved_device_list();

    if (switchedActive) {
        save_active_device(newActiveDev);
        if (s_on_active_changed_cb) {
            s_on_active_changed_cb(newActiveDev);
        }
    } else if (clearedActive) {
        clear_saved_active_device();
        linkplay_client_reset_session();
        linkplay_client_reset_track_cache();
        UiEvent stEvt;
        memset(&stEvt, 0, sizeof(stEvt));
        stEvt.type = UI_EVT_PLAYER_STATE;
        stEvt.data.player.state = PLAY_STATE_NONE;
        xQueueSend(xQueueUiState, &stEvt, 0);
    }

    // Broadcast current device list to UI
    discovery_service_broadcast_list();

    UiEvent doneEvt;
    doneEvt.type = UI_EVT_SCAN_STATUS;
    doneEvt.data.scan.is_scanning = false;
    xQueueSend(xQueueUiState, &doneEvt, 0);
}

void discovery_service_select_device(const char* ip) {
    if (!ip || strlen(ip) == 0) return;

    portENTER_CRITICAL(&s_device_mux);
    for (uint8_t i = 0; i < s_device_list.count; ++i) {
        if (strcmp(s_device_list.devices[i].ip, ip) == 0) {
            s_active_device = s_device_list.devices[i];
            s_active_device.is_active = true;
            s_has_active_device = true;
            break;
        }
    }
    for (uint8_t i = 0; i < s_device_list.count; ++i) {
        s_device_list.devices[i].is_active = (strcmp(s_device_list.devices[i].ip, s_active_device.ip) == 0);
    }
    portEXIT_CRITICAL(&s_device_mux);

    if (s_has_active_device) {
        save_active_device(s_active_device);
        log_i("Active device switched to: %s (%s)", s_active_device.name, s_active_device.ip);
        discovery_service_broadcast_list();

        if (s_on_active_changed_cb) {
            s_on_active_changed_cb(s_active_device);
        }
    }
}

bool discovery_service_get_active_device(WiiMDevice* dev) {
    if (!s_has_active_device) return false;
    if (dev) *dev = s_active_device;
    return true;
}

bool discovery_service_has_active_device() {
    return s_has_active_device;
}

void discovery_service_set_active_fixed_volume(bool is_fixed) {
    if (s_active_device.is_fixed_volume == is_fixed) return;

    portENTER_CRITICAL(&s_device_mux);
    s_active_device.is_fixed_volume = is_fixed;
    for (uint8_t i = 0; i < s_device_list.count; ++i) {
        if (strcmp(s_device_list.devices[i].ip, s_active_device.ip) == 0) {
            s_device_list.devices[i].is_fixed_volume = is_fixed;
            break;
        }
    }
    portEXIT_CRITICAL(&s_device_mux);
}

void discovery_service_broadcast_list() {
    portENTER_CRITICAL(&s_device_mux);
    DeviceList listCopy = s_device_list;
    portEXIT_CRITICAL(&s_device_mux);

    DeviceList* pList = (DeviceList*)malloc(sizeof(DeviceList));
    if (pList) {
        *pList = listCopy;
        UiEvent evt;
        evt.type = UI_EVT_DEVICES_UPDATED;
        evt.data.devices = pList;
        if (xQueueSend(xQueueUiState, &evt, 0) != pdTRUE) {
            free(pList);
        }
    }
}

uint8_t discovery_service_get_count() {
    portENTER_CRITICAL(&s_device_mux);
    uint8_t count = s_device_list.count;
    portEXIT_CRITICAL(&s_device_mux);
    return count;
}
