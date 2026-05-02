#pragma once
#include <stdint.h>

void touch_init();
void touch_update(bool isTouching, int x, int y);
bool touch_tapped();
bool touch_long_pressed();
int touch_x();
int touch_y();
bool touch_read(int* x, int* y);
