#include "display_driver.h"
#include <esp_heap_caps.h>
#include <Preferences.h>

LGFX_ESP32S3_Custom gfx;

// LVGL Display Buffer allocated in PSRAM
static const uint32_t DRAW_BUF_PIXELS = SCREEN_WIDTH * 80;
static lv_color_t *disp_draw_buf1 = nullptr;
static lv_color_t *disp_draw_buf2 = nullptr;
static lv_disp_draw_buf_t disp_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

LGFX_ESP32S3_Custom::LGFX_ESP32S3_Custom() {
    {
        auto cfg = _bus_instance.config();
        cfg.spi_host = SPI2_HOST;
        cfg.spi_mode = 0;
        cfg.freq_write = 40000000;
        cfg.freq_read  = 16000000;
        cfg.spi_3wire  = false;
        cfg.use_lock   = true;
        cfg.dma_channel = SPI_DMA_CH_AUTO;
        cfg.pin_sclk = TFT_SCLK;
        cfg.pin_mosi = TFT_MOSI;
        cfg.pin_miso = TFT_MISO;
        cfg.pin_dc   = TFT_DC;
        _bus_instance.config(cfg);
        _panel_instance.setBus(&_bus_instance);
    }

    {
        auto cfg = _panel_instance.config();
        cfg.pin_cs           = TFT_CS;
        cfg.pin_rst          = TFT_RST;
        cfg.pin_busy         = -1;
        cfg.panel_width      = SCREEN_WIDTH;
        cfg.panel_height     = SCREEN_HEIGHT;
        cfg.offset_x         = 0;
        cfg.offset_y         = 0;
        cfg.offset_rotation  = SCREEN_ROTATION;
        cfg.dummy_read_pixel = 8;
        cfg.dummy_read_bits  = 1;
        cfg.readable         = true;
        cfg.invert           = true;
        cfg.rgb_order        = false;
        cfg.dlen_16bit       = false;
        cfg.bus_shared       = false;
        _panel_instance.config(cfg);
    }

    {
        auto cfg = _light_instance.config();
        cfg.pin_bl      = TFT_BL;
        cfg.invert      = false; // Active HIGH
        cfg.freq        = BL_LEDC_FREQ;
        cfg.pwm_channel = BL_LEDC_CHANNEL;
        _light_instance.config(cfg);
        _panel_instance.setLight(&_light_instance);
    }

    {
        auto cfg = _touch_instance.config();
        cfg.x_min      = 0;
        cfg.x_max      = SCREEN_WIDTH - 1;
        cfg.y_min      = 0;
        cfg.y_max      = SCREEN_HEIGHT - 1;
        cfg.pin_int    = TOUCH_INT;
        cfg.pin_rst    = TOUCH_RST;
        cfg.bus_shared = false;
        cfg.offset_rotation = SCREEN_ROTATION;
        cfg.i2c_port   = TOUCH_I2C_PORT;
        cfg.i2c_addr   = TOUCH_ADDR;
        cfg.pin_sda    = TOUCH_SDA;
        cfg.pin_scl    = TOUCH_SCL;
        cfg.freq       = TOUCH_I2C_FREQ;
        _touch_instance.config(cfg);
        _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
}

static void display_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    gfx.pushImageDMA(area->x1, area->y1, w, h, (const uint16_t *)&color_p->full);
    gfx.waitDMA();
    lv_disp_flush_ready(disp);
}

// Touch input read callback for LVGL
static void touchpad_read_cb(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    uint16_t touchX, touchY;
    bool touched = gfx.getTouch(&touchX, &touchY);

    if (touched) {
        display_notify_touch();
        data->state = LV_INDEV_STATE_PR;

        int32_t x = touchX;
        int32_t y = touchY;

#if TOUCH_SWAP_XY
        int32_t tmp = x;
        x = y;
        y = tmp;
#endif

#if TOUCH_INVERT_X
        x = (SCREEN_WIDTH - 1) - x;
        if (x < 0) x = 0;
#endif

#if TOUCH_INVERT_Y
        y = (SCREEN_HEIGHT - 1) - y;
        if (y < 0) y = 0;
#endif

        data->point.x = (lv_coord_t)x;
        data->point.y = (lv_coord_t)y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void display_driver_init() {
    // 1. Initialize LovyanGFX
    gfx.init();
    gfx.setRotation(SCREEN_ROTATION);
    gfx.setColorDepth(16);
    display_set_backlight(BL_DEFAULT_BRIGHT);

    // 2. Initialize LVGL core
    lv_init();

    // 3. Allocate double draw buffers in Internal DMA SRAM (zero PSRAM bus contention)
    disp_draw_buf1 = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * DRAW_BUF_PIXELS, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    disp_draw_buf2 = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * DRAW_BUF_PIXELS, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);

    if (!disp_draw_buf1) {
        log_e("Internal DMA alloc failed for disp_draw_buf1, falling back to PSRAM");
        disp_draw_buf1 = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * DRAW_BUF_PIXELS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!disp_draw_buf2) {
        log_w("Internal DMA alloc failed for disp_draw_buf2, falling back to PSRAM");
        disp_draw_buf2 = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * DRAW_BUF_PIXELS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }

    lv_disp_draw_buf_init(&disp_buf, disp_draw_buf1, disp_draw_buf2, disp_draw_buf2 ? DRAW_BUF_PIXELS : (SCREEN_WIDTH * 20));

    // 4. Register LVGL Display Driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.flush_cb = display_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    // 5. Register LVGL Touch Input Driver
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read_cb;
    lv_indev_drv_register(&indev_drv);

    log_i("Display and Touch driver initialized successfully");
}

void display_set_backlight(uint8_t brightness) {
    gfx.setBrightness(brightness);
}

static unsigned long s_last_touch_ms = 0;
static bool s_is_dimmed = false;
static uint32_t s_dim_timeout_ms = 30000;
static uint16_t s_dim_timeout_sec = 30;
static bool s_dim_pref_loaded = false;

static void load_dim_preferences() {
    if (s_dim_pref_loaded) return;
    s_dim_pref_loaded = true;
    Preferences prefs;
    if (prefs.begin("wiimremote", true)) {
        s_dim_timeout_sec = prefs.getUShort("dim_sec", 30);
        s_dim_timeout_ms = (uint32_t)s_dim_timeout_sec * 1000;
        prefs.end();
        log_i("Loaded screen dim timeout: %u sec", s_dim_timeout_sec);
    }
}

void display_set_dim_timeout_sec(uint16_t seconds) {
    s_dim_timeout_sec = seconds;
    s_dim_timeout_ms = (uint32_t)seconds * 1000;
    Preferences prefs;
    if (prefs.begin("wiimremote", false)) {
        prefs.putUShort("dim_sec", seconds);
        prefs.end();
        log_i("Saved screen dim timeout: %u sec", seconds);
    }
    // If set to Never (0), restore brightness immediately if dimmed
    if (seconds == 0 && s_is_dimmed) {
        s_is_dimmed = false;
        display_set_backlight(BL_DEFAULT_BRIGHT);
    }
}

uint16_t display_get_dim_timeout_sec() {
    load_dim_preferences();
    return s_dim_timeout_sec;
}

void display_notify_touch() {
    s_last_touch_ms = millis();
    if (s_is_dimmed) {
        s_is_dimmed = false;
        display_set_backlight(BL_DEFAULT_BRIGHT);
    }
}

void display_check_inactivity() {
    load_dim_preferences();
    if (s_dim_timeout_ms == 0) return; // 0 = Never / Disabled

    if (s_last_touch_ms == 0) {
        s_last_touch_ms = millis();
        return;
    }
    // Dim backlight to ~15% after configured timeout of inactivity
    if (!s_is_dimmed && (millis() - s_last_touch_ms > s_dim_timeout_ms)) {
        s_is_dimmed = true;
        display_set_backlight(38); // 15% of 255
    }
}
