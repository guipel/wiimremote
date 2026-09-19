#include "net/wifi_service.h"
#include <Preferences.h>
#include "config.h"
#include "model.h"

extern QueueHandle_t xQueueUiState;

static Preferences s_wifi_prefs;
static WiFiConnectedCb s_on_connected_cb = nullptr;
static WiFiDisconnectedCb s_on_disconnected_cb = nullptr;

static bool s_wifi_connected = false;
static unsigned long s_last_wifi_check = 0;
static bool s_wifi_scanning = false;
static bool s_wifi_connecting = false;
static unsigned long s_wifi_connect_start = 0;
static String s_pending_connect_ssid = "";
static String s_pending_connect_pass = "";
static String s_cached_saved_ssid = "";
static String s_cached_saved_pass = "";

void wifi_service_init(WiFiConnectedCb onConnected, WiFiDisconnectedCb onDisconnected) {
    s_on_connected_cb = onConnected;
    s_on_disconnected_cb = onDisconnected;

    s_wifi_prefs.begin("wiimremote", false);

    WiFi.mode(WIFI_STA);

    bool setupDone = s_wifi_prefs.getBool("user_setup_done", false);
    s_cached_saved_ssid = setupDone ? s_wifi_prefs.getString("wifi_ssid", "") : "";
    s_cached_saved_pass = setupDone ? s_wifi_prefs.getString("wifi_pass", "") : "";

    if (s_cached_saved_ssid.length() > 0) {
        log_i("Connecting to saved Wi-Fi SSID: %s", s_cached_saved_ssid.c_str());
        WiFi.setSleep(WIFI_PS_MIN_MODEM);
        s_wifi_connecting = true;
        s_wifi_connect_start = millis();
        s_pending_connect_ssid = s_cached_saved_ssid;
        s_pending_connect_pass = s_cached_saved_pass;
        WiFi.begin(s_cached_saved_ssid.c_str(), s_cached_saved_pass.c_str());
    } else {
        log_w("No user-configured Wi-Fi credentials. Setup required!");
        s_wifi_prefs.remove("wifi_ssid");
        s_wifi_prefs.remove("wifi_pass");
        s_wifi_prefs.remove("user_setup_done");
        WiFi.disconnect(true, true);

        UiEvent evt;
        evt.type = UI_EVT_WIFI_SETUP_REQUIRED;
        xQueueSend(xQueueUiState, &evt, 0);
        wifi_service_start_scan();
    }

    s_last_wifi_check = millis();
}

void wifi_service_start_scan() {
    if (s_wifi_scanning) return;
    s_wifi_scanning = true;
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

    s_wifi_scanning = false;

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
    WiFi.setSleep(WIFI_PS_MIN_MODEM);
}

void wifi_service_connect(const char* ssid, const char* pass) {
    if (!ssid || strlen(ssid) == 0) return;
    log_i("Connecting to user-selected Wi-Fi: %s", ssid);
    s_wifi_connecting = true;
    s_wifi_connect_start = millis();
    s_pending_connect_ssid = ssid;
    s_pending_connect_pass = pass ? pass : "";

    WiFi.disconnect(false, false);
    WiFi.begin(ssid, pass);
}

void wifi_service_forget() {
    log_i("Forgetting saved Wi-Fi credentials");
    s_wifi_prefs.remove("wifi_ssid");
    s_wifi_prefs.remove("wifi_pass");
    s_wifi_prefs.remove("user_setup_done");
    s_wifi_connected = false;
    s_pending_connect_ssid = "";
    s_pending_connect_pass = "";
    s_cached_saved_ssid = "";
    s_cached_saved_pass = "";
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

    wifi_service_start_scan();
}

void wifi_service_reconnect() {
    if (s_cached_saved_ssid.length() > 0 && WiFi.status() != WL_CONNECTED) {
        log_i("Reconnecting to saved Wi-Fi: %s", s_cached_saved_ssid.c_str());
        wifi_service_connect(s_cached_saved_ssid.c_str(), s_cached_saved_pass.c_str());
    }
}

void wifi_service_handle() {
    unsigned long now = millis();
    wl_status_t status = WiFi.status();

    if (status == WL_CONNECTED) {
        if (!s_wifi_connected) {
            s_wifi_connected = true;
            bool wasUserConnecting = (s_pending_connect_ssid.length() > 0);
            s_wifi_connecting = false;
            String localIpStr = WiFi.localIP().toString();
            int8_t rssi = WiFi.RSSI();
            String ssidStr = WiFi.SSID();
            log_i("Wi-Fi Connected! IP: %s, RSSI: %d dBm", localIpStr.c_str(), rssi);

            // Save to NVS if newly connected via pending credentials
            if (wasUserConnecting) {
                s_wifi_prefs.putString("wifi_ssid", s_pending_connect_ssid);
                s_wifi_prefs.putString("wifi_pass", s_pending_connect_pass);
                s_wifi_prefs.putBool("user_setup_done", true);
                s_cached_saved_ssid = s_pending_connect_ssid;
                s_cached_saved_pass = s_pending_connect_pass;
                log_i("Saved Wi-Fi credentials to NVS: %s", s_pending_connect_ssid.c_str());
                s_pending_connect_ssid = "";
                s_pending_connect_pass = "";

                // Notify UI of connection success to close the modal
                UiEvent evtSuccess;
                evtSuccess.type = UI_EVT_WIFI_CONNECT_SUCCESS;
                strncpy(evtSuccess.data.wifi.ip, localIpStr.c_str(), sizeof(evtSuccess.data.wifi.ip) - 1);
                evtSuccess.data.wifi.ip[sizeof(evtSuccess.data.wifi.ip) - 1] = '\0';
                xQueueSend(xQueueUiState, &evtSuccess, 0);
            }

            // Post Wi-Fi status event to UI task
            UiEvent evt;
            evt.type = UI_EVT_WIFI_STATUS;
            evt.data.wifi.connected = true;
            evt.data.wifi.rssi = rssi;
            strncpy(evt.data.wifi.ip, localIpStr.c_str(), sizeof(evt.data.wifi.ip) - 1);
            evt.data.wifi.ip[sizeof(evt.data.wifi.ip) - 1] = '\0';
            strncpy(evt.data.wifi.ssid, ssidStr.c_str(), sizeof(evt.data.wifi.ssid) - 1);
            evt.data.wifi.ssid[sizeof(evt.data.wifi.ssid) - 1] = '\0';
            xQueueSend(xQueueUiState, &evt, 0);

            if (s_on_connected_cb) {
                s_on_connected_cb();
            }
        } else if (now - s_last_wifi_check >= 5000) {
            // Periodic RSSI refresh
            s_last_wifi_check = now;
            String localIpStr = WiFi.localIP().toString();
            int8_t rssi = WiFi.RSSI();
            String ssidStr = WiFi.SSID();
            UiEvent evt;
            evt.type = UI_EVT_WIFI_STATUS;
            evt.data.wifi.connected = true;
            evt.data.wifi.rssi = rssi;
            strncpy(evt.data.wifi.ip, localIpStr.c_str(), sizeof(evt.data.wifi.ip) - 1);
            evt.data.wifi.ip[sizeof(evt.data.wifi.ip) - 1] = '\0';
            strncpy(evt.data.wifi.ssid, ssidStr.c_str(), sizeof(evt.data.wifi.ssid) - 1);
            evt.data.wifi.ssid[sizeof(evt.data.wifi.ssid) - 1] = '\0';
            xQueueSend(xQueueUiState, &evt, 0);
        }
    } else {
        if (s_wifi_connected) {
            s_wifi_connected = false;
            if (s_on_disconnected_cb) {
                s_on_disconnected_cb();
            }
            log_w("Wi-Fi Connection Lost! Attempting reconnect...");
            UiEvent evt;
            evt.type = UI_EVT_WIFI_STATUS;
            evt.data.wifi.connected = false;
            evt.data.wifi.rssi = -100;
            evt.data.wifi.ip[0] = '\0';
            evt.data.wifi.ssid[0] = '\0';
            xQueueSend(xQueueUiState, &evt, 0);
        }

        if (s_wifi_connecting) {
            if (now - s_wifi_connect_start >= WIFI_CONNECT_TIMEOUT_MS) {
                s_wifi_connecting = false;
                log_w("Wi-Fi connection to %s timed out!", s_pending_connect_ssid.c_str());
                s_pending_connect_ssid = "";
                s_pending_connect_pass = "";
                UiEvent evt;
                evt.type = UI_EVT_WIFI_CONNECT_FAILED;
                xQueueSend(xQueueUiState, &evt, 0);
                wifi_service_start_scan();
            }
        } else {
            if (s_cached_saved_ssid.length() > 0 && now - s_last_wifi_check >= WIFI_RECONNECT_INTERVAL_MS) {
                s_last_wifi_check = now;
                log_i("Reconnecting to Wi-Fi...");
                WiFi.reconnect();
            }
        }
    }
}

bool wifi_service_is_connected() {
    return s_wifi_connected;
}

int8_t wifi_service_get_rssi() {
    return WiFi.RSSI();
}

String wifi_service_get_ip() {
    return WiFi.localIP().toString();
}

String wifi_service_get_ssid() {
    return WiFi.SSID();
}
