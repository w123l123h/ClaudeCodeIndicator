# Bluetooth Advertising Duty Cycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** After BLE disconnect, switch from continuous advertising to intermittent advertising (10s on / 50s off) after 30 minutes, allowing CPU to enter light sleep during sleep windows.

**Architecture:** Add a two-phase state machine to `BleServer` with FreeRTOS timers. Phase 1 runs 30 minutes of continuous advertising. Phase 2 alternates between 10s awake (advertising, PM lock acquired) and 50s asleep (advertising stopped, PM lock released → light sleep). A new `ble_power_ctrl_cb_t` callback (same pattern as existing `ble_connect_cb_t`) lets timer callbacks control the PM lock.

**Tech Stack:** ESP-IDF, FreeRTOS timers, NimBLE stack, ESP32-C3 power management

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `firmware/main/ble_server.h` | Modify | New callback type, `AdvPhase` enum, timer handles, phase constants, method declarations |
| `firmware/main/ble_server.cpp` | Modify | State machine implementation: timers, callbacks, event handling |
| `firmware/main/main.cpp` | Modify | Register power_ctrl callback (1 function + 1 line in `app_main`) |

---

### Task 1: Update ble_server.h — New Types and Members

**Files:**
- Modify: `firmware/main/ble_server.h`

- [ ] **Step 1: Add `ble_power_ctrl_cb_t` typedef in the extern "C" block**

Add after the existing `ble_connect_cb_t` typedef (line 14):

```cpp
// 电源控制回调: void on_power_ctrl(bool allow_sleep)
// allow_sleep=true  → release PM lock → CPU can enter light sleep
// allow_sleep=false → acquire PM lock → stay awake for advertising
typedef void (*ble_power_ctrl_cb_t)(bool allow_sleep);
```

The full extern "C" block becomes:

```cpp
#ifdef __cplusplus
extern "C" {
#endif

// 消息回调: void on_message(const char* msg)
typedef void (*ble_message_cb_t)(const char* msg);
// 连接状态回调: void on_connect_change(bool connected)
typedef void (*ble_connect_cb_t)(bool connected);
// 电源控制回调: void on_power_ctrl(bool allow_sleep)
typedef void (*ble_power_ctrl_cb_t)(bool allow_sleep);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Add `AdvPhase` enum, static constants, and new member declarations**

Add in the `private:` section of class `BleServer` (before the existing `m_conn_handle` line):

```cpp
    // 断连后广播阶段
    enum class AdvPhase { NONE, PHASE1_CONTINUOUS, PHASE2_AWAKE, PHASE2_SLEEP };

    static constexpr uint32_t PHASE1_DURATION_MS = 30 * 60 * 1000;  // 30 min
    static constexpr uint32_t PHASE2_AWAKE_MS    = 10 * 1000;       // 10s
    static constexpr uint32_t PHASE2_SLEEP_MS    = 50 * 1000;       // 50s

    AdvPhase m_adv_phase = AdvPhase::NONE;
    TimerHandle_t m_phase1_timer = nullptr;  // 30-min one-shot
    TimerHandle_t m_phase2_timer = nullptr;  // alternating one-shot (AWAKE↔SLEEP)
    ble_power_ctrl_cb_t m_power_cb = nullptr;
```

- [ ] **Step 3: Add new public method declarations**

Add after `set_connect_callback` (line 27):

```cpp
    void set_power_ctrl_callback(ble_power_ctrl_cb_t cb);
```

- [ ] **Step 4: Add new private method and static callback declarations**

Add in the `private:` section after `m_device_name`:

```cpp
    void cancel_phase_timers();
    void start_phase1();

    static void phase1_cb(TimerHandle_t t);
    static void phase2_cb(TimerHandle_t t);
```

Wait — the static callbacks need to be public (same pattern as `watchdog_cb` which is declared public at line 41). Move `phase1_cb` and `phase2_cb` to the public static section.

Adjust: add to the public section near `watchdog_cb` (line 41):

```cpp
    static void phase1_cb(TimerHandle_t t);
    static void phase2_cb(TimerHandle_t t);
```

And add to private section:

```cpp
    void cancel_phase_timers();
    void start_phase1();
```

- [ ] **Step 5: Commit**

```bash
git add firmware/main/ble_server.h
git commit -m "feat: add AdvPhase enum, power_ctrl callback, timer handles to ble_server.h

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 2: Implement New Methods in ble_server.cpp

**Files:**
- Modify: `firmware/main/ble_server.cpp`

- [ ] **Step 1: Add `set_power_ctrl_callback()` implementation**

Add after `set_connect_callback` (after line 264):

```cpp
void BleServer::set_power_ctrl_callback(ble_power_ctrl_cb_t cb)
{
    m_power_cb = cb;
}
```

- [ ] **Step 2: Add `cancel_phase_timers()` implementation**

Add before `start_advertise` method (before line 213):

```cpp
void BleServer::cancel_phase_timers()
{
    if (m_phase1_timer) {
        xTimerStop(m_phase1_timer, 0);
        xTimerDelete(m_phase1_timer, 0);
        m_phase1_timer = nullptr;
    }
    if (m_phase2_timer) {
        xTimerStop(m_phase2_timer, 0);
        xTimerDelete(m_phase2_timer, 0);
        m_phase2_timer = nullptr;
    }
    m_adv_phase = AdvPhase::NONE;
}
```

- [ ] **Step 3: Add `start_phase1()` implementation**

Add after `cancel_phase_timers`:

```cpp
void BleServer::start_phase1()
{
    ESP_LOGI(TAG, "Adv phase 1: continuous advertising (30 min)");
    m_adv_phase = AdvPhase::PHASE1_CONTINUOUS;

    m_phase1_timer = xTimerCreate(
        "adv_ph1",
        pdMS_TO_TICKS(PHASE1_DURATION_MS),
        pdFALSE,  // one-shot
        this,     // pvTimerID → retrieved by phase1_cb
        phase1_cb
    );
    if (m_phase1_timer) {
        xTimerStart(m_phase1_timer, 0);
    } else {
        ESP_LOGE(TAG, "Failed to create phase1 timer");
    }
}
```

- [ ] **Step 4: Add `phase1_cb` implementation**

Add after `watchdog_cb` (after line 165):

```cpp
void BleServer::phase1_cb(TimerHandle_t t)
{
    BleServer* self = static_cast<BleServer*>(pvTimerGetTimerID(t));
    if (!self) return;

    // One-shot timer fired — delete it
    xTimerDelete(self->m_phase1_timer, 0);
    self->m_phase1_timer = nullptr;

    ESP_LOGI(TAG, "Adv phase 2: intermittent — AWAKE (10s)");

    // Ensure PM lock acquired (stay awake for advertising)
    if (self->m_power_cb) self->m_power_cb(false);

    self->m_adv_phase = AdvPhase::PHASE2_AWAKE;
    self->_advertise_with_retry();

    // Start 10s awake timer
    self->m_phase2_timer = xTimerCreate(
        "adv_ph2",
        pdMS_TO_TICKS(PHASE2_AWAKE_MS),
        pdFALSE,
        self,
        phase2_cb
    );
    if (self->m_phase2_timer) {
        xTimerStart(self->m_phase2_timer, 0);
    } else {
        ESP_LOGE(TAG, "Failed to create phase2 timer");
    }
}
```

- [ ] **Step 5: Add `phase2_cb` implementation**

Add after `phase1_cb`:

```cpp
void BleServer::phase2_cb(TimerHandle_t t)
{
    BleServer* self = static_cast<BleServer*>(pvTimerGetTimerID(t));
    if (!self) return;

    // One-shot timer fired — delete it (will be re-created for next sub-phase)
    xTimerDelete(self->m_phase2_timer, 0);
    self->m_phase2_timer = nullptr;

    if (self->m_adv_phase == AdvPhase::PHASE2_AWAKE) {
        // AWAKE → SLEEP
        ESP_LOGI(TAG, "Adv phase 2: intermittent — SLEEP (%lu ms), adv stopped",
                 (unsigned long)PHASE2_SLEEP_MS);
        self->m_adv_phase = AdvPhase::PHASE2_SLEEP;
        ble_gap_adv_stop();
        if (self->m_power_cb) self->m_power_cb(true);  // release PM lock → allow light sleep

        // Schedule next awake
        self->m_phase2_timer = xTimerCreate(
            "adv_ph2",
            pdMS_TO_TICKS(PHASE2_SLEEP_MS),
            pdFALSE,
            self,
            phase2_cb
        );
        if (self->m_phase2_timer) {
            xTimerStart(self->m_phase2_timer, 0);
        }
    } else if (self->m_adv_phase == AdvPhase::PHASE2_SLEEP) {
        // SLEEP → AWAKE
        ESP_LOGI(TAG, "Adv phase 2: intermittent — AWAKE (%lu ms)",
                 (unsigned long)PHASE2_AWAKE_MS);
        self->m_adv_phase = AdvPhase::PHASE2_AWAKE;
        if (self->m_power_cb) self->m_power_cb(false);  // acquire PM lock → stay awake
        self->_advertise_with_retry();

        // Schedule next sleep
        self->m_phase2_timer = xTimerCreate(
            "adv_ph2",
            pdMS_TO_TICKS(PHASE2_AWAKE_MS),
            pdFALSE,
            self,
            phase2_cb
        );
        if (self->m_phase2_timer) {
            xTimerStart(self->m_phase2_timer, 0);
        }
    }
}
```

- [ ] **Step 6: Commit**

```bash
git add firmware/main/ble_server.cpp
git commit -m "feat: add AdvPhase state machine — phase1/phase2 timers and callbacks

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 3: Update gap_event_cb — Wire State Machine Into BLE Events

**Files:**
- Modify: `firmware/main/ble_server.cpp:115-131`

- [ ] **Step 1: Update `BLE_GAP_EVENT_DISCONNECT` to start phase 1**

Replace the DISCONNECT case (lines 115-122):

**Old:**
```cpp
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected, restarting advertise");
        self->m_conn_handle = 0;
        if (self->m_conn_cb) self->m_conn_cb(false);
        // 停止数据看门狗
        self->stop_watchdog();
        self->_advertise_with_retry();
        break;
```

**New:**
```cpp
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected, restarting advertise");
        self->m_conn_handle = 0;
        self->cancel_phase_timers();  // safety: clean any leftover phase state
        if (self->m_conn_cb) self->m_conn_cb(false);
        // 停止数据看门狗
        self->stop_watchdog();
        self->_advertise_with_retry();
        self->start_phase1();  // start 30-min phase1 timer
        break;
```

- [ ] **Step 2: Update `BLE_GAP_EVENT_CONNECT` — cancel timers + log on success**

Replace lines 81-113 (the entire CONNECT case):

**Old:**
```cpp
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            self->m_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected, handle=%d", self->m_conn_handle);
            if (self->m_conn_cb) self->m_conn_cb(true);

            // 设置连接参数（缩短 supervision timeout 到 4s）
            {
                struct ble_gap_upd_params params = {};
                // ms → 1.25ms units: *4/5
                params.itvl_min = (BLE_CONN_INTERVAL_MIN * 4) / 5;
                params.itvl_max = (BLE_CONN_INTERVAL_MAX * 4) / 5;
                params.latency = BLE_SLAVE_LATENCY;
                params.supervision_timeout = BLE_SUPERVISION_TIMEOUT;  // *10ms
                params.min_ce_len = 0;
                params.max_ce_len = 0;
                int rc = ble_gap_update_params(self->m_conn_handle, &params);
                if (rc != 0) {
                    ESP_LOGW(TAG, "ble_gap_update_params failed: %d", rc);
                } else {
                    ESP_LOGI(TAG, "Connection params set: itvl=%d-%dms, sup_to=%dms",
                             BLE_CONN_INTERVAL_MIN, BLE_CONN_INTERVAL_MAX,
                             BLE_SUPERVISION_TIMEOUT * 10);
                }
            }

            // Start watchdog immediately on connect.
            // PAIR_CONFIRM will stop it if user needs time to confirm pairing.
            self->start_watchdog();
        } else {
            ESP_LOGW(TAG, "Connection failed, restarting advertise");
            self->_advertise_with_retry();
        }
        break;
```

**New:**
```cpp
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            bool was_in_phase = (self->m_adv_phase != AdvPhase::NONE);
            self->cancel_phase_timers();  // cancel phase1/phase2 timers
            if (was_in_phase) {
                ESP_LOGI(TAG, "Adv phase timers cancelled (reconnected)");
            }
            self->m_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected, handle=%d", self->m_conn_handle);
            if (self->m_conn_cb) self->m_conn_cb(true);

            // 设置连接参数（缩短 supervision timeout 到 4s）
            {
                struct ble_gap_upd_params params = {};
                // ms → 1.25ms units: *4/5
                params.itvl_min = (BLE_CONN_INTERVAL_MIN * 4) / 5;
                params.itvl_max = (BLE_CONN_INTERVAL_MAX * 4) / 5;
                params.latency = BLE_SLAVE_LATENCY;
                params.supervision_timeout = BLE_SUPERVISION_TIMEOUT;  // *10ms
                params.min_ce_len = 0;
                params.max_ce_len = 0;
                int rc = ble_gap_update_params(self->m_conn_handle, &params);
                if (rc != 0) {
                    ESP_LOGW(TAG, "ble_gap_update_params failed: %d", rc);
                } else {
                    ESP_LOGI(TAG, "Connection params set: itvl=%d-%dms, sup_to=%dms",
                             BLE_CONN_INTERVAL_MIN, BLE_CONN_INTERVAL_MAX,
                             BLE_SUPERVISION_TIMEOUT * 10);
                }
            }

            // Start watchdog immediately on connect.
            // PAIR_CONFIRM will stop it if user needs time to confirm pairing.
            self->start_watchdog();
        } else {
            ESP_LOGW(TAG, "Connection failed, restarting advertise");
            if (self->m_adv_phase != AdvPhase::PHASE2_SLEEP) {
                self->_advertise_with_retry();
            }
        }
        break;
```

- [ ] **Step 3: Update `BLE_GAP_EVENT_ADV_COMPLETE` to skip restart during SLEEP phase**

Replace lines 123-126:

**Old:**
```cpp
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGW(TAG, "Advertising stopped, restarting");
        self->_advertise_with_retry();
        break;
```

**New:**
```cpp
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (self->m_adv_phase == AdvPhase::PHASE2_SLEEP) {
            // Expected — we called ble_gap_adv_stop() intentionally
            ESP_LOGI(TAG, "Adv complete during SLEEP phase — not restarting");
        } else {
            ESP_LOGW(TAG, "Advertising stopped unexpectedly, restarting");
            self->_advertise_with_retry();
        }
        break;
```

- [ ] **Step 4: Commit**

```bash
git add firmware/main/ble_server.cpp
git commit -m "feat: wire AdvPhase state machine into BLE gap events

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 4: Register Power Control Callback in main.cpp

**Files:**
- Modify: `firmware/main/main.cpp`

- [ ] **Step 1: Add `ble_power_ctrl_callback` function**

Add after `ble_connect_callback` (after line 240):

```cpp
// 电源控制回调（由 BLE server 的 phase 定时器触发）
static void ble_power_ctrl_callback(bool allow_sleep)
{
    if (!g_power) return;
    if (allow_sleep) {
        g_power->enable();   // release PM lock → allow light sleep
    } else {
        g_power->disable();  // acquire PM lock → stay awake
    }
}
```

- [ ] **Step 2: Register callback in `app_main`**

Add after `g_ble->set_connect_callback(ble_connect_callback)` (currently line 302):

```cpp
    g_ble->set_power_ctrl_callback(ble_power_ctrl_callback);
```

The section becomes:

```cpp
    g_ble->init();
    g_ble->set_message_callback(handle_ble_message);
    g_ble->set_connect_callback(ble_connect_callback);
    g_ble->set_power_ctrl_callback(ble_power_ctrl_callback);
    g_ble->start_advertise();
```

- [ ] **Step 3: Commit**

```bash
git add firmware/main/main.cpp
git commit -m "feat: register power_ctrl callback for AdvPhase state machine

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 5: Build and Verify Compilation

**Files:** None (build only)

- [ ] **Step 1: Build the firmware**

```bash
cd firmware && idf.py build
```

Expected: Build succeeds with no errors or warnings.

- [ ] **Step 2: Verify new symbols in output**

```bash
cd firmware/build && xtensa-esp32s3-elf-nm ClaudeCodeIndicator.elf 2>/dev/null || riscv32-esp-elf-nm ClaudeCodeIndicator.elf | grep -E "phase1_cb|phase2_cb|cancel_phase|start_phase1|power_ctrl"
```

Expected: Symbols `phase1_cb`, `phase2_cb`, `cancel_phase_timers`, `start_phase1`, `set_power_ctrl_callback` all present.

- [ ] **Step 3: Commit (if any sdkconfig changes were generated)**

```bash
git status
# If sdkconfig changed unexpectedly, review and decide
```

---

### Task 6: Flash and Verify Runtime Behavior

**Files:** None (manual test)

**Quick test:** Temporarily shorten `PHASE1_DURATION_MS` to `30 * 1000` (30 seconds) for testing, then flash.

- [ ] **Step 1: Temporarily shorten phase1 for testing**

In `ble_server.h`, change:
```cpp
static constexpr uint32_t PHASE1_DURATION_MS = 30 * 1000;  // 30s for testing
```

Build and flash:
```bash
cd firmware && idf.py build flash monitor
```

- [ ] **Step 2: Verify PHASE1 log on disconnect**

Connect from Python desktop-relay, then disconnect (stop the Python app or turn off Bluetooth on PC).

Expected log output:
```
Disconnected, restarting advertise
PM lock acquired — light sleep blocked
Advertising started
Adv phase 1: continuous advertising (30 min)
```

- [ ] **Step 3: Verify PHASE2 transition**

Wait 30 seconds. Expected:
```
Adv phase 2: intermittent — AWAKE (10s)
PM lock acquired — light sleep blocked
Advertising started
```

After 10 seconds:
```
Adv phase 2: intermittent — SLEEP (50000 ms), adv stopped
PM lock released — light sleep enabled
💤 Light sleep enter (expected 50000000 us)
```

After 50 seconds:
```
⏰ Light sleep exit (slept 50000000 us)
Adv phase 2: intermittent — AWAKE (10s)
PM lock acquired — light sleep blocked
Advertising started
```

- [ ] **Step 4: Verify reconnect cancels timers**

While in PHASE1 or PHASE2 AWAKE, connect from Python. Expected:
```
Connected, handle=N
Adv phase timers cancelled (reconnected)
```

If connecting during PHASE2 SLEEP, the device won't be advertising — wait for the next AWAKE window and connect then.

- [ ] **Step 5: Restore PHASE1_DURATION_MS to 30 min**

```cpp
static constexpr uint32_t PHASE1_DURATION_MS = 30 * 60 * 1000;  // 30 min
```

Build and flash:
```bash
cd firmware && idf.py build flash
```

- [ ] **Step 6: Commit if any changes were made during testing**

```bash
git status
```

---

### Task 7: Final Commit and Cleanup

- [ ] **Step 1: Verify final state**

```bash
git status
git diff
```

- [ ] **Step 2: Commit test configuration restoration (if any)**

```bash
git add firmware/main/ble_server.h
git commit -m "chore: restore PHASE1_DURATION_MS to 30min after testing"
```
