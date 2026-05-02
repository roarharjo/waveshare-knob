# Encoder Fix — Timer-Polled Bidi Switch Driver

## Problem

The dial encoder on the Waveshare ESP32-S3-Knob-Touch-LCD-1.8 is erratic. The current
implementation in `src/encoder.cpp` uses GPIO interrupts (falling edge, 50ms debounce)
which fundamentally mismatches the hardware.

The knob is a **bidirectional switch**, not a quadrature encoder:
- CW rotation pulses only channel A (GPIO 8)
- CCW rotation pulses only channel B (GPIO 7)
- Channels are independent — no phase relationship

Every known working implementation (Waveshare official demo, ESPHome KrX3D component,
VolosR project) uses **timer-based polling with rising-edge detection and counter-based
debounce**.

## Goal

Reliable CW/CCW detection to cycle through 3 screens. This is a learning exercise to
master the device's hardware interfaces — the final application is not the focus.

## Design

### Approach: Port the Waveshare `bidi_switch_knob` pattern

Rewrite `encoder.cpp` to use a dedicated `esp_timer` callback instead of GPIO interrupts.

### Encoder State Machine

Per-channel state tracking:

```
struct channel_state {
    uint8_t last_level;      // previous GPIO reading (0 or 1)
    uint8_t debounce_count;  // consecutive ticks at new level
};
```

Timer callback (every 3ms):
1. Read GPIO 8 (channel A) and GPIO 7 (channel B)
2. For each channel:
   - If pin is LOW (0):
     - If level changed from last reading: reset debounce counter
     - Else: increment debounce counter (tracks how long pin stayed low)
   - If pin is HIGH (1):
     - If level changed from last reading AND debounce counter >= DEBOUNCE_TICKS:
       this is a confirmed rising edge → register one step, reset counter
     - If level changed but debounce counter < DEBOUNCE_TICKS: bounce, reset counter
     - If level unchanged: reset debounce counter
   - Update last_level to current reading
3. Channel A confirmed edge → CW (+1), Channel B confirmed edge → CCW (-1)
4. Update screen index: `(idx + delta + 3) % 3`

### Constants (matching Waveshare demo)

| Constant | Value | Meaning |
|---|---|---|
| TICKS_INTERVAL | 3ms | Timer period |
| DEBOUNCE_TICKS | 2 | Ticks required to confirm edge (6ms) |

### Files Changed

| File | Change |
|---|---|
| `src/encoder.cpp` | Full rewrite: remove ISRs, add esp_timer polling with bidi state machine |
| `src/encoder.h` | Same public API: `encoder_init()`, `encoder_poll()`, `encoder_get_count()`, `encoder_update_screen()`, `encoder_get_screen()` |
| `platformio.ini` | Remove `madhephaestus/ESP32Encoder` from lib_deps |

### Files NOT Changed

- `src/main.cpp` — encoder API stays the same, no changes needed
- `src/touch.cpp` / `touch.h` — already working, no changes
- `src/display.cpp` — no changes
- WiFi/NTP/Weather modules — irrelevant

### Debug Output

- Per-step serial print: `[ENC ] CW count=5` or `[ENC ] CCW count=4`
- Existing heartbeat `enc=N` every 5 seconds (unchanged)
- Existing on-screen `enc:N` debug overlay (unchanged)

## Reference Implementations

| Source | Pattern | Location |
|---|---|---|
| Waveshare official | esp_timer 3ms, debounce 2 ticks | `temp_demo/.../bidi_switch_knob.c` |
| ESPHome KrX3D | loop() polling, debounce counters | `temp_krx3d/.../rotary_encoder_custom.cpp` |
| VolosR | bidi_switch_knob.h (same API) | `temp_volosr/KnobRGBControl/bidi_switch_knob.h` |

## Success Criteria

- Turning the dial CW reliably advances one screen per detent
- Turning the dial CCW reliably goes back one screen per detent
- No phantom steps, no missed steps, no double-counting
- Serial output confirms each step with direction
