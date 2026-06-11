# Claude Code Indicator — 设计规格

**日期**: 2026-06-10
**状态**: 已确认

---

## 概述

物理 LED 指示灯系统，通过 ESP32-C3 + WS2812B LED 实时显示 Claude Code 工作状态（工作中/等待确认/完成/报错），附带电池低电量告警。

## 目录结构

```
/start.bat              ← 启动电脑端主程序（调用 desktop-relay/main.py）
/install.bat            ← 安装 Hook（调用 installer/install.py）
/package.py             ← 打包脚本：生成客户分发 zip
/firmware/              ← ESP-IDF 项目（C++）
  CMakeLists.txt
  main/
    CMakeLists.txt
    main.cpp            ← 入口：初始化 → 自检 → BLE → 事件循环
    config.h            ← 所有参数宏
    led_controller.h/cpp
    ble_server.h/cpp
    battery_monitor.h/cpp
    power_manager.h/cpp
/desktop-relay/         ← 电脑端主程序（Python）
  main.py
  config.py
  ble_client.py
  tcp_server.py
  pairing.py
/hook-notifier/         ← Claude Code Hook 脚本（Python）
  notify.py
/installer/             ← 安装程序（Python）
  install.py
```

## 组件

### 1. ESP32 固件

- 芯片: ESP32-C3-MINI-1-N4
- 平台: ESP-IDF 5.5.3, C++
- LED: 3×WS2812B 串联于 GPIO3, 使用 led_strip (RMT)
- 电池: GPIO0 ADC, 分压 50%
- BLE: NimBLE

**入口流程**: 初始化 → 自检序列（红 200ms / 橙 200ms / 绿 200ms / 全灭）→ BLE 广播 → 事件循环

**LED 状态表**:

| 消息 | LED1 (红) | LED2 (橙) | LED3 (绿) | 超时 |
|------|-----------|-----------|-----------|------|
| WORKING | 关 | 常亮 | 关 | 10分钟 |
| WAITING_USER | 关 | 闪烁 | 关 | 10分钟 |
| COMPLETED | 关 | 关 | 常亮 | 10分钟 |
| ERROR | 常亮 | 关 | 关 | 10分钟 |
| PAIR_CONFIRM | 常亮(绿) | 常亮(绿) | 常亮(绿) | 无 |
| PAIR_SUCCESS | 关 | 关 | 关 | — |
| 电池 ≤ 3.5V | 常亮 | — | — | 永久保持 |
| 电池 > 3.5V | 关 | — | — | — |

**省电**: 空闲 5s → Light-sleep, 4s 唤醒一次。BLE 活动自动唤醒。

### 2. BLE 通信协议

- Service UUID: `0000ff00-0000-1000-8000-00805f9b34fb`
- Char UUID: `0000ff01-0000-1000-8000-00805f9b34fb`, Write + Notify
- 消息格式: 纯文本 + `\n` 结尾
- Connection Interval: 30-50ms, Supervision Timeout: 4000ms, Slave Latency: 0

### 3. 电脑端主程序

- Python asyncio 单线程管理 BLE (bleak) + TCP server
- TCP 端口: `127.0.0.1:54321`
- 保活: 10s KEEPALIVE, 3s 超时等 ALIVE, 连续 3 次失败触发重连
- 重连: 指数退避 1s/2s/4s/8s, 上限 30s
- 配对: 首次扫描 → 连接 → PAIR_CONFIRM → Tkinter 对话框 → PAIR_SUCCESS → 保存 `device.json`（当前目录）
- 后续启动: 读取 `device.json`，只连指定设备
- 单实例: start.bat 检测已有进程则退出

### 4. Claude Code Hook 脚本

- 通过 settings.json hooks 触发
- 事件映射: 工具调用→WORKING, 等待确认→WAITING_USER, 完成→COMPLETED, 报错→ERROR
- TCP 连接 `127.0.0.1:54321`, 发送即关闭
- 超时 2s, 静默忽略（不影响 CC 运行）

### 5. 安装程序

- `pip install bleak`（已有则跳过）
- 写入 `~/.claude/settings.json` hooks 段, 保留已有 hooks
- 验证安装结果并输出

### 6. 打包脚本 (`package.py`)

- 位于项目根目录
- 运行时收集客户分发文件，打包为 `ClaudeCodeIndicator.zip`
- 解压后目录结构：

```
ClaudeCodeIndicator/
  start.bat              ← 直接从根目录复制
  install.bat            ← 直接从根目录复制
  desktop-relay/         ← Python 文件
  hook-notifier/         ← Python 文件
  installer/             ← Python 文件
```

- 不包含 `firmware/`（客户不需要）和 `package.py`（打包工具自身）
- `.bat` 已在根目录，打包无需移动，路径天然正确

## 参数管理

所有参数通过变量/宏定义，不硬编码：

- 固件: `config.h` 宏定义
- Python: 各模块顶部常量

## 验收标准

1. ESP32-C3 固件编译通过 (`/idf-build`)
2. 电脑端主程序可正常启动, BLE 连接成功, TCP 端口监听
3. 端到端消息链路: Hook → TCP → BLE → LED 状态正确
4. 电池低电量告警验证
5. Light-sleep 省电模式验证
