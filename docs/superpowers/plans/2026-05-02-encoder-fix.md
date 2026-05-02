# Encoder Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the broken ISR-based encoder driver with a timer-polled bidi switch driver that reliably detects CW/CCW rotation.

**Architecture:** A single `esp_timer` fires every 3ms and reads GPIO 8 (channel A) and GPIO 7 (channel B) directly. Per-channel counter-based debounce confirms rising edges after 2 stable ticks (6ms). This matches the Waveshare official `bidi_switch_knob` pattern exactly.

**Tech Stack:** ESP-IDF `esp_timer` + `driver/gpio.h`, Arduino framework, PlatformIO

**Spec:** `docs/superpowers/specs/2026-05-02-encoder-fix-design.md`

---

## File Structure

| File | Role | Change |
|---|---|---|
| `src/encoder.cpp` | Timer-polled bidi switch encoder driver | Full rewrite |
| `src/encoder.h` | Public API (unchanged signatures) | No change |
| `platformio.ini` | Build config | Remove `ESP32Encoder` lib |
| `include/board_pins.h` | Pin definitions | No change (already correct) |
| `src/main.cpp` | Main loop | No change (API is stable) |

---

### Task 1: Remove the ESP32Encoder quadrature library

This library is for quadrature encoders. The hardware is a bidirectional switch. Removing it first ensures nothing depends on it.

**Files:**
- Modify: `platformio.ini:37-39`

- [ ] **Step 1: Remove ESP32Encoder from lib_deps**

In `platformio.ini`, change the `lib_deps` section from:

```ini
lib_deps =
  lvgl/lvgl@~9.2.0
  madhephaestus/ESP32Encoder
  bblanchon/ArduinoJson@^7
```

to:

```ini
lib_deps =
  lvgl/lvgl@~9.2.0
  bblanchon/ArduinoJson@^7
```

- [ ] **Step 2: Verify no code imports ESP32Encoder**

Run: `grep -r "ESP32Encoder" src/ include/`

Expected: No matches. The current `encoder.cpp` uses raw `driver/gpio.h` and `esp_timer.h`, not the ESP32Encoder library. The library was a leftover from an earlier attempt.

- [ ] **Step 3: Commit**

```bash
git add platformio.ini
git commit -m "chore: remove ESP32Encoder quadrature library (wrong encoder type)"
```

---

### Task 2: Rewrite encoder.cpp — timer-polled bidi switch driver

This is the core change. Replace the entire ISR-based implementation with a timer-polled state machine matching the Waveshare `bidi_switch_knob.c` pattern.

**Files:**
- Rewrite: `src/encoder.cpp`

**Reference:** `temp_demo/.../bidi_switch_knob.c` lines 20-21 (constants), 64-88 (`process_knob_channel`), 90-112 (`knob_handler` + timer callback), 270-291 (GPIO init)

- [ ] **Step 1: Write the new encoder.cpp**

Replace the entire contents of `src/encoder.cpp` with:

```cpp
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
```

- [ ] **Step 2: Verify encoder.h API is unchanged**

The header should still contain exactly these signatures:

```cpp
#pragma once
#include <stdint.h>

void encoder_init();
void encoder_poll();
void encoder_update_screen();
int32_t encoder_get_count();
int encoder_get_screen(int screenCount);
```

Run: `cat src/encoder.h`

Expected: Matches the above. No changes needed.

- [ ] **Step 3: Build**

Run: `pio run -e knob`

Expected: Compiles with 0 errors. Possible warnings about `Serial.printf` in timer callback (acceptable — `Serial` is safe on ESP32 from timer task context, just slower than UART direct write).

- [ ] **Step 4: Commit**

```bash
git add src/encoder.cpp
git commit -m "feat: rewrite encoder to timer-polled bidi switch driver

Replace ISR-based encoder (falling edge, 50ms debounce) with
timer-polled approach matching Waveshare official bidi_switch_knob:
- esp_timer at 3ms interval
- counter-based debounce (2 ticks = 6ms)
- rising edge detection per channel
- Channel A = CW, Channel B = CCW"
```

---

### Task 3: Flash and verify on hardware

This is the acceptance test — the encoder must work reliably on the actual device.

**Files:** None (hardware test only)

- [ ] **Step 1: Flash firmware**

Run: `pio run -e knob --target upload`

Expected: Upload succeeds to COM9.

- [ ] **Step 2: Open serial monitor**

Run: `pio device monitor -p COM9 -b 115200`

Expected: See startup messages including `[ENC ] Encoder ready (timer-polled bidi switch, 3ms)`

- [ ] **Step 3: Test CW rotation**

Turn the dial clockwise slowly, one detent at a time.

Expected for each detent:
```
[ENC ] CW  count=1
[ENC ] CW  count=2
[ENC ] CW  count=3
```

Verify: exactly one `CW` message per detent. No double-counting. No missed steps.

- [ ] **Step 4: Test CCW rotation**

Turn the dial counter-clockwise slowly, one detent at a time.

Expected for each detent:
```
[ENC ] CCW count=2
[ENC ] CCW count=1
[ENC ] CCW count=0
```

Verify: exactly one `CCW` message per detent. No phantom steps.

- [ ] **Step 5: Test screen switching**

The heartbeat log shows which screen is active: `scr=0` (Clock), `scr=1` (Weather), `scr=2` (Diagnostics).

Turn CW one click: screen advances 0→1. Turn CW again: 1→2. Turn CW again: 2→0 (wraps). Turn CCW: 0→2. Turn CCW: 2→1.

Expected: Display changes match encoder direction, one screen per detent.

- [ ] **Step 6: Test fast rotation**

Spin the dial quickly in both directions.

Expected: No crashes, no lockups. Count may jump by several, but every step is logged and screen cycles correctly.

- [ ] **Step 7: Commit verification note (optional)**

If everything works:

```bash
git commit --allow-empty -m "test: encoder hardware verified — reliable CW/CCW detection"
```
