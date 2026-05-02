#include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "encoder.h"
#include "touch.h"
#include "wifi_manager.h"
#include "ntp_time.h"
#include "weather.h"
#include "haptic.h"

// Forward declaration from touch.cpp
extern bool touch_read(int* x, int* y);

static Screen currentScreen = SCREEN_CLOCK;
static bool showOverlay = false;
static uint32_t lastDraw = 0;
static uint32_t lastWeatherFetch = 0;

// Latched touch state for the touch test screen — accumulates between draws
static bool latch_touching = false;
static bool latch_tapped = false;
static bool latch_longPressed = false;
static int latch_x = 0, latch_y = 0;
static uint32_t latch_touch_time = 0;  // last time touch_read returned true
static int32_t knob_value = 50;       // volume knob value (0-100)
static int32_t knob_before_mute = 50; // value before mute
static bool knob_muted = false;
static int32_t knob_enc_base = 0;     // encoder count when entering knob screen
static int knob_btn_hit = 0;          // 0=none, 1=back, 2=mute (for UI feedback)
static bool knob_btn_acted = false;   // true once action fires, reset when finger leaves zone

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Waveshare ESP32-S3 Knob v1 ===");
    Serial.printf("[MAIN] Free PSRAM: %u KB\n", ESP.getFreePsram() / 1024);

    display_init();
    encoder_init();
    touch_init();
    haptic_init();
    wifi_init();
    ntp_init();
    weather_init();
    Serial.println("[MAIN] Setup complete — clock starts immediately");
}

void loop() {
    uint32_t now = millis();

    // WiFi health check
    wifi_check();

    // Read touch
    int tx = 0, ty = 0;
    bool touching = touch_read(&tx, &ty);
    touch_update(touching, tx, ty);

    // Latch touch state — debounce the pulsing INT pin
    // Use 150ms gap to consider finger truly lifted (INT pulses every ~30ms)
    static uint32_t touch_start_time = 0;
    static bool touch_hold_fired = false;
    static uint32_t touch_haptic_cooldown = 0; // prevent haptic spam
    if (touching) {
        if (!latch_touching) {
            // New touch begins
            touch_start_time = now;
            touch_hold_fired = false;
            // Haptic on touch-down (skip on volume screen — buttons have own haptic)
            if (currentScreen != SCREEN_ANIM && now - touch_haptic_cooldown > 300) {
                haptic_play(HAPTIC_CLICK);
                touch_haptic_cooldown = now;
            }
        }
        latch_touching = true;
        latch_x = tx;
        latch_y = ty;
        latch_touch_time = now;
    }
    // Check if finger is still "latched" (within 150ms of last pulse)
    bool was_touching = latch_touching;
    if (!touching && latch_touching && now - latch_touch_time > 150) {
        // Finger truly lifted
        uint32_t dur = now - touch_start_time;
        if (!touch_hold_fired) {
            if (dur > 30) {
                latch_tapped = true;
                Serial.printf("[TCH ] Trykk (%ums)\n", dur);
            }
        }
        latch_touching = false;
        lastDraw = 0;
    }
    // Detect hold WHILE finger is down (not on release)
    if (latch_touching && !touch_hold_fired) {
        uint32_t dur = now - touch_start_time;
        if (dur > 1000) {
            latch_longPressed = true;
            touch_hold_fired = true;  // fire only once per hold
            haptic_play(HAPTIC_DOUBLE_CLICK);
            Serial.printf("[TCH ] Holdt (%ums)\n", dur);
            lastDraw = 0;
        }
    }

    // Encoder poll
    encoder_poll();
    if (currentScreen == SCREEN_ANIM) {
        // Buttons use raw `touching` + `tx/ty` — zero latency, like touch test
        int prev_btn = knob_btn_hit;
        if (touching && ty > 220) {
            knob_btn_hit = (tx >= 180) ? 1 : 2;
        } else if (!touching) {
            knob_btn_hit = 0;
            knob_btn_acted = false;
        }
        if (knob_btn_hit != prev_btn) lastDraw = 0;

        // Fire action once per touch in zone
        if (knob_btn_hit > 0 && !knob_btn_acted) {
            knob_btn_acted = true;
            if (knob_btn_hit == 1) {
                haptic_play(HAPTIC_DOUBLE_CLICK);
                currentScreen = SCREEN_CLOCK;
                encoder_set_screen(SCREEN_CLOCK);
                lastDraw = 0;
            } else {
                haptic_play(HAPTIC_CLICK);
                if (knob_muted) {
                    knob_muted = false;
                    knob_value = knob_before_mute;
                } else {
                    knob_before_mute = knob_value;
                    knob_muted = true;
                    knob_value = 0;
                }
                lastDraw = 0;
            }
        }

        // Encoder controls volume
        int32_t delta = encoder_get_count() - knob_enc_base;
        if (delta != 0) {
            if (knob_muted) {
                knob_muted = false;
                knob_value = knob_before_mute;
            }
            knob_value += delta * 2;
            if (knob_value < 0) knob_value = 0;
            if (knob_value > 100) knob_value = 100;
            knob_enc_base = encoder_get_count();
            haptic_play(HAPTIC_CLICK);
            lastDraw = 0;
        }
        latch_tapped = false;
        latch_longPressed = false;
    } else {
        // Normal screen switching
        encoder_update_screen(SCREEN_COUNT);
        Screen newScreen = (Screen)encoder_get_screen(SCREEN_COUNT);
        if (newScreen != currentScreen) {
            if (newScreen == SCREEN_ANIM) {
                knob_enc_base = encoder_get_count();  // sync encoder position
            }
            currentScreen = newScreen;
            lastDraw = 0;
            haptic_play(HAPTIC_CLICK);
        }
    }

    // Weather fetch: on first connect + every 10 min
    if (wifi_connected()) {
        if (lastWeatherFetch == 0 || now - lastWeatherFetch > 600000) {
            weather_fetch();
            lastWeatherFetch = now;
        }
    }

    // Serial heartbeat every 5s
    static uint32_t lastHeartbeat = 0;
    if (now - lastHeartbeat > 5000) {
        Serial.printf("[MAIN] alive t=%us scr=%d enc=%d heap=%u time=%s\n",
                      (unsigned)(now/1000), currentScreen, encoder_get_count(), ESP.getFreeHeap(),
                      ntp_time_str().c_str());
        lastHeartbeat = now;
    }

    // Update screen content (fast for interactive screens, 1s for others)
    uint32_t drawInterval = (currentScreen == SCREEN_TOUCH_TEST || currentScreen == SCREEN_ANIM) ? 50 : 1000;
    if (now - lastDraw >= drawInterval) {
        lastDraw = now;
        ntp_synced();

        switch (currentScreen) {
            case SCREEN_CLOCK: {
                String summary = String(weather_temp(), 1) + "C " + weather_condition();
                display_draw_clock(currentScreen, ntp_time_str(), ntp_date_str(), summary);
                break;
            }
            case SCREEN_WEATHER:
                display_draw_weather(currentScreen, weather_temp(), weather_condition(), weather_last_fetch());
                break;
            case SCREEN_DIAG: {
                DiagData d;
                d.rssi = wifi_rssi();
                d.freeHeap = ESP.getFreeHeap();
                d.freePsram = ESP.getFreePsram();
                d.uptime = now / 1000;
                d.lastNtpSync = ntp_last_sync();
                d.lastWeatherFetch = weather_last_fetch();
                d.encoderCount = encoder_get_count();
                d.touchX = touch_x();
                d.touchY = touch_y();
                display_draw_diag(currentScreen, d);
                break;
            }
            case SCREEN_TOUCH_TEST:
                display_draw_touch_test(currentScreen, latch_touching, latch_x, latch_y,
                                        latch_tapped, latch_longPressed);
                latch_tapped = false;
                latch_longPressed = false;
                break;
            case SCREEN_ANIM:
                display_draw_knob(currentScreen, knob_value, knob_muted, knob_btn_hit);
                break;
            default: break;
        }

        // Show encoder/touch debug on non-interactive screens
        if (currentScreen != SCREEN_TOUCH_TEST && currentScreen != SCREEN_ANIM) {
            extern void display_set_debug(const char *text);
            char dbg[64];
            snprintf(dbg, sizeof(dbg), "enc:%d  tch:%d,%d", (int)encoder_get_count(), touch_x(), touch_y());
            display_set_debug(dbg);
        }

        display_flush();
    }

    // LVGL tick — drives animations and rendering
    lv_timer_handler();
}
