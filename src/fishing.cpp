#include "fishing.h"
#include "haptic.h"
#include <Arduino.h>
#include <stdlib.h>

// ============ FISH DATA ============

struct FishTemplate {
    const char* name;
    float min_weight, max_weight;
    float min_start, max_start;
    float min_pull, max_pull;
    float min_stamina, max_stamina;
    float min_surge_interval, max_surge_interval;
    float max_tension;
};

static const FishTemplate fish_templates[FISH_TYPE_COUNT] = {
    {"Sei",     2,    5,    50,   80,   2,    4,    15000, 25000, 8000, 15000, 60},
    {"Torsk",   5,    15,   80,   150,  4,    7,    30000, 50000, 6000, 12000, 70},
    {"Laks",    8,    20,   100,  200,  6,    10,   45000, 75000, 4000,  8000, 80},
    {"Kveite",  20,   100,  150,  300,  8,    15,   60000,120000, 3000,  6000, 90},
};

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
static float         g_tension = 0;
static float         g_tension_pct = 0;
static float         g_max_tension = 60;
static float         g_fish_pull = 0;
static float         g_fish_base_pull = 0;

static float         g_reel_speed = 0;
static float         g_reel_speed_prev = 0;
static int32_t       g_reel_history[4] = {0};
static uint8_t       g_reel_hist_idx = 0;

static uint32_t      g_phase_start = 0;
static uint32_t      g_phase_duration = 0;
static uint32_t      g_fight_start = 0;
static uint32_t      g_total_fight_time = 0;

static bool          g_surge_warned = false;
static uint32_t      g_next_surge_time = 0;
static uint32_t      g_snap_start = 0;
static float         g_stamina_total = 0;

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

    float fatigue = clampf((float)g_total_fight_time / g_stamina_total, 0, 1.0f);
    float calm_mult = 1.0f + fatigue * 1.5f;
    float fight_mult = 1.0f - fatigue * 0.5f;
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

    g_fish_weight = randf(ft.min_weight, ft.max_weight);
    float wf = lerp_factor(g_fish_weight, ft.min_weight, ft.max_weight);

    g_start_distance = lerp(ft.min_start, ft.max_start, wf);
    g_line_distance = g_start_distance;
    g_max_distance = g_start_distance + 50.0f;
    g_fish_base_pull = lerp(ft.min_pull, ft.max_pull, wf);
    g_stamina_total = lerp(ft.min_stamina, ft.max_stamina, wf);
    g_max_tension = ft.max_tension;

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

    if (encoder_delta < 0) encoder_delta = 0;
    g_reel_history[g_reel_hist_idx] = encoder_delta;
    g_reel_hist_idx = (g_reel_hist_idx + 1) % 4;

    float sum = 0;
    for (int i = 0; i < 4; i++) sum += g_reel_history[i];
    g_reel_speed_prev = g_reel_speed;
    g_reel_speed = sum / 4.0f;

    float acceleration = g_reel_speed - g_reel_speed_prev;
    float accel_bonus = 1.0f + clampf(acceleration, 0, 5.0f) * 0.2f;

    float speed_factor = 0.5f;
    float reel_rate = g_reel_speed * speed_factor * accel_bonus;

    g_line_distance -= reel_rate;
    g_line_distance += g_fish_pull * 0.05f;

    float tension_from_reel = g_reel_speed * 3.0f;
    float tension_from_fish = g_fish_pull * g_reel_speed * 1.5f;
    float tension_raw = tension_from_reel + tension_from_fish;

    if (encoder_delta == 0) {
        tension_raw = g_tension * 0.85f;
    }

    g_tension = lerp(g_tension, tension_raw, 0.3f);
    g_tension_pct = clampf(g_tension / g_max_tension * 100.0f, 0, 100.0f);

    if (now - g_phase_start >= g_phase_duration) {
        advance_phase();
    }

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

    if (encoder_delta > 0) {
        static uint32_t last_reel_haptic = 0;
        if (now - last_reel_haptic >= 80) {
            haptic_play(HAPTIC_CLICK_30);
            last_reel_haptic = now;
        }
    }

    if (g_line_distance <= 0) {
        g_line_distance = 0;
        g_game_state = FISH_WON;
        haptic_play(HAPTIC_DOUBLE_CLICK);
        return;
    }

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
