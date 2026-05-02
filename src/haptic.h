#pragma once
#include <stdint.h>

bool haptic_init();
void haptic_play(uint8_t effect);

// Convenience effects (DRV2605 ROM library 6, LRA)
#define HAPTIC_CLICK        1   // Strong Click 100%
#define HAPTIC_CLICK_60     2   // Strong Click 60%
#define HAPTIC_CLICK_30     3   // Strong Click 30%
#define HAPTIC_DOUBLE_CLICK 10  // Double Click 100%
#define HAPTIC_BUZZ         14  // Strong Buzz 100%
#define HAPTIC_SOFT_BUMP    7   // Soft Bump 100%
#define HAPTIC_ALERT        15  // 750ms Alert 100%
