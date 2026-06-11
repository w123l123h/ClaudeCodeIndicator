## ADDED Requirements

### Requirement: 启动自检序列
固件启动时 SHALL 依次点亮 LED1（红色）、LED2（橙色）、LED3（绿色），每次间隔 200ms，之后关闭所有 LED。

#### Scenario: 正常上电自检
- **WHEN** ESP32-C3 上电启动
- **THEN** LED1 亮红色 200ms，然后 LED2 亮橙色 200ms，然后 LED3 亮绿色 200ms，然后全部熄灭

### Requirement: BLE 广播
启动自检完成后 SHALL 开始 BLE 广播，广播名称为 `ClaudeCodeIndicator_<蓝牙MAC地址>`。

#### Scenario: BLE 广播名称
- **WHEN** 自检序列完成
- **THEN** 设备开始 BLE 广播，广播名称包含 "ClaudeCodeIndicator" 和设备蓝牙 MAC 地址

### Requirement: LED 状态指示
固件 SHALL 根据收到的 BLE 指令控制 LED 状态：
- **工作中**：点亮 LED2（橙色），关闭 LED3
- **等待用户确认**：LED2（橙色）闪烁，关闭 LED3
- **工作完成**：点亮 LED3（绿色），关闭 LED2
- **配对确认**：点亮所有 LED（绿色）
- **配对成功**：关闭所有 LED

#### Scenario: 收到工作中指令
- **WHEN** 通过 BLE 收到 "WORKING" 指令
- **THEN** LED2 亮橙色常亮，LED3 关闭

#### Scenario: 收到等待确认指令
- **WHEN** 通过 BLE 收到 "WAITING_USER" 指令
- **THEN** LED2 橙色闪烁（亮 500ms / 灭 500ms），LED3 关闭

#### Scenario: 收到工作完成指令
- **WHEN** 通过 BLE 收到 "COMPLETED" 指令
- **THEN** LED3 亮绿色常亮，LED2 关闭

#### Scenario: 收到配对确认指令
- **WHEN** 通过 BLE 收到 "PAIR_CONFIRM" 指令
- **THEN** 所有 LED 亮绿色常亮

#### Scenario: 收到配对成功指令
- **WHEN** 通过 BLE 收到 "PAIR_SUCCESS" 指令
- **THEN** 所有 LED 关闭

### Requirement: 保活响应
固件 SHALL 在收到保活请求时立即回复确认消息，维持 BLE 连接活跃。

#### Scenario: 保活响应
- **WHEN** 通过 BLE 收到 "KEEPALIVE" 指令
- **THEN** 通过 BLE Notify 回复 "ALIVE" 消息

### Requirement: 电池电压监测
固件 SHALL 定期读取 GPIO0 ADC 值，经过 10 次移动平均滤波后换算为实际电压（考虑 50% 分压比）。检测周期为 5 秒。

#### Scenario: 低电量告警
- **WHEN** 电池电压 ≤ 3.5V
- **THEN** LED1 亮红色常亮

#### Scenario: 电量恢复正常
- **WHEN** 电池电压 > 3.5V（从低电量恢复）
- **THEN** LED1 关闭

### Requirement: Light-sleep 省电模式
在无 BLE 活动时 SHALL 进入 Light-sleep 模式，每 4 秒唤醒一次以维持蓝牙连接。BLE 连接参数 SHALL 设置合理的 supervision timeout 防止断开。

#### Scenario: 空闲进入省电
- **WHEN** 超过 5 秒未收到任何 BLE 指令
- **THEN** 进入 Light-sleep 模式，以 4 秒间隔唤醒

#### Scenario: BLE 活动唤醒
- **WHEN** 处于 Light-sleep 且收到 BLE 请求
- **THEN** 自动唤醒并处理请求

### Requirement: 可配置参数
所有引脚定义、定时参数、电压阈值、LED 颜色 SHALL 通过头文件中的宏定义或 constexpr 变量配置，不得硬编码。

#### Scenario: 参数集中管理
- **WHEN** 需要修改引脚或时间参数
- **THEN** 只需修改头文件中的宏定义或常量即可
