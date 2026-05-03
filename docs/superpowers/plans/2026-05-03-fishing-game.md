# Fiskespill (Fishing Reel Game) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the volume knob screen (`SCREEN_ANIM`) with a fishing reel game where the encoder acts as a physical reel, the outer arc shows line status, and the center shows a live fishing dashboard.

**Architecture:** Game logic lives in a new `fishing.cpp/.h` module (state machine, physics, fish data). Display rendering is a new `display_draw_fishing()` function in `display.cpp`. Main loop dispatches to the fishing module when `currentScreen == SCREEN_ANIM`, replacing the volume knob logic. All existing screens remain unchanged.

**Tech Stack:** ESP32-S3 Arduino, LVGL 9.2, DRV2605L haptic, timer-polled bidi encoder.

**Spec:** `docs/superpowers/specs/2026-05-03-fishing-game-design.md`

---

## File Structure

| Action | File | Responsibility |
|--------|------|---------------|
| Create | `src/fishing.h` | Public API: `fishing_start()`, `fishing_tick()`, `FishingState` struct |
| Create | `src/fishing.cpp` | Game state machine, fish behavior, reel physics, tension math |
| Modify | `src/display.h` | Remove `display_draw_knob()`, add `display_draw_fishing()` |
| Modify | `src/display.cpp` | Replace SCREEN_ANIM volume UI with fishing UI in `rebuild_screen()`, new `display_draw_fishing()` |
| Modify | `src/main.cpp` | Replace volume knob logic in `SCREEN_ANIM` block with fishing game logic |

---

## Task 1: Fishing Game Module — Data Types and API

**Files:**
- Create: `src/fishing.h`

- [ ] **Step 1: Create fishing.h with all types and API**

```cpp
#pragma once
#include <stdint.h>

// Game states
enum FishGameState {
    FISH_IDLE,
    FISH_FIGHTING,
    FISH_WON,
    FISH_LOST
};

// Fish behavior phases
enum FishPhase {
    PHASE_CALM,
    PHASE_RESTLESS,
    PHASE_FIGHTING,
    PHASE_SURGING,
    PHASE_TIRED
};

// Loss reasons
enum LossReason {
    LOSS_NONE,
    LOSS_LINE_SNAPPED,
    LOSS_FISH_ESCAPED
};

// Fish type IDs
enum FishType {
    FISH_SEI,
    FISH_TORSK,
    FISH_LAKS,
    FISH_KVEITE,
    FISH_TYPE_COUNT
};

// Read-only snapshot of game state for display
struct FishingState {
    FishGameState game_state;
    FishPhase     fish_phase;
    LossReason    loss_reason;
    FishType      fish_type;
    float         fish_weight;       // kg
    float         line_distance;     // meters remaining
    float         start_distance;    // meters at game start
    float         max_distance;      // escape threshold
    float         tension_pct;       // 0-100
    float         reel_speed;        // smoothed speed value
    uint32_t      fight_time_ms;     // total fight duration
    const char*   fish_name;         // Norwegian name (pointer to static string)
};

// Initialize module (call once at boot)
void fishing_init();

// Start a new game (random fish selection)
void fishing_start();

// Advance game by one tick. Call every 50ms.
// encoder_delta: encoder count change since last tick (positive = CW = reeling)
void fishing_tick(int32_t encoder_delta);

// Get current state snapshot for rendering
FishingState fishing_get_state();
```

- [ ] **Step 2: Verify header compiles**

Run: `cd /c/Users/roarh/code/waveshare_knob && pio run -e knob 2>&1 | tail -5`

Expected: SUCCESS (header is not yet included anywhere, just checking syntax if included).

- [ ] **Step 3: Commit**

```bash
git add src/fishing.h
git commit -m "feat(fishing): add game module header with types and API"
```

---

## Task 2: Fishing Game Module — Core Implementation

**Files:**
- Create: `src/fishing.cpp`

- [ ] **Step 1: Create fishing.cpp with full game logic**

```cpp
#include "fishing.h"
#include "haptic.h"
#include <Arduino.h>
#include <stdlib.h>

// ============ FISH DATA ============

struct FishTemplate {
    const char* name;
    float min_weight, max_weight;
    float min_start, max_start;       // start distance range
    float min_pull, max_pull;         // pull strength range
    float min_stamina, max_stamina;   // total fight time before permanent tire
    float min_surge_interval, max_surge_interval;
    float max_tension;                // line strength for this fish class
};

static const FishTemplate fish_templates[FISH_TYPE_COUNT] = {
    // name,    wMin, wMax, dMin, dMax, pMin, pMax, sMin,  sMax, sgMin, sgMax, maxT
    {"Sei",     2,    5,    50,   80,   2,    4,    15000, 25000, 8000, 15000, 60},
    {"Torsk",   5,    15,   80,   150,  4,    7,    30000, 50000, 6000, 12000, 70},
    {"Laks",    8,    20,   100,  200,  6,    10,   45000, 75000, 4000,  8000, 80},
    {"Kveite",  20,   100,  150,  300,  8,    15,   60000,120000, 3000,  6000, 90},
};

// Selection weights: Sei 40%, Torsk 30%, Laks 20%, Kveite 10%
static const uint8_t fish_weights[FISH_TYPE_COUNT] = {40, 30, 20, 10};

// ============ GAME STATE ============

static FishGameState g_game_state = FISH_IDLE;
static FishPhase     g_fish_phase = PHASE_CALM;
static LossReason    g_loss_reason = LOSS_NONE;
static FishType      g_fish_type = FISH_SEI;
static float         g_fish_weight = 0;
static float         g_line_distance = 0;
static float         g_start_distance = 0;
static float         g_max_distance = 0;
static float         g_tension = 0;        // raw tension value
static float         g_tension_pct = 0;    // 0-100
static float         g_max_tension = 60;
static float         g_fish_pull = 0;      // current pull strength
static float         g_fish_base_pull = 0; // template pull for this fish

// Reel speed tracking
static float         g_reel_speed = 0;     // smoothed
static float         g_reel_speed_prev = 0;
static int32_t       g_reel_history[4] = {0};
static uint8_t       g_reel_hist_idx = 0;

// Phase timing
static uint32_t      g_phase_start = 0;
static uint32_t      g_phase_duration = 0;
static uint32_t      g_fight_start = 0;
static uint32_t      g_total_fight_time = 0;

// Surge warning
static bool          g_surge_warned = false;
static uint32_t      g_next_surge_time = 0;

// Tension snap timer (must exceed 100% for 200ms)
static uint32_t      g_snap_start = 0;

// Fatigue: fish tires over total fight time
static float         g_stamina_total = 0;  // ms before permanent fatigue

// ============ HELPERS ============

static float randf(float lo, float hi) {
    return lo + (float)random(0, 10000) / 10000.0f * (hi - lo);
}

static float lerp_factor(float weight, float min_w, float max_w) {
    if (max_w <= min_w) return 0.5f;
    return (weight - min_w) / (max_w - min_w);
}

static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// ============ PHASE MANAGEMENT ============

static void enter_phase(FishPhase phase) {
    g_fish_phase = phase;
    g_phase_start = millis();

    // Fatigue factor: as fight goes on, calm/tired phases lengthen, fight/surge shorten
    float fatigue = clampf((float)g_total_fight_time / g_stamina_total, 0, 1.0f);
    float calm_mult = 1.0f + fatigue * 1.5f;   // calm gets longer
    float fight_mult = 1.0f - fatigue * 0.5f;   // fighting gets shorter
    if (fight_mult < 0.3f) fight_mult = 0.3f;

    switch (phase) {
        case PHASE_CALM:
            g_phase_duration = (uint32_t)(randf(3000, 8000) * calm_mult);
            g_fish_pull = randf(0.5f, 1.0f);
            break;
        case PHASE_RESTLESS:
            g_phase_duration = (uint32_t)randf(2000, 4000);
            g_fish_pull = randf(1.0f, 2.0f);
            haptic_play(HAPTIC_DOUBLE_CLICK);
            break;
        case PHASE_FIGHTING:
            g_phase_duration = (uint32_t)(randf(3000, 6000) * fight_mult);
            g_fish_pull = randf(2.0f, 4.0f) * (g_fish_base_pull / 5.0f);
            break;
        case PHASE_SURGING:
            g_phase_duration = (uint32_t)(randf(1000, 3000) * fight_mult);
            g_fish_pull = randf(5.0f, 10.0f) * (g_fish_base_pull / 5.0f);
            g_surge_warned = false;
            break;
        case PHASE_TIRED:
            g_phase_duration = (uint32_t)(randf(2000, 5000) * calm_mult);
            g_fish_pull = randf(0.2f, 0.5f);
            break;
    }
}

static void advance_phase() {
    switch (g_fish_phase) {
        case PHASE_CALM:     enter_phase(PHASE_RESTLESS); break;
        case PHASE_RESTLESS: enter_phase(PHASE_FIGHTING); break;
        case PHASE_FIGHTING: enter_phase(PHASE_SURGING);  break;
        case PHASE_SURGING:  enter_phase(PHASE_TIRED);    break;
        case PHASE_TIRED:    enter_phase(PHASE_CALM);     break;
    }
}

// ============ PUBLIC API ============

void fishing_init() {
    g_game_state = FISH_IDLE;
    randomSeed(analogRead(0) ^ millis());
}

void fishing_start() {
    // Select fish type by weighted random
    uint8_t roll = random(0, 100);
    uint8_t cumulative = 0;
    g_fish_type = FISH_SEI;
    for (int i = 0; i < FISH_TYPE_COUNT; i++) {
        cumulative += fish_weights[i];
        if (roll < cumulative) {
            g_fish_type = (FishType)i;
            break;
        }
    }

    const FishTemplate& ft = fish_templates[g_fish_type];

    // Randomize weight within range
    g_fish_weight = randf(ft.min_weight, ft.max_weight);
    float wf = lerp_factor(g_fish_weight, ft.min_weight, ft.max_weight);

    // Scale stats by weight within type range
    g_start_distance = lerp(ft.min_start, ft.max_start, wf);
    g_line_distance = g_start_distance;
    g_max_distance = g_start_distance + 50.0f;
    g_fish_base_pull = lerp(ft.min_pull, ft.max_pull, wf);
    g_stamina_total = lerp(ft.min_stamina, ft.max_stamina, wf);
    g_max_tension = ft.max_tension;

    // Reset state
    g_tension = 0;
    g_tension_pct = 0;
    g_reel_speed = 0;
    g_reel_speed_prev = 0;
    g_snap_start = 0;
    g_total_fight_time = 0;
    g_fight_start = millis();
    for (int i = 0; i < 4; i++) g_reel_history[i] = 0;
    g_reel_hist_idx = 0;

    g_loss_reason = LOSS_NONE;
    g_game_state = FISH_FIGHTING;
    enter_phase(PHASE_CALM);
}

void fishing_tick(int32_t encoder_delta) {
    if (g_game_state != FISH_FIGHTING) return;

    uint32_t now = millis();
    g_total_fight_time = now - g_fight_start;

    // --- Reel speed (smoothed over 4 ticks = 200ms) ---
    if (encoder_delta < 0) encoder_delta = 0; // CCW does nothing
    g_reel_history[g_reel_hist_idx] = encoder_delta;
    g_reel_hist_idx = (g_reel_hist_idx + 1) % 4;

    float sum = 0;
    for (int i = 0; i < 4; i++) sum += g_reel_history[i];
    g_reel_speed_prev = g_reel_speed;
    g_reel_speed = sum / 4.0f;

    // --- Acceleration bonus ---
    float acceleration = g_reel_speed - g_reel_speed_prev;
    float accel_bonus = 1.0f + clampf(acceleration, 0, 5.0f) * 0.2f;

    // --- Reel rate (meters reeled this tick) ---
    float speed_factor = 0.5f;
    float reel_rate = g_reel_speed * speed_factor * accel_bonus;

    // --- Update line distance ---
    g_line_distance -= reel_rate;
    g_line_distance += g_fish_pull * 0.05f; // fish pull per tick (50ms)

    // --- Tension calculation ---
    float tension_from_reel = g_reel_speed * 3.0f;
    float tension_from_fish = g_fish_pull * g_reel_speed * 1.5f;
    float tension_raw = tension_from_reel + tension_from_fish;

    // Decay when not reeling
    if (encoder_delta == 0) {
        tension_raw = g_tension * 0.85f; // decay toward 0
    }

    // Smooth tension
    g_tension = lerp(g_tension, tension_raw, 0.3f);
    g_tension_pct = clampf(g_tension / g_max_tension * 100.0f, 0, 100.0f);

    // --- Phase transitions ---
    if (now - g_phase_start >= g_phase_duration) {
        advance_phase();
    }

    // --- Surge warning (haptic buzz 500ms before surge) ---
    if (g_fish_phase == PHASE_FIGHTING) {
        uint32_t time_in_phase = now - g_phase_start;
        uint32_t remaining = (g_phase_duration > time_in_phase) ? g_phase_duration - time_in_phase : 0;
        if (remaining <= 500 && !g_surge_warned) {
            g_surge_warned = true;
            haptic_play(HAPTIC_BUZZ);
        }
    } else {
        g_surge_warned = false;
    }

    // --- Haptic clicks while reeling ---
    if (encoder_delta > 0) {
        static uint32_t last_reel_haptic = 0;
        if (now - last_reel_haptic >= 80) {
            haptic_play(HAPTIC_CLICK_30);
            last_reel_haptic = now;
        }
    }

    // --- Win/lose checks ---

    // Win: line fully reeled in
    if (g_line_distance <= 0) {
        g_line_distance = 0;
        g_game_state = FISH_WON;
        haptic_play(HAPTIC_DOUBLE_CLICK);
        return;
    }

    // Lose: line snapped (tension >= 100% for 200ms)
    if (g_tension_pct >= 100.0f) {
        if (g_snap_start == 0) g_snap_start = now;
        if (now - g_snap_start >= 200) {
            g_game_state = FISH_LOST;
            g_loss_reason = LOSS_LINE_SNAPPED;
            haptic_play(HAPTIC_ALERT);
            return;
        }
    } else {
        g_snap_start = 0;
    }

    // Lose: fish escaped
    if (g_line_distance >= g_max_distance) {
        g_game_state = FISH_LOST;
        g_loss_reason = LOSS_FISH_ESCAPED;
        haptic_play(HAPTIC_SOFT_BUMP);
        return;
    }
}

FishingState fishing_get_state() {
    const FishTemplate& ft = fish_templates[g_fish_type];
    FishingState s;
    s.game_state = g_game_state;
    s.fish_phase = g_fish_phase;
    s.loss_reason = g_loss_reason;
    s.fish_type = g_fish_type;
    s.fish_weight = g_fish_weight;
    s.line_distance = g_line_distance;
    s.start_distance = g_start_distance;
    s.max_distance = g_max_distance;
    s.tension_pct = g_tension_pct;
    s.reel_speed = g_reel_speed;
    s.fight_time_ms = g_total_fight_time;
    s.fish_name = ft.name;
    return s;
}
```

- [ ] **Step 2: Compile to check for errors**

Run: `cd /c/Users/roarh/code/waveshare_knob && pio run -e knob 2>&1 | tail -10`

Expected: SUCCESS (fishing.cpp compiles but no one calls it yet).

- [ ] **Step 3: Commit**

```bash
git add src/fishing.h src/fishing.cpp
git commit -m "feat(fishing): implement game state machine, fish AI, reel physics"
```

---

## Task 3: Display — Fishing Screen UI

**Files:**
- Modify: `src/display.h` — remove `display_draw_knob`, add `display_draw_fishing`
- Modify: `src/display.cpp` — replace SCREEN_ANIM rebuild_screen block and add `display_draw_fishing()`

- [ ] **Step 1: Update display.h**

Replace the knob declaration with the fishing one. Change:

```cpp
// btn_hit: 0=none, 1=back pressed, 2=mute pressed
void display_draw_knob(Screen active, int32_t value, bool muted, int btn_hit);
```

To:

```cpp
#include "fishing.h"
void display_draw_fishing(Screen active, const FishingState& fs);
```

- [ ] **Step 2: Replace SCREEN_ANIM block in rebuild_screen()**

In `display.cpp`, find the `} else if (s == SCREEN_ANIM) {` block (currently the volume knob UI starting around line 368). Replace the entire block from `} else if (s == SCREEN_ANIM) {` up to (but not including) the closing `}` before `lv_screen_load(scr);` with:

```cpp
    } else if (s == SCREEN_ANIM) {
        // Title
        lv_obj_t *title = lv_label_create(scr);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0x00AAFF), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);
        lv_label_set_text(title, "FISKESPILL");

        // Background arc track — full 270 degree sweep
        lv_obj_t *track = lv_arc_create(scr);
        lv_obj_set_size(track, 280, 280);
        lv_arc_set_rotation(track, 135);
        lv_arc_set_bg_angles(track, 0, 270);
        lv_arc_set_value(track, 0);
        lv_obj_set_style_arc_width(track, 20, LV_PART_MAIN);
        lv_obj_set_style_arc_color(track, lv_color_hex(0x222222), LV_PART_MAIN);
        lv_obj_set_style_arc_width(track, 0, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(track, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(track, LV_ALIGN_CENTER, 0, 10);

        // Reel arc (indicator) — reuse lbl_weather
        lbl_weather = lv_arc_create(scr);
        lv_obj_set_size(lbl_weather, 280, 280);
        lv_arc_set_rotation(lbl_weather, 135);
        lv_arc_set_bg_angles(lbl_weather, 0, 270);
        lv_arc_set_range(lbl_weather, 0, 100);
        lv_arc_set_value(lbl_weather, 0);
        lv_obj_set_style_arc_width(lbl_weather, 0, LV_PART_MAIN);
        lv_obj_set_style_arc_width(lbl_weather, 20, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(lbl_weather, lv_color_hex(0x00FF88), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(lbl_weather, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_remove_flag(lbl_weather, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(lbl_weather, LV_ALIGN_CENTER, 0, 10);

        // Center: distance label — reuse lbl_time
        lbl_time = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(lbl_time, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, -20);
        lv_label_set_text(lbl_time, "Trykk for\na kaste");

        // Fish state label — reuse lbl_date
        lbl_date = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_date, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_align(lbl_date, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(lbl_date, 200);
        lv_obj_align(lbl_date, LV_ALIGN_CENTER, 0, 20);
        lv_label_set_text(lbl_date, "");

        // Tension bar background — reuse lbl_info
        lbl_info = lv_obj_create(scr);
        lv_obj_set_size(lbl_info, 200, 8);
        lv_obj_set_style_radius(lbl_info, 4, 0);
        lv_obj_set_style_bg_color(lbl_info, lv_color_hex(0x00FF88), 0);
        lv_obj_set_style_bg_opa(lbl_info, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(lbl_info, 0, 0);
        lv_obj_align(lbl_info, LV_ALIGN_CENTER, 0, 50);

        // Speed label — reuse lbl_debug
        lbl_debug = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl_debug, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_debug, lv_color_hex(0x666666), 0);
        lv_obj_align(lbl_debug, LV_ALIGN_CENTER, 0, 70);
        lv_label_set_text(lbl_debug, "");
    }
```

- [ ] **Step 3: Replace display_draw_knob() with display_draw_fishing()**

Delete the entire `display_draw_knob()` function (around lines 586-633) and replace with:

```cpp
void display_draw_fishing(Screen active, const FishingState& fs) {
    if (!scr || cur_screen != active) rebuild_screen(active);

    if (fs.game_state == FISH_IDLE) {
        // Idle screen — prompt to start
        if (lbl_time) {
            lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_24, 0);
            lv_label_set_text(lbl_time, "Trykk for\na kaste");
            lv_obj_set_style_text_color(lbl_time, lv_color_hex(0x00AAFF), 0);
            lv_obj_set_style_text_align(lbl_time, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(lbl_time, 280);
        }
        if (lbl_date) lv_label_set_text(lbl_date, "");
        if (lbl_debug) lv_label_set_text(lbl_debug, "");
        if (lbl_weather) lv_arc_set_value(lbl_weather, 0);
        if (lbl_info) lv_obj_set_size(lbl_info, 0, 8);
        lv_obj_invalidate(lv_screen_active());
        return;
    }

    if (fs.game_state == FISH_WON) {
        if (lbl_time) {
            lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_48, 0);
            lv_label_set_text(lbl_time, "FANGST!");
            lv_obj_set_style_text_color(lbl_time, lv_color_hex(0x00FF88), 0);
            lv_obj_set_style_text_align(lbl_time, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(lbl_time, 360);
        }
        if (lbl_date) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s  %.1f kg\n%us",
                     fs.fish_name, fs.fish_weight,
                     (unsigned)(fs.fight_time_ms / 1000));
            lv_label_set_text(lbl_date, buf);
            lv_obj_set_style_text_color(lbl_date, lv_color_hex(0xFFFFFF), 0);
        }
        if (lbl_debug) lv_label_set_text(lbl_debug, "Trykk for a spille igjen");
        if (lbl_weather) lv_arc_set_value(lbl_weather, 100);
        if (lbl_info) lv_obj_set_size(lbl_info, 0, 8);
        lv_obj_invalidate(lv_screen_active());
        return;
    }

    if (fs.game_state == FISH_LOST) {
        if (lbl_time) {
            lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_48, 0);
            lv_label_set_text(lbl_time, "MISTET!");
            lv_obj_set_style_text_color(lbl_time, lv_color_hex(0xFF4444), 0);
            lv_obj_set_style_text_align(lbl_time, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(lbl_time, 360);
        }
        if (lbl_date) {
            const char* reason = (fs.loss_reason == LOSS_LINE_SNAPPED)
                ? "Snora royk!" : "Fisken stakk av!";
            lv_label_set_text(lbl_date, reason);
            lv_obj_set_style_text_color(lbl_date, lv_color_hex(0xFFAAAA), 0);
        }
        if (lbl_debug) lv_label_set_text(lbl_debug, "Trykk for a prove igjen");
        if (lbl_weather) lv_arc_set_value(lbl_weather, 0);
        if (lbl_info) lv_obj_set_size(lbl_info, 0, 8);
        lv_obj_invalidate(lv_screen_active());
        return;
    }

    // --- FISH_FIGHTING state ---

    // Reel arc: percentage of line reeled in
    int reel_pct = (int)((1.0f - fs.line_distance / fs.start_distance) * 100.0f);
    if (reel_pct < 0) reel_pct = 0;
    if (reel_pct > 100) reel_pct = 100;

    // Arc color based on tension
    uint32_t arc_color;
    if (fs.tension_pct < 50)       arc_color = 0x00FF88;  // green
    else if (fs.tension_pct < 75)  arc_color = 0xFFAA00;  // yellow
    else                           arc_color = 0xFF4444;  // red

    if (lbl_weather) {
        lv_arc_set_value(lbl_weather, reel_pct);
        lv_obj_set_style_arc_color(lbl_weather, lv_color_hex(arc_color), LV_PART_INDICATOR);
    }

    // Distance label
    if (lbl_time) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%dm", (int)fs.line_distance);
        lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_48, 0);
        lv_label_set_text(lbl_time, buf);
        lv_obj_set_style_text_color(lbl_time, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_align(lbl_time, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(lbl_time, 360);
    }

    // Fish state label
    if (lbl_date) {
        const char* state_text;
        uint32_t state_color;
        switch (fs.fish_phase) {
            case PHASE_CALM:     state_text = "Rolig";     state_color = 0x448844; break;
            case PHASE_RESTLESS: state_text = "Urolig";    state_color = 0xFFAA00; break;
            case PHASE_FIGHTING: state_text = "Kjemper!";  state_color = 0xFF8800; break;
            case PHASE_SURGING:  state_text = "DRAR!";     state_color = 0xFF4444; break;
            case PHASE_TIRED:    state_text = "Sliten";    state_color = 0x4488AA; break;
            default:             state_text = "";          state_color = 0x888888; break;
        }
        lv_label_set_text(lbl_date, state_text);
        lv_obj_set_style_text_color(lbl_date, lv_color_hex(state_color), 0);
    }

    // Tension bar
    if (lbl_info) {
        int bar_w = (int)(fs.tension_pct * 2.0f); // 0-200px
        if (bar_w < 0) bar_w = 0;
        if (bar_w > 200) bar_w = 200;
        lv_obj_set_size(lbl_info, bar_w, 8);
        lv_obj_set_style_bg_color(lbl_info, lv_color_hex(arc_color), 0);
        lv_obj_align(lbl_info, LV_ALIGN_CENTER, 0, 50);
    }

    // Speed label
    if (lbl_debug) {
        const char* speed_text;
        if (fs.reel_speed < 0.5f)      speed_text = "";
        else if (fs.reel_speed < 2.0f) speed_text = "Sakte";
        else if (fs.reel_speed < 4.0f) speed_text = "Middels";
        else if (fs.reel_speed < 6.0f) speed_text = "Fort";
        else                           speed_text = "Maks!";
        lv_label_set_text(lbl_debug, speed_text);
    }

    lv_obj_invalidate(lv_screen_active());
}
```

- [ ] **Step 4: Compile**

Run: `cd /c/Users/roarh/code/waveshare_knob && pio run -e knob 2>&1 | tail -10`

Expected: Will fail because main.cpp still calls `display_draw_knob()`. That's fixed in Task 4.

- [ ] **Step 5: Commit display changes**

```bash
git add src/display.h src/display.cpp
git commit -m "feat(fishing): replace volume knob UI with fishing game display"
```

---

## Task 4: Main Loop — Wire Up Fishing Game

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add fishing include**

At the top of `main.cpp`, after the existing includes, add:

```cpp
#include "fishing.h"
```

- [ ] **Step 2: Remove volume knob state variables**

Delete these lines (around lines 24-29):

```cpp
static int32_t knob_value = 50;       // volume knob value (0-100)
static int32_t knob_before_mute = 50; // value before mute
static bool knob_muted = false;
static int32_t knob_enc_base = 0;     // encoder count when entering knob screen
static int knob_btn_hit = 0;          // 0=none, 1=back, 2=mute (for UI feedback)
static bool knob_btn_acted = false;   // true once action fires, reset when finger leaves zone
```

Replace with:

```cpp
static int32_t fish_enc_base = 0;     // encoder count when entering fishing screen
```

- [ ] **Step 3: Add fishing_init() to setup()**

In `setup()`, after `weather_init();` add:

```cpp
    fishing_init();
```

- [ ] **Step 4: Replace SCREEN_ANIM input handling**

Replace the entire `if (currentScreen == SCREEN_ANIM) {` block (lines ~108-156) with:

```cpp
    if (currentScreen == SCREEN_ANIM) {
        FishingState fs = fishing_get_state();

        if (fs.game_state == FISH_FIGHTING) {
            // During fight: encoder controls reel, no screen switching
            int32_t delta = encoder_get_count() - fish_enc_base;
            fish_enc_base = encoder_get_count();
            fishing_tick(delta);
        } else {
            // IDLE / WON / LOST: tap to start/restart, encoder navigates
            if (latch_tapped) {
                if (fs.game_state == FISH_IDLE || fs.game_state == FISH_WON || fs.game_state == FISH_LOST) {
                    fishing_start();
                    fish_enc_base = encoder_get_count();
                }
                latch_tapped = false;
            }

            // Allow screen switching when not fighting
            encoder_update_screen(SCREEN_COUNT);
            Screen newScreen = (Screen)encoder_get_screen(SCREEN_COUNT);
            if (newScreen != currentScreen) {
                currentScreen = newScreen;
                lastDraw = 0;
                haptic_play(HAPTIC_CLICK);
            }
        }
        latch_tapped = false;
        latch_longPressed = false;
    } else {
```

- [ ] **Step 5: Replace SCREEN_ANIM draw call**

In the `switch (currentScreen)` draw block, replace:

```cpp
            case SCREEN_ANIM:
                display_draw_knob(currentScreen, knob_value, knob_muted, knob_btn_hit);
                break;
```

With:

```cpp
            case SCREEN_ANIM: {
                FishingState fs = fishing_get_state();
                if (fs.game_state == FISH_FIGHTING) {
                    // Tick the game even on draw frames (keeps physics running)
                    int32_t delta = encoder_get_count() - fish_enc_base;
                    fish_enc_base = encoder_get_count();
                    fishing_tick(delta);
                }
                display_draw_fishing(currentScreen, fs);
                break;
            }
```

Wait — the tick is already called in the input section above. We shouldn't double-tick. Fix: only call `fishing_get_state()` in the draw section, no extra tick:

```cpp
            case SCREEN_ANIM: {
                FishingState fs = fishing_get_state();
                display_draw_fishing(currentScreen, fs);
                break;
            }
```

- [ ] **Step 6: Remove the encoder sync for SCREEN_ANIM entry**

In the `} else {` block (normal screen switching, around line 162), find:

```cpp
            if (newScreen == SCREEN_ANIM) {
                knob_enc_base = encoder_get_count();  // sync encoder position
            }
```

Replace with:

```cpp
            if (newScreen == SCREEN_ANIM) {
                fish_enc_base = encoder_get_count();
            }
```

- [ ] **Step 7: Compile and verify**

Run: `cd /c/Users/roarh/code/waveshare_knob && pio run -e knob 2>&1 | tail -10`

Expected: SUCCESS with no errors. Warnings about unused touch/weather variables are fine.

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp
git commit -m "feat(fishing): wire up fishing game in main loop, replace volume knob"
```

---

## Task 5: Flash and Test

- [ ] **Step 1: Full clean build**

Run: `cd /c/Users/roarh/code/waveshare_knob && pio run -e knob -t clean && pio run -e knob 2>&1 | tail -5`

Expected: SUCCESS

- [ ] **Step 2: Flash to device**

Run: `cd /c/Users/roarh/code/waveshare_knob && pio run -e knob -t upload 2>&1 | tail -5`

Expected: SUCCESS. Device hard resets.

- [ ] **Step 3: Functional test checklist**

| Test | How | Expected |
|------|-----|----------|
| Navigate to fishing screen | Rotate encoder to 5th screen | See "FISKESPILL" title, "Trykk for a kaste" |
| Start game | Tap screen | Distance appears (e.g., "127m"), fish state shows |
| Reel in | Rotate CW fast | Distance decreases, arc fills, haptic clicks |
| Fish fights | Wait for "Kjemper!" state | Distance increases, tension rises |
| Surge warning | Wait for fighting phase end | Haptic buzz, then "DRAR!" with high tension |
| Tension danger | Reel hard during surge | Arc turns red, bar fills |
| Win game | Reel distance to 0 | "FANGST!" with fish name/weight |
| Restart | Tap after win | Back to idle screen |
| Lose: snap | Reel full speed during surge | "MISTET!" / "Snora royk!" |
| Lose: escape | Don't reel at all | "MISTET!" / "Fisken stakk av!" |
| Leave screen | Rotate encoder while idle/won/lost | Navigates to other screens |
| Return | Rotate back to fishing screen | Shows idle state, not mid-game |

- [ ] **Step 4: Final commit with any fixes**

```bash
git add -A
git commit -m "feat(fishing): fishing reel game complete, replaces volume screen"
```
