#include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "encoder.h"
#include "touch.h"
#include "wifi_manager.h"
#include "ntp_time.h"
#include "weather.h"
#include "haptic.h"
#include "fishing.h"

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
static int32_t fish_enc_base = 0;     // encoder count when entering fishing screen
static bool fish_screen_entered = false;

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
    fishing_init();
    Serial.println("[MAIN] Setup complete — clock starts immediately");
}

void loop() {
    uint32_t now = millis();

    // WiFi health check
    wifi_check();

    // Read touch (skip on fishing screen — frees I2C bus for haptic)
    int tx = 0, ty = 0;
    bool touching = false;
    if (currentScreen != SCREEN_ANIM) {
        touching = touch_read(&tx, &ty);
        touch_update(touching, tx, ty);
    }

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
        FishingState fs = fishing_get_state();

        // Touch DISABLED on fishing screen — frees I2C for haptic
        latch_tapped = false;
        latch_longPressed = false;

        // Simple encoder tracking — no complex windows
        static int32_t fish_nav_acc = 0;   // accumulated nav delta
        static uint32_t fish_enter_time = 0;

        if (!fish_screen_entered) {
            fish_enc_base = encoder_get_count();
            fish_nav_acc = 0;
            fish_enter_time = now;
            fish_screen_entered = true;
        }

        if (fs.game_state == FISH_FIGHTING) {
            // During fight: encoder controls reel only
            static uint32_t last_fish_tick = 0;
            if (now - last_fish_tick >= 50) {
                int32_t delta = encoder_get_count() - fish_enc_base;
                fish_enc_base = encoder_get_count();
                fishing_tick(delta);
                last_fish_tick = now;
            }
            fish_nav_acc = 0;  // reset nav accumulator during fight
        } else {
            // IDLE / WON / LOST
            // 1s cooldown on entry + 2s cooldown after game end
            bool in_cooldown = (now - fish_enter_time < 1000) ||
                               (fs.end_time_ms > 0 && now - fs.end_time_ms < 2000);

            int32_t raw_delta = encoder_get_count() - fish_enc_base;
            fish_enc_base = encoder_get_count();

            if (in_cooldown) {
                // Absorb everything during cooldown
                fish_nav_acc = 0;
            } else {
                fish_nav_acc += raw_delta;

                if (fish_nav_acc > 3) {
                    // Fast CW = cast!
                    fishing_start();
                    fish_enc_base = encoder_get_count();
                    fish_nav_acc = 0;
                    fish_enter_time = now;
                    haptic_play(HAPTIC_BUZZ);
                } else if (fish_nav_acc < -2) {
                    // CCW = exit to previous screen
                    fish_screen_entered = false;
                    int prev = (SCREEN_ANIM - 1 + SCREEN_COUNT) % SCREEN_COUNT;
                    encoder_set_screen(prev);
                    currentScreen = (Screen)prev;
                    lastDraw = 0;
                    haptic_play(HAPTIC_CLICK);
                }

                // Slow decay — only every 500ms to prevent draining fast input
                static uint32_t last_decay = 0;
                if (raw_delta == 0 && now - last_decay >= 500) {
                    if (fish_nav_acc > 0) fish_nav_acc--;
                    else if (fish_nav_acc < 0) fish_nav_acc++;
                    last_decay = now;
                }
            }
        }
        latch_tapped = false;
        latch_longPressed = false;
    } else {
        // Normal screen switching
        encoder_update_screen(SCREEN_COUNT);
        Screen newScreen = (Screen)encoder_get_screen(SCREEN_COUNT);
        if (newScreen != currentScreen) {
            if (newScreen == SCREEN_ANIM) {
                fish_enc_base = encoder_get_count();
                fish_screen_entered = false;  // trigger re-init on next frame
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
            case SCREEN_ANIM: {
                FishingState fs = fishing_get_state();
                display_draw_fishing(currentScreen, fs);
                break;
            }
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

    // LVGL tick — drives rendering
    lv_timer_handler();
}
