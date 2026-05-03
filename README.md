# Waveshare ESP32-S3 Knob

Multi-screen smart knob app for the [Waveshare ESP32-S3 Touch LCD 1.28](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28) — a round 360x360 display with rotary encoder, capacitive touch, and haptic feedback.

Rotate the encoder to switch screens. Touch and haptics throughout.

## Screens

| # | Screen | Description |
|---|--------|-------------|
| 1 | **Klokke** | NTP clock with date and weather summary (Europe/Oslo) |
| 2 | **Vaer** | Weather for Vadso via Open-Meteo, Norwegian condition names |
| 3 | **Diagnostikk** | WiFi RSSI, heap, PSRAM, uptime, encoder count |
| 4 | **Touch Test** | Live touch coordinates, tap/hold detection, touch point indicator |
| 5 | **Fiskespill** | Fishing reel game — cast, fight, and land fish with the knob |

## Fiskespill (Fishing Game)

The rotary encoder becomes a fishing reel. Spin CW fast to cast, then reel in with CCW. The outer arc shows line remaining; the center shows distance, fish state, and tension. Touch is disabled on this screen (hand grips the knob).

| Input | Action |
|-------|--------|
| Fast CW | Cast (start / restart) |
| CCW | Reel in during fight, exit when idle |

**Fish:** Kontepella, Sei, Torsk, Laks, Kveite — species is hidden during the fight and revealed on catch.

**Mechanics:** Fish cycles through calm / restless / fighting / surging / tired phases. The optimal reel speed shifts per phase — reel too fast during a surge and the line snaps. Steadiness and acceleration affect tension and efficiency. Haptic feedback scales with speed, phase, and tension.

## Hardware

- **MCU:** ESP32-S3 (16MB flash, 8MB OPI PSRAM)
- **Display:** 360x360 round IPS, ST77916 over QSPI
- **Encoder:** Bidirectional switch (not quadrature — one channel per direction)
- **Touch:** CST816T capacitive, I2C 0x15
- **Haptic:** DRV2605L LRA motor, I2C 0x5A, library 6
- **USB:** Two ESP chips share one USB-C — flip cable to select ESP32-S3 (COM9) vs ESP32 (COM7)

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
  TECHNICAL_REFERENCE.md   # Hardware details, driver notes
```

All on-screen text is in Norwegian (Bokmal). ASCII only — the Montserrat font lacks ae/oe/aa.
