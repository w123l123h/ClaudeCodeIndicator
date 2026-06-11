# LED 驱动 BLE 通信协议 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 BLE 通信从事件驱动改为 LED 亮灯驱动，ESP32 固件解析 JSON 指令直接控制 LED，桌面端负责事件→指令翻译。

**Architecture:** ESP32 固件新增 cJSON 解析层，每 LED 独立 FreeRTOS Timer 管理超时，主循环驱动闪烁。桌面端 desktop-relay 新增 `EVENT_LED_MAP` 映射，将 TCP 事件转为 JSON 后通过 BLE 发送。hook-notifier 不改。

**Tech Stack:** ESP-IDF 5.5.3 (C++17, cJSON, NimBLE, led_strip), Python 3.13 (bleak, asyncio)

---

## File Map

| 文件 | 操作 | 职责 |
|------|------|------|
| `firmware/main/config.h` | 修改 | 删旧事件宏，保留保活/配对/BLE/LED 参数 |
| `firmware/main/main.cpp` | 大幅改写 | LED 状态机 + JSON 解析 + 独立定时器 + 闪烁循环 |
| `firmware/main/ble_server.cpp` | 小改 | 消息分发加 `{` 首字符检测，路由到 JSON 解析 |
| `firmware/main/CMakeLists.txt` | 修改 | 加 `cjson` 依赖 |
| `desktop-relay/config.py` | 修改 | 新增 `EVENT_LED_MAP` 字典 |
| `desktop-relay/main.py` | 小改 | `_on_tcp_message` 调用翻译函数后通过 BLE 发送 JSON |
| `desktop-relay/tcp_server.py` | 小改 | 回调签名改为传递原始 msg，由 main.py 翻译 |

---

### Task 1: firmware/config.h — 清理事件宏

**文件:** `firmware/main/config.h`

- [ ] **Step 1: 删除废弃的事件消息宏**

删除第 42-50 行的 `MSG_WORKING`、`MSG_WAITING_USER`、`MSG_COMPLETED`、`MSG_ERROR` 四个宏：

```cpp
// 删除以下 4 行：
#define MSG_WORKING         "WORKING"
#define MSG_WAITING_USER    "WAITING_USER"
#define MSG_COMPLETED       "COMPLETED"
#define MSG_ERROR           "ERROR"
```

保留 `MSG_KEEPALIVE`、`MSG_ALIVE`、`MSG_PAIR_CONFIRM`、`MSG_PAIR_SUCCESS`、`CC_LED_TIMEOUT_MS`、`LED_BLINK_PERIOD_MS` 及其他所有硬件/BLE/电池参数。

- [ ] **Step 2: 新增可选日志宏**

在上述删除位置加入：

```cpp
// LED 指令日志标记
#define MSG_LED_CMD         "LED_CMD"
```

- [ ] **Step 3: Commit**

```bash
git add firmware/main/config.h
git commit -m "feat: remove event macros, keep keepalive/pair macros in config.h
Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 2: firmware/main.cpp — 重写为主 LED 状态机

**文件:** `firmware/main/main.cpp`

**范围:** 完全替换 `handle_ble_message` 及相关的全局状态、定时器回调、主循环。

- [ ] **Step 1: 添加 cJSON 头文件**

在文件顶部现有 include 区域追加：

```cpp
#include "cJSON.h"
```

- [ ] **Step 2: 替换全局变量**

删除以下旧状态变量（原第 18-26 行区域）：
```cpp
// 删除：
static TimerHandle_t g_cc_timeout_timer = nullptr;
static bool g_battery_low = false;
static bool g_has_error = false;
static bool is_waiting_user = false;
static TickType_t g_waiting_user_start_tick = 0;
static bool is_waiting_ble_connect = true;
```

替换为：
```cpp
// LED 运行时状态（每个 LED 独立定时器 + 闪烁标志）
struct LedState {
    TimerHandle_t timer = nullptr;
    bool blink = false;
    uint8_t r = 0, g = 0, b = 0;  // 当前颜色（灭后恢复闪烁用）
};
static LedState g_led_states[3];
static bool g_battery_low = false;
static bool g_connected = false;
```

- [ ] **Step 3: 新增 `led_timeout_callback` 函数**

```cpp
// 单个 LED 超时回调：熄灭该 LED，清除 blink
static void led_timeout_callback(TimerHandle_t t) {
    for (int i = 0; i < 3; i++) {
        if (g_led_states[i].timer == t) {
            // 电池低电时 LED0 受保护
            if (i == 0 && g_battery_low) return;
            g_led->set_led(i, 0, 0, 0);
            g_led_states[i].blink = false;
            g_led_states[i].timer = nullptr;
            ESP_LOGI(TAG, "LED%d timeout — off", i);
            return;
        }
    }
}
```

- [ ] **Step 4: 新增 `apply_led_command` 函数**

```cpp
// 执行单条 LED 指令
static void apply_led_command(int id, bool on, uint8_t r, uint8_t g, uint8_t b,
                               uint32_t timeout_s, bool blink) {
    if (id < 0 || id > 2) return;

    // 电池低电时保护 LED0
    if (id == 0 && g_battery_low) {
        ESP_LOGW(TAG, "LED0 blocked by battery protection");
        return;
    }

    LedState& state = g_led_states[id];

    // 取消旧定时器
    if (state.timer) {
        xTimerStop(state.timer, 0);
        xTimerDelete(state.timer, 0);
        state.timer = nullptr;
    }

    if (!on) {
        // 关闭 LED
        g_led->set_led(id, 0, 0, 0);
        state.blink = false;
        state.r = state.g = state.b = 0;
        ESP_LOGI(TAG, "LED%d off", id);
    } else {
        // 保存颜色（用于闪烁恢复）
        state.r = r;
        state.g = g;
        state.b = b;
        state.blink = blink;
        g_led->set_led(id, r, g, b);

        // 计算超时 tick
        uint32_t timeout_ms = (timeout_s > 0) ? (timeout_s * 1000) : CC_LED_TIMEOUT_MS;
        state.timer = xTimerCreate(
            "led_to", pdMS_TO_TICKS(timeout_ms),
            pdFALSE,                       // 一次性定时器
            (void*)(uintptr_t)id,          // 传递 LED id
            led_timeout_callback
        );
        if (state.timer) {
            xTimerStart(state.timer, 0);
        }
        ESP_LOGI(TAG, "LED%d on rgb=(%d,%d,%d) timeout=%us blink=%d",
                 id, r, g, b, (unsigned)(timeout_ms / 1000), blink);
    }
}
```

- [ ] **Step 5: 新增 `parse_led_json` 函数**

```cpp
// 解析 LED JSON 指令，逐个调用 apply_led_command
static void parse_led_json(const char* json_str) {
    cJSON* root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error: %s", cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "unknown");
        g_ble->send_response("ERR:JSON_PARSE");
        return;
    }

    cJSON* leds = cJSON_GetObjectItem(root, "leds");
    if (!leds || !cJSON_IsArray(leds)) {
        ESP_LOGE(TAG, "JSON missing 'leds' array");
        cJSON_Delete(root);
        g_ble->send_response("ERR:JSON_PARSE");
        return;
    }

    int count = cJSON_GetArraySize(leds);
    for (int i = 0; i < count && i < 3; i++) {
        cJSON* item = cJSON_GetArrayItem(leds, i);
        if (!item) continue;

        cJSON* j_id = cJSON_GetObjectItem(item, "id");
        cJSON* j_on = cJSON_GetObjectItem(item, "on");
        if (!j_id || !j_on) continue;

        int id = j_id->valueint;
        bool on = cJSON_IsTrue(j_on);

        cJSON* j_r = cJSON_GetObjectItem(item, "r");
        cJSON* j_g = cJSON_GetObjectItem(item, "g");
        cJSON* j_b = cJSON_GetObjectItem(item, "b");
        uint8_t r = (uint8_t)(j_r ? j_r->valueint : 0);
        uint8_t g = (uint8_t)(j_g ? j_g->valueint : 0);
        uint8_t b = (uint8_t)(j_b ? j_b->valueint : 0);

        cJSON* j_to = cJSON_GetObjectItem(item, "timeout");
        uint32_t timeout_s = (j_to && j_to->valueint > 0) ? (uint32_t)j_to->valueint : 0;

        cJSON* j_blink = cJSON_GetObjectItem(item, "blink");
        bool blink = cJSON_IsTrue(j_blink);

        apply_led_command(id, on, r, g, b, timeout_s, blink);
    }

    cJSON_Delete(root);
    g_ble->send_response("OK");
}
```

- [ ] **Step 6: 新增 `handle_pair_confirm` 和 `handle_pair_success` 函数**

```cpp
static void handle_pair_confirm() {
    // 取消所有 LED 定时器
    for (int i = 0; i < 3; i++) {
        if (g_led_states[i].timer) {
            xTimerStop(g_led_states[i].timer, 0);
            xTimerDelete(g_led_states[i].timer, 0);
            g_led_states[i].timer = nullptr;
        }
        g_led_states[i].blink = false;
    }
    g_led->all_on({0, 255, 0});  // 三灯全绿
    ESP_LOGI(TAG, "Pair confirm — all LEDs green");
}

static void handle_pair_success() {
    // 全灭 + 清状态
    for (int i = 0; i < 3; i++) {
        if (g_led_states[i].timer) {
            xTimerStop(g_led_states[i].timer, 0);
            xTimerDelete(g_led_states[i].timer, 0);
            g_led_states[i].timer = nullptr;
        }
        g_led_states[i].blink = false;
        g_led_states[i].r = g_led_states[i].g = g_led_states[i].b = 0;
    }
    g_led->all_off();
    g_battery_low = false;
    ESP_LOGI(TAG, "Pair success — all off, state cleared");
}
```

- [ ] **Step 7: 替换 `handle_ble_message` 函数**

完整替换：

```cpp
// BLE 消息处理 (C 回调)
static void handle_ble_message(const char* msg) {
    // JSON 指令：以 '{' 开头
    if (msg[0] == '{') {
        if (g_power) g_power->on_activity();
        parse_led_json(msg);
        return;
    }

    // 保活
    if (strcmp(msg, MSG_KEEPALIVE) == 0) {
        g_ble->send_response(MSG_ALIVE);
        return;
    }

    // 配对
    if (strcmp(msg, MSG_PAIR_CONFIRM) == 0) {
        handle_pair_confirm();
        return;
    }
    if (strcmp(msg, MSG_PAIR_SUCCESS) == 0) {
        handle_pair_success();
        return;
    }

    // 未知消息静默忽略
    ESP_LOGW(TAG, "Unknown BLE message: %s", msg);
}
```

- [ ] **Step 8: 更新 `ble_connect_callback`**

```cpp
// BLE 连接状态回调
static void ble_connect_callback(bool connected) {
    g_connected = connected;
    if (connected) {
        g_led->set_led(2, 0, 0, 0);  // 停止绿色闪烁
        if (g_power) g_power->enable();
    } else {
        if (g_power) g_power->disable();
    }
}
```

- [ ] **Step 9: 更新 `battery_low_callback`**

```cpp
// 电池低电量回调
static void battery_low_callback(bool low) {
    g_battery_low = low;
    if (low) {
        // LED0 红色闪烁（优先级最高）
        if (g_led_states[0].timer) {
            xTimerStop(g_led_states[0].timer, 0);
            xTimerDelete(g_led_states[0].timer, 0);
            g_led_states[0].timer = nullptr;
        }
        g_led_states[0].r = 255;
        g_led_states[0].g = 0;
        g_led_states[0].b = 0;
        g_led_states[0].blink = true;
        ESP_LOGW(TAG, "Battery LOW (<= 3.5V) — LED0 red blink");
    } else {
        // 电池恢复，释放 LED0
        g_led_states[0].blink = false;
        if (g_led_states[0].timer == nullptr) {
            g_led->set_led(0, 0, 0, 0);
            g_led_states[0].r = g_led_states[0].g = g_led_states[0].b = 0;
        }
        ESP_LOGI(TAG, "Battery OK (> 3.5V) — LED0 released");
    }
}
```

- [ ] **Step 10: 替换主循环**

删除旧主循环（原闪烁 + BLE 等待逻辑），替换为：

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

- [ ] **Step 11: 删除 `cc_timeout_callback` 和 `start_cc_timeout` 函数**

这两个函数（原第 28-57 行）已不再需要，删除。

- [ ] **Step 12: Commit**

```bash
git add firmware/main/main.cpp
git commit -m "feat: rewrite main.cpp to LED-driven state machine with per-LED timers
Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 3: firmware/ble_server.cpp — 消息分发更新

**文件:** `firmware/main/ble_server.cpp`

- [ ] **Step 1: 修改 `gatt_write_cb` 的消息处理逻辑**

当前 `gatt_write_cb`（第 58-75 行）直接调用 `g_msg_cb(buf)`。不需要改这里——消息分发已经在 `main.cpp` 的 `handle_ble_message` 中处理（`{` 开头走 JSON，否则文本匹配）。

**无需修改 `ble_server.cpp`。** 消息路由完全在 `main.cpp` 的 `handle_ble_message` 回调中完成。

原因：`gatt_write_cb` 只负责读取数据并调用回调，消息内容的判断全部在 `main.cpp` 的回调函数中。

- [ ] **Step 1: Commit (skip — no changes needed)**

---

### Task 4: firmware/CMakeLists.txt — 添加 cJSON 依赖

**文件:** `firmware/main/CMakeLists.txt`

- [ ] **Step 1: 在 REQUIRES 中添加 cjson**

```cmake
idf_component_register(
    SRCS "main.cpp" "led_controller.cpp" "ble_server.cpp" "battery_monitor.cpp" "power_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES led_strip nvs_flash bt esp_adc json
)
```

- [ ] **Step 2: Commit**

```bash
git add firmware/main/CMakeLists.txt
git commit -m "feat: add cjson dependency for JSON LED command parsing
Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 5: desktop-relay/config.py — 添加事件映射表

**文件:** `desktop-relay/config.py`

- [ ] **Step 1: 在文件末尾追加 EVENT_LED_MAP**

```python
# 事件 → LED 指令映射（由 main.py 的 _translate_event 使用）
# 每个事件对应一个 leds 列表，每项含：id, on, r, g, b, blink (可选)
EVENT_LED_MAP = {
    "WORKING": {
        "leds": [
            {"id": 1, "on": True,  "r": 255, "g": 128, "b": 0},
            {"id": 2, "on": False},
        ]
    },
    "WAITING_USER": {
        "leds": [
            {"id": 1, "on": True,  "r": 255, "g": 128, "b": 0, "blink": True},
            {"id": 2, "on": False},
        ]
    },
    "COMPLETED": {
        "leds": [
            {"id": 1, "on": False},
            {"id": 2, "on": True,  "r": 0, "g": 255, "b": 0},
        ]
    },
    "ERROR": {
        "leds": [
            {"id": 0, "on": True,  "r": 255, "g": 0, "b": 0},
            {"id": 1, "on": False},
            {"id": 2, "on": False},
        ]
    },
}
```

- [ ] **Step 2: Commit**

```bash
git add desktop-relay/config.py
git commit -m "feat: add EVENT_LED_MAP for event-to-LED-command translation
Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 6: desktop-relay/main.py — 新增事件翻译

**文件:** `desktop-relay/main.py`

- [ ] **Step 1: 导入 EVENT_LED_MAP**

修改第 9-10 行的 import：

```python
from config import (
    KEEPALIVE_INTERVAL, KEEPALIVE_RESPONSE_TIMEOUT, KEEPALIVE_MAX_FAILURES,
    EVENT_LED_MAP,
)
```

- [ ] **Step 2: 在 `DesktopRelay` 类中添加 `_translate_event` 方法**

在 `_on_ble_message` 方法之后添加：

```python
    def _translate_event(self, event: str) -> str | None:
        """将事件名翻译为 JSON LED 指令字符串"""
        import json
        cmd = EVENT_LED_MAP.get(event)
        if cmd is None:
            logger.warning(f"Unknown event: {event}")
            return None
        return json.dumps(cmd, separators=(',', ':'))
```

- [ ] **Step 3: 添加 `_on_tcp_message` 方法并修改 TCP 回调**

新增方法处理 TCP 消息（替代直接发送到 BLE）：

```python
    async def _on_tcp_message(self, msg: str):
        """TCP 消息处理：翻译事件 → JSON，然后通过 BLE 发送"""
        json_cmd = self._translate_event(msg)
        if json_cmd:
            await self.ble.send(json_cmd)
        elif msg in ("PAIR_CONFIRM", "PAIR_SUCCESS"):
            # 配对事件直接透传
            await self.ble.send(msg)
```

- [ ] **Step 4: 更新 `__init__` 中 TCP 回调**

修改第 26 行：

```python
# 旧：
self.tcp = TcpRelayServer(self.ble.send)

# 新：
self.tcp = TcpRelayServer(self._on_tcp_message)
```

- [ ] **Step 5: Commit**

```bash
git add desktop-relay/main.py
git commit -m "feat: add event-to-JSON translation in desktop-relay main.py
Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 7: desktop-relay/tcp_server.py — 更新消息白名单

**文件:** `desktop-relay/tcp_server.py`

- [ ] **Step 1: 更新 VALID_MESSAGES**

第 8 行，增加配对事件+LED 指令标识（或者直接放宽为允许所有规定的消息）：

```python
VALID_MESSAGES = {"WORKING", "WAITING_USER", "COMPLETED", "ERROR"}
# 保持不变——事件名还是这 4 个，hook-notifier 没变
```

**`tcp_server.py` 无需改逻辑。** 原因：`_handle_client` 收到合法消息后调用 `self._ble_send(msg)`，而这个回调现在被 main.py 设为 `_on_tcp_message`，已经在 main.py 中完成翻译。

- [ ] **Step 1: Commit (skip — no changes needed)**

---

### Task 8: 编译固件验证

**使用 `/idf-build` 技能**

- [ ] **Step 1: 编译 firmware**

```
/idf-build
```
目标：编译零错误零警告。

- [ ] **Step 2: 确认编译输出**

检查输出中的关键信息：
- `cJSON` 链接成功
- `main.cpp` 编译通过
- 生成的 bin 文件路径

- [ ] **Step 3: Commit (如有小修)**

如有编译警告或错误修复，提交修正 commit。

---

### Task 9: 集成验证测试

手动或自动化验证关键路径：

- [ ] **Step 1: BLE 连接 + 保活**

启动 `desktop-relay`，确认 BLE 连接成功，`KEEPALIVE`/`ALIVE` 循环正常。

- [ ] **Step 2: 工作事件 → LED1 橙色**

运行 `hook-notifier/notify.py tool_start`，确认 ESP32 的 LED1（索引 1）亮橙色。

- [ ] **Step 3: 等待用户确认 → LED1 橙色闪烁**

运行 `hook-notifier/notify.py notification`，确认 LED1（索引 1）橙色闪烁（500ms 周期）。

- [ ] **Step 4: 工作完成 → LED2 绿色**

运行 `hook-notifier/notify.py stop`（exit_code=0），确认 LED2（索引 2）亮绿色，LED1 灭。

- [ ] **Step 5: 错误 → LED0 红色**

运行 `hook-notifier/notify.py stop 1`（exit_code≠0），确认 LED0 红色常亮，其余灭。

- [ ] **Step 6: LED 超时**

无需主动测试——验证超时定时器创建成功（日志可见 `LED0 timeout — off`），10 分钟后自动灭。

- [ ] **Step 7: 配对流程**

首次连接，确认 `PAIR_CONFIRM` 让 3 灯全绿，`PAIR_SUCCESS` 全灭。

- [ ] **Step 8: 电池低电**

（如有可调电源）电压 ≤3.5V 时 LED0 红色闪烁，恢复后 LED0 释放。
