## Context

当前系统由 ESP32-C3 固件（BLE Peripheral）和 Python desktop-relay（BLE Central）组成。桌面端通过 BLE 发送 LED 控制指令到 ESP32，每 10 秒发送一次 KEEPALIVE 保活。

**断连场景**：
- Python 进程退出（Ctrl+C / kill / 崩溃），BLE 物理连接中断
- ESP32 侧 BLE 协议层依赖 supervision timeout 检测断连——NimBLE 默认值 20+ 秒
- 连接参数（conn_interval, supervision_timeout）从未显式协商，由 NimBLE 使用默认值
- 若 PC 端 BLE 适配器不发送 disconnect PDU（强制退出时常见），ESP32 必须等到 supervision timeout 才触发 `BLE_GAP_EVENT_DISCONNECT`

**当前代码结构**：ESP32 `ble_server.cpp` 已在 `BLE_GAP_EVENT_DISCONNECT` 和 `BLE_GAP_EVENT_ADV_COMPLETE` 中重启广播，逻辑正确，但恢复速度取决于 supervision timeout。

## Goals / Non-Goals

**Goals:**
- ESP32 在 PC 端断连后 4-6 秒内检测到并恢复广播（协议层保证）
- ESP32 在 20 秒无数据时主动断开（应用层双保险）
- Python 端优雅退出时主动发送 BLE disconnect，消除协议层等待
- Python 端重新启动后无需重启硬件即可连上 ESP32

**Non-Goals:**
- 不改变 BLE 配对/绑定策略（保持现有 PAIR_CONFIRM 流程）
- 不引入 Android/iOS 新平台支持
- 不改动 HTTP/TCP 服务器逻辑
- 不处理 PC 端 BLE 适配器硬件故障场景

## Decisions

### Decision 0: ESP32 重构 — 全局状态移入 BleServer 成员

当前 `ble_server.cpp` 使用 C 风格全局变量（`g_conn_handle`, `g_msg_cb`, `g_conn_cb`），`BleServer` class 只是一个 thin wrapper。为支持看门狗 Timer 和未来的可测试性，将核心状态重构为 `BleServer` 的 private 成员变量：

- `g_conn_handle` → `m_conn_handle`
- `g_msg_cb` → `m_msg_cb`
- `g_conn_cb` → `m_conn_cb`
- 新增 `m_watchdog` (FreeRTOS Timer)

回调函数（`gatt_write_cb`, `gap_event_cb`）改为 static 成员函数，通过 `arg` 指针接收 `this`。`BleServer` 改为单例或由 `main.cpp` 持有指针传递。

**替代方案**：保持全局变量，看门狗也用全局 timer。被否决——全局状态不利于维护，且 Timer 回调传参需要 `this` 指针，用全局变量绕开反而更乱。

### Decision 1: ESP32 协议层 — 设置 prefered connection params

在 `BleServer::start_advertise()` 广播开始后，通过 `ble_gap_set_prefered_le_params()` 向 NimBLE 注册期望的连接参数。

```c
// 期望的连接参数
struct ble_gap_upd_params params = {
    .itvl_min   = BLE_GAP_INITIAL_CONN_ITVL_MIN,  // 15ms
    .itvl_max   = BLE_GAP_INITIAL_CONN_ITVL_MAX,  // 30ms
    .latency    = 0,                                // 不允许跳过连接事件
    .supervision_timeout = 400,                     // 400 × 10ms = 4000ms
    .min_ce_len = BLE_GAP_INITIAL_CONN_ITVL_MIN,
    .max_ce_len = BLE_GAP_INITIAL_CONN_ITVL_MAX,
};
```

- supervision_timeout = 400 (4000ms)：平衡检测速度与稳定性，足够容忍短时 RF 干扰
- latency = 0：不允许跳过连接事件，确保响应性和监督检测精度
- 选择 NimBLE API 而非直接操作 HCI：与现有 NimBLE 架构一致，无需引入额外依赖

**替代方案**：仅靠应用层看门狗。被否决，因为应用层看门狗无法在协议层 timer 失效的场景下工作。

### Decision 2: ESP32 应用层 — 数据看门狗

在 `BleServer` 中添加成员 `TimerHandle_t m_watchdog`：
- 超时回调为 static 函数，通过 `pvTimerID` 传入 `this` 指针来调用成员方法
- 超时 20 秒（2x KEEPALIVE_INTERVAL，容忍 1 次丢包 + 1 次间隔）
- 每次收到 BLE 数据时（包括 KEEPALIVE 和 JSON 指令）调用 `xTimerReset(m_watchdog, 0)` 重置
- 超时回调调用 `ble_gap_terminate(m_conn_handle, BLE_ERR_REM_USER_CONN_TERM)` 主动断开

Timer 在 `BLE_GAP_EVENT_CONNECT` 中 `xTimerCreate + xTimerStart`，在 `BLE_GAP_EVENT_DISCONNECT` 中 `xTimerStop + xTimerDelete`。

**为什么 20 秒而非 30 秒**：20 秒刚好是 2 倍 keepalive 间隔。若选 30 秒（3x），用户断连后需等太久；15 秒在 RF 干扰严重时可能误触发。20 秒是折中。

### Decision 3: Python 侧 — 优雅退出

在 `main.py` 的 `finally` 块中调用 `ble.disconnect()`，确保 KeyboardInterrupt 和其他 Python 异常退出时主动发送 disconnect。

**局限**：`kill -9` / 进程崩溃时不会触发，这是方案仍需 ESP32 侧修复作为主力保障的原因。

### Decision 4: Python 侧 — 启动时适配器清理

在 `scan_and_connect` 方法最前面，尝试创建 `BleakClient` 并立即断开或通过 Bleak 扫描时的 reset 行为刷新适配器状态。Bleak 在 Windows 上使用 WinRT BLE API，每次扫描前会自动获取最新设备列表，但旧连接句柄可能被 OS 缓存。在起始处添加 `await asyncio.sleep(0.5)` 给 BLE 栈稳定的时间。

**替代方案**：使用 `pygatt` 或直接操作 Windows BLE API。被否决，保持 Bleak 依赖一致性。

## Risks / Trade-offs

- **[RF 干扰误触发看门狗]**：20 秒无数据时 ESP32 会主动断开。若 RF 环境差、连续丢包，可能导致正常连接被误断 → **Mitigation**: Python 侧 `reconnect_loop` 会自动重连；缩短 supervision timeout 到 4s 后，即使误断也能快速恢复
- **[电量消耗]**：latency=0 和 connection interval 15-30ms 略微增加 ESP32 功耗 → **Mitigation**: 当前设备 USB 供电，功耗不敏感；若未来电池供电可放宽参数
- **[NimBLE 连接参数协商失败]**：某些 BLE 中央设备可能拒绝参数更新请求 → **Mitigation**: `set_prefered_le_params` 只是"建议"参数，中央设备不采纳时会使用默认值；应用层看门狗作为降级保障始终生效
