#pragma once
#include <Arduino.h>

void ntp_init();
bool ntp_synced();
String ntp_time_str();
String ntp_date_str();
String ntp_last_sync();
