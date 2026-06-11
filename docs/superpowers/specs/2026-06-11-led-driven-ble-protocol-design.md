# LED 驱动 BLE 通信协议 — 设计方案

**日期：** 2026-06-11
**范围：** 将 BLE 通信从事件驱动改为 LED 亮灯驱动
**涉及：** firmware（ESP32）, desktop-relay（Python）, hook-notifier（Python 不变）

---

## 1. 目标

将 ESP32 与电脑端之间的 BLE 通信从"事件驱动"（发 `WORKING`/`WAITING_USER` 等，固件硬编码 LED 行为）改为"LED 驱动"（电脑端直接发 LED 控制指令，固件只管执行）。

## 2. 核心需求

- 3 个 WS2812B LED，每个可独立控制
- 控制参数：开关、RGB 颜色、自动关闭延时（秒）、闪烁开关
- 不提供延时参数时，默认 10 分钟（600 秒）硬件超时
- 一次请求可控制多个 LED
- 每个 LED 有独立超时定时器
- 电脑端 desktop-relay 负责将 Hook 事件翻译为 LED 指令
- 配对流程保留为特殊事件

---

## 3. BLE 协议设计

### 3.1 消息分类

| 类型 | 消息内容 | 方向 | 格式 |
|------|----------|------|------|
| 保活请求 | `KEEPALIVE` | 电脑→ESP32 | 纯文本 |
| 保活响应 | `ALIVE` | ESP32→电脑 | 纯文本 |
| 配对确认 | `PAIR_CONFIRM` | 电脑→ESP32 | 纯文本 |
| 配对成功 | `PAIR_SUCCESS` | 电脑→ESP32 | 纯文本 |
| LED 指令 | JSON 对象 | 电脑→ESP32 | JSON |
| LED 响应 | `OK` 或 `ERR:...` | ESP32→电脑 | 纯文本 |

### 3.2 JSON LED 指令格式

```json
{
  "leds": [
    {"id": 0, "on": true,  "r": 255, "g": 128, "b": 0, "timeout": 30, "blink": true},
    {"id": 1, "on": false}
  ]
}
```

**字段定义：**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `id` | int (0-2) | 是 | LED 索引，0-based |
| `on` | bool | 是 | `true` 开启，`false` 关闭 |
| `r` | int (0-255) | 仅 `on:true` | 红色分量 |
| `g` | int (0-255) | 仅 `on:true` | 绿色分量 |
| `b` | int (0-255) | 仅 `on:true` | 蓝色分量 |
| `timeout` | int (>0) | 否 | 自动关闭延时（秒），默认 600 |
| `blink` | bool | 否 | 是否闪烁，默认 `false`，周期 500ms |

**约束：**
- `leds` 数组长度上限 3，超出部分忽略
- `on: false` 时忽略 `r`/`g`/`b`/`timeout`/`blink`
- `r`/`g`/`b` 缺省时视为 `0`

### 3.3 BLE 层不变的部分

- 服务 UUID：`0000ff00-0000-1000-8000-00805f9b34fb`
- 特征 UUID：`0000ff01-0000-1000-8000-00805f9b34fb`
- 单特征 Write + Notify，双向通信
- 连接参数、设备命名规则不变

---

## 4. ESP32 固件设计

### 4.1 模块改动清单

| 文件 | 改动程度 | 说明 |
|------|----------|------|
| `config.h` | 中 | 删除旧事件宏，保留保活/配对/BLE/LED 参数 |
| `led_controller.h/cpp` | **不变** | 现有接口完全满足需求 |
| `ble_server.cpp` | 小 | 消息分发增加 JSON 首字符检测 |
| `main.cpp` | 大 | 用新 LED 状态机替换旧事件状态机 |
| `battery_monitor.cpp` | 不变 | 电池逻辑不动 |

### 4.2 config.h 变更

**删除：**
- `MSG_WORKING`、`MSG_WAITING_USER`、`MSG_COMPLETED`、`MSG_ERROR`

**保留：**
- `MSG_KEEPALIVE`、`MSG_ALIVE`、`MSG_PAIR_CONFIRM`、`MSG_PAIR_SUCCESS`
- `CC_LED_TIMEOUT_MS`（600000ms）
- `LED_BLINK_PERIOD_MS`（500ms）
- 所有硬件引脚、BLE UUID、电池参数

**新增：**
- `MSG_LED_CMD`（日志标记用，可选）

### 4.3 main.cpp 架构

**LED 运行时状态（全局）：**
```cpp
struct LedState {
    TimerHandle_t timer;   // 独立超时定时器
    bool blink;            // 闪烁开关
    uint8_t r, g, b;       // 当前颜色（灭后恢复用）
};
static LedState g_led_states[3];
```

**新增函数：**

1. **`parse_led_json(const char* json)`**
   - 用 cJSON 解析顶层 `leds` 数组
   - 遍历每项，校验字段，调用 `apply_led_command()`
   - 解析失败回应 `ERR:JSON_PARSE`

2. **`apply_led_command(id, on, r, g, b, timeout_s, blink)`**
   - `on: false` → `led->set_led(id, 0,0,0)` + 取消该 LED 定时器 + 清除 blink
   - `on: true` → `led->set_led(id, r,g,b)` + 创建/重置该 LED 定时器 + 设置 blink 标志
   - `timeout_s` 为 0 → 使用 `CC_LED_TIMEOUT_MS / 1000`

3. **`led_timeout_callback(TimerHandle t)`**
   - 根据 timer handle 找到对应 LED
   - 熄灭该 LED，清除 blink 标志
   - 若该 LED 是 LED0 且电池低电，不熄（电池红灯闪烁优先）

**保活处理：** 收到 `KEEPALIVE` → 发送 `ALIVE`，不碰 LED。

**配对处理：**
- `PAIR_CONFIRM` → 3 灯全绿常亮、取消所有 LED 定时器
- `PAIR_SUCCESS` → 3 灯全灭 + 清除所有状态

**主循环闪烁逻辑：**
```cpp
while (true) {
    static bool blink_toggle = false;
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

### 4.4 ble_server.cpp 消息分发

`gatt_write_cb` 收到消息后：
```
if (msg 以 '{' 开头) → parse_led_json(msg)
else if (msg == "KEEPALIVE") → send_response("ALIVE")
else if (msg == "PAIR_CONFIRM") → 配对确认逻辑
else if (msg == "PAIR_SUCCESS") → 配对成功逻辑
else → 忽略
```

### 4.5 电池优先级

电池低电（≤3.5V）时 LED0 红色闪烁——固件在电池回调中设置 `g_led_states[0] = {r=255, g=0, b=0, blink=true}`，由主循环驱动闪烁。此状态下任何 LED 指令不覆盖 LED0——`apply_led_command` 中检测 `g_battery_low && id == 0` 时跳过设置。电池恢复（>3.5V）后 LED0 恢复正常控制，清除 blink 标志。

---

## 5. 桌面端设计

### 5.1 hook-notifier（`notify.py`）— 不变

继续监听 Claude Code hooks，向 TCP 54321 发送 `WORKING` / `WAITING_USER` / `COMPLETED` / `ERROR`。

### 5.2 desktop-relay 新增事件映射

`main.py` 中 `DesktopRelay` 新增方法 `_translate_event(event: str) -> str`：

**事件 → LED 指令映射：**

| 事件 | LED 指令 |
|------|----------|
| `WORKING` | LED1(索引1): 橙色 `{255,128,0}` 常亮, LED2(索引2): 关闭 |
| `WAITING_USER` | LED1(索引1): 橙色 `{255,128,0}` 闪烁, LED2(索引2): 关闭 |
| `COMPLETED` | LED2(索引2): 绿色 `{0,255,0}` 常亮, LED1(索引1): 关闭 |
| `ERROR` | LED0(索引0): 红色 `{255,0,0}` 常亮, LED1(索引1): 关闭, LED2(索引2): 关闭 |

映射定义放在 `config.py` 的 `EVENT_LED_MAP` 字典中，非硬编码。

`TcpRelayServer` 收到 TCP 消息后调用映射函数，得到 JSON 字符串，通过 `ble.send(json_str)` 发送。

### 5.3 保活 & 配对 — 不变

- 保活：继续每 10 秒发 `KEEPALIVE` 纯文本
- 配对：继续发 `PAIR_CONFIRM` / `PAIR_SUCCESS` 纯文本

---

## 6. 时序图

```
┌──────────────┐     ┌─────────────┐     ┌──────────┐
│ hook-notifier │     │desktop-relay│     │  ESP32   │
│  (notify.py) │     │  (main.py)  │     │(BLE firmware)│
└──────┬───────┘     └──────┬──────┘     └─────┬────┘
       │                    │                  │
       │ TCP: "WORKING"     │                  │
       │───────────────────>│                  │
       │                    │                  │
       │                    │ translate event  │
       │                    │ to JSON          │
       │                    │                  │
       │                    │ BLE write:       │
       │                    │ {"leds":[...]}   │
       │                    │─────────────────>│
       │                    │                  │ parse + apply
       │                    │                  │ start LED timer
       │                    │  BLE notify:     │
       │                    │    "OK"          │
       │                    │<─────────────────│
       │                    │                  │
       │                    │ ... 30s later ...│
       │                    │                  │ timeout →
       │                    │                  │ LED auto-off
```

---

## 7. 错误处理

| 场景 | ESP32 处理 | 响应 |
|------|-----------|------|
| JSON 解析失败 | 忽略，不改 LED | `ERR:JSON_PARSE` |
| `id` 超出范围 (0-2) | 跳过该 led 项 | `OK`（忽略越界项） |
| `on: true` 但 r/g/b 全为 0 | 照常执行（黑色 = 灭） | `OK` |
| `timeout` 为负数 | 视为未提供，用默认 600s | `OK` |
| BLE 写入失败 | —（ble_client.py 已有重连） | 无 |

---

## 8. 测试要点

1. **JSON 格式正确性** — 各字段组合，固件能正确解析并亮灯
2. **多 LED 同时控制** — 一条 JSON 控制 2-3 个 LED
3. **独立超时** — LED1 30 秒超时、LED2 默认 600 秒超时，互不干扰
4. **关闭指令** — `on: false` 立即熄灯并取消定时器
5. **闪烁** — `blink: true` 时 LED 以 500ms 周期交替亮灭
6. **电池优先级** — 低电时 LED0 红色闪烁，不被 LED 指令覆盖；恢复后 LED0 恢复正常
7. **保活不干扰** — `KEEPALIVE` 不改变 LED 状态
8. **配对流程** — `PAIR_CONFIRM`/`PAIR_SUCCESS` 行为正确
9. **无效 JSON** — 返回错误响应，LED 状态不变
10. **编译通过** — 使用 `/idf-build` 验证 firmware 编译

---

## 9. 不涉及（明确排除）

- LED 硬件引脚/型号不变（GPIO3, WS2812B）
- BLE 服务 UUID 不变
- 配对 UX 流程不变
- 电池监测逻辑不变
- 电源管理（Light-sleep）逻辑不变
- 安装程序不变
