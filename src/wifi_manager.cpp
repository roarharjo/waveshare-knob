#include "wifi_manager.h"
#include "secrets.h"
#include <WiFi.h>

static uint32_t lastCheck = 0;

void wifi_init() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WIFI] Connecting to ");
    Serial.println(WIFI_SSID);
}

bool wifi_connected() { return WiFi.status() == WL_CONNECTED; }
int wifi_rssi() { return WiFi.RSSI(); }
String wifi_ip() { return WiFi.localIP().toString(); }

void wifi_check() {
    if (millis() - lastCheck < 5000) return;
    lastCheck = millis();
    if (!wifi_connected()) {
        Serial.println("[WIFI] Reconnecting...");
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
}
