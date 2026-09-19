#include "net/discovery_service.h"
#include "net/net_utils.h"
#include "net/wifi_service.h"
#include <Preferences.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "config.h"

extern QueueHandle_t xQueueUiState;

static Preferences s_dev_prefs;
static WiFiUDP s_udp_ssdp;
static ActiveDeviceChangedCb s_on_active_changed_cb = nullptr;

static DeviceList s_device_list;
static portMUX_TYPE s_device_mux = portMUX_INITIALIZER_UNLOCKED;
static WiiMDevice s_active_device;
static bool s_has_active_device = false;

static void load_saved_device() {
    String savedIp = s_dev_prefs.getString("active_ip", "");
    String savedUuid = s_dev_prefs.getString("active_uuid", "");
    String savedName = s_dev_prefs.getString("active_name", "");

    if (savedIp.length() > 0) {
        strncpy(s_active_device.ip, savedIp.c_str(), sizeof(s_active_device.ip) - 1);
        strncpy(s_active_device.uuid, savedUuid.c_str(), sizeof(s_active_device.uuid) - 1);
        strncpy(s_active_device.name, savedName.c_str(), sizeof(s_active_device.name) - 1);
        s_active_device.is_active = true;
        s_active_device.is_fixed_volume = s_dev_prefs.getBool("fixed_vol", false);
        s_has_active_device = true;
        log_i("Loaded saved active device: %s (%s, Fixed=%d)", s_active_device.name, s_active_device.ip, s_active_device.is_fixed_volume ? 1 : 0);
    }
}

static void save_active_device(const WiiMDevice& dev) {
    s_dev_prefs.putString("active_ip", dev.ip);
    s_dev_prefs.putString("active_uuid", dev.uuid);
    s_dev_prefs.putString("active_name", dev.name);
    s_dev_prefs.putBool("fixed_vol", dev.is_fixed_volume);
    log_i("Saved active device to NVS: %s (%s, Fixed=%d)", dev.name, dev.ip, dev.is_fixed_volume ? 1 : 0);
}

static void load_saved_device_list() {
    uint8_t count = s_dev_prefs.getUChar("dev_count", 0);
    if (count > MAX_DISCOVERED_DEVICES) count = MAX_DISCOVERED_DEVICES;

    for (uint8_t i = 0; i < count; ++i) {
        char keyIp[12], keyUuid[14], keyName[14];
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
            dev.is_active = false;
            s_device_list.devices[s_device_list.count++] = dev;
        }
    }
    log_i("Loaded %u saved devices from NVS", s_device_list.count);
}

static void save_saved_device_list() {
    portENTER_CRITICAL(&s_device_mux);
    DeviceList snapshot = s_device_list;
    portEXIT_CRITICAL(&s_device_mux);

    s_dev_prefs.putUChar("dev_count", snapshot.count);
    for (uint8_t i = 0; i < snapshot.count; ++i) {
        char keyIp[12], keyUuid[14], keyName[14];
        snprintf(keyIp, sizeof(keyIp), "dev_ip_%u", i);
        snprintf(keyUuid, sizeof(keyUuid), "dev_uuid_%u", i);
        snprintf(keyName, sizeof(keyName), "dev_name_%u", i);
        s_dev_prefs.putString(keyIp, snapshot.devices[i].ip);
        s_dev_prefs.putString(keyUuid, snapshot.devices[i].uuid);
        s_dev_prefs.putString(keyName, snapshot.devices[i].name);
    }
    // Clean up stale keys beyond current count
    for (uint8_t i = snapshot.count; i < MAX_DISCOVERED_DEVICES; ++i) {
        char keyIp[12], keyUuid[14], keyName[14];
        snprintf(keyIp, sizeof(keyIp), "dev_ip_%u", i);
        snprintf(keyUuid, sizeof(keyUuid), "dev_uuid_%u", i);
        snprintf(keyName, sizeof(keyName), "dev_name_%u", i);
        s_dev_prefs.remove(keyIp);
        s_dev_prefs.remove(keyUuid);
        s_dev_prefs.remove(keyName);
    }
}

static bool query_device_status(const char* ip, WiiMDevice* outDevice) {
    if (!ip || strlen(ip) == 0) return false;

    String devName = "";
    String uuid = "";

    // 1. FAST PATH: Query plain HTTP UPnP description.xml on port 49152 (instant, no TLS overhead!)
    {
        HTTPClient http;
        String url = "http://" + String(ip) + ":" + String(WIIM_UPNP_PORT) + "/description.xml";
        http.setTimeout(800);
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
        secureClient.setTimeout(2); // 2 sec timeout

        HTTPClient https;
        String url = "https://" + String(ip) + "/httpapi.asp?command=getStatusEx";
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
        strncpy(outDevice->name, devName.c_str(), sizeof(outDevice->name) - 1);
        strncpy(outDevice->ip, ip, sizeof(outDevice->ip) - 1);
        strncpy(outDevice->uuid, uuid.c_str(), sizeof(outDevice->uuid) - 1);
        outDevice->is_active = false;
    }

    log_i("Discovered WiiM Device: Name='%s', IP=%s, UUID=%s", devName.c_str(), ip, uuid.c_str());
    return true;
}

static void add_or_update_device(const WiiMDevice& dev) {
    portENTER_CRITICAL(&s_device_mux);

    bool exists = false;
    bool changed = false;
    for (uint8_t i = 0; i < s_device_list.count; ++i) {
        if (strcmp(s_device_list.devices[i].uuid, dev.uuid) == 0 ||
            strcmp(s_device_list.devices[i].ip, dev.ip) == 0) {
            exists = true;
            if (strcmp(s_device_list.devices[i].name, dev.name) != 0 ||
                strcmp(s_device_list.devices[i].ip, dev.ip) != 0) {
                strncpy(s_device_list.devices[i].name, dev.name, sizeof(s_device_list.devices[i].name) - 1);
                strncpy(s_device_list.devices[i].ip, dev.ip, sizeof(s_device_list.devices[i].ip) - 1);
                changed = true;
            }
            break;
        }
    }

    if (!exists && s_device_list.count < MAX_DISCOVERED_DEVICES) {
        s_device_list.devices[s_device_list.count] = dev;
        s_device_list.count++;
        changed = true;
    }

    // Auto-select if no active device, or if this matches saved device
    bool newActive = false;
    if (!s_has_active_device) {
        s_active_device = dev;
        s_active_device.is_active = true;
        s_has_active_device = true;
        newActive = true;
        changed = true;
    } else if (strcmp(s_active_device.uuid, dev.uuid) == 0) {
        if (strcmp(s_active_device.ip, dev.ip) != 0 || strcmp(s_active_device.name, dev.name) != 0) {
            s_active_device = dev;
            s_active_device.is_active = true;
            changed = true;
        }
    }

    // Update is_active flag in list
    for (uint8_t i = 0; i < s_device_list.count; ++i) {
        s_device_list.devices[i].is_active = (strcmp(s_device_list.devices[i].ip, s_active_device.ip) == 0);
    }

    portEXIT_CRITICAL(&s_device_mux);

    if (newActive) {
        save_active_device(s_active_device);
        if (s_on_active_changed_cb) {
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

    load_saved_device();
    load_saved_device_list();

    if (s_device_list.count > 0) {
        discovery_service_broadcast_list();
    }
}

void discovery_service_start_ssdp_listener() {
    s_udp_ssdp.beginMulticast(IPAddress(239, 255, 255, 250), SSDP_PORT);
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

    IPAddress mcastIP(239, 255, 255, 250);
    s_udp_ssdp.beginPacket(mcastIP, SSDP_PORT);
    s_udp_ssdp.write((const uint8_t*)ssdp_msearch_renderer, strlen(ssdp_msearch_renderer));
    s_udp_ssdp.endPacket();

    s_udp_ssdp.beginPacket(mcastIP, SSDP_PORT);
    s_udp_ssdp.write((const uint8_t*)ssdp_msearch_wiimu, strlen(ssdp_msearch_wiimu));
    s_udp_ssdp.endPacket();
}

void discovery_service_process_ssdp() {
    int packetSize;
    while ((packetSize = s_udp_ssdp.parsePacket()) > 0) {
        char packetBuffer[1024];
        int len = s_udp_ssdp.read(packetBuffer, sizeof(packetBuffer) - 1);
        if (len <= 0) continue;
        packetBuffer[len] = '\0';

        IPAddress remoteIP = s_udp_ssdp.remoteIP();
        String ipStr = remoteIP.toString();

        String packetContent(packetBuffer);
        if (packetContent.indexOf("MediaRenderer") >= 0 ||
            packetContent.indexOf("Linkplay") >= 0 ||
            packetContent.indexOf("LinkPlay") >= 0 ||
            packetContent.indexOf("WiFiAudio") >= 0 ||
            packetContent.indexOf("WiiM") >= 0 ||
            packetContent.indexOf("wiim") >= 0) {

            int locIdx = packetContent.indexOf("LOCATION: http://");
            if (locIdx < 0) locIdx = packetContent.indexOf("location: http://");
            if (locIdx >= 0) {
                int start = locIdx + 17;
                int colonIdx = packetContent.indexOf(":", start);
                int slashIdx = packetContent.indexOf("/", start);
                int end = (colonIdx > 0 && colonIdx < slashIdx) ? colonIdx : slashIdx;
                if (end > start) {
                    ipStr = packetContent.substring(start, end);
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

            WiiMDevice dev;
            if (query_device_status(ipStr.c_str(), &dev)) {
                add_or_update_device(dev);
            }
        }
    }
}

void discovery_service_trigger_rescan() {
    if (!wifi_service_is_connected()) return;
    log_i("Triggering device rescan (NVS IPs + SSDP)...");

    UiEvent evt;
    evt.type = UI_EVT_SCAN_STATUS;
    evt.data.scan.is_scanning = true;
    xQueueSend(xQueueUiState, &evt, 0);

    // 1. Probe previously-discovered IPs from NVS (fast path)
    portENTER_CRITICAL(&s_device_mux);
    DeviceList savedSnapshot = s_device_list;
    portEXIT_CRITICAL(&s_device_mux);
    for (uint8_t i = 0; i < savedSnapshot.count; ++i) {
        WiiMDevice dev;
        if (query_device_status(savedSnapshot.devices[i].ip, &dev)) {
            add_or_update_device(dev);
        }
        vTaskDelay(pdMS_TO_TICKS(20)); // Yield to prevent WDT timeout
    }

    // 2. Broadcast SSDP discovery
    discovery_service_send_ssdp_query();

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
    s_active_device.is_fixed_volume = is_fixed;
    save_active_device(s_active_device);
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
