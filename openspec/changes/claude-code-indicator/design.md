## Context

本项目是一个 Claude Code 物理状态指示器系统，由三部分组成：ESP32-C3 固件（LED 显示器 + BLE 外设）、电脑端 Python 中继程序（BLE 中央 + TCP 服务器）、Claude Code Hook 通知脚本。当前为全新项目，无现有代码约束。

目标硬件：ESP32-C3-MINI-1-N4，3 颗 WS2812B LED 串联于 GPIO3，电池分压 50% 接 GPIO0 ADC。

## Goals / Non-Goals

**Goals:**
- ESP32 固件能通过 BLE 接收指令并控制 3 颗 WS2812B LED 显示不同颜色/闪烁模式
- 电脑端程序通过 BLE 与 ESP32 保持稳定连接，通过 TCP 54321 端口接收本地通知
- Claude Code 通过 Hook 机制自动发送状态事件
- 安装脚本一键配置所有 Hook，用户零手工配置
- 电池低电量（≤3.5V）时红色 LED 告警
- ESP32 空闲时进入 Light-sleep 省电，每 4 秒唤醒一次维持蓝牙连接

**Non-Goals:**
- 不支持多设备同时连接
- 不支持 WiFi 或云端远程通知
- 不实现 OTA 固件升级
- 不实现移动端 App
- 不处理 Claude Code 以外的应用状态通知

## Decisions

### 1. BLE 协议设计：单 Service + 单 Characteristic（Write + Notify）

**选择**: 使用一个自定义 GATT Service，内含一个支持 Write（电脑→ESP32）和 Notify（ESP32→电脑）的 Characteristic。消息格式为纯文本字符串（如 `"WORKING"`, `"WAITING_USER"`, `"COMPLETED"`）。

**理由**: 简单直观，调试方便，无需复杂的二进制协议。低吞吐量场景下文本协议足矣。

**备选**: 二进制协议 — 更省带宽但可读性差，调试困难。多 Characteristic 分离命令和响应 — 增加复杂度无实际收益。

### 2. 电脑端架构：单进程 BLE + TCP 事件循环

**选择**: 使用 Python `asyncio` 统一管理 BLE（bleak 库）和 TCP server 的事件循环，单线程异步模型。

**理由**: 避免多线程带来的同步复杂性，asyncio 对 bleak 和 socket 都有良好支持。BLE 连接和 TCP 连接都是 IO 密集型，单线程异步足够。

**备选**: 多线程（BLE 线程 + TCP 线程）— 需要 Queue 通信，增加竞态风险。

### 3. 配对流程：首次连接弹出 Tkinter 确认对话框

**选择**: 电脑端首次扫描到设备后弹出 Tkinter 对话框让用户确认，确认后将蓝牙名称写入本地 JSON 配置文件，后续启动只连该名称的设备。

**理由**: Tkinter 是 Python 标准库自带，无需额外安装；JSON 配置文件简单可靠。

### 4. Claude Code Hook 集成：使用 settings.json hooks

**选择**: 安装程序在 `~/.claude/settings.json` 的 `hooks` 段添加脚本路径，Hook 类型使用 `postToolUse` 事件触发通知。

**理由**: Claude Code 原生 Hook 机制，无需修改 Claude Code 源码，稳定可靠。

### 5. LED 控制：使用 ESP-IDF led_strip 组件

**选择**: 使用 ESP-IDF 官方 `led_strip` 组件通过 RMT 驱动 WS2812B。

**理由**: 官方组件稳定可靠，已针对 ESP32-C3 优化，无需自行实现 RMT 时序。

### 6. 电池检测：ADC 分压 + 移动平均滤波

**选择**: GPIO0 读取 ADC 值，通过 10 次移动平均滤波，分压比 50% 换算实际电压。每隔 5 秒检测一次。

**理由**: 简单有效，移动平均消除 ADC 噪声。

## Risks / Trade-offs

- **[Risk] BLE 连接不稳定（距离/干扰）** → 电脑端实现自动重连机制，重试间隔递增
- **[Risk] Light-sleep 期间丢失 BLE 命令** → 使用 4 秒唤醒窗口足够，BLE 连接参数设置合理的 supervision timeout
- **[Risk] Python bleak 库跨平台兼容性** → 仅支持 Windows，使用 bleak 的 Windows BLE 后端
- **[Risk] 电池电量 ADC 读数不准确** → 使用移动平均 + 校准偏移量，仅做低电量告警不要求精度
- **[Trade-off] 文本协议而非二进制** → 牺牲极少量带宽换取可调试性和可扩展性

## Open Questions

- 电池分压电路的精确电阻值待硬件确定后校准 ADC 换算系数
- 是否需要支持修改蓝牙广播名称（当前固定为 `ClaudeCodeIndicator` + MAC 后缀）
