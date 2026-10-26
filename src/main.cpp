#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
#include <driver/gpio.h>
#include "config.h"
#include "model.h"
#include "display_driver.h"
#include "power_manager.h"
#include "network_manager.h"
#include "ui.h"

// Instantiate Global Queues
QueueHandle_t xQueueUiCmd = nullptr;
QueueHandle_t xQueueUiState = nullptr;

// UI Task (Pinned to Core 1)
void ui_task_entry(void* param) {
    log_i("UI Task started on Core %d", xPortGetCoreID());
    esp_task_wdt_add(NULL);

    // Initialize UI Layout & Widgets
    display_set_rotation(power_manager_get_rotation_180());
    ui_init();

    // Flush the initial frame (Startup Progress Screen) to GRAM before turning on backlight PWM
    lv_timer_handler();
    gfx.waitDMA();
    power_manager_init();

    for (;;) {
        esp_task_wdt_reset();

        // 1. Process pending state events from Network Task (Core 0)
        ui_process_events();

        // 2. Check touch inactivity for backlight power management (auto-dim and deep sleep)
        power_manager_check_inactivity();

        // 3. Handle LVGL animations, timers, and rendering
        uint32_t delay_ms = lv_timer_handler();

        // 4. Dynamic frame delay: sleep 35ms (~28 Hz) while dimmed to save power, or 4..16ms when awake
        if (power_manager_is_dimmed()) {
            delay_ms = 35;
        } else {
            if (delay_ms > 16) delay_ms = 16;
            else if (delay_ms < 4) delay_ms = 4;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

// System Hardware Setup
static void hardware_pins_init() {
    // 1. Release deep-sleep GPIO holds on PA_EN and Backlight, then mute audio amp and keep backlight LOW
    gpio_hold_dis((gpio_num_t)PIN_PA_EN);
    gpio_hold_dis((gpio_num_t)TFT_BL);
    pinMode(PIN_PA_EN, OUTPUT);
    digitalWrite(PIN_PA_EN, HIGH);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW);

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

    // 2. Initialize Task Watchdog Timer (15s timeout, panic on hang)
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = 15000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&wdt_cfg);
#else
    esp_task_wdt_init(15, true);
#endif

    // 3. PSRAM & Memory Diagnostics
    log_i("Total Heap:  %u bytes", ESP.getHeapSize());
    log_i("Free Heap:   %u bytes", ESP.getFreeHeap());
    log_i("Total PSRAM: %u bytes", ESP.getPsramSize());
    log_i("Free PSRAM:  %u bytes", ESP.getFreePsram());

    if (ESP.getPsramSize() == 0) {
        log_w("WARNING: PSRAM was not detected! Check board_build.arduino.memory_type in platformio.ini");
    }

    // 4. Create Inter-Task Communication Queues
    xQueueUiCmd = xQueueCreate(QUEUE_UI_CMD_LEN, sizeof(UiCommand));
    xQueueUiState = xQueueCreate(QUEUE_UI_STATE_LEN, sizeof(UiEvent));

    if (!xQueueUiCmd || !xQueueUiState) {
        log_e("CRITICAL: Failed to create FreeRTOS queues!");
        while (1) delay(1000);
    }

    // 5. Initialize LovyanGFX Display and FT6336G Capacitive Touch FIRST (backlight stays off until first UI frame)
    display_driver_init();

    // 6. Spawn Network & Polling Task on Core 0
    BaseType_t netTaskOk = xTaskCreatePinnedToCore(
        network_task_entry,
        "NetTask",
        NET_TASK_STACK_SIZE,
        nullptr,
        NET_TASK_PRIORITY,
        nullptr,
        NET_TASK_CORE
    );

    // 7. Spawn UI & Display Task on Core 1
    BaseType_t uiTaskOk = xTaskCreatePinnedToCore(
        ui_task_entry,
        "UiTask",
        UI_TASK_STACK_SIZE,
        nullptr,
        UI_TASK_PRIORITY,
        nullptr,
        UI_TASK_CORE
    );

    if (netTaskOk != pdPASS || uiTaskOk != pdPASS) {
        log_e("CRITICAL: Failed to start FreeRTOS tasks (NetTask=%d, UiTask=%d)!", (int)netTaskOk, (int)uiTaskOk);
        while (1) delay(1000);
    }

    log_i("FreeRTOS dual-core tasks started successfully");
}

void loop() {
    // Main Arduino loop yields to FreeRTOS scheduler
    vTaskDelete(NULL);
}
