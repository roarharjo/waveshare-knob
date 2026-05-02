#include "weather.h"
#include "secrets.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

static float _temp = 0;
static String _condition = "N/A";
static String _lastFetch = "never";

static const char* wmo_description(int code) {
    if (code == 0) return "Klart";
    if (code == 1) return "Mest klart";
    if (code == 2) return "Delvis skyet";
    if (code == 3) return "Overskyet";
    if (code == 45 || code == 48) return "Toke";
    if (code >= 51 && code <= 55) return "Yr";
    if (code == 61) return "Lett regn";
    if (code == 63) return "Regn";
    if (code == 65) return "Kraftig regn";
    if (code == 66 || code == 67) return "Underkjolt regn";
    if (code == 71) return "Lett sno";
    if (code == 73) return "Sno";
    if (code == 75) return "Kraftig sno";
    if (code == 77) return "Snokorn";
    if (code >= 80 && code <= 82) return "Byger";
    if (code >= 85 && code <= 86) return "Snobyger";
    if (code >= 95) return "Tordenvaer";
    return "Ukjent";
}

void weather_init() {
    Serial.println("[WTHR] Weather module ready");
}

bool weather_fetch() {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    String url = "https://api.open-meteo.com/v1/forecast?latitude="
        + String(WEATHER_LAT, 4) + "&longitude=" + String(WEATHER_LON, 4)
        + "&current=temperature_2m,weather_code";

    http.begin(url);
    int code = http.GET();

    if (code == 200) {
        JsonDocument doc;
        deserializeJson(doc, http.getString());
        _temp = doc["current"]["temperature_2m"];
        int wcode = doc["current"]["weather_code"];
        _condition = wmo_description(wcode);

        struct tm t;
        if (getLocalTime(&t, 0)) {
            char buf[9];
            strftime(buf, sizeof(buf), "%H:%M:%S", &t);
            _lastFetch = String(buf);
        }
        Serial.printf("[WTHR] %.1fC, %s\n", _temp, _condition.c_str());
        http.end();
        return true;
    }

    Serial.printf("[WTHR] Fetch failed: %d\n", code);
    http.end();
    return false;
}

float weather_temp() { return _temp; }
String weather_condition() { return _condition; }
String weather_last_fetch() { return _lastFetch; }
