#pragma once

#include <Arduino.h>
#include "config.h"
#include "model.h"

class WiiMNetworkManager {
public:
    static WiiMNetworkManager& getInstance() {
        static WiiMNetworkManager instance;
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

    // Synchronize poll timestamps after an immediate active-device fetch
    void markActiveDevicePolled();

private:
    WiiMNetworkManager();
    ~WiiMNetworkManager() = default;
    WiiMNetworkManager(const WiiMNetworkManager&) = delete;
    WiiMNetworkManager& operator=(const WiiMNetworkManager&) = delete;

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
