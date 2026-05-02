#include "encoder.h"
#include "board_pins.h"
#include <Arduino.h>
#include "driver/gpio.h"
#include "esp_timer.h"

// Timer-polled bidirectional switch encoder
// Matches Waveshare official bidi_switch_knob pattern:
//   - CW rotation: only channel A (GPIO 8) pulses
//   - CCW rotation: only channel B (GPIO 7) pulses
//   - 3ms polling interval, 2-tick debounce (6ms)

#define TICKS_INTERVAL_US  3000   // 3ms in microseconds
#define DEBOUNCE_TICKS     2      // ticks at new level before accepting edge

// Per-channel state
static uint8_t level_a = 0;
static uint8_t level_b = 0;
static uint8_t debounce_a = 0;
static uint8_t debounce_b = 0;

// Shared encoder state (written by timer, read by main loop)
static volatile int32_t enc_count = 0;

// Screen state
static int screen_idx = 0;
static int32_t last_switch = 0;

static esp_timer_handle_t enc_timer = NULL;

// Bidi switch channel processor — direct port of Waveshare's process_knob_channel().
// When pin goes LOW: track how long it stays low (debounce counter).
// When pin goes HIGH after being LOW for >= DEBOUNCE_TICKS: confirmed rising edge.
static void process_channel(uint8_t current, uint8_t *prev, uint8_t *deb_cnt, int delta) {
    if (current == 0) {
        // Pin is LOW — accumulate debounce if level is stable
        if (current != *prev)
            *deb_cnt = 0;       // just went low: reset counter
        else
            (*deb_cnt)++;       // still low: count ticks
    } else {
        // Pin is HIGH
        if (current != *prev && ++(*deb_cnt) >= DEBOUNCE_TICKS) {
            // Rising edge after pin was low for long enough — confirmed step
            *deb_cnt = 0;
            enc_count += delta;
            Serial.printf("[ENC ] %s count=%d\n", delta > 0 ? "CW " : "CCW", (int)enc_count);
        } else {
            *deb_cnt = 0;       // bounce or still high
        }
    }
    *prev = current;
}

// Timer callback — runs every 3ms on ESP timer task
static void IRAM_ATTR enc_timer_cb(void *arg) {
    uint8_t a = gpio_get_level((gpio_num_t)PIN_ENC_A);
    uint8_t b = gpio_get_level((gpio_num_t)PIN_ENC_B);

    process_channel(a, &level_a, &debounce_a, +1);  // Channel A = CW
    process_channel(b, &level_b, &debounce_b, -1);  // Channel B = CCW
}

void encoder_init() {
    // Configure encoder pins: input, pull-up, no interrupts
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << PIN_ENC_A) | (1ULL << PIN_ENC_B);
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);

    // Read initial pin levels
    level_a = gpio_get_level((gpio_num_t)PIN_ENC_A);
    level_b = gpio_get_level((gpio_num_t)PIN_ENC_B);

    // Create and start 3ms periodic timer
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = enc_timer_cb;
    timer_args.name = "enc_poll";
    timer_args.dispatch_method = ESP_TIMER_TASK;
    esp_timer_create(&timer_args, &enc_timer);
    esp_timer_start_periodic(enc_timer, TICKS_INTERVAL_US);

    Serial.println("[ENC ] Encoder ready (timer-polled bidi switch, 3ms)");
}

void encoder_poll() {
    // No-op: timer handles everything
}

int32_t encoder_get_count() {
    return enc_count;
}

void encoder_update_screen() {
    int32_t delta = enc_count - last_switch;
    if (delta >= 1) {
        screen_idx = (screen_idx + 1) % 3;
        last_switch = enc_count;
    } else if (delta <= -1) {
        screen_idx = (screen_idx - 1 + 3) % 3;
        last_switch = enc_count;
    }
}

int encoder_get_screen(int screenCount) {
    return screen_idx;
}
