#pragma once
#include <Arduino.h>

void weather_init();
bool weather_fetch();
float weather_temp();
String weather_condition();
String weather_last_fetch();
