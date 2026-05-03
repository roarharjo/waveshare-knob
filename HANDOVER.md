# Waveshare ESP32-S3-Knob-Touch-LCD-1.8 — Technical Reference

## Hardware Summary

- **MCU**: ESP32-S3 (QFN56, rev 0.2), 240MHz dual-core, 16MB flash, 8MB OPI PSRAM
- **Display**: 360x360 round IPS LCD, ST77916 controller over QSPI
- **Touch**: CST816T capacitive touch, I2C address 0x15
- **Encoder**: Bidirectional switch (NOT quadrature — each direction pulses only one channel)
- **Haptic**: DRV2605L on I2C address 0x5A, LRA motor
- **Audio**: PCM5100A DAC + MEMS mic (not used)
- **Two ESP chips**: ESP32-S3 (main) and ESP32 (secondary, BT/audio). USB-C orientation selects chip: one side = ESP32-S3 (COM9, VID 303A), flipped = ESP32 (COM7, CH340)
- **MAC**: ac:a7:04:ef:46:1c

## Verified Pin Mapping

| Function | GPIO | Notes |
|---|---|---|
| LCD CLK (PCLK) | 13 | QSPI clock |
| LCD D0 (MOSI) | 15 | |
| LCD D1 | 16 | |
| LCD D2 | 17 | |
| LCD D3 | 18 | |
| LCD CS | 14 | Manual toggle via GPIO |
| LCD RST | 21 | Active low, needs 10ms pulse |
| LCD Backlight | 47 | PWM via ledcWrite |
| I2C SDA | 11 | Shared bus: touch + haptic |
| I2C SCL | 12 | NOT GPIO 10! |
| Touch INT | 9 | Active LOW, pulsing (see Touch section) |
| Touch RST | 10 | Active low |
| Encoder A | 8 | CW channel |
| Encoder B | 7 | CCW channel |

---

## 1. Display — ST77916 QSPI

### The Problem
The ST77916 uses QSPI (4 data lines, no DC pin). Standard libraries fail:
- Arduino_GFX `Panel_ST77916` — garbled (wrong QSPI opcode framing)
- LovyanGFX `Panel_ST77916` — no QSPI support
- Custom SPI with `command_bits=8, address_bits=24` — garbled
- Custom SPI with `command_bits=32` — ESP-IDF 4.4 cmd field is uint16_t

### Working Solution (ESPHome approach)
SPI device config: `command_bits=0, address_bits=0`, flags `SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY`.

Per-transaction: `SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY`.

- **Register writes**: `cmd_bits=8, cmd=0x02, addr_bits=24, addr=(reg<<8)`, single-line data
- **Pixel writes**: `cmd_bits=8, cmd=0x32, addr_bits=24, addr=(0x2C<<8)`, `SPI_TRANS_MODE_QIO` for quad data

CS is toggled manually via GPIO (not SPI peripheral).

### Init Sequence
The display requires a **Waveshare-specific init sequence** that differs from all generic ST77916 init tables. Key differences from Arduino_GFX/Espressif defaults:
- `0xF0=0x28` (not `0x08`), `0x73=0xF0`, `0x7C=0xD1`
- Completely different timing registers (page 0x10)
- Different gamma tables
- `0xB8=0x86` (not `0x04`), `0xDD/0xDE=0x4F` (not `0x35`)

Source: `temp_volosr/KnobRGBControl/lcd_bsp.c` (`lcd_init_cmds` array). The Waveshare demo uses `esp_lcd_sh8601` driver (SH8601-compatible protocol).

### LVGL Integration
- LVGL 9.2 with 16-bit color, byte-swapped (`LV_COLOR_16_SWAP=1`)
- Partial rendering mode with 21600-byte buffer in PSRAM
- `lv_refr_now()` for explicit frame push on non-animated screens
- `lv_timer_handler()` called every loop iteration

### Critical Bug Found: Dangling Label Pointer
`rebuild_screen()` destroys all children of `scr` via `lv_obj_del(scr)`, but static label pointers (`lbl_debug` etc.) must ALL be nulled in the reset line. Missing any causes `lv_label_set_text` to crash with `LoadProhibited` on screen switch.

---

## 2. Encoder — Bidirectional Switch

### Hardware Behavior
This is NOT a standard quadrature encoder. It is a **bidirectional switch**:
- CW rotation: only channel A (GPIO 8) pulses LOW then HIGH
- CCW rotation: only channel B (GPIO 7) pulses LOW then HIGH
- Channels are completely independent — no phase relationship

### What Does NOT Work
- **GPIO interrupts (ISR)**: Unreliable. ISR timing conflicts with LVGL/SPI/I2C operations
- **Falling edge detection**: Wrong — the working pattern uses rising edges
- **50ms time-based debounce**: Too slow, misses fast rotation
- **ESP32Encoder library**: Designed for quadrature, wrong for bidi switch
- **PCNT hardware**: Designed for quadrature, overkill for bidi switch

### Working Solution: Timer-Polled (Waveshare Official Pattern)
Direct port of `bidi_switch_knob.c` from the Waveshare demo:

**Timer**: `esp_timer` at 3ms periodic interval (`ESP_TIMER_TASK` dispatch)

**Per-channel state machine** (`process_channel()`):
1. Read GPIO level
2. If LOW and changed from HIGH: reset debounce counter
3. If LOW and unchanged: increment debounce counter (tracks LOW duration)
4. If HIGH and changed from LOW: check debounce counter >= `DEBOUNCE_TICKS`
   - Yes: confirmed rising edge — register one step
   - No: bounce — reset counter
5. If HIGH and unchanged: reset counter

**Constants**: `TICKS_INTERVAL = 3ms`, `DEBOUNCE_TICKS = 2` (6ms effective debounce)

**Screen switching**: Delta-based — `enc_count - last_switch` determines direction. Wraps with modulo.

**Key insight**: The timer runs independently of the main loop, so LVGL rendering, WiFi, or I2C activity cannot cause missed encoder steps.

### Reference Implementations
| Source | Pattern | Location |
|---|---|---|
| Waveshare official | esp_timer 3ms, debounce 2 ticks | `temp_demo/.../bidi_switch_knob.c` |
| ESPHome KrX3D | loop() polling, debounce counters | `temp_krx3d/.../rotary_encoder_custom.cpp` |

---

## 3. Touch — CST816T

### Hardware Behavior
The CST816T capacitive touch controller communicates over I2C at address 0x15.

**Critical finding: INT pin is PULSING, not level-based.**

The INT pin (GPIO 9) goes LOW briefly (~5-30ms) when touch data is available, then returns HIGH. During continuous finger contact, it pulses LOW repeatedly with gaps. It does NOT stay LOW for the duration of the touch.

### Reading Touch Data
```
1. Check digitalRead(PIN_TOUCH_INT) == LOW  (gate — skip if HIGH)
2. I2C read 6 bytes from register 0x01
3. data[1] = finger count (0 = no touch)
4. X = ((data[2] & 0x0F) << 8) | data[3]
5. Y = ((data[4] & 0x0F) << 8) | data[5]
```

Coordinate range: 0-359 for both axes on the 360x360 display.

### Touch State Debouncing (Latch System)
Because INT pulses, raw `touch_read()` returns true intermittently. A "latch" system smooths this:

- `latch_touching`: set TRUE when `touch_read()` returns true, stays true for 150ms after last successful read
- `latch_x/latch_y`: updated only when `touch_read()` returns true (always fresh coordinates)
- **Finger lift detection**: `!touching && latch_touching && (now - latch_touch_time > 150ms)`
- **Tap detection**: On finger lift, if duration > 30ms and < 1000ms
- **Hold detection**: While finger is down, if duration > 1000ms (fires once via `touch_hold_fired` flag)

### Touch on Interactive Screens (Volume Knob)
For button-like UI elements, the latch system adds too much latency. The volume screen uses **raw `touching` + `tx/ty`** directly:

```
if (touching && ty > 220) {
    knob_btn_hit = (tx >= 180) ? BACK : MUTE;
}
```

Button action fires once per touch-down via `knob_btn_acted` flag, reset when `!touching`.

### Performance Gotcha: Render Blocking
If `display_flush()` / `lv_refr_now()` is called every loop iteration, the LVGL render blocks for several milliseconds. During this time, `touch_read()` doesn't run, and INT pulses are missed. **Only redraw when data actually changes.**

### I2C Bus Sharing
Touch (0x15) and haptic (0x5A) share the same I2C bus (SDA=11, SCL=12). Both use Arduino `Wire` library. No bus contention issues observed in single-threaded context, but calling `haptic_play()` (4 I2C writes) immediately before `touch_read()` can occasionally cause a missed read.

---

## 4. Haptic — DRV2605L

### Hardware
- I2C address: 0x5A
- Motor type: **LRA** (Linear Resonance Actuator)
- Waveform library: **6** (LRA-optimized)
- 123 pre-loaded ROM effects

### Initialization
```
1. Probe: Wire.beginTransmission(0x5A) — check endTransmission() == 0
2. Read status: register 0x00, device ID = (status >> 5) & 0x07
3. Set mode: register 0x01 = 0x00 (internal trigger)
4. Select library: register 0x03 = 6 (LRA library)
5. Set LRA mode: register 0x1A bit 7 = 1
```

### Playing an Effect
```
1. Stop: register 0x0C = 0  (stop any playing effect)
2. Set effect: register 0x04 = effect_number (1-123)
3. End marker: register 0x05 = 0
4. Fire: register 0x0C = 1
```

### What Does NOT Work
- **ERM mode with library 1**: Some effects produce no perceptible output on this device's LRA motor. Effect 1 (Strong Click) works, but effects 24 (Sharp Tick) and 47 (Buzz) do not.
- **Playing effect without stopping first**: If a previous effect is still playing, the new one may not start. Always write GO=0 before setting up a new effect.

### Working Effects (Library 6, LRA)
| Effect | Number | Use Case |
|---|---|---|
| Strong Click 100% | 1 | Encoder click, button press, touch feedback |
| Double Click 100% | 10 | Back button, confirmations |

### Haptic + Touch Interaction
Haptic on touch-down causes I2C bus activity that can interfere with the next `touch_read()`. Mitigations:
- Skip touch-down haptic on screens with button-specific haptic (volume screen)
- Use cooldown timer (300ms min between touch-down haptics)

---

## 5. I2C Bus Architecture

Single I2C bus shared between two peripherals:

| Device | Address | Speed | Usage |
|---|---|---|---|
| CST816T (touch) | 0x15 | 400kHz | Read 6 bytes per touch event |
| DRV2605L (haptic) | 0x5A | 400kHz | Write 2 bytes per register |

Initialized once by `touch_init()` via `Wire.begin(11, 12)`. Haptic uses the same `Wire` instance.

No bus arbitration issues in single-threaded Arduino context. Both devices are accessed only from the main loop (encoder uses its own timer, not I2C).

---

## 6. Architecture & Main Loop

### Init Order (matters!)
```
display_init()   → SPI bus, LVGL, panel init
encoder_init()   → GPIO config, esp_timer start
touch_init()     → Wire.begin(), CST816T reset
haptic_init()    → DRV2605 probe + config (needs Wire)
wifi_init()      → WiFi.begin() non-blocking
ntp_init()       → configTzTime()
weather_init()   → (no-op)
```

### Loop Structure
```
1. wifi_check()          — reconnect if needed
2. touch_read()          — I2C read if INT is LOW
3. touch_update()        — update tap/hold state
4. latch debounce        — 150ms smoothing of INT pulses
5. encoder_poll()        — no-op (timer handles it)
6. screen-specific logic — volume knob buttons OR screen switching
7. weather_fetch()       — HTTP every 10 min
8. serial heartbeat      — every 5s
9. screen draw           — interval-gated (50ms-1000ms depending on screen)
10. lv_timer_handler()   — LVGL tick, animations, internal timers
```

### Screen Draw Intervals
| Screen | Interval | Reason |
|---|---|---|
| Clock | 1000ms | Time updates every second |
| Weather | 1000ms | Data updates every 10 min |
| Diagnostics | 1000ms | System stats |
| Touch Test | 50ms | Need responsive coordinate display |
| Volume Knob | 50ms | Need responsive button feedback |

---

## 7. Current State (v3)

### Working
- **Display**: LVGL 9 via custom QSPI driver with Waveshare init
- **Encoder**: Timer-polled bidi switch, reliable CW/CCW, 1 step per detent
- **Touch**: CST816T coordinates, tap detection, hold detection
- **Haptic**: DRV2605L LRA, scaled click intensity, phase-based feedback
- **Clock**: NTP time (24h, Europe/Oslo), Norwegian date format
- **Weather**: Open-Meteo for Vadso, Norwegian condition names
- **5 screens**: Klokke, Vaer, Diagnostikk, Touch Test, Fiskespill
- **Fishing game**: Reel game with 5 fish types, AI behavior, tension/speed mechanics, haptic feedback
- **UI language**: Norwegian (limited to ASCII — no ae/oe/aa in Montserrat font)

### Known Issues / Limitations
- **LVGL arc crash**: `lv_arc_set_value(0)` or `lv_arc_set_value(100)` triggers null pointer in `circ_calc_aa4` (LVGL 9.2 bug). Workaround: clamp arc values to 2-98.
- **I2C bus contention**: Touch and haptic share I2C bus. Touch reads disabled on fishing screen to prevent crashes during fast haptic writes.
- **Montserrat font**: LVGL's bundled font only includes ASCII. Norwegian characters (ae, oe, aa) require custom font build.
- **No factory firmware backup**: Original firmware was not saved before flashing.

### Project Structure
```
waveshare_knob/
├── platformio.ini              # ESP32@6.6.0, LVGL 9, ArduinoJson
├── include/
│   ├── board_pins.h            # GPIO definitions
│   ├── secrets.h               # WiFi, location, timezone (gitignored)
│   └── secrets.example.h
├── src/
│   ├── main.cpp                # Setup + main loop + screen dispatch
│   ├── display.cpp/.h          # QSPI driver + LVGL + all screen layouts
│   ├── fishing.cpp/.h          # Fishing reel game module
│   ├── encoder.cpp/.h          # Timer-polled bidi switch driver
│   ├── touch.cpp/.h            # CST816T I2C reader
│   ├── haptic.cpp/.h           # DRV2605L driver
│   ├── wifi_manager.cpp/.h     # WiFi connect/reconnect
│   ├── ntp_time.cpp/.h         # NTP with Norwegian date format
│   └── weather.cpp/.h          # Open-Meteo with Norwegian conditions
├── docs/
│   ├── TECHNICAL_REFERENCE.md  # Hardware details, driver notes
│   └── superpowers/            # Design specs and implementation plans
├── temp_demo/                  # Official Waveshare demo (reference)
├── temp_volosr/                # VolosR project (display init source)
├── temp_krx3d/                 # KrX3D ESPHome (encoder + haptic config)
├── temp_iot/                   # Espressif esp-iot-solution
└── temp_esphome/               # ESPHome source
```

### Build & Flash
```bash
pio run -e knob                        # Build
pio run -e knob --target upload        # Build + flash (COM9)
pio device monitor -p COM9 -b 115200   # Serial monitor
```

---

## 8. Key Reference Files

| File | Contains |
|---|---|
| `temp_volosr/KnobRGBControl/lcd_bsp.c` | Working display init sequence |
| `temp_demo/.../bidi_switch_knob.c` | Official encoder driver (timer-polled) |
| `temp_krx3d/.../rotary_encoder_custom.cpp` | ESPHome encoder (polling variant) |
| `temp_krx3d/example/esphome/.../drv2605.yaml` | DRV2605 config (LRA, library 6) |
| `temp_demo/.../03_DRV2605_Test/` | Official haptic demo |

---

## 9. Lessons Learned

1. **Encoder**: Never use ISR-based detection for bidi switches. Timer polling at 3ms with counter-based debounce is the only reliable approach. The Waveshare official driver is the reference.

2. **Touch INT pin**: The CST816T INT is pulsing, not level-based. Any code that assumes `digitalRead(INT) == LOW` means "finger is touching" will fail. You need a debounce/latch layer with ~150ms gap detection.

3. **Touch responsiveness vs render blocking**: Calling `lv_refr_now()` every loop iteration kills touch responsiveness. The LVGL render takes milliseconds, during which INT pulses are missed. Only render on actual data changes.

4. **Haptic motor type**: This device uses LRA, not ERM. Using ERM library/mode causes many effects to be imperceptible. KrX3D's ESPHome config confirmed: library 6, LRA mode.

5. **I2C bus sharing**: Touch and haptic share one bus. Haptic I2C writes immediately before touch reads can cause missed reads. Separate touch-down haptic from button-specific haptic to reduce bus traffic.

6. **LVGL dangling pointers**: When `rebuild_screen()` deletes the screen object, ALL static label/object pointers must be nulled. Missing one causes `LoadProhibited` crash on next label update.

7. **Encoder sync on programmatic screen change**: If code sets `currentScreen` directly (e.g., back button), the encoder's `screen_idx` must also be updated via `encoder_set_screen()`. Otherwise the encoder's stale index overwrites `currentScreen` on the next loop.

8. **Raw touch vs latched touch**: For UI buttons that need immediate response, use the raw `touching` + `tx/ty` from `touch_read()`. The latch system (150ms debounce) adds latency that makes buttons feel sluggish. Reserve the latch for gesture detection (tap, hold).
