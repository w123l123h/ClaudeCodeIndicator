# LED Blink Delay & Timeout — Design Spec

**Date:** 2026-06-12
**Status:** approved

## Overview

Two changes to LED control:

1. **Blink delay parameter** — add `blink_ms` (uint16, ms) to the hardware control JSON so each LED can blink at its own rate instead of the hardcoded 500ms.
2. **5-minute timeout** — all LED events sent from `main.py` include `"timeout": 300` so LEDs auto-off after 5 minutes, preventing indefinite drain.

## JSON Schema

### LED object (existing + new fields)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `id` | int | required | LED index (0–2) |
| `on` | bool | required | Turn LED on or off |
| `r` | uint8 | 0 | Red component |
| `g` | uint8 | 0 | Green component |
| `b` | uint8 | 0 | Blue component |
| `blink` | bool | false | Enable blinking |
| `blink_ms` | uint16 | 500 | **New.** Blink toggle interval in ms. Only meaningful when `blink: true`. |
| `timeout` | uint32 | 600 (firmware default) | Seconds until LED auto-off. Now explicitly set to 300 by all main.py events. |

### Example

```json
{
  "leds": [
    {"id": 1, "on": true, "r": 255, "g": 128, "b": 0, "blink": true, "blink_ms": 300, "timeout": 300},
    {"id": 2, "on": false}
  ]
}
```

## Changes

### 1. `desktop-relay/config.py` — EVENT_LED_MAP

Add `"timeout": 300` to all four events. Add `"blink_ms": 300` to WAITING_USER.

```python
EVENT_LED_MAP = {
    "WORKING": {
        "leds": [
            {"id": 1, "on": True,  "r": 255, "g": 128, "b": 0, "timeout": 300},
            {"id": 2, "on": False},
        ]
    },
    "WAITING_USER": {
        "leds": [
            {"id": 1, "on": True,  "r": 255, "g": 128, "b": 0, "blink": True, "blink_ms": 300, "timeout": 300},
            {"id": 2, "on": False},
        ]
    },
    "COMPLETED": {
        "leds": [
            {"id": 1, "on": False},
            {"id": 2, "on": True,  "r": 0, "g": 255, "b": 0, "timeout": 300},
        ]
    },
    "ERROR": {
        "leds": [
            {"id": 0, "on": True,  "r": 255, "g": 0, "b": 0, "timeout": 300},
            {"id": 1, "on": False},
            {"id": 2, "on": False},
        ]
    },
}
```

### 2. `firmware/main/config.h`

No changes. `LED_BLINK_PERIOD_MS` (500) remains as the default when `blink_ms` is not specified.

Add a new constant for the blink tick resolution:

```cpp
#define BLINK_TICK_MS  50  // Fast tick for per-LED blink timing
```

### 3. `firmware/main/main.cpp`

#### 3a. LedState struct — add fields

```cpp
struct LedState {
    TimerHandle_t timer = nullptr;
    bool blink = false;
    uint16_t blink_ms = LED_BLINK_PERIOD_MS;
    uint16_t blink_counter = 0;
    bool blink_on = false;
    uint8_t r = 0, g = 0, b = 0;
};
```

#### 3b. `apply_led_command()` — accept blink_ms

Add `uint16_t blink_ms` parameter. Store it in `LedState::blink_ms`.

#### 3c. `parse_led_json()` — parse blink_ms

```cpp
cJSON* j_blink_ms = cJSON_GetObjectItem(item, "blink_ms");
uint16_t blink_ms = (j_blink_ms && j_blink_ms->valueint > 0)
                    ? (uint16_t)j_blink_ms->valueint
                    : LED_BLINK_PERIOD_MS;
apply_led_command(id, on, r, g, b, timeout_s, blink, blink_ms);
```

#### 3d. Main blink loop — per-LED timing

Replace the single-toggle loop with a fast-tick + counter approach:

```cpp
while (true) {
    for (int i = 0; i < 3; i++) {
        if (g_battery_low && i == 0) continue;
        LedState& s = g_led_states[i];
        if (s.blink && s.blink_ms > 0) {
            s.blink_counter += BLINK_TICK_MS;
            if (s.blink_counter >= s.blink_ms) {
                s.blink_counter = 0;
                s.blink_on = !s.blink_on;
                if (s.blink_on) {
                    g_led->set_led(i, s.r, s.g, s.b);
                } else {
                    g_led->set_led(i, 0, 0, 0);
                }
            }
        }
    }
    vTaskDelay(pdMS_TO_TICKS(BLINK_TICK_MS));
}
```

## Edge Cases

| Case | Behavior |
|------|----------|
| `blink: true`, no `blink_ms` | Falls back to 500ms default |
| `blink_ms: 0` or negative | Treated as "not set", uses 500ms |
| LED off while blinking | `apply_led_command(on=false)` clears blink state |
| Rapid event switching | Old timer deleted before new one created |
| Battery low | LED0 blocked entirely by `g_battery_low` check |
| Timeout fires during blink | Callback sets `blink=false`, blink loop stops |

## Testing

| Test | Input | Expected |
|------|-------|----------|
| Default blink | `{"leds":[{"id":1,"on":true,"r":255,"g":0,"b":0,"blink":true}]}` | LED1 blinks at 500ms |
| Custom blink_ms | `blink_ms: 200` | LED1 toggles every 200ms |
| 5-min timeout | WORKING event | LED off after 5 min |
| Non-blink with timeout | COMPLETED event | LED2 green, off after 5 min |
| Backward compat | Old JSON (no blink_ms, no timeout) | 500ms blink / firmware default timeout |
| Battery low + blink | Trigger low battery | LED0 red blink at 500ms, JSON LED0 commands blocked |

## Files Affected

| File | Change |
|------|--------|
| `desktop-relay/config.py` | Update EVENT_LED_MAP with timeout and blink_ms |
| `firmware/main/config.h` | Add BLINK_TICK_MS |
| `firmware/main/main.cpp` | LedState, parse_led_json, apply_led_command, blink loop |
