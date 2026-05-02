#include "touch.h"
#include "board_pins.h"
#include <Arduino.h>
#include <Wire.h>

#define CST816_ADDR 0x15

static int _x = 0, _y = 0;
static bool _tapped = false;
static bool _longPressed = false;
static bool _wasTouching = false;
static uint32_t _touchStart = 0;

void touch_init() {
    // Hardware reset the CST816T
    pinMode(PIN_TOUCH_RST, OUTPUT);
    digitalWrite(PIN_TOUCH_RST, LOW);
    delay(20);
    digitalWrite(PIN_TOUCH_RST, HIGH);
    delay(100);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);
    pinMode(PIN_TOUCH_INT, INPUT);

    Serial.println("[TCH ] Touch initialized");
}

void touch_update(bool isTouching, int x, int y) {
    _tapped = false;
    _longPressed = false;

    if (isTouching) {
        _x = x;
        _y = y;
        if (!_wasTouching) {
            _touchStart = millis();
        }
    } else if (_wasTouching) {
        uint32_t duration = millis() - _touchStart;
        if (duration > 800) {
            _longPressed = true;
            Serial.printf("[TCH ] Long press at %d,%d\n", _x, _y);
        } else if (duration > 50) {
            _tapped = true;
            Serial.printf("[TCH ] Tap at %d,%d\n", _x, _y);
        }
    }
    _wasTouching = isTouching;
}

bool touch_tapped() { return _tapped; }
bool touch_long_pressed() { return _longPressed; }
int touch_x() { return _x; }
int touch_y() { return _y; }

bool touch_read(int* x, int* y) {
    if (digitalRead(PIN_TOUCH_INT) != LOW) return false;

    Wire.beginTransmission(CST816_ADDR);
    Wire.write(0x01);
    if (Wire.endTransmission() != 0) return false;

    if (Wire.requestFrom(CST816_ADDR, 6) != 6) return false;

    uint8_t data[6];
    for (int i = 0; i < 6; i++) data[i] = Wire.read();

    uint8_t fingers = data[1];
    if (fingers == 0) return false;

    *x = ((data[2] & 0x0F) << 8) | data[3];
    *y = ((data[4] & 0x0F) << 8) | data[5];
    return true;
}
