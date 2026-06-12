# Blink Delay & Timeout — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add per-LED `blink_ms` parameter (ms) to hardware control JSON for configurable blink rate, and add `"timeout": 300` to all LED events from main.py so LEDs auto-off after 5 minutes.

**Architecture:** The `LedState` struct gains `blink_ms`, `blink_counter`, and `blink_on` fields. The main blink loop switches from a single 500ms toggle to a fast 50ms tick with per-LED counters, so each LED can blink at its own rate. The JSON already supported `timeout`; we just populate it from main.py now. `blink_ms` defaults to `LED_BLINK_PERIOD_MS` (500) when absent.

**Tech Stack:** Python (desktop-relay), C++17 (ESP-IDF firmware), cJSON, FreeRTOS timers

---

### Task 1: Update EVENT_LED_MAP in desktop-relay/config.py

**Files:**
- Modify: `desktop-relay/config.py:25-51`

- [ ] **Step 1: Add timeout and blink_ms to EVENT_LED_MAP**

Replace the `EVENT_LED_MAP` block (lines 25-51) with:

```python
# 事件 → LED 指令映射（由 main.py 的 _translate_event 使用）
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

- [ ] **Step 2: Verify the Python dict is valid**

Run:
```bash
cd desktop-relay && python -c "from config import EVENT_LED_MAP; import json; print(json.dumps(EVENT_LED_MAP['WAITING_USER'], indent=2))"
```
Expected: prints the WAITING_USER dict with `blink_ms: 300` and `timeout: 300`.

- [ ] **Step 3: Commit**

```bash
git add desktop-relay/config.py
git commit -m "feat: add timeout=300 and blink_ms=300 to LED event map

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 2: Add BLINK_TICK_MS to firmware/main/config.h

**Files:**
- Modify: `firmware/main/config.h:19`

- [ ] **Step 1: Add BLINK_TICK_MS constant**

In `firmware/main/config.h`, after line 19 (`#define LED_BLINK_PERIOD_MS      500`), insert:

```cpp
#define BLINK_TICK_MS            50   // Fast tick resolution for per-LED blink timing
```

The resulting block reads:

```cpp
// 时序参数 (ms)
#define SELF_TEST_DELAY_MS       500
#define LED_BLINK_PERIOD_MS      500
#define BLINK_TICK_MS            50   // Fast tick resolution for per-LED blink timing
#define CC_LED_TIMEOUT_MS        600000  // 10 分钟
```

- [ ] **Step 2: Commit**

```bash
git add firmware/main/config.h
git commit -m "feat: add BLINK_TICK_MS constant for per-LED blink timing

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 3: Update firmware/main/main.cpp — LedState, apply_led_command, parse_led_json, blink loop

**Files:**
- Modify: `firmware/main/main.cpp:22-26` (LedState struct)
- Modify: `firmware/main/main.cpp:47-88` (apply_led_command)
- Modify: `firmware/main/main.cpp:92-138` (parse_led_json)
- Modify: `firmware/main/main.cpp:288-305` (main blink loop)
- Modify: `firmware/main/main.cpp:204-213` (ble_connect_callback — fix aggregate init)
- Modify: `firmware/main/main.cpp:33-43` (led_timeout_callback — clear new fields)

- [ ] **Step 1: Update LedState struct (lines 22-26)**

Replace:
```cpp
struct LedState {
    TimerHandle_t timer = nullptr;
    bool blink = false;
    uint8_t r = 0, g = 0, b = 0;
};
```

With:
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

- [ ] **Step 2: Update led_timeout_callback — clear new fields (line 37-38)**

Replace the lines inside `led_timeout_callback` where LED state is cleared. Currently:
```cpp
            g_led->set_led(i, 0, 0, 0);
            g_led_states[i].blink = false;
            g_led_states[i].timer = nullptr;
```

With:
```cpp
            g_led->set_led(i, 0, 0, 0);
            g_led_states[i].blink = false;
            g_led_states[i].blink_counter = 0;
            g_led_states[i].blink_on = false;
            g_led_states[i].timer = nullptr;
```

- [ ] **Step 3: Update apply_led_command signature and body (lines 47-88)**

Replace the entire `apply_led_command` function with:

```cpp
// 执行单条 LED 指令
static void apply_led_command(int id, bool on, uint8_t r, uint8_t g, uint8_t b,
                               uint32_t timeout_s, bool blink, uint16_t blink_ms) {
    if (id < 0 || id > 2) return;

    if (id == 0 && g_battery_low) {
        ESP_LOGW(TAG, "LED0 blocked by battery protection");
        return;
    }

    LedState& state = g_led_states[id];

    if (state.timer) {
        xTimerStop(state.timer, 0);
        xTimerDelete(state.timer, 0);
        state.timer = nullptr;
    }

    if (!on) {
        g_led->set_led(id, 0, 0, 0);
        state.blink = false;
        state.blink_counter = 0;
        state.blink_on = false;
        state.r = state.g = state.b = 0;
        ESP_LOGI(TAG, "LED%d off", id);
    } else {
        state.r = r;
        state.g = g;
        state.b = b;
        state.blink = blink;
        state.blink_ms = (blink_ms > 0) ? blink_ms : LED_BLINK_PERIOD_MS;
        state.blink_counter = 0;
        state.blink_on = false;
        g_led->set_led(id, r, g, b);

        uint32_t timeout_ms = (timeout_s > 0) ? (timeout_s * 1000) : CC_LED_TIMEOUT_MS;
        state.timer = xTimerCreate(
            "led_to", pdMS_TO_TICKS(timeout_ms),
            pdFALSE,
            (void*)(uintptr_t)id,
            led_timeout_callback
        );
        if (state.timer) {
            xTimerStart(state.timer, 0);
        }
        ESP_LOGI(TAG, "LED%d on rgb=(%d,%d,%d) timeout=%us blink=%d blink_ms=%u",
                 id, r, g, b, (unsigned)(timeout_ms / 1000), blink, (unsigned)blink_ms);
    }
}
```

- [ ] **Step 4: Update parse_led_json — parse and pass blink_ms (lines 128-134)**

Replace the blink-parsing block at lines 131-134:
```cpp
        cJSON* j_blink = cJSON_GetObjectItem(item, "blink");
        bool blink = cJSON_IsTrue(j_blink);

        apply_led_command(id, on, r, g, b, timeout_s, blink);
```

With:
```cpp
        cJSON* j_blink = cJSON_GetObjectItem(item, "blink");
        bool blink = cJSON_IsTrue(j_blink);

        cJSON* j_blink_ms = cJSON_GetObjectItem(item, "blink_ms");
        uint16_t blink_ms = (j_blink_ms && j_blink_ms->valueint > 0)
                            ? (uint16_t)j_blink_ms->valueint
                            : LED_BLINK_PERIOD_MS;

        apply_led_command(id, on, r, g, b, timeout_s, blink, blink_ms);
```

- [ ] **Step 5: Fix ble_connect_callback — aggregate init with new fields (lines 205 and 210)**

The aggregate initialization `g_led_states[2] = {nullptr, true, 0, 255, 0};` no longer matches the struct layout after adding new fields. Replace both occurrences:

Line 205 (connected, turn LED2 off):
```cpp
        g_led_states[2].blink = false;
```
Replace with:
```cpp
        g_led_states[2].blink = false;
        g_led_states[2].blink_counter = 0;
        g_led_states[2].blink_on = false;
```

Line 209-210 (disconnected, resume LED2 green blink):
```cpp
        // 断连后广播会重启，恢复等待指示
        g_led_states[2] = {nullptr, true, 0, 255, 0};
```
Replace with:
```cpp
        // 断连后广播会重启，恢复等待指示
        g_led_states[2].timer = nullptr;
        g_led_states[2].blink = true;
        g_led_states[2].blink_ms = LED_BLINK_PERIOD_MS;
        g_led_states[2].blink_counter = 0;
        g_led_states[2].blink_on = false;
        g_led_states[2].r = 0;
        g_led_states[2].g = 255;
        g_led_states[2].b = 0;
```

- [ ] **Step 6: Replace main blink loop (lines 288-305)**

Replace the entire blink loop block:
```cpp
    // 主循环：处理 LED 闪烁
    bool blink_toggle = false;

    while (true) {
        blink_toggle = !blink_toggle;

        for (int i = 0; i < 3; i++) {
            if (g_led_states[i].blink) {
                if (blink_toggle) {
                    g_led->set_led(i, g_led_states[i].r, g_led_states[i].g, g_led_states[i].b);
                } else {
                    g_led->set_led(i, 0, 0, 0);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(LED_BLINK_PERIOD_MS));
    }
```

With:
```cpp
    // 主循环：处理 LED 闪烁（per-LED timing via fast tick + counter）
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

- [ ] **Step 7: Commit**

```bash
git add firmware/main/main.cpp
git commit -m "feat: add per-LED blink_ms timing and blink_counter loop

- LedState gains blink_ms, blink_counter, blink_on fields
- apply_led_command accepts blink_ms parameter with fallback to default
- parse_led_json reads optional blink_ms from JSON
- Main loop uses 50ms fast tick with per-LED counter for independent blink rates
- Fix ble_connect_callback to initialize new LedState fields explicitly

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Post-Implementation Verification

- [ ] **Verify 1: Confirm config.py produces correct JSON**

```bash
cd desktop-relay && python -c "
from config import EVENT_LED_MAP
import json
for evt, cmd in EVENT_LED_MAP.items():
    j = json.dumps(cmd, separators=(',', ':'))
    print(f'{evt}: {j}')
    # Verify timeout is present on active LEDs
    if evt == 'WAITING_USER':
        assert 'blink_ms' in j, 'WAITING_USER missing blink_ms'
        assert 'timeout' in j, 'WAITING_USER missing timeout'
    if evt == 'WORKING':
        assert 'timeout' in j, 'WORKING missing timeout'
"
```
Expected: prints all 4 events with no assertion errors.

- [ ] **Verify 2: Build firmware**

Use the `idf-build` skill, or run:
```bash
# (ESP-IDF build — see idf-build skill for exact command)
```
Expected: compilation succeeds with no errors.

- [ ] **Verify 3: Manual test matrix**

| Test | Send JSON | Expected behavior |
|------|-----------|-------------------|
| Default blink (no blink_ms) | `{"leds":[{"id":1,"on":true,"r":255,"g":0,"b":0,"blink":true}]}` | LED1 blinks at 500ms (default) |
| Custom blink_ms=200 | `{"leds":[{"id":1,"on":true,"r":255,"g":0,"b":0,"blink":true,"blink_ms":200}]}` | LED1 toggles every 200ms |
| 5-min timeout | send WORKING event via HTTP | LED goes off after 5 minutes |
| WAITING_USER blink | send WAITING_USER event | LED1 blinks at 300ms, times out after 5min |
| Backward compat | old JSON without blink_ms or timeout | works with 500ms blink / 10min timeout |
