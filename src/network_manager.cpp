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

static void on_active_device_changed(const WiiMDevice& dev) {
    linkplay_client_reset_session();
    linkplay_client_reset_track_cache();

    // 1. Fetch hardware config FIRST so we know if the new device is fixed or variable
    bool isFixed = dev.is_fixed_volume;
    if (linkplay_client_fetch_config(dev.ip, isFixed)) {
        discovery_service_set_active_fixed_volume(isFixed);
    }

    // 2. Poll player status with verified isFixed mode
    PlayerState st;
    linkplay_client_poll_active(dev.ip, isFixed, st);
    linkplay_client_fetch_presets(dev.ip);

    // 3. Synchronize poll timers so runTaskLoop() doesn't immediately re-poll
    WiiMNetworkManager::getInstance().markActiveDevicePolled();
}

static void on_wifi_connected() {
    discovery_service_start_ssdp_listener();
    discovery_service_broadcast_list();
    discovery_service_trigger_rescan();
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
      _initialPresetsFetched(false)
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
    _initialPresetsFetched = true;
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
                    log_i("Dispatching Next Track");
                    linkplay_client_send_cmd(activeDev.ip, "setPlayerCmd:next");
                    _lastStatusPoll = millis() - (STATUS_POLL_INTERVAL_MS - 150);
                }
                break;

            case CMD_PREV:
                if (hasActive) {
                    uint32_t pos_ms = cmd.data.seek_ms;
                    log_i("Dispatching Previous: curpos=%u ms", pos_ms);

                    if (pos_ms > 5000) {
                        // Mid-song (>5s): Restart current song from 0:00
                        log_i("Mid-song (>5s): restarting current song from beginning");
                        linkplay_client_seek(activeDev.ip, 0);
                    } else {
                        // Within first 5s: Move to previous track
                        log_i("Within first 5s (<=5s): moving to previous track");
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
                    linkplay_client_seek(activeDev.ip, cmd.data.seek_ms);
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
                upnp_client_clear_servers();
                discovery_service_send_upnp_server_query();
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

        // 3. Auto-fetch presets, metadata, and device config once connected
        if (hasActive && !_initialPresetsFetched) {
            markActiveDevicePolled();
            log_i("Auto-fetching presets, metadata, and config for %s...", activeDev.name);
            linkplay_client_fetch_presets(activeDev.ip);
            linkplay_client_fetch_meta(activeDev.ip);

            bool isFixed = activeDev.is_fixed_volume;
            if (linkplay_client_fetch_config(activeDev.ip, isFixed) && isFixed != activeDev.is_fixed_volume) {
                discovery_service_set_active_fixed_volume(isFixed);
                activeDev.is_fixed_volume = isFixed;
            }
            PlayerState st;
            linkplay_client_poll_active(activeDev.ip, activeDev.is_fixed_volume, st);
        }

        unsigned long now = millis();
        bool dimmed = power_manager_is_dimmed();
        const PlayerState& lastPs = linkplay_client_get_last_player_state();
        bool isPlayingOrBuffering = (lastPs.state == PLAY_STATE_PLAYING || lastPs.state == PLAY_STATE_BUFFERING);

        // 4. Adaptive status poll interval:
        //    - 5000ms when screen is dimmed (saves Wi-Fi TX/RX spikes)
        //    - 2500ms when awake but paused/stopped
        //    - 1000ms when awake and actively playing/buffering
        unsigned long pollInterval = dimmed ? 5000UL : (isPlayingOrBuffering ? STATUS_POLL_INTERVAL_MS : 2500UL);
        if (hasActive && (now - _lastStatusPoll >= pollInterval)) {
            _lastStatusPoll = now;
            PlayerState st;
            linkplay_client_poll_active(activeDev.ip, activeDev.is_fixed_volume, st);
        }

        // 5. Poll device configuration (volume_control / fixed mode) every 30000ms (skip while dimmed)
        if (hasActive && !dimmed && (now - _lastConfigPoll >= 30000)) {
            _lastConfigPoll = now;
            bool isFixed = false;
            if (linkplay_client_fetch_config(activeDev.ip, isFixed) && isFixed != activeDev.is_fixed_volume) {
                discovery_service_set_active_fixed_volume(isFixed);
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
