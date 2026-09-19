#pragma once

#include <Arduino.h>
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

    // Select active streamer by IP
    void selectDevice(const char* ip);

    // Get active device info
    bool getActiveDevice(WiiMDevice* dev);

private:
    NetworkManager();
    ~NetworkManager() = default;
    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    void processIncomingCommands();

    unsigned long _lastStatusPoll;
    unsigned long _lastConfigPoll;
    unsigned long _lastSSDPBroadcast;
    unsigned long _lastVolumeSent;
    uint8_t _pendingVolume;
    bool _volumePending;
    bool _initialPresetsFetched;
};

void network_task_entry(void* param);
