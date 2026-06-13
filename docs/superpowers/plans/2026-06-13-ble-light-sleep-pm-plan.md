# BLE Light Sleep Power Management — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace manual `esp_light_sleep_start()` with ESP-IDF PM framework + `esp_pm_lock`, and fix watchdog to start on BLE connect with PAIR_CONFIRM pause.

**Architecture:** ESP-IDF Power Management framework (`CONFIG_PM_ENABLE=y`) coordinates CPU light sleep with BLE controller automatically. Application uses `esp_pm_lock` to gate sleep: lock released when BLE connected (allow sleep between connection events), acquired when disconnected (stay awake for advertising). Watchdog starts on BLE connect, pauses on PAIR_CONFIRM, restarts on PAIR_SUCCESS.

**Tech Stack:** ESP-IDF 5.5.3, NimBLE, ESP32-C3 (single core, RISC-V, 160MHz), FreeRTOS

---

### Task 1: Enable CONFIG_PM_ENABLE in sdkconfig

**Files:**
- Modify: `firmware/sdkconfig`

- [ ] **Step 1: Enable PM framework**

Change line `# CONFIG_PM_ENABLE is not set` → `CONFIG_PM_ENABLE=y`.

```ini
# Before (line ~1391):
# CONFIG_PM_ENABLE is not set

# After:
CONFIG_PM_ENABLE=y
```

- [ ] **Step 2: Verify sdkconfig is consistent**

Run: `cd firmware && idf.py reconfigure`
Expected: No errors. This regenerates sdkconfig with any auto-dependencies (e.g., FreeRTOS tickless idle will auto-enable).

- [ ] **Step 3: Commit**

```bash
git add firmware/sdkconfig
git commit -m "feat: enable CONFIG_PM_ENABLE for BLE-coordinated light sleep"
```

---

### Task 2: Increase BLE connection interval to 100ms

**Files:**
- Modify: `firmware/main/config.h:38-39`

- [ ] **Step 1: Update connection interval defines**

```cpp
// Before:
#define BLE_CONN_INTERVAL_MIN  30   // ms
#define BLE_CONN_INTERVAL_MAX  50   // ms

// After:
#define BLE_CONN_INTERVAL_MIN  100   // ms
#define BLE_CONN_INTERVAL_MAX  100   // ms
```

Longer interval = more idle time between BLE connection events = CPU can sleep deeper/longer.

- [ ] **Step 2: Commit**

```bash
git add firmware/main/config.h
git commit -m "feat: increase BLE connection interval to 100ms for power saving"
```

---

### Task 3: Rewrite power_manager with esp_pm_lock

**Files:**
- Modify: `firmware/main/power_manager.h`
- Modify: `firmware/main/power_manager.cpp`

- [ ] **Step 1: Update header — add pm_lock member**

```cpp
// power_manager.h
#pragma once

#include "esp_pm.h"

class PowerManager {
public:
    void start();
    void enable();       // Release PM lock → allow light sleep (BLE connected)
    void disable();      // Acquire PM lock → prevent light sleep (no connection)
    void on_activity();  // No-op (BLE controller handles wakeup automatically)

private:
    esp_pm_lock_handle_t m_pm_lock = nullptr;
};
```

- [ ] **Step 2: Rewrite cpp — full replace**

Replace entire `power_manager.cpp`:

```cpp
#include "power_manager.h"
#include "config.h"
#include "esp_log.h"
#include "esp_pm.h"

static const char* TAG = "power";

void PowerManager::start()
{
    // Configure ESP-IDF PM framework for light sleep
    esp_pm_config_t pm_cfg = {
        .max_freq_mhz = 160,          // ESP32-C3 default max
        .min_freq_mhz = 160,          // Keep max when awake (BLE needs it)
        .light_sleep_enable = true,   // Allow automatic light sleep
    };
    esp_err_t ret = esp_pm_configure(&pm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_configure failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "PM framework configured (light_sleep_enable=true)");

    // Create lock — acquired = prevent sleep, released = allow sleep
    ret = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "ble_state", &m_pm_lock);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_lock_create failed: %s", esp_err_to_name(ret));
        return;
    }

    // Acquire immediately: no BLE connection yet, stay awake for advertising
    esp_pm_lock_acquire(m_pm_lock);
    ESP_LOGI(TAG, "PM started: light sleep blocked (no BLE connection)");
}

void PowerManager::enable()
{
    if (!m_pm_lock) return;
    esp_err_t ret = esp_pm_lock_release(m_pm_lock);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "PM lock released — light sleep enabled (BLE connected)");
    }
}

void PowerManager::disable()
{
    if (!m_pm_lock) return;
    esp_err_t ret = esp_pm_lock_acquire(m_pm_lock);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "PM lock acquired — light sleep blocked (BLE disconnected)");
    }
}

void PowerManager::on_activity()
{
    // No-op: BLE controller's internal PM lock handles connection events.
    // When data arrives, the controller holds its lock during the connection
    // event, preventing sleep. After processing, the lock releases and the
    // PM framework can re-enter light sleep. No manual coordination needed.
}
```

- [ ] **Step 3: Update includes**

Remove `esp_sleep.h` and `freertos/timers.h` from `power_manager.cpp` — no longer needed. The `esp_pm.h` include via the header suffices.

- [ ] **Step 4: Commit**

```bash
git add firmware/main/power_manager.h firmware/main/power_manager.cpp
git commit -m "feat: replace manual esp_light_sleep_start with ESP-IDF PM framework esp_pm_lock"
```

---

### Task 4: Start watchdog on BLE connect

**Files:**
- Modify: `firmware/main/ble_server.cpp:81-107`

- [ ] **Step 1: Add start_watchdog() in CONNECT handler**

In `gap_event_cb`, case `BLE_GAP_EVENT_CONNECT`, after setting connection params, add `self->start_watchdog()`:

```cpp
case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
        self->m_conn_handle = event->connect.conn_handle;
        ESP_LOGI(TAG, "Connected, handle=%d", self->m_conn_handle);
        if (self->m_conn_cb) self->m_conn_cb(true);

        // Set connection params ...
        // (existing ble_gap_update_params block unchanged)

        // Start watchdog immediately on connect.
        // PAIR_CONFIRM will stop it if user needs time to confirm pairing.
        self->start_watchdog();
    } else {
        ESP_LOGW(TAG, "Connection failed, restarting advertise");
        self->_advertise_with_retry();
    }
    break;
```

- [ ] **Step 2: Commit**

```bash
git add firmware/main/ble_server.cpp
git commit -m "feat: start watchdog immediately on BLE connect"
```

---

### Task 5: Pause watchdog on PAIR_CONFIRM, restart on PAIR_SUCCESS

**Files:**
- Modify: `firmware/main/main.cpp:157-184`

- [ ] **Step 1: Add stop_watchdog() in handle_pair_confirm()**

```cpp
static void handle_pair_confirm() {
    // Stop watchdog — user needs time to confirm pairing dialog
    g_ble->stop_watchdog();
    
    for (int i = 0; i < 3; i++) {
        if (g_led_states[i].timer) {
            xTimerStop(g_led_states[i].timer, 0);
            xTimerDelete(g_led_states[i].timer, 0);
            g_led_states[i].timer = nullptr;
        }
        g_led_states[i].blink = false;
    }
    g_led->all_on({0, 255, 0});
    ESP_LOGI(TAG, "Pair confirm — all LEDs green, watchdog paused");
}
```

- [ ] **Step 2: handle_pair_success() stays as-is**

`start_watchdog()` is already called here — it restarts the watchdog after PAIR_CONFIRM stopped it. No changes needed.

- [ ] **Step 3: Commit**

```bash
git add firmware/main/main.cpp
git commit -m "feat: pause watchdog on PAIR_CONFIRM, restart on PAIR_SUCCESS"
```

---

### Task 6: Build and verify firmware compiles

**Files:**
- No file changes — verification only

- [ ] **Step 1: Build firmware**

Run: `cd firmware && idf.py build`
Expected: Build succeeds with no errors.

- [ ] **Step 2: Check for PM-related log messages**

Scan build output for warnings about PM, sleep, or power. Expected: none.

---

### Task 7: Full integration test

- [ ] **Step 1: Flash firmware**

Run: `cd firmware && idf.py flash`

- [ ] **Step 2: Verify clean reconnect without disconnect loop**

1. Start desktop-relay: `python desktop-relay/main.py`
2. Confirm connection and pairing flow works
3. Wait > 5 seconds idle → check ESP32 logs for "PM lock released — light sleep enabled"
4. Kill Python with Ctrl+C
5. Wait for disconnect
6. Restart Python
7. **Verify:** No "unhandled services changed event" warning from bleak
8. **Verify:** No "Send failed (KEEPALIVE)" followed by immediate disconnect
9. **Verify:** Connection stays stable for > 30 seconds

- [ ] **Step 3: Commit any test fixes if needed**
