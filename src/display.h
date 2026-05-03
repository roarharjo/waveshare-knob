#pragma once
#include <Arduino.h>

#define COL_BG        0x0000
#define COL_PRIMARY   0xFFFF
#define COL_SECONDARY 0xAD55
#define COL_DIM       0x6B4D
#define COL_ACCENT    0x07FF
#define COL_WARM      0xFD20

enum Screen {
  SCREEN_CLOCK,
  SCREEN_WEATHER,
  SCREEN_DIAG,
  SCREEN_TOUCH_TEST,
  SCREEN_ANIM,
  SCREEN_COUNT
};

struct DiagData {
  int rssi;
  uint32_t freeHeap;
  uint32_t freePsram;
  uint32_t uptime;
  String lastNtpSync;
  String lastWeatherFetch;
  int32_t encoderCount;
  int touchX;
  int touchY;
};

void display_init();
void display_draw_clock(Screen active, const String& timeStr, const String& dateStr, const String& weatherSummary);
void display_draw_weather(Screen active, float temp, const String& condition, const String& lastUpdate);
void display_draw_diag(Screen active, const DiagData& d);
void display_draw_touch_test(Screen active, bool touching, int x, int y,
                              bool tapped, bool longPressed);
#include "fishing.h"
void display_draw_fishing(Screen active, const FishingState& fs);
bool display_get_touch(int* x, int* y);
void display_show_message(const char* msg, uint16_t color);
void display_flush();
void display_set_brightness(uint8_t level);
