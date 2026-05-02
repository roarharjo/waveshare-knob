#pragma once
#include <stdint.h>

void encoder_init();
void encoder_poll();
void encoder_update_screen(int screenCount);
int32_t encoder_get_count();
int encoder_get_screen(int screenCount);
void encoder_set_screen(int idx);
