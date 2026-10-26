#include "network_manager.h"
#include "net/wifi_service.h"
#include "net/discovery_service.h"
#include "net/linkplay_client.h"
#include "net/lyrics_client.h"
#include "net/upnp_client.h"
#include "power_manager.h"
#include <esp_task_wdt.h>
#include "config.h"

extern QueueHandle_t xQueueUiCmd;
extern QueueHandle_t xQueueUiState;

static void emit_boot_status(uint8_t pct, bool hasError, bool completed, const char* msg) {
    UiEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = UI_EVT_BOOT_STATUS;
    evt.data.boot.progress_pct = pct;
    evt.data.boot.has_error = hasError;
    evt.data.boot.completed = completed;
    if (msg) {
        strncpy(evt.data.boot.message, msg, sizeof(evt.data.boot.message) - 1);
    }
    xQueueSend(xQueueUiState, &evt, 0);
}

static void on_active_device_changed(const WiiMDevice& dev) {
    (void)dev;
    if (wifi_service_is_connected()) {
        WiiMNetworkManager::getInstance().syncActiveDevice();
    }
}

static void on_wifi_connected() {
    discovery_service_start_ssdp_listener();
    discovery_service_broadcast_list();
    WiiMNetworkManager::getInstance().syncActiveDevice();
}

static void on_wifi_disconnected() {
    linkplay_client_reset_session();
}

WiiMNetworkManager::WiiMNetworkManager()
    : _lastStatusPoll(0),
      _lastConfigPoll(0),
      _lastSSDPBroadcast(0),
      _lastVolumeSent(0),
      _pendingVolume(0),
      _volumePending(false),
      _deviceSynced(false),
      _syncFailed(false),
      _syncInProgress(false)
{
}

void WiiMNetworkManager::init() {
    log_i("Initializing WiiMNetworkManager...");

    linkplay_client_init();
    upnp_client_init();
    discovery_service_init(on_active_device_changed);
    wifi_service_init(on_wifi_connected, on_wifi_disconnected);
}

void WiiMNetworkManager::triggerRescan() {
    discovery_service_trigger_rescan();
}

void WiiMNetworkManager::selectDevice(const char* ip) {
    discovery_service_select_device(ip);
}

bool WiiMNetworkManager::getActiveDevice(WiiMDevice* dev) {
    return discovery_service_get_active_device(dev);
}

void WiiMNetworkManager::markActiveDevicePolled() {
    unsigned long now = millis();
    _lastStatusPoll = now;
    _lastConfigPoll = now;
    _deviceSynced = true;
    _syncFailed = false;
}

bool WiiMNetworkManager::syncActiveDevice() {
    if (_syncInProgress) return false;
    _syncInProgress = true;
    _deviceSynced = false;
    _syncFailed = false;

    WiiMDevice activeDev;
    bool hasActive = discovery_service_get_active_device(&activeDev);

    // If no active device is saved in NVS (first boot), scan the network first
    if (!hasActive) {
        emit_boot_status(40, false, false, "Searching for WiiM streamers...");
        discovery_service_trigger_rescan();
        hasActive = discovery_service_get_active_device(&activeDev);
        if (!hasActive) {
            _syncFailed = true;
            _syncInProgress = false;
            emit_boot_status(40, true, false, "No WiiM streamers found");
            return false;
        }
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "Connecting to %.40s...", activeDev.name);
    emit_boot_status(50, false, false, msg);

    linkplay_client_reset_session();
    linkplay_client_reset_track_cache();
    esp_task_wdt_reset();

    bool isFixed = activeDev.is_fixed_volume;
    bool configOk = linkplay_client_fetch_config(activeDev.ip, isFixed);
    PlayerState st;
    memset(&st, 0, sizeof(st));
    bool statusOk = false;

    if (configOk) {
        discovery_service_set_active_fixed_volume(isFixed);
        activeDev.is_fixed_volume = isFixed;
        esp_task_wdt_reset();
        statusOk = linkplay_client_poll_active(activeDev.ip, isFixed, st);
    }

    // If initial query failed (e.g. cold ARP on wake or DHCP IP changed), run a quick SSDP check and retry once
    if (!configOk || !statusOk) {
        discovery_service_send_ssdp_query();
        for (uint8_t w = 0; w < 8; ++w) {
            esp_task_wdt_reset();
            discovery_service_process_ssdp();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        discovery_service_get_active_device(&activeDev);
        linkplay_client_reset_session();
        esp_task_wdt_reset();

        configOk = linkplay_client_fetch_config(activeDev.ip, isFixed);
        if (configOk) {
            discovery_service_set_active_fixed_volume(isFixed);
            activeDev.is_fixed_volume = isFixed;
            esp_task_wdt_reset();
            statusOk = linkplay_client_poll_active(activeDev.ip, isFixed, st);
        }
    }

    if (!configOk || !statusOk) {
        _syncFailed = true;
        _syncInProgress = false;
        snprintf(msg, sizeof(msg), "Cannot reach %.40s", activeDev.name);
        emit_boot_status(50, true, false, msg);
        return false;
    }

    emit_boot_status(80, false, false, "Loading presets...");
    esp_task_wdt_reset();
    bool presetsOk = linkplay_client_fetch_presets(activeDev.ip);
    if (!presetsOk) {
        esp_task_wdt_reset();
        presetsOk = linkplay_client_fetch_presets(activeDev.ip);
    }
    if (!presetsOk) {
        _syncFailed = true;
        _syncInProgress = false;
        snprintf(msg, sizeof(msg), "Failed to load presets from %.30s", activeDev.name);
        emit_boot_status(80, true, false, msg);
        return false;
    }

    if (st.state != PLAY_STATE_NONE && st.state != PLAY_STATE_UNKNOWN) {
        esp_task_wdt_reset();
        linkplay_client_fetch_meta(activeDev.ip);
    }

    markActiveDevicePolled();
    _syncInProgress = false;
    emit_boot_status(100, false, true, "Ready");
    return true;
}

void WiiMNetworkManager::processIncomingCommands() {
    UiCommand cmd;
    WiiMDevice activeDev;
    bool hasActive = discovery_service_get_active_device(&activeDev);

    while (xQueueReceive(xQueueUiCmd, &cmd, 0) == pdTRUE) {
        switch (cmd.type) {
            case CMD_PLAY_PAUSE:
                if (hasActive) {
                    const PlayerState& ps = linkplay_client_get_last_player_state();
                    if (ps.state == PLAY_STATE_PLAYING) {
                        log_i("Dispatching Pause command");
                        linkplay_client_send_cmd(activeDev.ip, "setPlayerCmd:pause");
                    } else if (ps.state == PLAY_STATE_PAUSED) {
                        log_i("Dispatching Resume command");
                        linkplay_client_send_cmd(activeDev.ip, "setPlayerCmd:resume");
                    } else {
                        log_i("Dispatching Play command");
                        linkplay_client_send_cmd(activeDev.ip, "setPlayerCmd:play");
                    }
                    _lastStatusPoll = millis() - (STATUS_POLL_INTERVAL_MS - 150);
                }
                break;

            case CMD_NEXT:
                if (hasActive) {
                    log_i("Dispatching Next command");
                    linkplay_client_send_cmd(activeDev.ip, "setPlayerCmd:next");
                    _lastStatusPoll = millis() - (STATUS_POLL_INTERVAL_MS - 150);
                }
                break;

            case CMD_PREV:
                if (hasActive) {
                    uint32_t pos_ms = cmd.data.seek_ms;

                    if (pos_ms > 5000) {
                        // Mid-song (>5s): Restart current song from 0:00
                        log_i("Dispatching Prev (Restart track -> Seek to 0:00)");
                        linkplay_client_send_cmd(activeDev.ip, "setPlayerCmd:seek:0");
                    } else {
                        // Within first 5s: Move to previous track
                        log_i("Dispatching Prev (Previous track)");
                        linkplay_client_send_cmd(activeDev.ip, "setPlayerCmd:prev");
                    }
                    _lastStatusPoll = millis() - (STATUS_POLL_INTERVAL_MS - 150);
                }
                break;

            case CMD_SET_VOL:
                _pendingVolume = cmd.data.volume;
                _volumePending = true;
                break;

            case CMD_SET_MUTE:
                if (hasActive) {
                    log_i("Dispatching Mute: %d", cmd.data.mute ? 1 : 0);
                    linkplay_client_send_cmd(activeDev.ip, cmd.data.mute ? "setPlayerCmd:mute:1" : "setPlayerCmd:mute:0");
                    _lastStatusPoll = millis() - (STATUS_POLL_INTERVAL_MS - 150);
                }
                break;

            case CMD_TRIGGER_PRESET:
                if (hasActive) {
                    log_i("Dispatching Preset: %d", cmd.data.preset_index);
                    char presetCmd[32];
                    snprintf(presetCmd, sizeof(presetCmd), "MCUKeyShortClick:%u", cmd.data.preset_index);
                    linkplay_client_send_cmd(activeDev.ip, String(presetCmd));
                    _lastStatusPoll = millis() - (STATUS_POLL_INTERVAL_MS - 250);
                }
                break;

            case CMD_SELECT_DEVICE:
                selectDevice(cmd.data.device_ip);
                hasActive = discovery_service_get_active_device(&activeDev);
                break;

            case CMD_TRIGGER_RESCAN:
                triggerRescan();
                hasActive = discovery_service_get_active_device(&activeDev);
                break;

            case CMD_SEEK_POSITION:
                if (hasActive) {
                    log_i("Dispatching Seek to %u ms", cmd.data.seek_ms);
                    char seekCmd[32];
                    snprintf(seekCmd, sizeof(seekCmd), "setPlayerCmd:seek:%u", (unsigned int)(cmd.data.seek_ms / 1000));
                    linkplay_client_send_cmd(activeDev.ip, String(seekCmd));
                    _lastStatusPoll = millis() - (STATUS_POLL_INTERVAL_MS - 250);
                }
                break;

            case CMD_WIFI_START_SCAN:
                wifi_service_start_scan();
                break;

            case CMD_WIFI_CONNECT:
                wifi_service_connect(cmd.data.wifi_connect.ssid, cmd.data.wifi_connect.password);
                break;

            case CMD_WIFI_FORGET:
                wifi_service_forget();
                break;

            case CMD_WIFI_RECONNECT:
                wifi_service_reconnect();
                break;

            case CMD_FETCH_LYRICS: {
                const char* reqTitle = (strlen(cmd.data.lyrics_req.title) > 0)
                                       ? cmd.data.lyrics_req.title
                                       : linkplay_client_get_last_title().c_str();
                const char* reqArtist = (strlen(cmd.data.lyrics_req.artist) > 0)
                                        ? cmd.data.lyrics_req.artist
                                        : linkplay_client_get_last_artist().c_str();
                if (reqTitle && strlen(reqTitle) > 0) {
                    log_i("Dispatching on-demand lyrics fetch for: %s - %s", reqTitle, reqArtist);
                    // Close persistent WiiM TLS session first to free ~40KB internal SRAM for LRCLIB TLS handshake
                    linkplay_client_reset_session();
                    lyrics_client_fetch(reqTitle, reqArtist);
                }
                break;
            }

            case CMD_SWITCH_MODE:
                if (hasActive && strlen(cmd.data.input_mode) > 0) {
                    char modeCmd[40];
                    snprintf(modeCmd, sizeof(modeCmd), "setPlayerCmd:switchmode:%s", cmd.data.input_mode);
                    log_i("Dispatching Input Mode switch: %s", modeCmd);
                    linkplay_client_send_cmd(activeDev.ip, String(modeCmd));
                    _lastStatusPoll = millis() - (STATUS_POLL_INTERVAL_MS - 250);
                }
                break;

            case CMD_UPNP_SCAN_SERVERS:
                discovery_service_send_upnp_server_query();
                for (uint8_t w = 0; w < 12; ++w) {
                    esp_task_wdt_reset();
                    discovery_service_process_ssdp();
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                upnp_client_broadcast_servers();
                break;

            case CMD_UPNP_BROWSE:
                upnp_client_browse(cmd.data.upnp_browse.server_index,
                                   cmd.data.upnp_browse.object_id,
                                   cmd.data.upnp_browse.title,
                                   cmd.data.upnp_browse.start_index);
                break;

            case CMD_UPNP_PLAY_TRACK:
                if (hasActive) {
                    linkplay_client_reset_track_cache();
                    if (!upnp_client_play_track(activeDev.ip, cmd.data.upnp_play.track_index, activeDev.is_fixed_volume)) {
                        log_w("UPnP play_track failed for track index %u on %s", cmd.data.upnp_play.track_index, activeDev.ip);
                    }
                    _lastStatusPoll = millis() - (STATUS_POLL_INTERVAL_MS - 200);
                }
                break;

            case CMD_RETRY_SYNC:
                if (!wifi_service_is_connected()) {
                    emit_boot_status(20, false, false, "Connecting to Wi-Fi...");
                    wifi_service_reconnect();
                } else {
                    syncActiveDevice();
                }
                hasActive = discovery_service_get_active_device(&activeDev);
                break;
        }
    }

    // Check throttled volume dispatch
    if (_volumePending && hasActive) {
        if (activeDev.is_fixed_volume) {
            _volumePending = false; // Suppress volume commands when in fixed mode
        } else {
            unsigned long now = millis();
            if (now - _lastVolumeSent >= VOLUME_THROTTLE_MS) {
                _lastVolumeSent = now;
                _volumePending = false;
                char volCmd[32];
                snprintf(volCmd, sizeof(volCmd), "setPlayerCmd:vol:%u", _pendingVolume);
                linkplay_client_send_cmd(activeDev.ip, String(volCmd));
            }
        }
    }
}

void WiiMNetworkManager::runTaskLoop() {
    // 1. Process incoming UI commands FIRST (works even when Wi-Fi is disconnected)
    processIncomingCommands();

    wifi_service_handle();

    if (wifi_service_is_connected()) {
        // 2. Background network discovery
        discovery_service_process_ssdp();

        WiiMDevice activeDev;
        bool hasActive = discovery_service_get_active_device(&activeDev);

        // 3. Auto-sync active device if not yet synced and not waiting on user Retry
        if (hasActive && !_deviceSynced && !_syncFailed) {
            syncActiveDevice();
            hasActive = discovery_service_get_active_device(&activeDev);
        }

        unsigned long now = millis();
        bool dimmed = power_manager_is_dimmed();
        const PlayerState& lastPs = linkplay_client_get_last_player_state();
        bool isPlayingOrBuffering = (lastPs.state == PLAY_STATE_PLAYING || lastPs.state == PLAY_STATE_BUFFERING);

        // 4. Adaptive status poll interval (runs only after initial device sync succeeds):
        //    - 5000ms when screen is dimmed (saves Wi-Fi TX/RX spikes)
        //    - 2500ms when awake but paused/stopped
        //    - 1000ms when awake and actively playing/buffering
        unsigned long pollInterval = dimmed ? 5000UL : (isPlayingOrBuffering ? STATUS_POLL_INTERVAL_MS : 2500UL);
        if (hasActive && _deviceSynced && (now - _lastStatusPoll >= pollInterval)) {
            _lastStatusPoll = now;
            PlayerState st;
            linkplay_client_poll_active(activeDev.ip, activeDev.is_fixed_volume, st);
        }

        // 5. Poll device configuration (volume_control / fixed mode) every 30000ms (skip while dimmed)
        if (hasActive && _deviceSynced && !dimmed && (now - _lastConfigPoll >= 30000)) {
            _lastConfigPoll = now;
            bool isFixed = false;
            if (linkplay_client_fetch_config(activeDev.ip, isFixed) && isFixed != activeDev.is_fixed_volume) {
                discovery_service_set_active_fixed_volume(isFixed);
                _lastStatusPoll = 0;
            }
        }

        // 6. Periodic SSDP refresh when awake (every 10s if 0 devices, or every 60s to catch newly added/re-IP'd devices)
        unsigned long ssdpInterval = (discovery_service_get_count() == 0) ? SSDP_DISCOVERY_INTERVAL_MS : 60000UL;
        if (!dimmed && (now - _lastSSDPBroadcast >= ssdpInterval)) {
            _lastSSDPBroadcast = now;
            discovery_service_send_ssdp_query();
        }

        vTaskDelay(pdMS_TO_TICKS(dimmed ? 25 : 1)); // Yield to Wi-Fi/lwIP stack on Core 0
    } else {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void network_task_entry(void* param) {
    esp_task_wdt_add(NULL);
    WiiMNetworkManager& net = WiiMNetworkManager::getInstance();
    net.init();

    for (;;) {
        esp_task_wdt_reset();
        net.runTaskLoop();
        vTaskDelay(pdMS_TO_TICKS(10)); // Yield cleanly to FreeRTOS scheduler
    }
}
