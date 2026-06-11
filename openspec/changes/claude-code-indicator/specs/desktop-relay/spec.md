## ADDED Requirements

### Requirement: BLE 客户端
电脑端程序 SHALL 作为 BLE Central 扫描并连接 ESP32 设备。首次运行且无配对记录时，扫描所有包含 "ClaudeCodeIndicator" 的广播设备并弹出确认对话框；已有配对记录则直接连接指定设备。

#### Scenario: 首次扫描与配对
- **WHEN** 首次运行无配置文件
- **THEN** 扫描到包含 "ClaudeCodeIndicator" 的设备并弹出配对确认对话框

#### Scenario: 再次启动直连
- **WHEN** 配置文件存在且其中记录的设备在范围内
- **THEN** 直接连接到指定设备，不弹出对话框

### Requirement: 配对确认对话框
配对过程中 SHALL 向 ESP32 发送 "PAIR_CONFIRM" 指令，然后弹出 Tkinter 对话框询问用户是否看到三个 LED 都亮绿色。用户确认后发送 "PAIR_SUCCESS" 并保存配置。

#### Scenario: 用户确认配对
- **WHEN** 用户点击"是"确认三个 LED 都亮绿色
- **THEN** 发送 "PAIR_SUCCESS"，保存蓝牙名称到 `~/.claude-code-indicator/device.json`

#### Scenario: 用户拒绝配对
- **WHEN** 用户点击"否"
- **THEN** 断开连接，程序退出

### Requirement: TCP 服务器
电脑端程序 SHALL 在本地 `127.0.0.1:54321` 开启 TCP 服务器，接受来自 Claude Code 通知程序的连接。

#### Scenario: TCP 服务启动
- **WHEN** 电脑端程序启动
- **THEN** TCP 端口 54321 开始监听本地连接

#### Scenario: TCP 消息转发
- **WHEN** TCP 收到合法消息（WORKING/WAITING_USER/COMPLETED）
- **THEN** 通过 BLE 将对应指令转发给 ESP32

### Requirement: 保活机制
电脑端程序 SHALL 每 10 秒通过 BLE 发送 "KEEPALIVE" 请求，并在 3 秒内等待 "ALIVE" 回复。连续 3 次无回复则视为断线。

#### Scenario: 定时保活
- **WHEN** BLE 连接已建立
- **THEN** 每 10 秒自动发送 "KEEPALIVE" 消息

#### Scenario: 保活超时检测
- **WHEN** 连续 3 次保活请求无 "ALIVE" 回复
- **THEN** 认为连接断开，启动重连流程

### Requirement: 参数配置
所有参数（TCP 端口、保活间隔、保活超时次数、重连参数、配置文件路径）SHALL 通过脚本顶部的常量定义，不得硬编码散落各处。

#### Scenario: 参数修改
- **WHEN** 需要修改保活间隔
- **THEN** 只需修改脚本顶部常量 `KEEPALIVE_INTERVAL` 即可

### Requirement: 启动脚本
提供 `.bat` 启动脚本，使用 Python 路径 `C:\Users\joe06\AppData\Local\Programs\Python\Python313` 启动电脑端主程序，并检测是否已有实例运行，如有则直接退出。

#### Scenario: 正常启动
- **WHEN** 双击 .bat 文件
- **THEN** 使用指定 Python 路径启动电脑端主程序

#### Scenario: 重复启动保护
- **WHEN** 已有一个实例在运行中，再次双击 .bat
- **THEN** 新进程检测到已有实例并立即退出
