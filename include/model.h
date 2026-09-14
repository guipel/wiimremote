#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// =============================================================================
// Core Data Models
// =============================================================================

#define MAX_DISCOVERED_DEVICES  12
#define MAX_PRESETS             12

enum PlayState {
    PLAY_STATE_UNKNOWN = 0,
    PLAY_STATE_PLAYING,
    PLAY_STATE_PAUSED,
    PLAY_STATE_STOPPED
};

struct WiiMDevice {
    char name[64];
    char ip[24];
    char uuid[64];
    bool is_active;
    bool is_fixed_volume;
};

struct DeviceList {
    uint8_t count;
    WiiMDevice devices[MAX_DISCOVERED_DEVICES];
};

struct StreamInfo {
    char format[16];        // e.g. "FLAC", "MP3", "AAC"
    uint32_t sample_rate;   // e.g. 44100, 96000, 192000
    uint8_t bit_depth;      // e.g. 16, 24
};

struct PlayerState {
    PlayState state;
    uint8_t volume;         // 0 - 100
    bool mute;
    bool is_fixed_volume;   // True if fixed volume mode (external preamp)
    uint32_t curpos_ms;     // Track position in ms
    uint32_t totlen_ms;     // Track total duration in ms
    StreamInfo stream;
    char vendor[32];        // e.g. "Amazon Music", "Spotify", "Tidal"
    uint16_t track_num;     // e.g. 11
    uint16_t track_total;   // e.g. 97
};

struct TrackMeta {
    char title[128];
    char artist[96];
    char album[96];
    uint32_t sample_rate;   // e.g. 96000, 192000
    uint8_t bit_depth;      // e.g. 24, 32
};

struct PresetItem {
    uint8_t index;          // 1 - 12
    char name[48];
};

struct PresetList {
    uint8_t count;
    PresetItem items[MAX_PRESETS];
};

// =============================================================================
// Wi-Fi Scanning & Provisioning Data Structures
// =============================================================================
struct WiFiNetworkInfo {
    char ssid[33];
    int8_t rssi;
    bool is_open;
};

struct WiFiScanList {
    uint8_t count;
    WiFiNetworkInfo networks[20];
};

struct LyricsInfo {
    char title[128];
    char artist[96];
    char* text; // dynamically allocated lyrics text or nullptr
    bool is_loading;
};

// =============================================================================
// Inter-Task Communication (FreeRTOS Queues)
// =============================================================================

// Events: Network Task (Core 0) -> UI Task (Core 1)
enum UiEventType {
    UI_EVT_WIFI_STATUS = 0,
    UI_EVT_DEVICES_UPDATED,
    UI_EVT_PLAYER_STATE,
    UI_EVT_META_UPDATED,
    UI_EVT_PRESETS_UPDATED,
    UI_EVT_SCAN_STATUS,
    UI_EVT_WIFI_SCAN_RESULT,
    UI_EVT_WIFI_SETUP_REQUIRED,
    UI_EVT_WIFI_CONNECT_SUCCESS,
    UI_EVT_WIFI_CONNECT_FAILED,
    UI_EVT_LYRICS_UPDATED
};

struct UiEvent {
    UiEventType type;
    union {
        struct {
            bool connected;
            int8_t rssi;
            char ip[24];
            char ssid[33];
        } wifi;
        DeviceList* devices;
        PlayerState player;
        TrackMeta* meta;
        PresetList* presets;
        struct {
            bool is_scanning;
        } scan;
        WiFiScanList* wifi_scan;
        LyricsInfo* lyrics;
    } data;
};

inline void ui_event_free(UiEvent* evt) {
    if (!evt) return;
    if (evt->type == UI_EVT_DEVICES_UPDATED && evt->data.devices) {
        free(evt->data.devices);
        evt->data.devices = nullptr;
    } else if (evt->type == UI_EVT_META_UPDATED && evt->data.meta) {
        free(evt->data.meta);
        evt->data.meta = nullptr;
    } else if (evt->type == UI_EVT_PRESETS_UPDATED && evt->data.presets) {
        free(evt->data.presets);
        evt->data.presets = nullptr;
    } else if (evt->type == UI_EVT_WIFI_SCAN_RESULT && evt->data.wifi_scan) {
        free(evt->data.wifi_scan);
        evt->data.wifi_scan = nullptr;
    } else if (evt->type == UI_EVT_LYRICS_UPDATED && evt->data.lyrics) {
        if (evt->data.lyrics->text) {
            free(evt->data.lyrics->text);
            evt->data.lyrics->text = nullptr;
        }
        free(evt->data.lyrics);
        evt->data.lyrics = nullptr;
    }
}

// Commands: UI Task (Core 1) -> Network Task (Core 0)
enum CmdType {
    CMD_PLAY_PAUSE = 0,
    CMD_NEXT,
    CMD_PREV,
    CMD_SET_VOL,
    CMD_SET_MUTE,
    CMD_TRIGGER_PRESET,
    CMD_SELECT_DEVICE,
    CMD_TRIGGER_RESCAN,
    CMD_SEEK_POSITION,
    CMD_WIFI_START_SCAN,
    CMD_WIFI_CONNECT,
    CMD_WIFI_FORGET,
    CMD_WIFI_RECONNECT
};

struct UiCommand {
    CmdType type;
    union {
        uint8_t volume;             // For CMD_SET_VOL
        bool mute;                  // For CMD_SET_MUTE
        uint8_t preset_index;       // For CMD_TRIGGER_PRESET (1..12)
        char device_ip[24];         // For CMD_SELECT_DEVICE
        uint32_t seek_ms;           // For CMD_SEEK_POSITION
        struct {
            char ssid[33];
            char password[65];
        } wifi_connect;             // For CMD_WIFI_CONNECT
    } data;
};

// Global queue handles
extern QueueHandle_t xQueueUiCmd;
extern QueueHandle_t xQueueUiState;
