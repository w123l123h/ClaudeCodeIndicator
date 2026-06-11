## ADDED Requirements

### Requirement: Claude Code Hook 注册
安装程序 SHALL 在 `~/.claude/settings.json` 的 `hooks` 段注册通知脚本，使 Claude Code 在特定事件时自动触发状态通知。

#### Scenario: Hook 配置写入
- **WHEN** 安装程序运行完成
- **THEN** `settings.json` 中包含指向通知脚本的 hook 配置

### Requirement: 状态事件类型
通知脚本 SHALL 支持以下 Hook 事件映射：
- 用户消息展开（UserPromptExpansion 触发）→ `WORKING`
- 工具调用开始（PreToolUse 触发）→ `WORKING`
- 等待用户确认（PermissionRequest 触发）→ `WAITING_USER`
- 任务停止（Stop 触发）→ `COMPLETED` 或 `ERROR`

#### Scenario: 工作中通知
- **WHEN** Claude Code 开始执行工具调用
- **THEN** 通过 TCP 向 `127.0.0.1:54321` 发送消息 `WORKING`

#### Scenario: 用户消息展开通知
- **WHEN** 用户在 Claude Code 中输入消息（UserPromptExpansion 触发）
- **THEN** 通过 HTTP POST 向 `http://127.0.0.1:54321/hook` 发送事件
- **THEN** 电脑端主程序通过 BLE 发送 `WORKING` 消息
- **THEN** 指示灯 LED2 显示橙色常亮

#### Scenario: 等待确认通知
- **WHEN** Claude Code 正在等待用户确认
- **THEN** 通过 TCP 向 `127.0.0.1:54321` 发送消息 `WAITING_USER`

#### Scenario: 工作完成通知
- **WHEN** Claude Code 完成所有任务且无待处理确认
- **THEN** 通过 TCP 向 `127.0.0.1:54321` 发送消息 `COMPLETED`

### Requirement: TCP 通信
通知脚本 SHALL 通过 TCP 连接到电脑端主程序的 `54321` 端口，发送单条消息后关闭连接。连接超时 SHALL 设为 2 秒，超时则静默忽略不抛异常。

#### Scenario: 正常发送
- **WHEN** 电脑端主程序正在运行且 Hook 触发
- **THEN** TCP 连接成功，消息发送成功，连接关闭

#### Scenario: 主程序未运行
- **WHEN** 电脑端主程序未启动时 Hook 触发
- **THEN** TCP 连接超时，静默忽略，不影响 Claude Code 正常运行

### Requirement: 可配置参数
所有参数（TCP 端口、目标地址、超时时间）SHALL 通过脚本顶部常量定义。

#### Scenario: 参数集中管理
- **WHEN** 需要修改目标端口
- **THEN** 只需修改脚本顶部常量 `TCP_PORT` 即可
