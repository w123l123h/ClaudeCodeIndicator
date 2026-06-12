## ADDED Requirements

### Requirement: BLE state encapsulated in BleServer

BLE 核心状态 SHALL 封装为 `BleServer` 的私有成员变量，不再使用文件级全局变量。回调函数通过 `arg` 指针接收 `this` 来访问成员。

#### Scenario: 连接状态作为成员变量

- **WHEN** BLE 连接建立或断开
- **THEN** `BleServer` 通过 `m_conn_handle` 成员变量追踪当前连接句柄，而非全局 `g_conn_handle`

#### Scenario: 回调通过 this 指针访问成员

- **WHEN** `gatt_write_cb` 或 `gap_event_cb` 被 NimBLE 调用
- **THEN** 回调函数通过 `arg` 参数获取 `BleServer*`，调用成员方法（如 `reset_watchdog()`）

### Requirement: BLE connection supervision timeout

ESP32 BLE 固件 SHALL 在广播启动后通过 NimBLE API 设置期望的 BLE 连接参数，其中 supervision timeout 不得超过 4000ms。

#### Scenario: ESP32 设置连接参数

- **WHEN** `BleServer::start_advertise()` 被调用且 NimBLE host 已就绪
- **THEN** 固件调用 `ble_gap_set_prefered_le_params()` 设置 itvl_min=15ms, itvl_max=30ms, latency=0, supervision_timeout=400（即 4000ms）

#### Scenario: 连接参数协商失败降级

- **WHEN** BLE 中央设备拒绝或忽略推荐的连接参数
- **THEN** ESP32 接受中央设备的参数，不因参数协商失败而停止或崩溃，应用层看门狗作为降级保障仍然生效

### Requirement: Application-layer data watchdog

ESP32 固件 SHALL 维护一个应用层数据看门狗定时器，在 20 秒内未收到任何 BLE 数据时主动断开当前连接并重启广播。

#### Scenario: 连接建立时启动看门狗

- **WHEN** BLE 连接建立（`BLE_GAP_EVENT_CONNECT` 且 status=0）
- **THEN** 启动一个 20 秒的 FreeRTOS 一次性定时器

#### Scenario: 收到数据时重置看门狗

- **WHEN** ESP32 收到任何 BLE 写入数据（包括 KEEPALIVE 和 JSON 指令）
- **THEN** 看门狗定时器重置为 20 秒

#### Scenario: 看门狗超时主动断开

- **WHEN** 看门狗定时器在 20 秒内未被重置（即无数据到达）
- **THEN** ESP32 调用 `ble_gap_terminate()` 主动断开当前连接，并触发 `BLE_GAP_EVENT_DISCONNECT` 事件，从而重启广播

#### Scenario: 连接断开时删除看门狗

- **WHEN** BLE 连接断开（`BLE_GAP_EVENT_DISCONNECT`）
- **THEN** 看门狗定时器被停止并删除

### Requirement: BLE disconnection triggers advertising restart

ESP32 固件 SHALL 在任何 BLE 断开事件（无论是远端断开、协议层超时、还是应用层主动断开）发生后立即重新开始广播。

#### Scenario: 协议层检测到断连后重新广播

- **WHEN** NimBLE 协议层检测到 supervision timeout 或远端主动断开连接
- **THEN** 固件在 `BLE_GAP_EVENT_DISCONNECT` 处理中调用 `ble_gap_adv_start()` 恢复广播

#### Scenario: 广播完成后重新开始

- **WHEN** BLE 广播因任何原因停止（触发 `BLE_GAP_EVENT_ADV_COMPLETE`）
- **THEN** 固件重新调用 `ble_gap_adv_start()` 无限期广播
