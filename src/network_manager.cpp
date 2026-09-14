#include "network_manager.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <ArduinoJson.h>

static WiFiUDP udpSSDP;
static Preferences prefs;

// Ensure any string (whether already UTF-8 or legacy Latin-1 / ISO-8859-1) is valid UTF-8
static String toValidUtf8(const String& str) {
    bool validUtf8 = true;
    size_t i = 0;
    size_t len = str.length();
    while (i < len) {
        uint8_t c = (uint8_t)str[i];
        if (c < 0x80) {
            i++;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < len && ((uint8_t)str[i + 1] & 0xC0) == 0x80) {
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < len && ((uint8_t)str[i + 1] & 0xC0) == 0x80 && ((uint8_t)str[i + 2] & 0xC0) == 0x80) {
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < len && ((uint8_t)str[i + 1] & 0xC0) == 0x80 && ((uint8_t)str[i + 2] & 0xC0) == 0x80 && ((uint8_t)str[i + 3] & 0xC0) == 0x80) {
            i += 4;
        } else {
            validUtf8 = false;
            break;
        }
    }
    if (validUtf8) return str;

    // String has isolated Latin-1 / ISO-8859-1 bytes; transcode each non-ASCII byte to UTF-8
    String out = "";
    out.reserve(len * 2);
    for (size_t j = 0; j < len; ++j) {
        uint8_t b = (uint8_t)str[j];
        if (b < 0x80) {
            out += (char)b;
        } else {
            out += (char)(0xC0 | (b >> 6));
            out += (char)(0x80 | (b & 0x3F));
        }
    }
    return out;
}

// LinkPlay often hex-encodes metadata strings (Title, Artist, Album)
static String decodeHexString(const char* hex) {
    if (!hex || strlen(hex) == 0) return "";
    size_t len = strlen(hex);
    // Check if it looks like a valid hex string (even length, hex chars)
    bool isHex = (len % 2 == 0);
    for (size_t i = 0; i < len && isHex; ++i) {
        char c = hex[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            isHex = false;
        }
    }
    if (!isHex || len < 2) {
        return toValidUtf8(String(hex)); // Not hex encoded, return as valid UTF-8 string
    }

    String decoded = "";
    decoded.reserve(len / 2);
    for (size_t i = 0; i < len; i += 2) {
        char byteChars[3] = { hex[i], hex[i + 1], '\0' };
        char byteVal = (char)strtol(byteChars, nullptr, 16);
        if (byteVal != 0) {
            decoded += byteVal;
        }
    }
    return toValidUtf8(decoded);
}

// URL-encoder for external API parameters
static String urlEncode(const char* str) {
    if (!str) return "";
    String encoded = "";
    size_t len = strlen(str);
    for (size_t i = 0; i < len; ++i) {
        char c = str[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += "+";
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
            encoded += buf;
        }
    }
    return encoded;
}

// Simple XML tag extractor for UPnP description.xml
static String extractXmlTag(const String& xml, const String& tag) {
    String openTag = "<" + tag + ">";
    String closeTag = "</" + tag + ">";
    int startIdx = xml.indexOf(openTag);
    if (startIdx < 0) return "";
    startIdx += openTag.length();
    int endIdx = xml.indexOf(closeTag, startIdx);
    if (endIdx < 0) return "";
    return xml.substring(startIdx, endIdx);
}

NetworkManager::NetworkManager()
    : _wifiConnected(false),
      _lastWiFiCheck(0),
      _lastStatusPoll(0),
      _lastConfigPoll(0),
      _lastMetaPoll(0),
      _lastSSDPBroadcast(0),
      _lastVolumeSent(0),
      _pendingVolume(0),
      _volumePending(false),
      _isScanning(false),
      _scanCurrentHost(1),
      _lastScanStepTime(0),
      _hasActiveDevice(false),
      _initialPresetsFetched(false),
      _lastKnownTrackTitle(""),
      _lastLyricsTitle(""),
      _lastLyricsArtist(""),
      _metaResolved(false),
      _cachedTrackDuration_ms(0),
      _activeMode(0),
      _activePlicurr(0),
      _activePlicount(0),
      _persistentIp(""),
      _persistentHttpConfigured(false),
      _wifiScanning(false),
      _wifiConnecting(false),
      _wifiConnectStart(0),
      _pendingConnectSsid(""),
      _pendingConnectPass("")
{
    _deviceMux = portMUX_INITIALIZER_UNLOCKED;
    memset(&_activeDevice, 0, sizeof(_activeDevice));
    memset(&_lastPlayerState, 0, sizeof(_lastPlayerState));
    memset(&_deviceList, 0, sizeof(_deviceList));
}

void NetworkManager::resetPersistentHttp() {
    _persistentHttp.end();
    _persistentClient.stop();
    _persistentIp = "";
    _persistentHttpConfigured = false;
}

int NetworkManager::executeApiGet(const String& cmd, String& outPayload) {
    if (!_hasActiveDevice || !_wifiConnected) return -1;

    String targetIp = _activeDevice.ip;
    String path = "/httpapi.asp?command=" + cmd;
    String fullUrl = "https://" + targetIp + path;

    // Reset session if target IP changed or client not configured yet
    if (_persistentIp != targetIp || !_persistentHttpConfigured) {
        resetPersistentHttp();
        _persistentClient.setInsecure();
        _persistentHttp.setReuse(true);
        _persistentHttp.setTimeout(HTTP_REQUEST_TIMEOUT_MS);
        _persistentIp = targetIp;
        _persistentHttpConfigured = true;
    }

    if (!_persistentHttp.connected()) {
        if (!_persistentHttp.begin(_persistentClient, fullUrl)) {
            log_e("HTTPS begin failed for: %s", fullUrl.c_str());
            _persistentClient.stop();
            return -1;
        }
    } else {
        _persistentHttp.setURL(path);
    }

    int code = _persistentHttp.GET();

    // If socket dropped by server (e.g. keep-alive timeout), reconnect once
    if (code <= 0) {
        log_w("Keep-alive socket dropped (%d), reconnecting to %s...", code, targetIp.c_str());
        resetPersistentHttp();
        _persistentClient.setInsecure();
        _persistentHttp.setReuse(true);
        _persistentHttp.setTimeout(HTTP_REQUEST_TIMEOUT_MS);
        _persistentIp = targetIp;
        _persistentHttpConfigured = true;

        if (_persistentHttp.begin(_persistentClient, fullUrl)) {
            code = _persistentHttp.GET();
        }
    }

    if (code == HTTP_CODE_OK) {
        outPayload = _persistentHttp.getString();
    } else {
        log_w("HTTP GET command '%s' returned %d", cmd.c_str(), code);
    }

    return code;
}

void NetworkManager::init() {
    log_i("Initializing NetworkManager...");

    // Initialize Preferences
    prefs.begin("wiimremote", false);
    loadSavedDevice();

    // Start Wi-Fi in station mode
    WiFi.mode(WIFI_STA);

    bool setupDone = prefs.getBool("user_setup_done", false);
    String savedSsid = setupDone ? prefs.getString("wifi_ssid", "") : "";
    String savedPass = setupDone ? prefs.getString("wifi_pass", "") : "";

    if (savedSsid.length() > 0) {
        log_i("Connecting to saved Wi-Fi SSID: %s", savedSsid.c_str());
        WiFi.setSleep(WIFI_PS_MIN_MODEM);
        _wifiConnecting = true;
        _wifiConnectStart = millis();
        _pendingConnectSsid = savedSsid;
        _pendingConnectPass = savedPass;
        WiFi.begin(savedSsid.c_str(), savedPass.c_str());
    } else {
        log_w("No user-configured Wi-Fi credentials. Setup required!");
        prefs.remove("wifi_ssid");
        prefs.remove("wifi_pass");
        prefs.remove("user_setup_done");
        WiFi.disconnect(true, true);

        UiEvent evt;
        evt.type = UI_EVT_WIFI_SETUP_REQUIRED;
        xQueueSend(xQueueUiState, &evt, 0);
        startWiFiScan();
    }

    _lastWiFiCheck = millis();
}

void NetworkManager::loadSavedDevice() {
    String savedIp = prefs.getString("active_ip", "");
    String savedUuid = prefs.getString("active_uuid", "");
    String savedName = prefs.getString("active_name", "");

    if (savedIp.length() > 0) {
        strncpy(_activeDevice.ip, savedIp.c_str(), sizeof(_activeDevice.ip) - 1);
        strncpy(_activeDevice.uuid, savedUuid.c_str(), sizeof(_activeDevice.uuid) - 1);
        strncpy(_activeDevice.name, savedName.c_str(), sizeof(_activeDevice.name) - 1);
        _activeDevice.is_active = true;
        _activeDevice.is_fixed_volume = prefs.getBool("fixed_vol", false);
        _hasActiveDevice = true;
        log_i("Loaded saved active device: %s (%s, Fixed=%d)", _activeDevice.name, _activeDevice.ip, _activeDevice.is_fixed_volume ? 1 : 0);
    }
}

void NetworkManager::saveActiveDevice(const WiiMDevice& dev) {
    prefs.putString("active_ip", dev.ip);
    prefs.putString("active_uuid", dev.uuid);
    prefs.putString("active_name", dev.name);
    prefs.putBool("fixed_vol", dev.is_fixed_volume);
    log_i("Saved active device to NVS: %s (%s, Fixed=%d)", dev.name, dev.ip, dev.is_fixed_volume ? 1 : 0);
}

void NetworkManager::startWiFiScan() {
    if (_wifiScanning) return;
    _wifiScanning = true;
    log_i("Starting synchronous Wi-Fi network scan on Core 0 (current status: %d)...", WiFi.status());

    bool wasConnected = (WiFi.status() == WL_CONNECTED);
    if (wasConnected) {
        log_i("Temporarily disconnecting from AP for clean channel scan...");
        WiFi.disconnect(false, false);
        unsigned long waitStart = millis();
        while (WiFi.status() == WL_CONNECTED && millis() - waitStart < 1000) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(WIFI_PS_NONE);
    WiFi.scanDelete();

    int16_t n = WiFi.scanNetworks(false, false, false, 150);
    log_i("WiFi.scanNetworks returned: %d", n);

    if (n < 0) {
        log_w("Scan returned %d, retrying after 200ms...", n);
        vTaskDelay(pdMS_TO_TICKS(200));
        WiFi.scanDelete();
        n = WiFi.scanNetworks(false, false, false, 200);
        log_i("Retry scan returned: %d", n);
    }

    _wifiScanning = false;

    WiFiScanList* pScan = (WiFiScanList*)malloc(sizeof(WiFiScanList));
    if (pScan) {
        pScan->count = 0;
        if (n > 0) {
            for (int16_t i = 0; i < n && pScan->count < 20; ++i) {
                String ssid = WiFi.SSID(i);
                if (ssid.length() == 0) continue;

                bool dup = false;
                for (uint8_t j = 0; j < pScan->count; ++j) {
                    if (strcmp(pScan->networks[j].ssid, ssid.c_str()) == 0) {
                        dup = true;
                        break;
                    }
                }
                if (dup) continue;

                WiFiNetworkInfo& net = pScan->networks[pScan->count++];
                strncpy(net.ssid, ssid.c_str(), sizeof(net.ssid) - 1);
                net.ssid[sizeof(net.ssid) - 1] = '\0';
                net.rssi = WiFi.RSSI(i);
                net.is_open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
                log_i("  [%d] SSID: '%s', RSSI: %d dBm, Open: %d", pScan->count, net.ssid, net.rssi, net.is_open ? 1 : 0);
            }
        }
        UiEvent evt;
        evt.type = UI_EVT_WIFI_SCAN_RESULT;
        evt.data.wifi_scan = pScan;
        xQueueSend(xQueueUiState, &evt, 0);
    }
    WiFi.scanDelete();
}

void NetworkManager::connectWiFi(const char* ssid, const char* pass) {
    if (!ssid || strlen(ssid) == 0) return;
    log_i("Connecting to user-selected Wi-Fi: %s", ssid);
    _wifiConnecting = true;
    _wifiConnectStart = millis();
    _pendingConnectSsid = ssid;
    _pendingConnectPass = pass ? pass : "";

    WiFi.disconnect(false, false);
    WiFi.begin(ssid, pass);
}

void NetworkManager::forgetWiFi() {
    log_i("Forgetting saved Wi-Fi credentials");
    prefs.remove("wifi_ssid");
    prefs.remove("wifi_pass");
    prefs.remove("user_setup_done");
    _wifiConnected = false;
    _pendingConnectSsid = "";
    _pendingConnectPass = "";
    WiFi.disconnect(false, true);

    unsigned long waitStart = millis();
    while (WiFi.status() == WL_CONNECTED && millis() - waitStart < 1000) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    UiEvent evtStatus;
    evtStatus.type = UI_EVT_WIFI_STATUS;
    evtStatus.data.wifi.connected = false;
    evtStatus.data.wifi.rssi = -100;
    evtStatus.data.wifi.ip[0] = '\0';
    evtStatus.data.wifi.ssid[0] = '\0';
    xQueueSend(xQueueUiState, &evtStatus, 0);

    UiEvent evt;
    evt.type = UI_EVT_WIFI_SETUP_REQUIRED;
    xQueueSend(xQueueUiState, &evt, 0);

    startWiFiScan();
}

void NetworkManager::handleWiFi() {
    unsigned long now = millis();
    wl_status_t status = WiFi.status();

    if (status == WL_CONNECTED) {
        if (!_wifiConnected) {
            _wifiConnected = true;
            bool wasUserConnecting = (_pendingConnectSsid.length() > 0);
            _wifiConnecting = false;
            log_i("Wi-Fi Connected! IP: %s, RSSI: %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());

            // Save to NVS if newly connected via pending credentials
            if (wasUserConnecting) {
                prefs.putString("wifi_ssid", _pendingConnectSsid);
                prefs.putString("wifi_pass", _pendingConnectPass);
                prefs.putBool("user_setup_done", true);
                log_i("Saved Wi-Fi credentials to NVS: %s", _pendingConnectSsid.c_str());
                _pendingConnectSsid = "";
                _pendingConnectPass = "";

                // Notify UI of connection success to close the modal
                UiEvent evtSuccess;
                evtSuccess.type = UI_EVT_WIFI_CONNECT_SUCCESS;
                strncpy(evtSuccess.data.wifi.ip, WiFi.localIP().toString().c_str(), sizeof(evtSuccess.data.wifi.ip) - 1);
                xQueueSend(xQueueUiState, &evtSuccess, 0);
            }

            // Initialize UDP listener for SSDP
            udpSSDP.beginMulticast(IPAddress(239, 255, 255, 250), SSDP_PORT);

            // Post Wi-Fi status event to UI task
            UiEvent evt;
            evt.type = UI_EVT_WIFI_STATUS;
            evt.data.wifi.connected = true;
            evt.data.wifi.rssi = WiFi.RSSI();
            strncpy(evt.data.wifi.ip, WiFi.localIP().toString().c_str(), sizeof(evt.data.wifi.ip) - 1);
            strncpy(evt.data.wifi.ssid, WiFi.SSID().c_str(), sizeof(evt.data.wifi.ssid) - 1);
            evt.data.wifi.ssid[sizeof(evt.data.wifi.ssid) - 1] = '\0';
            xQueueSend(xQueueUiState, &evt, 0);

            // Start discovery immediately
            triggerRescan();
        } else if (now - _lastWiFiCheck >= 5000) {
            // Periodic RSSI refresh
            _lastWiFiCheck = now;
            UiEvent evt;
            evt.type = UI_EVT_WIFI_STATUS;
            evt.data.wifi.connected = true;
            evt.data.wifi.rssi = WiFi.RSSI();
            strncpy(evt.data.wifi.ip, WiFi.localIP().toString().c_str(), sizeof(evt.data.wifi.ip) - 1);
            strncpy(evt.data.wifi.ssid, WiFi.SSID().c_str(), sizeof(evt.data.wifi.ssid) - 1);
            evt.data.wifi.ssid[sizeof(evt.data.wifi.ssid) - 1] = '\0';
            xQueueSend(xQueueUiState, &evt, 0);
        }
    } else {
        if (_wifiConnected) {
            _wifiConnected = false;
            resetPersistentHttp();
            log_w("Wi-Fi Connection Lost! Attempting reconnect...");
            UiEvent evt;
            evt.type = UI_EVT_WIFI_STATUS;
            evt.data.wifi.connected = false;
            evt.data.wifi.rssi = -100;
            evt.data.wifi.ip[0] = '\0';
            evt.data.wifi.ssid[0] = '\0';
            xQueueSend(xQueueUiState, &evt, 0);
        }

        if (_wifiConnecting) {
            if (now - _wifiConnectStart >= WIFI_CONNECT_TIMEOUT_MS) {
                _wifiConnecting = false;
                log_w("Wi-Fi connection to %s timed out!", _pendingConnectSsid.c_str());
                _pendingConnectSsid = "";
                _pendingConnectPass = "";
                UiEvent evt;
                evt.type = UI_EVT_WIFI_CONNECT_FAILED;
                xQueueSend(xQueueUiState, &evt, 0);
                startWiFiScan();
            }
        } else {
            bool setupDone = prefs.getBool("user_setup_done", false);
            String savedSsid = setupDone ? prefs.getString("wifi_ssid", "") : "";
            if (savedSsid.length() > 0 && now - _lastWiFiCheck >= WIFI_RECONNECT_INTERVAL_MS) {
                _lastWiFiCheck = now;
                log_i("Reconnecting to Wi-Fi...");
                WiFi.reconnect();
            }
        }
    }
}

static const char* known_candidate_ips[] = {
    "192.168.50.27",  // WiiM Office
    "192.168.50.80",  // WiiM Garage
    "192.168.50.183", // WiiM L1090
    "192.168.50.20"   // WiiM L1230
};

void NetworkManager::triggerRescan() {
    if (!_wifiConnected) return;
    log_i("Triggering device rescan (Priority IPs + SSDP + Subnet)...");

    UiEvent evt;
    evt.type = UI_EVT_SCAN_STATUS;
    evt.data.scan.is_scanning = true;
    xQueueSend(xQueueUiState, &evt, 0);

    // 1. Immediately probe known candidate IPs first (fast discovery in <100ms)
    for (size_t i = 0; i < sizeof(known_candidate_ips) / sizeof(known_candidate_ips[0]); ++i) {
        WiiMDevice dev;
        if (queryDeviceStatus(known_candidate_ips[i], &dev)) {
            addOrUpdateDevice(dev);
        }
        vTaskDelay(pdMS_TO_TICKS(20)); // Yield to prevent WDT timeout
    }

    _isScanning = true;
    _scanCurrentHost = 1;

    // 2. Broadcast SSDP discovery
    sendSSDPQuery();

    UiEvent doneEvt;
    doneEvt.type = UI_EVT_SCAN_STATUS;
    doneEvt.data.scan.is_scanning = false;
    xQueueSend(xQueueUiState, &doneEvt, 0);
}

void NetworkManager::sendSSDPQuery() {
    if (!_wifiConnected) return;

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

    // SSDP M-SEARCH for all devices
    const char* ssdp_msearch_all =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: ssdp:all\r\n\r\n";

    IPAddress mcastIP(239, 255, 255, 250);
    udpSSDP.beginPacket(mcastIP, SSDP_PORT);
    udpSSDP.write((const uint8_t*)ssdp_msearch_renderer, strlen(ssdp_msearch_renderer));
    udpSSDP.endPacket();

    udpSSDP.beginPacket(mcastIP, SSDP_PORT);
    udpSSDP.write((const uint8_t*)ssdp_msearch_wiimu, strlen(ssdp_msearch_wiimu));
    udpSSDP.endPacket();

    udpSSDP.beginPacket(mcastIP, SSDP_PORT);
    udpSSDP.write((const uint8_t*)ssdp_msearch_all, strlen(ssdp_msearch_all));
    udpSSDP.endPacket();

    _lastSSDPBroadcast = millis();
}

void NetworkManager::processSSDPPackets() {
    int packetSize;
    while ((packetSize = udpSSDP.parsePacket()) > 0) {
        char packetBuffer[1024];
        int len = udpSSDP.read(packetBuffer, sizeof(packetBuffer) - 1);
        if (len <= 0) continue;
        packetBuffer[len] = '\0';

        IPAddress remoteIP = udpSSDP.remoteIP();
        String ipStr = remoteIP.toString();

        // Check if packet contains LinkPlay or MediaRenderer headers
        String packetContent(packetBuffer);
        if (packetContent.indexOf("MediaRenderer") >= 0 ||
            packetContent.indexOf("Linkplay") >= 0 ||
            packetContent.indexOf("LinkPlay") >= 0 ||
            packetContent.indexOf("WiFiAudio") >= 0 ||
            packetContent.indexOf("WiiM") >= 0 ||
            packetContent.indexOf("wiim") >= 0) {

            // Check if LOCATION header gives an IP
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

            // Deduplicate: If streamer is already known in list, ignore duplicate packets
            bool alreadyKnown = false;
            portENTER_CRITICAL(&_deviceMux);
            for (uint8_t i = 0; i < _deviceList.count; ++i) {
                if (strcmp(_deviceList.devices[i].ip, ipStr.c_str()) == 0) {
                    alreadyKnown = true;
                    break;
                }
            }
            portEXIT_CRITICAL(&_deviceMux);

            if (alreadyKnown) continue;

            WiiMDevice dev;
            if (queryDeviceStatus(ipStr.c_str(), &dev)) {
                addOrUpdateDevice(dev);
            }
        }
    }
}

void NetworkManager::runSubnetScanStep() {
    // Subnet scan disabled to preserve lwIP socket descriptors (fd 0..3) for HTTPS polling and control.
    // Discovery is handled reliably by candidate IP probing and SSDP M-SEARCH broadcasts.
    _isScanning = false;
}

bool NetworkManager::queryDeviceStatus(const char* ip, WiiMDevice* outDevice) {
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
                    devName = extractXmlTag(payload, "friendlyName");
                    uuid = extractXmlTag(payload, "UDN");
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

void NetworkManager::addOrUpdateDevice(const WiiMDevice& dev) {
    portENTER_CRITICAL(&_deviceMux);

    bool exists = false;
    bool changed = false;
    for (uint8_t i = 0; i < _deviceList.count; ++i) {
        if (strcmp(_deviceList.devices[i].uuid, dev.uuid) == 0 ||
            strcmp(_deviceList.devices[i].ip, dev.ip) == 0) {
            exists = true;
            if (strcmp(_deviceList.devices[i].name, dev.name) != 0 ||
                strcmp(_deviceList.devices[i].ip, dev.ip) != 0) {
                strncpy(_deviceList.devices[i].name, dev.name, sizeof(_deviceList.devices[i].name) - 1);
                strncpy(_deviceList.devices[i].ip, dev.ip, sizeof(_deviceList.devices[i].ip) - 1);
                changed = true;
            }
            break;
        }
    }

    if (!exists && _deviceList.count < MAX_DISCOVERED_DEVICES) {
        _deviceList.devices[_deviceList.count] = dev;
        _deviceList.count++;
        changed = true;
    }

    // Auto-select if no active device, or if this matches saved device
    bool newActive = false;
    if (!_hasActiveDevice) {
        _activeDevice = dev;
        _activeDevice.is_active = true;
        _hasActiveDevice = true;
        newActive = true;
        changed = true;
    } else if (strcmp(_activeDevice.uuid, dev.uuid) == 0) {
        if (strcmp(_activeDevice.ip, dev.ip) != 0 || strcmp(_activeDevice.name, dev.name) != 0) {
            _activeDevice = dev;
            _activeDevice.is_active = true;
            changed = true;
        }
    }

    // Update is_active flag in list
    for (uint8_t i = 0; i < _deviceList.count; ++i) {
        _deviceList.devices[i].is_active = (strcmp(_deviceList.devices[i].ip, _activeDevice.ip) == 0);
    }

    DeviceList listCopy = _deviceList;
    portEXIT_CRITICAL(&_deviceMux);

    if (newActive) {
        saveActiveDevice(_activeDevice);
    }

    // Broadcast updated device list to UI only if list actually changed
    if (changed) {
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
}

void NetworkManager::selectDevice(const char* ip) {
    if (!ip || strlen(ip) == 0) return;

    portENTER_CRITICAL(&_deviceMux);
    for (uint8_t i = 0; i < _deviceList.count; ++i) {
        if (strcmp(_deviceList.devices[i].ip, ip) == 0) {
            _activeDevice = _deviceList.devices[i];
            _activeDevice.is_active = true;
            _hasActiveDevice = true;
            break;
        }
    }
    // Update active flags
    for (uint8_t i = 0; i < _deviceList.count; ++i) {
        _deviceList.devices[i].is_active = (strcmp(_deviceList.devices[i].ip, _activeDevice.ip) == 0);
    }
    DeviceList listCopy = _deviceList;
    portEXIT_CRITICAL(&_deviceMux);

    if (_hasActiveDevice) {
        resetPersistentHttp();
        saveActiveDevice(_activeDevice);
        log_i("Active device switched to: %s (%s)", _activeDevice.name, _activeDevice.ip);

        // Notify UI of updated device selection
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

        // Reset metadata and query new active device
        _lastKnownTrackTitle = "";
        _metaResolved = false;
        fetchTrackMeta();
        fetchPresetInfo();
        pollActiveDevice();
        fetchDeviceConfig();
    }
}

bool NetworkManager::getActiveDevice(WiiMDevice* dev) {
    if (!_hasActiveDevice) return false;
    if (dev) *dev = _activeDevice;
    return true;
}

bool NetworkManager::sendHttpCommand(const String& cmd) {
    if (!_hasActiveDevice || !_wifiConnected) return false;
    String payload;
    int httpCode = executeApiGet(cmd, payload);
    return (httpCode == HTTP_CODE_OK);
}

void NetworkManager::pollActiveDevice() {
    if (!_hasActiveDevice || !_wifiConnected) return;

    String payload;
    int httpCode = executeApiGet("getPlayerStatus", payload);
    if (httpCode != HTTP_CODE_OK || payload.length() == 0) {
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) return;

    UiEvent evt;
    evt.type = UI_EVT_PLAYER_STATE;

    // Parse status
    const char* statusStr = doc["status"] | "stop";
    if (strcmp(statusStr, "play") == 0) {
        evt.data.player.state = PLAY_STATE_PLAYING;
    } else if (strcmp(statusStr, "pause") == 0) {
        evt.data.player.state = PLAY_STATE_PAUSED;
    } else {
        evt.data.player.state = PLAY_STATE_STOPPED;
    }

    // Volume & Mute
    const char* volStr = doc["vol"] | "0";
    evt.data.player.volume = (uint8_t)atoi(volStr);
    const char* muteStr = doc["mute"] | "0";
    evt.data.player.mute = (strcmp(muteStr, "1") == 0 || atoi(muteStr) == 1);
    evt.data.player.is_fixed_volume = _activeDevice.is_fixed_volume;

    // Track duration and position
    const char* totlenStr = doc["totlen"] | "0";
    const char* curposStr = doc["curpos"] | "0";
    uint32_t totlen = strtoul(totlenStr, nullptr, 10);
    uint32_t curpos = strtoul(curposStr, nullptr, 10);

    // If totlen is given in seconds (< 10000 for normal song durations), convert to ms
    if (totlen > 0 && totlen < 10000) {
        totlen *= 1000;
    }

    // Fallback: If LinkPlay reports totlen == 0 (e.g. Amazon Music / Prime streams),
    // use the cached duration obtained from UPnP AVTransport ONLY when actively playing
    if (totlen == 0 && _cachedTrackDuration_ms > 0 && evt.data.player.state == PLAY_STATE_PLAYING) {
        totlen = _cachedTrackDuration_ms;
    }

    // Atomic validation: Curpos cannot legitimately exceed totlen in normal playback.
    // If curpos > totlen, this is a desynchronized transition state between old position and new length.
    // Suppress both until the new track's playback is properly synchronized.
    if (totlen > 0 && curpos > totlen) {
        totlen = 0;
        curpos = 0;
    } else if (evt.data.player.state != PLAY_STATE_PLAYING && totlen == 0) {
        curpos = 0;
    }

    evt.data.player.totlen_ms = totlen;
    evt.data.player.curpos_ms = curpos;

    // Vendor / Service Provider
    const char* rawVendor = doc["vendor"] | "";
    static const struct { const char* raw; const char* clean; } vendor_map[] = {
        {"Prime", "Amazon Music"},
        {"Spotify", "Spotify"},
        {"Tidal", "Tidal"},
        {"Qobuz", "Qobuz"},
        {"TuneIn", "TuneIn"},
        {"Deezer", "Deezer"}
    };
    const char* cleanVendor = nullptr;
    for (size_t i = 0; i < sizeof(vendor_map) / sizeof(vendor_map[0]); ++i) {
        if (strcasecmp(rawVendor, vendor_map[i].raw) == 0) {
            cleanVendor = vendor_map[i].clean;
            break;
        }
    }
    if (!cleanVendor) {
        cleanVendor = (strlen(rawVendor) > 0) ? rawVendor : "WiiM";
    }
    strncpy(evt.data.player.vendor, cleanVendor, sizeof(evt.data.player.vendor) - 1);

    // Operating Mode
    const char* modeStr = doc["mode"] | "0";
    _activeMode = (uint8_t)atoi(modeStr);

    // Queue / Playlist Track Count
    const char* plicurrStr = doc["plicurr"] | "0";
    const char* plicountStr = doc["plicount"] | "0";
    _activePlicurr = (uint16_t)atoi(plicurrStr);
    _activePlicount = (uint16_t)atoi(plicountStr);
    evt.data.player.track_num = _activePlicurr;
    evt.data.player.track_total = _activePlicount;

    // Stream / Audio Format
    const char* rate = doc["rate"] | doc["SampleRate"] | doc["samplerate"] | "";
    const char* bitDepth = doc["bitDepth"] | doc["bitdepth"] | "";
    const char* format = doc["format"] | doc["Type"] | doc["type"] | doc["vendor"] | "FLAC";

    strncpy(evt.data.player.stream.format, format, sizeof(evt.data.player.stream.format) - 1);
    evt.data.player.stream.sample_rate = atoi(rate);
    evt.data.player.stream.bit_depth = (uint8_t)atoi(bitDepth);

    _lastPlayerState = evt.data.player;
    xQueueSend(xQueueUiState, &evt, 0);

    // Track metadata decoding (Title and Artist in getPlayerStatus are hex encoded)
    const char* rawTitle = doc["Title"] | doc["title"] | "";
    const char* rawArtist = doc["Artist"] | doc["artist"] | "";
    if (strlen(rawTitle) > 0) {
        String decTitle = decodeHexString(rawTitle);
        if (_lastKnownTrackTitle != decTitle) {
            _lastKnownTrackTitle = decTitle;
            _cachedTrackDuration_ms = 0;
            _metaResolved = false;
            if (fetchTrackMeta()) {
                _metaResolved = true;
            }
            fetchUpnpTrackDuration();
        } else if (!_metaResolved && evt.data.player.state == PLAY_STATE_PLAYING) {
            // Track is playing but resolution wasn't ready on the first instant (buffering).
            // Naturally complete it on this normal status tick without extra timers.
            if (fetchTrackMeta()) {
                _metaResolved = true;
            }
        }
    } else {
        _lastKnownTrackTitle = "";
        _metaResolved = false;
    }
}

bool NetworkManager::fetchTrackMeta() {
    if (!_hasActiveDevice || !_wifiConnected) return false;

    String payload;
    int httpCode = executeApiGet("getMetaInfo", payload);
    if (httpCode != HTTP_CODE_OK || payload.length() == 0) {
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) return false;

    TrackMeta* pMeta = (TrackMeta*)malloc(sizeof(TrackMeta));
    if (!pMeta) return false;
    memset(pMeta, 0, sizeof(TrackMeta));

    const char* rawTitle = doc["metaData"]["title"] | doc["Title"] | doc["title"] | "Unknown Title";
    const char* rawArtist = doc["metaData"]["artist"] | doc["Artist"] | doc["artist"] | "Unknown Artist";
    const char* rawAlbum = doc["metaData"]["album"] | doc["Album"] | doc["album"] | "Unknown Album";
    String decTitle = decodeHexString(rawTitle);
    String decArtist = decodeHexString(rawArtist);
    String decAlbum = decodeHexString(rawAlbum);

    strncpy(pMeta->title, decTitle.c_str(), sizeof(pMeta->title) - 1);
    strncpy(pMeta->artist, decArtist.c_str(), sizeof(pMeta->artist) - 1);
    strncpy(pMeta->album, decAlbum.c_str(), sizeof(pMeta->album) - 1);

    // Robust parsing of sampleRate and bitDepth (handles both integer and string variants)
    uint32_t sampleRate = 0;
    JsonVariant vRate = doc["metaData"]["sampleRate"];
    if (vRate.isNull()) vRate = doc["sampleRate"];
    if (vRate.is<uint32_t>()) {
        sampleRate = vRate.as<uint32_t>();
    } else if (vRate.is<const char*>()) {
        sampleRate = strtoul(vRate.as<const char*>(), nullptr, 10);
    }

    uint8_t bitDepth = 0;
    JsonVariant vDepth = doc["metaData"]["bitDepth"];
    if (vDepth.isNull()) vDepth = doc["bitDepth"];
    if (vDepth.is<uint8_t>()) {
        bitDepth = vDepth.as<uint8_t>();
    } else if (vDepth.is<const char*>()) {
        bitDepth = (uint8_t)atoi(vDepth.as<const char*>());
    }

    pMeta->sample_rate = sampleRate;
    pMeta->bit_depth = bitDepth;

    UiEvent evt;
    evt.type = UI_EVT_META_UPDATED;
    evt.data.meta = pMeta;

    if (xQueueSend(xQueueUiState, &evt, 0) != pdTRUE) {
        free(pMeta);
    }

    // Trigger lyrics fetch if track title or artist changed
    if (_lastLyricsTitle != decTitle || _lastLyricsArtist != decArtist) {
        _lastLyricsTitle = decTitle;
        _lastLyricsArtist = decArtist;
        fetchLyrics(decTitle.c_str(), decArtist.c_str());
    }

    return (sampleRate > 0);
}

void NetworkManager::fetchLyrics(const char* title, const char* artist) {
    if (!title || strlen(title) == 0 || strcmp(title, "Ready for stream") == 0 || strcmp(title, "No Track Playing") == 0) {
        return;
    }

    // 1. Send loading status to UI
    LyricsInfo* pLoading = (LyricsInfo*)malloc(sizeof(LyricsInfo));
    if (pLoading) {
        memset(pLoading, 0, sizeof(LyricsInfo));
        strncpy(pLoading->title, title, sizeof(pLoading->title) - 1);
        strncpy(pLoading->artist, (artist ? artist : ""), sizeof(pLoading->artist) - 1);
        pLoading->text = strdup("Fetching lyrics from LRCLIB...");
        pLoading->is_loading = true;
        UiEvent evt;
        evt.type = UI_EVT_LYRICS_UPDATED;
        evt.data.lyrics = pLoading;
        if (xQueueSend(xQueueUiState, &evt, 0) != pdTRUE) {
            if (pLoading->text) free(pLoading->text);
            free(pLoading);
        }
    }

    // 2. Query LRCLIB
    String cleanTitle = title;
    String cleanArtist = (artist ? artist : "");

    auto queryLrclib = [](const String& t, const String& a, String& outLyrics) -> bool {
        WiFiClientSecure secureClient;
        secureClient.setInsecure();
        secureClient.setTimeout(4000);

        HTTPClient http;
        String url = "https://lrclib.net/api/get?track_name=" + urlEncode(t.c_str());
        if (a.length() > 0) {
            url += "&artist_name=" + urlEncode(a.c_str());
        }

        http.begin(secureClient, url);
        http.setUserAgent("WiiMRemote/1.0 (ESP32-S3)");
        int code = http.GET();
        if (code == 200) {
            String payload = http.getString();
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload);
            if (!err) {
                const char* plain = doc["plainLyrics"];
                if (plain && strlen(plain) > 0) {
                    outLyrics = plain;
                    http.end();
                    return true;
                }
            }
        }
        http.end();
        return false;
    };

    String lyricsText;
    bool found = queryLrclib(cleanTitle, cleanArtist, lyricsText);

    // If not found and title has parenthesized/bracketed additions, e.g. "Song (Remastered 2011)", try stripped title
    if (!found && (cleanTitle.indexOf('(') >= 0 || cleanTitle.indexOf('-') >= 0)) {
        String strippedTitle = cleanTitle;
        int p = strippedTitle.indexOf('(');
        if (p > 0) strippedTitle = strippedTitle.substring(0, p);
        p = strippedTitle.indexOf('-');
        if (p > 0) strippedTitle = strippedTitle.substring(0, p);
        strippedTitle.trim();
        if (strippedTitle.length() > 0 && strippedTitle != cleanTitle) {
            found = queryLrclib(strippedTitle, cleanArtist, lyricsText);
        }
    }

    // 3. Post result to UI
    LyricsInfo* pResult = (LyricsInfo*)malloc(sizeof(LyricsInfo));
    if (pResult) {
        memset(pResult, 0, sizeof(LyricsInfo));
        strncpy(pResult->title, title, sizeof(pResult->title) - 1);
        strncpy(pResult->artist, (artist ? artist : ""), sizeof(pResult->artist) - 1);
        pResult->is_loading = false;
        if (found && lyricsText.length() > 0) {
            pResult->text = strdup(lyricsText.c_str());
        } else {
            pResult->text = strdup("No lyrics found for this track.");
        }
        UiEvent evt;
        evt.type = UI_EVT_LYRICS_UPDATED;
        evt.data.lyrics = pResult;
        if (xQueueSend(xQueueUiState, &evt, 0) != pdTRUE) {
            if (pResult->text) free(pResult->text);
            free(pResult);
        }
    }
}


void NetworkManager::fetchUpnpTrackDuration() {
    if (!_hasActiveDevice || !_wifiConnected) return;

    HTTPClient http;
    String url = "http://" + String(_activeDevice.ip) + ":49152/upnp/control/rendertransport1";
    http.setTimeout(1500);

    if (!http.begin(url)) {
        return;
    }

    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    http.addHeader("SOAPAction", "\"urn:schemas-upnp-org:service:AVTransport:1#GetPositionInfo\"");

    const char* soapBody = "<?xml version=\"1.0\"?>"
                           "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
                           "<s:Body><u:GetPositionInfo xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
                           "<InstanceID>0</InstanceID>"
                           "</u:GetPositionInfo></s:Body></s:Envelope>";

    int httpCode = http.POST(soapBody);
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        int start = payload.indexOf("<TrackDuration>");
        if (start >= 0) {
            start += 15;
            int end = payload.indexOf("</TrackDuration>", start);
            if (end > start) {
                String durStr = payload.substring(start, end);
                int h = 0, m = 0, s = 0;
                if (sscanf(durStr.c_str(), "%d:%d:%d", &h, &m, &s) == 3) {
                    _cachedTrackDuration_ms = (uint32_t)(h * 3600 + m * 60 + s) * 1000;
                    log_i("UPnP TrackDuration: %s (%u ms)", durStr.c_str(), _cachedTrackDuration_ms);
                }
            }
        }
    }
    http.end();
}

void NetworkManager::fetchPresetInfo() {
    if (!_hasActiveDevice || !_wifiConnected) return;

    String payload;
    int httpCode = executeApiGet("getPresetInfo", payload);
    if (httpCode != HTTP_CODE_OK || payload.length() == 0) {
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);

    PresetList* pPresets = (PresetList*)malloc(sizeof(PresetList));
    if (!pPresets) return;
    memset(pPresets, 0, sizeof(PresetList));
    pPresets->count = MAX_PRESETS;

    // Initialize defaults 1..12
    for (uint8_t i = 0; i < MAX_PRESETS; ++i) {
        pPresets->items[i].index = i + 1;
        snprintf(pPresets->items[i].name, sizeof(pPresets->items[i].name), "Preset %d", i + 1);
    }

    if (!err) {
        JsonArray array = doc["preset_list"].as<JsonArray>();
        if (!array.isNull()) {
            uint8_t idx = 0;
            for (JsonObject item : array) {
                if (idx >= MAX_PRESETS) break;
                int pNum = item["number"] | item["preset_key"] | (idx + 1);
                const char* pName = item["name"] | item["title"] | "";
                if (pNum >= 1 && pNum <= MAX_PRESETS) {
                    pPresets->items[pNum - 1].index = pNum;
                    if (strlen(pName) > 0) {
                        String dec = decodeHexString(pName);
                        strncpy(pPresets->items[pNum - 1].name, dec.c_str(), sizeof(pPresets->items[pNum - 1].name) - 1);
                    }
                }
                idx++;
            }
        }
    }

    UiEvent evt;
    evt.type = UI_EVT_PRESETS_UPDATED;
    evt.data.presets = pPresets;

    if (xQueueSend(xQueueUiState, &evt, 0) != pdTRUE) {
        free(pPresets);
    }
}

void NetworkManager::fetchDeviceConfig() {
    if (!_hasActiveDevice || !_wifiConnected) return;

    String payload;
    int httpCode = executeApiGet("getStatusEx", payload);
    if (httpCode != HTTP_CODE_OK || payload.length() == 0) {
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) return;

    const char* vc = doc["volume_control"] | "0";
    bool isFixed = (strcmp(vc, "1") == 0);

    bool changed = (_activeDevice.is_fixed_volume != isFixed);
    _activeDevice.is_fixed_volume = isFixed;

    if (changed) {
        log_i("Streamer volume mode updated: Fixed=%d", isFixed ? 1 : 0);
        saveActiveDevice(_activeDevice);

        _lastPlayerState.is_fixed_volume = isFixed;
        UiEvent evt;
        evt.type = UI_EVT_PLAYER_STATE;
        evt.data.player = _lastPlayerState;
        xQueueSend(xQueueUiState, &evt, 0);
    }
}

void NetworkManager::seekPosition(uint32_t seek_ms) {
    if (!_hasActiveDevice || !_wifiConnected) return;

    uint32_t totalSec = seek_ms / 1000;
    uint8_t h = totalSec / 3600;
    uint8_t m = (totalSec % 3600) / 60;
    uint8_t s = totalSec % 60;

    char targetStr[16];
    snprintf(targetStr, sizeof(targetStr), "%02u:%02u:%02u", h, m, s);

    HTTPClient http;
    String url = "http://" + String(_activeDevice.ip) + ":49152/upnp/control/rendertransport1";
    http.setTimeout(1500);

    if (!http.begin(url)) {
        return;
    }

    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    http.addHeader("SOAPAction", "\"urn:schemas-upnp-org:service:AVTransport:1#Seek\"");

    String soapBody = "<?xml version=\"1.0\"?>"
                      "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
                      "<s:Body><u:Seek xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
                      "<InstanceID>0</InstanceID>"
                      "<Unit>REL_TIME</Unit>"
                      "<Target>" + String(targetStr) + "</Target>"
                      "</u:Seek></s:Body></s:Envelope>";

    int httpCode = http.POST(soapBody);
    log_i("UPnP Seek to %s returned HTTP %d", targetStr, httpCode);
    http.end();
}

void NetworkManager::processIncomingCommands() {
    UiCommand cmd;
    while (xQueueReceive(xQueueUiCmd, &cmd, 0) == pdTRUE) {
        switch (cmd.type) {
            case CMD_PLAY_PAUSE:
                log_i("Dispatching Play/Pause toggle");
                sendHttpCommand("setPlayerCmd:onepause");
                _lastStatusPoll = millis() - (STATUS_POLL_INTERVAL_MS - 150);
                break;

            case CMD_NEXT:
                log_i("Dispatching Next Track");
                sendHttpCommand("setPlayerCmd:next");
                _lastStatusPoll = millis() - (STATUS_POLL_INTERVAL_MS - 150);
                break;

            case CMD_PREV:
            {
                uint32_t pos_ms = cmd.data.seek_ms;
                log_i("Dispatching Previous: curpos=%u ms", pos_ms);

                if (pos_ms > 5000) {
                    // Mid-song (>5s): Restart current song from 0:00
                    log_i("Mid-song (>5s): restarting current song from beginning");
                    seekPosition(0);
                    sendHttpCommand("setPlayerCmd:seek:0");
                } else {
                    // Within first 5s: Move to previous track
                    log_i("Within first 5s (<=5s): moving to previous track");
                    sendHttpCommand("setPlayerCmd:prev");
                }
                _lastStatusPoll = millis() - (STATUS_POLL_INTERVAL_MS - 150);
                break;
            }

            case CMD_SET_VOL:
                // Throttled volume handling
                _pendingVolume = cmd.data.volume;
                _volumePending = true;
                break;

            case CMD_SET_MUTE:
                log_i("Dispatching Mute: %d", cmd.data.mute ? 1 : 0);
                sendHttpCommand(String("setPlayerCmd:mute:") + (cmd.data.mute ? "1" : "0"));
                _lastStatusPoll = millis() - (STATUS_POLL_INTERVAL_MS - 150);
                break;

            case CMD_TRIGGER_PRESET:
                log_i("Dispatching Preset: %d", cmd.data.preset_index);
                sendHttpCommand(String("MCUKeyShortClick:") + String(cmd.data.preset_index));
                _lastStatusPoll = millis() - (STATUS_POLL_INTERVAL_MS - 250);
                break;

            case CMD_SELECT_DEVICE:
                selectDevice(cmd.data.device_ip);
                break;

            case CMD_TRIGGER_RESCAN:
                triggerRescan();
                break;

            case CMD_SEEK_POSITION:
                log_i("Dispatching Seek to %u ms", cmd.data.seek_ms);
                seekPosition(cmd.data.seek_ms);
                break;

            case CMD_WIFI_START_SCAN:
                startWiFiScan();
                break;

            case CMD_WIFI_CONNECT:
                connectWiFi(cmd.data.wifi_connect.ssid, cmd.data.wifi_connect.password);
                break;

            case CMD_WIFI_FORGET:
                forgetWiFi();
                break;

            case CMD_WIFI_RECONNECT: {
                bool setupDone = prefs.getBool("user_setup_done", false);
                String savedSsid = setupDone ? prefs.getString("wifi_ssid", "") : "";
                String savedPass = setupDone ? prefs.getString("wifi_pass", "") : "";
                if (savedSsid.length() > 0 && WiFi.status() != WL_CONNECTED) {
                    log_i("Reconnecting to saved Wi-Fi: %s", savedSsid.c_str());
                    connectWiFi(savedSsid.c_str(), savedPass.c_str());
                }
                break;
            }
        }
    }

    // Check throttled volume dispatch
    if (_volumePending) {
        if (_activeDevice.is_fixed_volume) {
            _volumePending = false; // Suppress volume commands when in fixed mode
        } else {
            unsigned long now = millis();
            if (now - _lastVolumeSent >= VOLUME_THROTTLE_MS) {
                _lastVolumeSent = now;
                _volumePending = false;
                sendHttpCommand(String("setPlayerCmd:vol:") + String(_pendingVolume));
            }
        }
    }
}

void NetworkManager::runTaskLoop() {
    // 1. Process incoming UI commands FIRST (works even when Wi-Fi is disconnected)
    processIncomingCommands();

    handleWiFi();

    if (_wifiConnected) {
        // 2. Background network discovery
        processSSDPPackets();
        runSubnetScanStep();

        // 3. Auto-fetch presets, metadata, and device config once connected
        if (_hasActiveDevice && !_initialPresetsFetched) {
            _initialPresetsFetched = true;
            log_i("Auto-fetching presets, metadata, and config for %s...", _activeDevice.name);
            fetchPresetInfo();
            fetchTrackMeta();
            fetchDeviceConfig();
        }

        unsigned long now = millis();
        // 4. Poll status every 1000ms
        if (now - _lastStatusPoll >= STATUS_POLL_INTERVAL_MS) {
            _lastStatusPoll = now;
            pollActiveDevice();
        }

        // 5. Poll device configuration (volume_control / fixed mode) every 3000ms
        if (now - _lastConfigPoll >= 3000) {
            _lastConfigPoll = now;
            fetchDeviceConfig();
        }

        // 6. Periodic SSDP refresh only if no devices found yet
        if (_deviceList.count == 0 && (now - _lastSSDPBroadcast >= SSDP_DISCOVERY_INTERVAL_MS)) {
            sendSSDPQuery();
        }
    } else {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void network_task_entry(void* param) {
    NetworkManager& net = NetworkManager::getInstance();
    net.init();

    for (;;) {
        net.runTaskLoop();
        vTaskDelay(pdMS_TO_TICKS(10)); // Yield cleanly to FreeRTOS scheduler
    }
}
