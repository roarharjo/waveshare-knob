#include "display.h"
#include "board_pins.h"
#include <Arduino.h>
#include "driver/spi_master.h"
#include <lvgl.h>

// ============ RAW QSPI DRIVER (working, proven) ============
static spi_device_handle_t spi_dev;

static inline void cs_low()  { GPIO.out_w1tc = (1 << PIN_LCD_CS); }
static inline void cs_high() { GPIO.out_w1ts = (1 << PIN_LCD_CS); }

static void write_cmd_addr_data(uint8_t cmd_bits, uint32_t cmd, uint8_t addr_bits,
                                 uint32_t address, const uint8_t *data, size_t length,
                                 uint8_t bus_width = 1) {
    spi_transaction_ext_t desc = {};
    desc.base.flags = SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_DUMMY;
    if (bus_width == 4) desc.base.flags |= SPI_TRANS_MODE_QIO;
    desc.command_bits = cmd_bits;
    desc.address_bits = addr_bits;
    desc.dummy_bits = 0;
    desc.base.cmd = cmd;
    desc.base.addr = address;
    const size_t max_chunk = 32768;
    do {
        size_t chunk = (length > max_chunk) ? max_chunk : length;
        if (data && chunk) {
            desc.base.length = chunk * 8;
            desc.base.tx_buffer = data;
            length -= chunk;
            data += chunk;
        } else {
            length = 0;
            desc.base.length = 0;
        }
        spi_device_polling_start(spi_dev, (spi_transaction_t *)&desc, portMAX_DELAY);
        spi_device_polling_end(spi_dev, portMAX_DELAY);
        desc.command_bits = 0;
        desc.address_bits = 0;
    } while (length != 0);
}

static void qspi_write_reg(uint8_t reg, const uint8_t *data, size_t len) {
    cs_low();
    write_cmd_addr_data(8, 0x02, 24, (uint32_t)reg << 8, data, len, 1);
    cs_high();
}
static void qspi_write_reg8(uint8_t reg, uint8_t val) { qspi_write_reg(reg, &val, 1); }

// Send pixel data to a region
static void lcd_draw_bitmap(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                             const uint8_t *data, size_t len) {
    uint8_t ca[] = {(uint8_t)(x0>>8), (uint8_t)x0, (uint8_t)(x1>>8), (uint8_t)x1};
    qspi_write_reg(0x2A, ca, 4);
    uint8_t ra[] = {(uint8_t)(y0>>8), (uint8_t)y0, (uint8_t)(y1>>8), (uint8_t)y1};
    qspi_write_reg(0x2B, ra, 4);
    cs_low();
    write_cmd_addr_data(8, 0x32, 24, 0x2C << 8, data, len, 4);
    cs_high();
}

// ============ PANEL INIT (Waveshare-specific) ============
static void st77916_init_panel() {
    pinMode(PIN_LCD_RST, OUTPUT);
    digitalWrite(PIN_LCD_RST, HIGH); delay(100);
    digitalWrite(PIN_LCD_RST, LOW);  delay(10);
    digitalWrite(PIN_LCD_RST, HIGH); delay(120);

    qspi_write_reg8(0x3A, 0x55);
    qspi_write_reg8(0xF0, 0x28); qspi_write_reg8(0xF2, 0x28);
    qspi_write_reg8(0x73, 0xF0); qspi_write_reg8(0x7C, 0xD1);
    qspi_write_reg8(0x83, 0xE0); qspi_write_reg8(0x84, 0x61);
    qspi_write_reg8(0xF2, 0x82); qspi_write_reg8(0xF0, 0x00);
    qspi_write_reg8(0xF0, 0x01); qspi_write_reg8(0xF1, 0x01);
    qspi_write_reg8(0xB0, 0x56); qspi_write_reg8(0xB1, 0x4D);
    qspi_write_reg8(0xB2, 0x24); qspi_write_reg8(0xB4, 0x87);
    qspi_write_reg8(0xB5, 0x44); qspi_write_reg8(0xB6, 0x8B);
    qspi_write_reg8(0xB7, 0x40); qspi_write_reg8(0xB8, 0x86);
    qspi_write_reg8(0xBA, 0x00); qspi_write_reg8(0xBB, 0x08);
    qspi_write_reg8(0xBC, 0x08); qspi_write_reg8(0xBD, 0x00);
    qspi_write_reg8(0xC0, 0x80); qspi_write_reg8(0xC1, 0x10);
    qspi_write_reg8(0xC2, 0x37); qspi_write_reg8(0xC3, 0x80);
    qspi_write_reg8(0xC4, 0x10); qspi_write_reg8(0xC5, 0x37);
    qspi_write_reg8(0xC6, 0xA9); qspi_write_reg8(0xC7, 0x41);
    qspi_write_reg8(0xC8, 0x01); qspi_write_reg8(0xC9, 0xA9);
    qspi_write_reg8(0xCA, 0x41); qspi_write_reg8(0xCB, 0x01);
    qspi_write_reg8(0xD0, 0x91); qspi_write_reg8(0xD1, 0x68);
    qspi_write_reg8(0xD2, 0x68);
    uint8_t f5[] = {0x00, 0xA5};
    qspi_write_reg(0xF5, f5, 2);
    qspi_write_reg8(0xDD, 0x4F); qspi_write_reg8(0xDE, 0x4F);
    qspi_write_reg8(0xF1, 0x10); qspi_write_reg8(0xF0, 0x00);
    qspi_write_reg8(0xF0, 0x02);
    uint8_t e0[] = {0xF0,0x0A,0x10,0x09,0x09,0x36,0x35,0x33,0x4A,0x29,0x15,0x15,0x2E,0x34};
    qspi_write_reg(0xE0, e0, 14);
    uint8_t e1[] = {0xF0,0x0A,0x0F,0x08,0x08,0x05,0x34,0x33,0x4A,0x39,0x15,0x15,0x2D,0x33};
    qspi_write_reg(0xE1, e1, 14);
    qspi_write_reg8(0xF0, 0x10); qspi_write_reg8(0xF3, 0x10);
    qspi_write_reg8(0xE0, 0x07); qspi_write_reg8(0xE1, 0x00);
    qspi_write_reg8(0xE2, 0x00); qspi_write_reg8(0xE3, 0x00);
    qspi_write_reg8(0xE4, 0xE0); qspi_write_reg8(0xE5, 0x06);
    qspi_write_reg8(0xE6, 0x21); qspi_write_reg8(0xE7, 0x01);
    qspi_write_reg8(0xE8, 0x05); qspi_write_reg8(0xE9, 0x02);
    qspi_write_reg8(0xEA, 0xDA); qspi_write_reg8(0xEB, 0x00);
    qspi_write_reg8(0xEC, 0x00); qspi_write_reg8(0xED, 0x0F);
    qspi_write_reg8(0xEE, 0x00); qspi_write_reg8(0xEF, 0x00);
    qspi_write_reg8(0xF8, 0x00); qspi_write_reg8(0xF9, 0x00);
    qspi_write_reg8(0xFA, 0x00); qspi_write_reg8(0xFB, 0x00);
    qspi_write_reg8(0xFC, 0x00); qspi_write_reg8(0xFD, 0x00);
    qspi_write_reg8(0xFE, 0x00); qspi_write_reg8(0xFF, 0x00);
    qspi_write_reg8(0x60, 0x40); qspi_write_reg8(0x61, 0x04);
    qspi_write_reg8(0x62, 0x00); qspi_write_reg8(0x63, 0x42);
    qspi_write_reg8(0x64, 0xD9); qspi_write_reg8(0x65, 0x00);
    qspi_write_reg8(0x66, 0x00); qspi_write_reg8(0x67, 0x00);
    qspi_write_reg8(0x68, 0x00); qspi_write_reg8(0x69, 0x00);
    qspi_write_reg8(0x6A, 0x00); qspi_write_reg8(0x6B, 0x00);
    qspi_write_reg8(0x70, 0x40); qspi_write_reg8(0x71, 0x03);
    qspi_write_reg8(0x72, 0x00); qspi_write_reg8(0x73, 0x42);
    qspi_write_reg8(0x74, 0xD8); qspi_write_reg8(0x75, 0x00);
    qspi_write_reg8(0x76, 0x00); qspi_write_reg8(0x77, 0x00);
    qspi_write_reg8(0x78, 0x00); qspi_write_reg8(0x79, 0x00);
    qspi_write_reg8(0x7A, 0x00); qspi_write_reg8(0x7B, 0x00);
    qspi_write_reg8(0x80, 0x48); qspi_write_reg8(0x81, 0x00);
    qspi_write_reg8(0x82, 0x06); qspi_write_reg8(0x83, 0x02);
    qspi_write_reg8(0x84, 0xD6); qspi_write_reg8(0x85, 0x04);
    qspi_write_reg8(0x86, 0x00); qspi_write_reg8(0x87, 0x00);
    qspi_write_reg8(0x88, 0x48); qspi_write_reg8(0x89, 0x00);
    qspi_write_reg8(0x8A, 0x08); qspi_write_reg8(0x8B, 0x02);
    qspi_write_reg8(0x8C, 0xD8); qspi_write_reg8(0x8D, 0x04);
    qspi_write_reg8(0x8E, 0x00); qspi_write_reg8(0x8F, 0x00);
    qspi_write_reg8(0x90, 0x48); qspi_write_reg8(0x91, 0x00);
    qspi_write_reg8(0x92, 0x0A); qspi_write_reg8(0x93, 0x02);
    qspi_write_reg8(0x94, 0xDA); qspi_write_reg8(0x95, 0x04);
    qspi_write_reg8(0x96, 0x00); qspi_write_reg8(0x97, 0x00);
    qspi_write_reg8(0x98, 0x48); qspi_write_reg8(0x99, 0x00);
    qspi_write_reg8(0x9A, 0x0C); qspi_write_reg8(0x9B, 0x02);
    qspi_write_reg8(0x9C, 0xDC); qspi_write_reg8(0x9D, 0x04);
    qspi_write_reg8(0x9E, 0x00); qspi_write_reg8(0x9F, 0x00);
    qspi_write_reg8(0xA0, 0x48); qspi_write_reg8(0xA1, 0x00);
    qspi_write_reg8(0xA2, 0x05); qspi_write_reg8(0xA3, 0x02);
    qspi_write_reg8(0xA4, 0xD5); qspi_write_reg8(0xA5, 0x04);
    qspi_write_reg8(0xA6, 0x00); qspi_write_reg8(0xA7, 0x00);
    qspi_write_reg8(0xA8, 0x48); qspi_write_reg8(0xA9, 0x00);
    qspi_write_reg8(0xAA, 0x07); qspi_write_reg8(0xAB, 0x02);
    qspi_write_reg8(0xAC, 0xD7); qspi_write_reg8(0xAD, 0x04);
    qspi_write_reg8(0xAE, 0x00); qspi_write_reg8(0xAF, 0x00);
    qspi_write_reg8(0xB0, 0x48); qspi_write_reg8(0xB1, 0x00);
    qspi_write_reg8(0xB2, 0x09); qspi_write_reg8(0xB3, 0x02);
    qspi_write_reg8(0xB4, 0xD9); qspi_write_reg8(0xB5, 0x04);
    qspi_write_reg8(0xB6, 0x00); qspi_write_reg8(0xB7, 0x00);
    qspi_write_reg8(0xB8, 0x48); qspi_write_reg8(0xB9, 0x00);
    qspi_write_reg8(0xBA, 0x0B); qspi_write_reg8(0xBB, 0x02);
    qspi_write_reg8(0xBC, 0xDB); qspi_write_reg8(0xBD, 0x04);
    qspi_write_reg8(0xBE, 0x00); qspi_write_reg8(0xBF, 0x00);
    qspi_write_reg8(0xC0, 0x10); qspi_write_reg8(0xC1, 0x47);
    qspi_write_reg8(0xC2, 0x56); qspi_write_reg8(0xC3, 0x65);
    qspi_write_reg8(0xC4, 0x74); qspi_write_reg8(0xC5, 0x88);
    qspi_write_reg8(0xC6, 0x99); qspi_write_reg8(0xC7, 0x01);
    qspi_write_reg8(0xC8, 0xBB); qspi_write_reg8(0xC9, 0xAA);
    qspi_write_reg8(0xD0, 0x10); qspi_write_reg8(0xD1, 0x47);
    qspi_write_reg8(0xD2, 0x56); qspi_write_reg8(0xD3, 0x65);
    qspi_write_reg8(0xD4, 0x74); qspi_write_reg8(0xD5, 0x88);
    qspi_write_reg8(0xD6, 0x99); qspi_write_reg8(0xD7, 0x01);
    qspi_write_reg8(0xD8, 0xBB); qspi_write_reg8(0xD9, 0xAA);
    qspi_write_reg8(0xF3, 0x01); qspi_write_reg8(0xF0, 0x00);
    qspi_write_reg8(0x21, 0x00);
    qspi_write_reg8(0x11, 0x00); delay(120);
    qspi_write_reg8(0x29, 0x00);
    qspi_write_reg8(0x36, 0x00); delay(20);
    Serial.println("[DISP] Panel init done");
}

// ============ LVGL INTEGRATION ============
static lv_display_t *lvgl_disp = NULL;

static uint32_t flush_count = 0;

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint16_t x0 = area->x1, y0 = area->y1;
    uint16_t x1 = area->x2, y1 = area->y2;
    size_t len = (x1 - x0 + 1) * (y1 - y0 + 1) * 2;
    lcd_draw_bitmap(x0, y0, x1, y1, px_map, len);
    flush_count++;
    lv_display_flush_ready(disp);
}

// ============ PUBLIC API ============
void display_init() {
    Serial.println("[DISP] Init QSPI + LVGL...");

    pinMode(PIN_LCD_CS, OUTPUT);
    digitalWrite(PIN_LCD_CS, HIGH);

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = PIN_LCD_D0;
    buscfg.miso_io_num = PIN_LCD_D1;
    buscfg.sclk_io_num = PIN_LCD_PCLK;
    buscfg.quadwp_io_num = PIN_LCD_D2;
    buscfg.quadhd_io_num = PIN_LCD_D3;
    buscfg.max_transfer_sz = 65536;
    buscfg.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS;
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {};
    devcfg.mode = 0;
    devcfg.clock_speed_hz = 40000000;
    devcfg.spics_io_num = -1;
    devcfg.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;
    devcfg.queue_size = 1;
    spi_bus_add_device(SPI2_HOST, &devcfg, &spi_dev);
    spi_device_acquire_bus(spi_dev, portMAX_DELAY);

    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_LCD_BL, 0);
    ledcWrite(0, 200);

    st77916_init_panel();

    // LVGL init
    lv_init();

    // Create display (360x360, 16-bit color)
    lvgl_disp = lv_display_create(360, 360);
    lv_display_set_flush_cb(lvgl_disp, lvgl_flush_cb);

    // Partial render with DMA-capable internal RAM buffers
    size_t buf_size = 360 * 20 * sizeof(lv_color_t);  // 20 rows at a time
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    lv_display_set_buffers(lvgl_disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    Serial.printf("[DISP] LVGL ready, buf=%u bytes\n", buf_size);
}

// Current screen state
static Screen cur_screen = SCREEN_CLOCK;
static lv_obj_t *scr = NULL;
static lv_obj_t *lbl_time = NULL;
static lv_obj_t *lbl_date = NULL;
static lv_obj_t *lbl_weather = NULL;
static lv_obj_t *lbl_info = NULL;
static lv_obj_t *lbl_debug = NULL;

static void rebuild_screen(Screen s) {
    if (scr) lv_obj_del(scr);
    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Screen indicator dots
    for (int i = 0; i < SCREEN_COUNT; i++) {
        lv_obj_t *dot = lv_obj_create(scr);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, 5, 0);
        lv_obj_set_pos(dot, 155 + i * 25, 15);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_color(dot, (i == s) ? lv_color_hex(0x00FFFF) : lv_color_hex(0x404040), 0);
    }

    lbl_time = lbl_date = lbl_weather = lbl_info = lbl_debug = NULL;

    if (s == SCREEN_CLOCK) {
        lbl_time = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(lbl_time, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, -40);
        lv_label_set_text(lbl_time, "--:--:--");

        lbl_date = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl_date, lv_color_hex(0xAAAAAA), 0);
        lv_obj_align(lbl_date, LV_ALIGN_CENTER, 0, 20);
        lv_label_set_text(lbl_date, "---");

        lbl_weather = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl_weather, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl_weather, lv_color_hex(0x888888), 0);
        lv_obj_align(lbl_weather, LV_ALIGN_CENTER, 0, 80);
        lv_label_set_text(lbl_weather, "");
    } else if (s == SCREEN_WEATHER) {
        lbl_time = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(lbl_time, lv_color_hex(0xFFA500), 0);
        lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, -40);
        lv_label_set_text(lbl_time, "0.0C");

        lbl_date = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl_date, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(lbl_date, LV_ALIGN_CENTER, 0, 20);
        lv_label_set_text(lbl_date, "N/A");

        lbl_info = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl_info, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl_info, lv_color_hex(0x666666), 0);
        lv_obj_align(lbl_info, LV_ALIGN_CENTER, 0, 80);
    } else {
        lbl_info = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl_info, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl_info, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_text_line_space(lbl_info, 8, 0);
        lv_obj_align(lbl_info, LV_ALIGN_TOP_LEFT, 40, 45);
        lv_label_set_text(lbl_info, "Loading...");
    }

    lv_screen_load(scr);
    cur_screen = s;
}

void display_draw_clock(Screen active, const String& timeStr, const String& dateStr, const String& weatherSummary) {
    if (!scr || cur_screen != active) rebuild_screen(active);
    if (lbl_time) lv_label_set_text(lbl_time, timeStr.c_str());
    if (lbl_date) lv_label_set_text(lbl_date, dateStr.c_str());
    if (lbl_weather) lv_label_set_text(lbl_weather, weatherSummary.c_str());
    lv_obj_invalidate(lv_screen_active());
}

void display_set_debug(const char *text) {
    if (!scr) return;
    if (!lbl_debug) {
        lbl_debug = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl_debug, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_debug, lv_color_hex(0x00FF00), 0);
        lv_obj_align(lbl_debug, LV_ALIGN_BOTTOM_MID, 0, -15);
    }
    lv_label_set_text(lbl_debug, text);
}

void display_draw_weather(Screen active, float temp, const String& condition, const String& lastUpdate) {
    if (!scr || cur_screen != active) rebuild_screen(active);
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1fC", temp);
    if (lbl_time) lv_label_set_text(lbl_time, buf);
    if (lbl_date) lv_label_set_text(lbl_date, condition.c_str());
    if (lbl_info) {
        String s = "Vadso  |  Updated: " + lastUpdate;
        lv_label_set_text(lbl_info, s.c_str());
    }
    lv_obj_invalidate(lv_screen_active());
}

void display_draw_diag(Screen active, const DiagData& d) {
    if (!scr || cur_screen != active) rebuild_screen(active);
    if (lbl_info) {
        char buf[256];
        uint32_t s = d.uptime;
        snprintf(buf, sizeof(buf),
            "RSSI:     %d dBm\n"
            "Heap:     %u KB\n"
            "PSRAM:    %u KB\n"
            "Uptime:   %uh %um %us\n"
            "NTP:      %s\n"
            "Weather:  %s\n"
            "Encoder:  %d\n"
            "Touch:    %d, %d",
            d.rssi, (unsigned)(d.freeHeap/1024), (unsigned)(d.freePsram/1024),
            (unsigned)(s/3600), (unsigned)((s%3600)/60), (unsigned)(s%60),
            d.lastNtpSync.c_str(), d.lastWeatherFetch.c_str(),
            (int)d.encoderCount, d.touchX, d.touchY);
        lv_label_set_text(lbl_info, buf);
    }
    lv_obj_invalidate(lv_screen_active());
}

void display_draw_touch_overlay(int x, int y) {}

void display_show_message(const char* msg, uint16_t color) {
    if (!scr) rebuild_screen(SCREEN_CLOCK);
    if (lbl_time) lv_label_set_text(lbl_time, msg);
}

bool display_get_touch(int* x, int* y) { return false; }

void display_flush() {
    lv_refr_now(lvgl_disp);
}

void display_set_brightness(uint8_t level) { ledcWrite(0, level); }
