# Waveshare ESP32-S3 Knob

Multi-screen smart knob app for the [Waveshare ESP32-S3 Touch LCD 1.28](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28) round display with rotary encoder, touch, and haptic feedback.

## Screens

1. **Klokke** — NTP clock with date and weather summary (Europe/Oslo)
2. **Vaer** — Weather for Vadso from Open-Meteo, Norwegian condition names
3. **Diagnostikk** — WiFi RSSI, heap, PSRAM, uptime, encoder count
4. **Touch Test** — Live touch coordinates, tap/hold detection, touch point indicator
5. **Volum** — Arc-style volume knob with encoder control, mute/back touch buttons

Navigate between screens by rotating the encoder. Touch and haptic feedback throughout.

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
| LCD D0–D3 | 15, 16, 17, 18 |
| LCD CS / RST / BL | 14 / 21 / 47 |
| I2C SDA / SCL | 11 / 12 |
| Encoder A (CW) / B (CCW) | 8 / 7 |
| Touch INT / RST | 9 / 10 |

## Project Structure

```
src/
  main.cpp            # Setup + main loop + touch state machine
  display.cpp/.h      # QSPI driver + LVGL + all 5 screen layouts
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
  TECHNICAL_REFERENCE.md  # Hardware details, driver notes, lessons learned
```

## UI Language

All on-screen text is in Norwegian (Bokmal). ASCII only — Montserrat font lacks ae/oe/aa.
