## ADDED Requirements

### Requirement: GATT Service 定义
ESP32 设备 SHALL 创建一个自定义 GATT Service（UUID: `0000ff00-0000-1000-8000-00805f9b34fb`），内含一个 Characteristic（UUID: `0000ff01-0000-1000-8000-00805f9b34fb`）支持 Write 和 Notify 操作。

#### Scenario: Service 注册
- **WHEN** ESP32 BLE 初始化完成
- **THEN** 自定义 GATT Service 和 Characteristic 已注册并可通过 BLE 发现

### Requirement: 消息格式
所有消息 SHALL 使用纯文本字符串格式，以换行符 `\n` 作为消息结束标志。消息类型包括：

| 方向 | 消息 | 含义 |
|------|------|------|
| PC→ESP32 | `KEEPALIVE` | 保活请求 |
| PC→ESP32 | `WORKING` | 工作中 |
| PC→ESP32 | `WAITING_USER` | 等待用户确认 |
| PC→ESP32 | `COMPLETED` | 工作完成 |
| PC→ESP32 | `PAIR_CONFIRM` | 配对确认 |
| PC→ESP32 | `PAIR_SUCCESS` | 配对成功 |
| ESP32→PC | `ALIVE` | 保活确认 |

#### Scenario: 消息发送与解析
- **WHEN** 一方通过 Characteristic Write/Notify 发送 `"WORKING\n"`
- **THEN** 接收方解析到 "WORKING" 消息并执行对应操作

### Requirement: 连接参数
BLE 连接 SHALL 使用以下参数范围确保稳定性：
- Connection Interval: 30ms ~ 50ms
- Supervision Timeout: 4000ms（大于 4 秒唤醒间隔）
- Slave Latency: 0

#### Scenario: 连接参数协商
- **WHEN** 电脑端与 ESP32 建立 BLE 连接
- **THEN** 连接参数在指定范围内协商成功

### Requirement: 断线处理
电脑端检测到 BLE 断开后 SHALL 自动尝试重连，重连间隔采用指数退避（1s, 2s, 4s, 8s, 上限 30s）。

#### Scenario: 自动重连
- **WHEN** BLE 连接意外断开
- **THEN** 电脑端在 1 秒后尝试重连，失败后逐次递增间隔，直到重连成功

### Requirement: 配对绑定
电脑端首次配对成功后 SHALL 将 ESP32 广播名称保存至本地 `~/.claude-code-indicator/device.json`，之后仅连接该名称的蓝牙设备。

#### Scenario: 首次配对绑定
- **WHEN** 用户首次通过配对确认对话框，电脑端发送 "PAIR_SUCCESS"
- **THEN** 广播名称持久化到配置文件

#### Scenario: 后续启动直接连接
- **WHEN** 电脑端程序再次启动且配置文件存在
- **THEN** 仅扫描并连接配置文件中记录的蓝牙名称
