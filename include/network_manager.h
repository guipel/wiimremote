#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "config.h"
#include "model.h"

class NetworkManager {
public:
    static NetworkManager& getInstance() {
        static NetworkManager instance;
        return instance;
    }

    void init();
    void runTaskLoop();

    // Trigger discovery immediately
    void triggerRescan();

    // Select active streamer by IP or UUID
    void selectDevice(const char* ip);

    // Get active device info
    bool getActiveDevice(WiiMDevice* dev);

private:
    NetworkManager();
    ~NetworkManager() = default;
    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    // Wi-Fi Connection handler
    void handleWiFi();

    // SSDP Discovery
    void sendSSDPQuery();
    void processSSDPPackets();

    // Subnet Scanner Fallback
    void runSubnetScanStep();

    // LinkPlay HTTP API (Persistent TLS Keep-Alive)
    int executeApiGet(const String& cmd, String& outPayload);
    void resetPersistentHttp();

    bool queryDeviceStatus(const char* ip, WiiMDevice* outDevice);
    void pollActiveDevice();
    void fetchDeviceConfig();
    bool fetchTrackMeta();
    void fetchPresetInfo();
    void fetchUpnpTrackDuration();

    // Command dispatch
    bool sendHttpCommand(const String& cmd);
    void seekPosition(uint32_t seek_ms);
    void processIncomingCommands();

    // Device management
    void addOrUpdateDevice(const WiiMDevice& dev);
    void loadSavedDevice();
    void saveActiveDevice(const WiiMDevice& dev);

    // State variables
    bool _wifiConnected;
    unsigned long _lastWiFiCheck;
    unsigned long _lastStatusPoll;
    unsigned long _lastConfigPoll;
    unsigned long _lastMetaPoll;
    unsigned long _lastSSDPBroadcast;
    unsigned long _lastVolumeSent;
    uint8_t _pendingVolume;
    bool _volumePending;

    // Subnet scan state
    bool _isScanning;
    int _scanCurrentHost;
    unsigned long _lastScanStepTime;

    // Active device
    WiiMDevice _activeDevice;
    PlayerState _lastPlayerState;
    bool _hasActiveDevice;
    bool _initialPresetsFetched;
    String _lastKnownTrackTitle;
    bool _metaResolved;
    uint32_t _cachedTrackDuration_ms;
    uint8_t _activeMode;
    uint16_t _activePlicurr;
    uint16_t _activePlicount;

    // Discovered devices list
    DeviceList _deviceList;
    portMUX_TYPE _deviceMux;

    // Persistent TLS socket & HTTP Client
    WiFiClientSecure _persistentClient;
    HTTPClient _persistentHttp;
    String _persistentIp;
    bool _persistentHttpConfigured;

    // Wi-Fi Provisioning & Scanning
    void startWiFiScan();
    void connectWiFi(const char* ssid, const char* pass);
    void forgetWiFi();
    bool _wifiScanning;
    bool _wifiConnecting;
    unsigned long _wifiConnectStart;
    String _pendingConnectSsid;
    String _pendingConnectPass;
};

void network_task_entry(void* param);
