# Fiskespill — Fishing Reel Game Design Spec

> Replaces the volume knob screen (`SCREEN_ANIM`) with a fishing reel game that uses the rotary encoder as a physical fishing reel.

## Overview

The player hooks a random fish and must reel it in using the physical dial. The outer ring of the round display animates as a fishing reel (line wrapping in/out), while the center shows a live dashboard with distance, tension, fish state, and speed. The game rewards fast reeling but punishes reckless speed when the fish is fighting — creating a tension/skill balance.

All UI text is in Norwegian (Bokmal, ASCII only — no ae/oe/aa in Montserrat font).

## Screen Slot

- Replaces `SCREEN_ANIM` in the existing 5-screen rotation
- Same encoder-navigate-to-enter pattern as current volume screen
- 50ms draw interval (same as current SCREEN_ANIM)
- Rename enum value semantics: `SCREEN_ANIM` now means "fishing game" (no enum change needed, just different content)

## Game States

### 1. FISH_IDLE
- Title: "FISKESPILL" (Montserrat 24, centered)
- Prompt: "Trykk for a kaste" (Montserrat 14, dimmed)
- Rotating the encoder navigates to other screens (normal screen switching)
- Tap anywhere → transition to FISH_FIGHTING

### 2. FISH_FIGHTING
- Main gameplay state
- Encoder rotation = reeling (CW = reel in)
- Encoder navigation between screens is disabled while fighting
- Tap does nothing during fight
- Transitions to FISH_WON when line_distance <= 0
- Transitions to FISH_LOST when line snaps (tension > 100%) or fish escapes (line_distance > max_distance)

### 3. FISH_WON
- Big text: "FANGST!" (Montserrat 48, green)
- Fish info: name and weight (e.g., "Torsk 12.3 kg")
- Distance reeled / time taken
- Prompt: "Trykk for a spille igjen"
- Tap → FISH_IDLE
- Encoder rotation → navigate to other screens

### 4. FISH_LOST
- Big text: "MISTET!" (Montserrat 48, red)
- Reason: "Snore royk!" (line snapped) or "Fisken stakk av!" (fish escaped)
- Prompt: "Trykk for a prove igjen"
- Tap → FISH_IDLE
- Encoder rotation → navigate to other screens

## Display Layout (FISH_FIGHTING)

### Outer Ring — The Reel
- LVGL arc spanning the screen edge, similar to current volume arc
- Arc represents line on the reel: full arc = all line reeled in, empty = all line out
- Arc value = `(1 - line_distance / start_distance) * 100`
- **Color**: gradient based on tension level
  - Green (`0x00FF88`) — tension 0-50%
  - Yellow (`0xFFAA00`) — tension 50-75%
  - Red (`0xFF4444`) — tension 75-100%
- **Animation**: when encoder is turning, the arc visually updates each frame giving a "reeling" feel
- Arc width: 20px (same as current volume arc)

### Center Dashboard
All centered in the middle of the display:

- **Line distance** — Montserrat 48, white. Shows meters remaining: "147m"
- **Fish state** — Montserrat 14, color-coded. One of:
  - "Rolig" (calm) — dim green
  - "Urolig" (restless) — yellow
  - "Kjemper!" (fighting) — orange
  - "DRAR!" (surging) — red, flashing
  - "Sliten" (tired) — dim blue
- **Tension bar** — Horizontal bar below distance, 200px wide, 8px tall
  - Fill width = tension percentage
  - Color matches arc ring color (green/yellow/red)
- **Reel speed** — Montserrat 14, dim. Shows current speed indication: "Sakte" / "Middels" / "Fort" / "Maks!"

## Fish Mechanics

### Fish Selection
Random fish selected when transitioning from IDLE → FIGHTING:

| Norsk navn | Weight range | Start distance | Pull strength | Stamina | Surge frequency |
|---|---|---|---|---|---|
| Sei | 2-5 kg | 50-80m | Low (2-4) | Low (15-25s) | Rare (8-15s) |
| Torsk | 5-15 kg | 80-150m | Medium (4-7) | Medium (30-50s) | Medium (6-12s) |
| Laks | 8-20 kg | 100-200m | High (6-10) | High (45-75s) | Frequent (4-8s) |
| Kveite | 20-100 kg | 150-300m | Very high (8-15) | Very high (60-120s) | Frequent (3-6s) |

Weight is randomized within range. All stats scale linearly with weight within the fish type's range.

Selection probability: Sei 40%, Torsk 30%, Laks 20%, Kveite 10%.

### Fish Behavior State Machine

```
CALM → RESTLESS → FIGHTING → SURGING → TIRED → CALM (cycle repeats)
```

- **CALM**: Fish drifts slowly outward (0.5-1.0 m/tick). Low base tension. Duration: 3-8s (random).
- **RESTLESS**: Fish starts pulling harder (1-2 m/tick). Tension modifier increases. Duration: 2-4s. Haptic: double click on entry.
- **FIGHTING**: Active resistance (2-4 m/tick base, scaled by fish strength). Significant tension when reeling. Duration: 3-6s.
- **SURGING** ("DRAR!"): Sudden strong pull (5-10 m/tick, scaled by strength). Very high tension if reeling. Duration: 1-3s. **Haptic buzz warning 500ms before surge starts.** This is the snap danger zone.
- **TIRED** ("Sliten"): Fish barely pulls (0.2-0.5 m/tick). Very low tension. Duration: 2-5s. Best time to reel hard.

Cycle timing varies by fish stamina — high stamina fish spend less time tired and more time fighting/surging.

As total fight time increases, fish gradually tires: surge frequency decreases, tired phases lengthen. This prevents infinite games.

### Fish Escape
If `line_distance > start_distance + 50m`, the fish escapes. This means the fish can take back more line than it started with if you don't reel at all.

## Reel Physics

### Encoder Input
- Read `encoder_get_count()` delta each tick (50ms)
- CW rotation (positive delta) = reeling in
- CCW rotation = no effect (can't let line out intentionally, it just doesn't reel)
- `reel_speed` = weighted moving average of recent deltas (smoothed over ~200ms / 4 ticks)

### Speed & Acceleration
- `reel_rate` (meters per tick) = `reel_speed * speed_factor`
- `speed_factor` base = 0.5 (so 1 detent per tick = 0.5m reeled)
- **Acceleration bonus**: if reel_speed is increasing (current > previous average), apply multiplier:
  - `accel_bonus = 1.0 + clamp(acceleration, 0, 5) * 0.2` (max 2.0x)
  - This means ramping up speed gives up to 2x reel rate
- High sustained speed is also good: `reel_rate` scales linearly with speed even without acceleration
- Net line change per tick: `line_distance -= (reel_rate * accel_bonus)` then `line_distance += fish_pull`

### Tension System
```
tension_from_reel = reel_speed * 3.0  (0-30 range at typical speeds)
tension_from_fish = fish_pull_current * reel_speed * 1.5  (fighting while reeling = high tension)
tension_raw = tension_from_reel + tension_from_fish
tension = lerp(tension_previous, tension_raw, 0.3)  (smoothed, doesn't spike instantly)
tension_pct = clamp(tension / max_tension, 0, 1.0) * 100
```

- `max_tension` varies by fish type (bigger fish = stronger line assumed, slightly higher threshold)
  - Sei: 60, Torsk: 70, Laks: 80, Kveite: 90
- Tension **decays** when not reeling: `tension_raw -= decay_rate * dt` (decays to 0 over ~1.5s)
- **Line snaps** when `tension_pct >= 100` for more than 200ms (brief spikes forgiven)

### Summary of the Skill Balance
- **Fast reeling during calm/tired** = optimal play (high speed, low fish resistance = manageable tension)
- **Fast reeling during fighting** = risky but effective (line comes in fast, but tension climbs)
- **Fast reeling during surge** = very dangerous (tension spikes, likely snap)
- **Not reeling during surge** = safe for line, but fish takes distance back
- **Acceleration bursts** = rewarded with multiplier, encourages dynamic "pump" action on the dial

## Haptic Feedback

| Event | Effect | Constant |
|---|---|---|
| Reel click (each detent while fighting) | Strong Click 30% | `HAPTIC_CLICK_30` (3) |
| Fish enters RESTLESS | Double Click | `HAPTIC_DOUBLE_CLICK` (10) |
| Surge warning (500ms before SURGING) | Strong Buzz | `HAPTIC_BUZZ` (14) |
| Line snap (LOST) | 750ms Alert | `HAPTIC_ALERT` (15) |
| Fish landed (WON) | Double Click | `HAPTIC_DOUBLE_CLICK` (10) |
| Fish escaped (LOST) | Soft Bump | `HAPTIC_SOFT_BUMP` (7) |

## Touch Integration

- **FISH_IDLE**: tap anywhere → start game (transition to FIGHTING)
- **FISH_FIGHTING**: tap does nothing (pure dial gameplay)
- **FISH_WON / FISH_LOST**: tap anywhere → return to IDLE

Touch uses the existing latch system from main.cpp (`latch_tapped`).

## Encoder Integration During Game

- **FISH_IDLE / FISH_WON / FISH_LOST**: encoder rotation navigates between screens (normal `encoder_update_screen` behavior)
- **FISH_FIGHTING**: encoder rotation is captured for reel input, screen switching is suppressed. The existing `knob_enc_base` pattern (sync encoder on entry, read delta) works here.

## Architecture

### New Code
- **Game state + physics**: new static variables and logic in `main.cpp` within the existing `if (currentScreen == SCREEN_ANIM)` block. Replaces the volume knob logic entirely.
- **Display function**: new `display_draw_fishing()` function in `display.cpp`, replacing `display_draw_knob()`. Uses same rebuild_screen pattern with LVGL objects.
- **Display header**: update `display.h` — remove `display_draw_knob()`, add `display_draw_fishing()`.

### Reused Infrastructure
- Encoder: `encoder_get_count()`, `encoder_set_screen()`, existing timer-polled driver
- Haptic: `haptic_play()` with existing effect constants
- Touch: existing `latch_tapped`, `touch_read()` from main loop
- Display: QSPI driver, LVGL, `rebuild_screen()` pattern, all existing label pointer pattern
- Screen enum: `SCREEN_ANIM` stays, just different content

### LVGL Objects for Fishing Screen
Reuse the existing static label pointers where possible:
- `lbl_weather` → reel arc (arc object, like current volume arc)
- `lbl_time` → distance label ("147m")
- `lbl_date` → fish state label ("Kjemper!")
- `lbl_info` → tension bar (rect object)
- `lbl_debug` → speed indicator

Plus a background arc track (local variable in rebuild_screen, doesn't need updating).

## Scope & Constraints

- **Fonts**: Montserrat 14, 24, 48 only (already compiled in)
- **No images/sprites**: text and LVGL primitives only (arcs, rects, labels)
- **Norwegian ASCII only**: no ae/oe/aa characters
- **50ms game tick**: all physics calculations per tick, not per frame
- **Memory**: no dynamic allocation during gameplay. All LVGL objects created once in rebuild_screen.
- **No sound**: device has DAC but it's unused. Haptic only.
