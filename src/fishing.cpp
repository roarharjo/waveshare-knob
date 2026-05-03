#include "fishing.h"
#include "haptic.h"
#include <Arduino.h>
#include <stdlib.h>
#include <math.h>

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
    //name        wMin wMax dMin dMax pMin pMax  sMin   sMax  sgMin sgMax maxT
    {"Kontepella", 0.1, 0.8, 20,  40,  1,   2,   8000, 15000, 10000,20000, 45},
    {"Sei",       2,   5,   50,  80,  2,   4,   15000, 25000, 8000, 15000, 60},
    {"Torsk",     5,   15,  80,  150, 4,   7,   30000, 50000, 6000, 12000, 70},
    {"Laks",      8,   20,  100, 200, 6,   10,  45000, 75000, 4000,  8000, 80},
    {"Kveite",    20,  100, 150, 300, 8,   15,  60000,120000, 3000,  6000, 90},
};

// Kontansen 35%, Sei 25%, Torsk 20%, Laks 13%, Kveite 7%
static const uint8_t fish_weights[FISH_TYPE_COUNT] = {35, 25, 20, 13, 7};

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

// Optimal speed zone — shifts with fish phase
static float         g_optimal_lo = 0;
static float         g_optimal_hi = 0;

// Steadiness tracking — variance of recent speeds
static float         g_speed_log[8] = {0};
static uint8_t       g_speed_log_idx = 0;
static float         g_steadiness = 100;    // 0-100
static float         g_reel_efficiency = 1.0f; // multiplier

static uint32_t      g_phase_start = 0;
static uint32_t      g_phase_duration = 0;
static uint32_t      g_fight_start = 0;
static uint32_t      g_total_fight_time = 0;

static bool          g_surge_warned = false;
static uint32_t      g_next_surge_time = 0;
static uint32_t      g_snap_start = 0;
static float         g_stamina_total = 0;
static uint32_t      g_end_time = 0;        // when game ended

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
            g_optimal_lo = 2.0f; g_optimal_hi = 5.0f;
            haptic_play(HAPTIC_SOFT_BUMP);
            break;
        case PHASE_RESTLESS:
            g_phase_duration = (uint32_t)randf(2000, 4000);
            g_fish_pull = randf(1.0f, 2.0f);
            g_optimal_lo = 1.5f; g_optimal_hi = 3.5f;
            haptic_play(HAPTIC_CLICK_60);
            break;
        case PHASE_FIGHTING:
            g_phase_duration = (uint32_t)(randf(3000, 6000) * fight_mult);
            g_fish_pull = randf(2.0f, 4.0f) * (g_fish_base_pull / 5.0f);
            g_optimal_lo = 0.5f; g_optimal_hi = 2.5f;  // narrow!
            haptic_play(HAPTIC_DOUBLE_CLICK);
            break;
        case PHASE_SURGING:
            g_phase_duration = (uint32_t)(randf(1000, 3000) * fight_mult);
            g_fish_pull = randf(5.0f, 10.0f) * (g_fish_base_pull / 5.0f);
            g_optimal_lo = 0.0f; g_optimal_hi = 1.0f;  // stop or snap!
            haptic_play(HAPTIC_ALERT);
            g_surge_warned = false;
            break;
        case PHASE_TIRED:
            g_phase_duration = (uint32_t)(randf(2000, 5000) * calm_mult);
            g_fish_pull = randf(0.2f, 0.5f);
            g_optimal_lo = 3.0f; g_optimal_hi = 7.0f;
            haptic_play(HAPTIC_CLICK_30);
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
    for (int i = 0; i < 8; i++) g_speed_log[i] = 0;
    g_speed_log_idx = 0;
    g_steadiness = 100;
    g_reel_efficiency = 1.0f;

    g_loss_reason = LOSS_NONE;
    g_end_time = 0;
    g_game_state = FISH_FIGHTING;
    enter_phase(PHASE_CALM);
}

void fishing_tick(int32_t encoder_delta) {
    if (g_game_state != FISH_FIGHTING) return;

    uint32_t now = millis();
    g_total_fight_time = now - g_fight_start;

    // Debug: periodic state dump
    static uint32_t last_debug = 0;
    if (now - last_debug >= 2000) {
        Serial.printf("[FISH] dist=%.0f tens=%.0f%% spd=%.1f phase=%d pull=%.1f heap=%u\n",
            g_line_distance, g_tension_pct, g_reel_speed, (int)g_fish_phase,
            g_fish_pull, (unsigned)ESP.getFreeHeap());
        last_debug = now;
    }

    // CCW = reel in (encoder gives negative for CCW)
    encoder_delta = -encoder_delta;
    if (encoder_delta < 0) encoder_delta = 0;
    g_reel_history[g_reel_hist_idx] = encoder_delta;
    g_reel_hist_idx = (g_reel_hist_idx + 1) % 4;

    float sum = 0;
    for (int i = 0; i < 4; i++) sum += g_reel_history[i];
    g_reel_speed_prev = g_reel_speed;
    g_reel_speed = sum / 4.0f;

    // --- Steadiness: variance of recent speeds ---
    g_speed_log[g_speed_log_idx] = g_reel_speed;
    g_speed_log_idx = (g_speed_log_idx + 1) % 8;
    float mean = 0;
    for (int i = 0; i < 8; i++) mean += g_speed_log[i];
    mean /= 8.0f;
    float variance = 0;
    for (int i = 0; i < 8; i++) {
        float diff = g_speed_log[i] - mean;
        variance += diff * diff;
    }
    variance /= 8.0f;
    // Steadiness: 100 = perfectly smooth, 0 = very jerky
    g_steadiness = clampf(100.0f - variance * 25.0f, 0, 100.0f);

    // --- Optimal speed zone check ---
    float speed_penalty = 0;
    if (g_reel_speed > 0.1f) {
        if (g_reel_speed < g_optimal_lo) {
            // Too slow: fish pulls harder, small tension penalty
            float undershoot = (g_optimal_lo - g_reel_speed) / g_optimal_lo;
            speed_penalty = undershoot * 5.0f;
        } else if (g_reel_speed > g_optimal_hi) {
            // Too fast: big tension penalty
            float overshoot = (g_reel_speed - g_optimal_hi) / g_optimal_hi;
            speed_penalty = overshoot * 15.0f;
        }
    }

    // --- Acceleration penalty: sudden changes spike tension ---
    float accel = fabsf(g_reel_speed - g_reel_speed_prev);
    float accel_penalty = accel * 3.0f;  // jerky changes cost tension

    // --- Reel efficiency: in-zone + steady = bonus, out-of-zone = penalty ---
    float zone_bonus = 1.0f;
    if (g_reel_speed >= g_optimal_lo && g_reel_speed <= g_optimal_hi) {
        zone_bonus = 1.0f + (g_steadiness / 100.0f) * 0.5f;  // up to 1.5x in zone + steady
    } else {
        zone_bonus = 0.6f;  // out of zone = only 60% effective
    }
    g_reel_efficiency = zone_bonus;

    float speed_factor = 0.125f;  // 4x harder — need serious cranking
    float reel_rate = g_reel_speed * speed_factor * zone_bonus;

    // --- Line distance: reel in vs fish pull ---
    g_line_distance -= reel_rate;
    // Fish pull aggressive — scales with phase, much harder when not reeling
    float pull_mult = (encoder_delta == 0) ? 4.0f : 1.5f;
    g_line_distance += g_fish_pull * 0.10f * pull_mult;

    // --- Tension: speed matters! Fast reeling builds real tension ---
    float tension_from_reel = g_reel_speed * 2.5f;
    float tension_from_fish = g_fish_pull * g_reel_speed * 1.8f;
    float tension_from_jerk = accel_penalty * 1.5f;
    float tension_raw = tension_from_reel + tension_from_fish + speed_penalty + tension_from_jerk;

    // Steadiness reduces tension (smooth reeling = less strain)
    float steadiness_reduction = (g_steadiness / 100.0f) * 0.3f;  // up to 30% reduction
    tension_raw *= (1.0f - steadiness_reduction);

    if (encoder_delta == 0) {
        tension_raw = g_tension * 0.90f;  // slow decay — tension lingers
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

    // --- Haptic: ONE effect per tick max to avoid I2C bus overload ---
    // Priority: snap warning > tension pulse > reel click > fish tug
    bool haptic_fired = false;

    // --- Win/lose checks (before haptic so we can fire the right effect) ---
    if (g_line_distance <= 0) {
        g_line_distance = 0;
        g_game_state = FISH_WON;
        g_end_time = now;
        haptic_play(HAPTIC_DOUBLE_CLICK);
        return;
    }

    if (g_tension_pct >= 100.0f) {
        if (g_snap_start == 0) g_snap_start = now;
        if (now - g_snap_start >= 200) {
            g_game_state = FISH_LOST;
            g_loss_reason = LOSS_LINE_SNAPPED;
            g_end_time = now;
            haptic_play(HAPTIC_ALERT);
            return;
        }
    } else {
        g_snap_start = 0;
    }

    if (g_line_distance >= g_max_distance) {
        g_game_state = FISH_LOST;
        g_loss_reason = LOSS_FISH_ESCAPED;
        g_end_time = now;
        haptic_play(HAPTIC_BUZZ);
        return;
    }

    // Near-snap warning (highest priority feedback)
    if (!haptic_fired && g_tension_pct > 85.0f) {
        static uint32_t last_snap_warn = 0;
        uint32_t interval = (g_tension_pct > 95.0f) ? 200 : 400;
        if (now - last_snap_warn >= interval) {
            haptic_play(HAPTIC_BUZZ);
            last_snap_warn = now;
            haptic_fired = true;
        }
    }

    // Reel clicks — scale intensity with speed
    if (!haptic_fired && encoder_delta > 0) {
        static uint32_t last_reel_haptic = 0;
        if (now - last_reel_haptic >= 100) {
            if (g_reel_speed > 4.0f)      haptic_play(HAPTIC_CLICK);
            else if (g_reel_speed > 2.0f) haptic_play(HAPTIC_CLICK_60);
            else                          haptic_play(HAPTIC_CLICK_30);
            last_reel_haptic = now;
            haptic_fired = true;
        }
    }

    // Fish tugging when not reeling (lowest priority)
    if (!haptic_fired && encoder_delta == 0 && g_fish_pull > 3.0f) {
        static uint32_t last_pull_haptic = 0;
        if (now - last_pull_haptic >= 500) {
            haptic_play(HAPTIC_SOFT_BUMP);
            last_pull_haptic = now;
        }
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
    s.optimal_speed_lo = g_optimal_lo;
    s.optimal_speed_hi = g_optimal_hi;
    s.steadiness = g_steadiness;
    s.reel_efficiency = g_reel_efficiency;
    s.fight_time_ms = g_total_fight_time;
    s.end_time_ms = g_end_time;
    s.fish_name = ft.name;
    return s;
}
