# Bluetooth Advertising Duty Cycle — Design

**Date:** 2026-06-13
**Status:** approved

## Problem

When the host computer shuts down (e.g., overnight), the ESP32 BLE disconnects and enters continuous advertising indefinitely. The PM lock is acquired on disconnect (`PowerManager::disable()`), preventing light sleep entirely. The CPU stays awake 24/7, draining the battery.

## Design

### Two-Phase Advertising After Disconnect

After BLE disconnects, advertising follows two phases:

```
CONNECTED (PM lock released, no advertising)
  │ BLE disconnect
  ▼
PHASE 1 — Continuous Advertising (0–30 min)
  │ PM lock ACQUIRED, advertising ON
  │ 30-min one-shot timer running
  ├── 30 min timer fires ──► PHASE 2
  └── reconnect ──► back to CONNECTED (cancel all timers)
  
PHASE 2 — Intermittent Advertising (30 min+)
  │ 60s cycle managed by timer
  ▼
  ┌─────────────────────────────────┐
  │ AWAKE (10s)                     │
  │ PM lock ACQUIRED, advertising ON│
  └────────┬────────────────────────┘
           │ 10s fires
           ▼
  ┌─────────────────────────────────┐
  │ SLEEP (50s)                     │
  │ PM lock RELEASED, advertising   │
  │ STOPPED, CPU → light sleep      │
  └────────┬────────────────────────┘
           │ 50s fires → back to AWAKE
           │
           └── reconnect at any point → CONNECTED
```

### Scope

Files changed:
- `firmware/main/ble_server.h` — new `AdvPhase` enum, timer handles, helper declarations, phase timing constants
- `firmware/main/ble_server.cpp` — state machine, timer callbacks, DISCONNECT/ADV_COMPLETE event logic

No changes to:
- `power_manager.h/.cpp` — interface unchanged
- `config.h` — timing constants live in `ble_server.h` as `static constexpr`
- `sdkconfig` — no config changes needed (light sleep + modem sleep already enabled)

Minor change to `main.cpp` — register a `power_ctrl` callback (same pattern as existing `message_callback` / `connect_callback`).

### New Callback (ble_server.h)

```cpp
// Callback type for power control (called by phase timer callbacks)
typedef void (*ble_power_ctrl_cb_t)(bool allow_sleep);

// Registration (same pattern as set_message_callback / set_connect_callback)
void set_power_ctrl_callback(ble_power_ctrl_cb_t cb);
ble_power_ctrl_cb_t m_power_cb = nullptr;
```

### New Members (ble_server.h)

```cpp
// Disconnect advertising phases
enum class AdvPhase { NONE, PHASE1_CONTINUOUS, PHASE2_AWAKE, PHASE2_SLEEP };

AdvPhase m_adv_phase = AdvPhase::NONE;
TimerHandle_t m_phase1_timer = nullptr;  // 30-min one-shot
TimerHandle_t m_phase2_timer = nullptr;  // 60s cycle (manages AWAKE↔SLEEP)

static constexpr uint32_t PHASE1_DURATION_MS  = 30 * 60 * 1000;  // 30 min
static constexpr uint32_t PHASE2_AWAKE_MS     = 10 * 1000;       // 10s
static constexpr uint32_t PHASE2_SLEEP_MS     = 50 * 1000;       // 50s
```

### Timer Callbacks

**phase1_cb** (one-shot, 30 min):
- Call `m_power_cb(false)` — acquire PM lock (ensure awake for advertising)
- Set `m_adv_phase = PHASE2_AWAKE`
- Start advertising via `_advertise_with_retry()`
- Start `m_phase2_timer` as one-shot with `PHASE2_AWAKE_MS` (10s)

**phase2_cb** (alternating one-shot):
- If AWAKE: stop advertising (`ble_gap_adv_stop()`), call `m_power_cb(true)` — release PM lock → allow light sleep, set phase to SLEEP, reload timer with `PHASE2_SLEEP_MS`
- If SLEEP: call `m_power_cb(false)` — acquire PM lock → stay awake, start advertising (`_advertise_with_retry()`), set phase to AWAKE, reload timer with `PHASE2_AWAKE_MS`

### Event Handling Changes (gap_event_cb)

**BLE_GAP_EVENT_DISCONNECT:**
- `cancel_phase_timers()` (safety — clean up any leftover phase state)
- Call existing `m_conn_cb(false)` → triggers `main.cpp::ble_connect_callback` → `g_power->disable()` (PM lock acquired)
- Restart advertising (existing `_advertise_with_retry()`)
- Start phase 1: `m_adv_phase = PHASE1_CONTINUOUS`, start `m_phase1_timer` (30 min one-shot)
- Log: `Adv phase 1: continuous advertising (30 min)`

**BLE_GAP_EVENT_CONNECT:**
- `cancel_phase_timers()` — clean up phase 1/2 timers
- Set `m_adv_phase = AdvPhase::NONE`
- Call existing `m_conn_cb(true)` → triggers `main.cpp::ble_connect_callback` → `g_power->enable()` (PM lock released)
- Log: `Adv phase timers cancelled (reconnected)` (only if was in a phase)
- Existing connection setup logic unchanged

**BLE_GAP_EVENT_ADV_COMPLETE:**
- If `PHASE2_SLEEP`: do NOT restart advertising — this is expected, we stopped it intentionally via `ble_gap_adv_stop()`
- If `PHASE1_CONTINUOUS` or `PHASE2_AWAKE`: restart advertising (existing `_advertise_with_retry()`)

### Helper Methods

```cpp
void cancel_phase_timers() {
    if (m_phase1_timer) { xTimerStop(m_phase1_timer, 0); xTimerDelete(m_phase1_timer, 0); m_phase1_timer = nullptr; }
    if (m_phase2_timer) { xTimerStop(m_phase2_timer, 0); xTimerDelete(m_phase2_timer, 0); m_phase2_timer = nullptr; }
    m_adv_phase = AdvPhase::NONE;
}
```

### Logging & Verification

Phase transitions logged at INFO level:

| Log Message | When |
|-------------|------|
| `Adv phase 1: continuous advertising (30 min)` | Entering PHASE1 |
| `Adv phase 2: intermittent — AWAKE (10s)` | Entering PHASE2 awake window |
| `Adv phase 2: intermittent — SLEEP (50s), adv stopped` | Entering PHASE2 sleep window |
| `Adv phase timers cancelled (reconnected)` | Reconnect during phase 1/2 |

Sleep verification uses existing PM callbacks (`CONFIG_PM_LIGHT_SLEEP_CALLBACKS`):
- `💤 Light sleep enter` — confirms CPU entered light sleep during PHASE2 SLEEP
- `⏰ Light sleep exit` — confirms CPU woke up

Expected log pattern during PHASE2:
```
Adv phase 2: intermittent — SLEEP (50s), adv stopped
PM lock released — light sleep enabled         ← from power_ctrl callback → g_power->enable()
💤 Light sleep enter (expected 50000000 us)     ← from PM callback
⏰ Light sleep exit (slept 50000000 us)          ← from PM callback
Adv phase 2: intermittent — AWAKE (10s)
PM lock acquired — light sleep blocked          ← from power_ctrl callback → g_power->disable()
Advertising started
```

### Edge Cases

| Scenario | Handling |
|----------|----------|
| **PHASE2 SLEEP → computer scans** | Device in light sleep, not advertising. Worst case: 50s delay before visible. Acceptable. |
| **PHASE2 AWAKE → connection** | `BLE_GAP_EVENT_CONNECT` cancels timers, normal connection. |
| **`ble_gap_adv_start` fails in PHASE2 AWAKE** | Log warning, continue. Next 60s cycle retries. |
| **PM lock acquire/release fails** | Log warning, don't block state machine. |
| **30-min timer fires during active connection** | Impossible — connection cancels all timers. |
| **Battery low** | Existing LED0 red-blink behavior unchanged, independent. |
| **Watchdog timeout → forced disconnect** | `ble_gap_terminate` → `DISCONNECT` event → start PHASE1 normally. |

### Non-Goals

- No deep sleep (light sleep provides sufficient power savings)
- No NVS persistence of phase state
- No configurable timing (hardcoded 30min/60s/10s)
- No Python desktop-relay changes
- No sdkconfig changes (modem sleep + light sleep already enabled)

## Verification

1. Flash firmware, connect, then disconnect → verify `Adv phase 1: continuous` log
2. Wait 30 minutes (or temporarily reduce `PHASE1_DURATION_MS` to 30s for testing) → verify transition to PHASE2
3. During PHASE2 SLEEP → verify `💤 Light sleep enter` and `⏰ Light sleep exit` logs appear
4. During PHASE2 SLEEP → verify `Advertising stopped` and no `Advertising started` until wake
5. Reconnect during PHASE1 → verify `Adv phase timers cancelled (reconnected)`
6. Reconnect during PHASE2 SLEEP (wait for awake window) → verify clean connection
7. Let device run overnight (real test) → check battery level next morning
