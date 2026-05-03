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
