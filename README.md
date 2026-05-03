# Waveshare ESP32-S3 Knob

Multi-screen smart knob app for the [Waveshare ESP32-S3 Touch LCD 1.28](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28) round display with rotary encoder, touch, and haptic feedback.

## Screens

1. **Klokke** — NTP clock with date and weather summary (Europe/Oslo)
2. **Vaer** — Weather for Vadso from Open-Meteo, Norwegian condition names
3. **Diagnostikk** — WiFi RSSI, heap, PSRAM, uptime, encoder count
4. **Touch Test** — Live touch coordinates, tap/hold detection, touch point indicator
5. **Fiskespill** — Fishing reel game with haptic feedback and tension mechanics

Navigate between screens by rotating the encoder. Touch and haptic feedback throughout.

## Fiskespill (Fishing Game)

The rotary encoder becomes a fishing reel. Cast by spinning CW fast, then reel in by spinning CCW. The outer arc shows line remaining, the center shows distance, fish state, and tension.

**Controls:**
- **Fast CW** — Cast (start/restart game)
- **CCW** — Reel in during fight / exit screen when idle
- Touch is disabled on this screen (hand grips the knob)

**Fish types:** Kontepella, Sei, Torsk, Laks, Kveite (hidden during fight, revealed on catch)

**Mechanics:**
- Fish AI cycles through calm/restless/fighting/surging/tired phases
- Optimal speed zone shifts per phase — reel too fast during a surge and the line snaps
- Steadiness and acceleration affect tension and reel efficiency
- Haptic feedback scales with reel speed, fish phase, and tension level

## Hardware

- **MCU:** ESP32-S3 (16MB flash, 8MB OPI PSRAM)
- **Display:** 360x360 round IPS, ST77916 over QSPI
- **Encoder:** Bidirectional switch (not quadrature — one channel per direction)
- **Touch:** CST816T capacitive, I2C 0x15
- **Haptic:** DRV2605L LRA motor, I2C 0x5A, library 6
- **Two ESP chips** on one USB-C: flip cable to select ESP32-S3 (COM9) vs ESP32 (COM7)

## Build & Flash

Requires [PlatformIO](https://platformio.org/).

```bash
# Copy secrets template and fill in your WiFi credentials
cp include/secrets.example.h include/secrets.h

# Build
pio run -e knob

# Flash (USB-C oriented to ESP32-S3 side, COM9)
pio run -e knob -t upload

# Serial monitor
pio device monitor -p COM9 -b 115200
```

## Pin Map

| Function | GPIO |
|----------|------|
| LCD PCLK | 13 |
| LCD D0-D3 | 15, 16, 17, 18 |
| LCD CS / RST / BL | 14 / 21 / 47 |
| I2C SDA / SCL | 11 / 12 |
| Encoder A (CW) / B (CCW) | 8 / 7 |
| Touch INT / RST | 9 / 10 |

## Project Structure

```
src/
  main.cpp            # Setup + main loop + screen dispatch
  display.cpp/.h      # QSPI driver + LVGL + all 5 screen layouts
  fishing.cpp/.h      # Fishing reel game: state machine, physics, fish AI
  encoder.cpp/.h      # Timer-polled bidi switch driver (3ms, esp_timer)
  touch.cpp/.h        # CST816T I2C reader
  haptic.cpp/.h       # DRV2605L LRA driver
  wifi_manager.cpp/.h # WiFi STA connect/reconnect
  ntp_time.cpp/.h     # NTP time with Norwegian date format
  weather.cpp/.h      # Open-Meteo API with Norwegian condition names
include/
  board_pins.h        # GPIO definitions
  secrets.h           # WiFi credentials, location, timezone (gitignored)
  secrets.example.h   # Template for secrets.h
docs/
  TECHNICAL_REFERENCE.md                          # Hardware details, driver notes
  superpowers/specs/2026-05-03-fishing-game-design.md  # Fishing game design spec
  superpowers/plans/2026-05-03-fishing-game.md         # Implementation plan
```

## UI Language

All on-screen text is in Norwegian (Bokmal). ASCII only — Montserrat font lacks ae/oe/aa.
