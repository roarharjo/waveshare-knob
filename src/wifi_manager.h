#pragma once
#include <Arduino.h>

void wifi_init();
bool wifi_connected();
int wifi_rssi();
String wifi_ip();
void wifi_check();
