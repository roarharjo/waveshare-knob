#include "haptic.h"
#include <Arduino.h>
#include <Wire.h>

// DRV2605 registers
#define DRV2605_ADDR        0x5A
#define DRV2605_REG_STATUS  0x00
#define DRV2605_REG_MODE    0x01
#define DRV2605_REG_LIBRARY 0x03
#define DRV2605_REG_WAVESEQ1 0x04
#define DRV2605_REG_GO      0x0C
#define DRV2605_REG_FEEDBACK 0x1A

static bool _ready = false;

static void drv_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(DRV2605_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static uint8_t drv_read(uint8_t reg) {
    Wire.beginTransmission(DRV2605_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(DRV2605_ADDR, (uint8_t)1);
    return Wire.read();
}

bool haptic_init() {
    // Check if DRV2605 is present
    Wire.beginTransmission(DRV2605_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[HAP ] DRV2605 not found at 0x5A");
        return false;
    }

    uint8_t status = drv_read(DRV2605_REG_STATUS);
    uint8_t id = (status >> 5) & 0x07;
    Serial.printf("[HAP ] DRV2605 found, device ID: 0x%02X\n", id);

    // Set mode: internal trigger (play on GO command)
    drv_write(DRV2605_REG_MODE, 0x00);

    // Select waveform library 6 (LRA-optimized, matches KrX3D ESPHome config)
    drv_write(DRV2605_REG_LIBRARY, 6);

    // Use LRA motor (set bit 7 of feedback register)
    uint8_t fb = drv_read(DRV2605_REG_FEEDBACK);
    drv_write(DRV2605_REG_FEEDBACK, fb | 0x80);

    _ready = true;
    Serial.println("[HAP ] Haptic ready (DRV2605, LRA, library 6)");
    return true;
}

void haptic_play(uint8_t effect) {
    if (!_ready || effect == 0 || effect > 123) return;

    // Stop any playing effect first
    drv_write(DRV2605_REG_GO, 0);

    drv_write(DRV2605_REG_WAVESEQ1, effect);  // slot 0: effect
    drv_write(DRV2605_REG_WAVESEQ1 + 1, 0);   // slot 1: end
    drv_write(DRV2605_REG_GO, 1);              // fire
}
