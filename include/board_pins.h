#pragma once

// Display - ST77916 QSPI (verified from multiple sources)
#define PIN_LCD_CS    14
#define PIN_LCD_PCLK  13
#define PIN_LCD_D0    15
#define PIN_LCD_D1    16
#define PIN_LCD_D2    17
#define PIN_LCD_D3    18
#define PIN_LCD_RST   21
#define PIN_LCD_BL    47

// Rotary encoder (verified)
#define PIN_ENC_A     8
#define PIN_ENC_B     7

// I2C - shared bus for touch + haptic (verified from community configs)
#define PIN_I2C_SDA   11
#define PIN_I2C_SCL   12

// Touch - CST816T (addr 0x15)
#define PIN_TOUCH_INT 9
#define PIN_TOUCH_RST 10
