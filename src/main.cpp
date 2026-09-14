#include <Arduino.h>
#include <esp_heap_caps.h>
#include "config.h"
#include "model.h"
#include "display_driver.h"
#include "network_manager.h"
#include "ui.h"

// Instantiate Global Queues
QueueHandle_t xQueueUiCmd = nullptr;
QueueHandle_t xQueueUiState = nullptr;

// UI Task (Pinned to Core 1)
void ui_task_entry(void* param) {
    log_i("UI Task started on Core %d", xPortGetCoreID());

    // Initialize UI Layout & Widgets
    ui_init();

    for (;;) {
        // 1. Process pending state events from Network Task (Core 0)
        ui_process_events();

        // 2. Check touch inactivity for backlight power management (auto-dim)
        display_check_inactivity();

        // 3. Handle LVGL animations, timers, and rendering
        uint32_t delay_ms = lv_timer_handler();

        // 4. Dynamic frame delay: responsive under interaction (down to 4ms), yielding when idle (up to 16ms)
        if (delay_ms > 16) delay_ms = 16;
        else if (delay_ms < 4) delay_ms = 4;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

// System Hardware Setup
static void hardware_pins_init() {
    // 1. Audio Amp Enable (PA_EN): Pin 1 must be driven HIGH immediately to prevent popping and keep amp muted
    pinMode(PIN_PA_EN, OUTPUT);
    digitalWrite(PIN_PA_EN, HIGH);

    // 2. Reserved Codec I2S pins: set to high-impedance input to avoid bus contention
    pinMode(PIN_I2S_MCLK, INPUT_PULLDOWN);
    pinMode(PIN_I2S_BCLK, INPUT_PULLDOWN);
    pinMode(PIN_I2S_WS,   INPUT_PULLDOWN);
    pinMode(PIN_I2S_DOUT, INPUT_PULLDOWN);
    pinMode(PIN_I2S_DIN,  INPUT_PULLDOWN);

    // 3. Status RGB Pin
    pinMode(PIN_RGB_STATUS, OUTPUT);
    digitalWrite(PIN_RGB_STATUS, LOW);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    log_i("=================================================");
    log_i("  WiiM ESP32-S3 Touchscreen Remote Control       ");
    log_i("=================================================");

    // 1. Initialize Hardware Pins
    hardware_pins_init();

    // 2. PSRAM & Memory Diagnostics
    log_i("Total Heap:  %u bytes", ESP.getHeapSize());
    log_i("Free Heap:   %u bytes", ESP.getFreeHeap());
    log_i("Total PSRAM: %u bytes", ESP.getPsramSize());
    log_i("Free PSRAM:  %u bytes", ESP.getFreePsram());

    if (ESP.getPsramSize() == 0) {
        log_w("WARNING: PSRAM was not detected! Check board_build.arduino.memory_type in platformio.ini");
    }

    // 3. Create Inter-Task Communication Queues
    xQueueUiCmd = xQueueCreate(QUEUE_UI_CMD_LEN, sizeof(UiCommand));
    xQueueUiState = xQueueCreate(QUEUE_UI_STATE_LEN, sizeof(UiEvent));

    if (!xQueueUiCmd || !xQueueUiState) {
        log_e("CRITICAL: Failed to create FreeRTOS queues!");
        while (1) delay(1000);
    }

    // 4. Initialize LovyanGFX Display and FT6336G Capacitive Touch
    display_driver_init();

    // 5. Spawn Network & Polling Task on Core 0
    xTaskCreatePinnedToCore(
        network_task_entry,
        "NetTask",
        NET_TASK_STACK_SIZE,
        nullptr,
        NET_TASK_PRIORITY,
        nullptr,
        NET_TASK_CORE
    );

    // 6. Spawn UI & Display Task on Core 1
    xTaskCreatePinnedToCore(
        ui_task_entry,
        "UiTask",
        UI_TASK_STACK_SIZE,
        nullptr,
        UI_TASK_PRIORITY,
        nullptr,
        UI_TASK_CORE
    );

    log_i("FreeRTOS dual-core tasks started successfully");
}

void loop() {
    // Main Arduino loop yields to FreeRTOS scheduler
    vTaskDelete(NULL);
}
