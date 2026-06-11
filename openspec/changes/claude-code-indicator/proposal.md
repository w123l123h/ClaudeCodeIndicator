## Why

Claude Code 运行长时间的 AI 任务时，用户无法直观了解其工作状态（工作中、等待确认、完成），需要频繁查看终端确认进度。通过在桌面上放置一个物理 LED 指示灯，用户可以一目了然地看到 Claude Code 的实时状态，提升工作效率和体验。

## What Changes

- **新增** ESP32-C3 固件项目（C++/ESP-IDF 5.5.3），控制 3 颗 WS2812B LED 显示状态，通过 BLE 接收指令，支持电池电量监测和低功耗模式
- **新增** 电脑端主程序（Python），作为 BLE 客户端连接 ESP32-C3，同时开启 TCP 端口 54321 接收本地通知，将状态指令中转到硬件
- **新增** Claude Code 通知程序（Python），通过 Hook 机制捕获 Claude Code 状态事件（工作中/等待确认/工作完成），通过 TCP 发送给电脑端主程序
- **新增** 安装程序（Python），自动将 Hook 脚本配置到 Claude Code 设置中
- **新增** 启动脚本（.bat），方便用户一键启动电脑端主程序，含单实例保护

## Capabilities

### New Capabilities

- `esp32-indicator-firmware`: ESP32-C3 固件，控制 WS2812B LED（红色/橙色/绿色），BLE 通信，电池电压监测，Light-sleep 低功耗管理
- `ble-communication`: ESP32 与电脑端之间的 BLE 通信协议，包括保活、工作中、等待确认、工作完成、配对确认、配对成功等消息类型
- `desktop-relay`: 电脑端 Python 程序，BLE 客户端连接 + TCP 服务器（端口 54321），支持首次配对绑定蓝牙名称，单实例保护，定时保活
- `claude-code-integration`: Claude Code Hook 集成脚本，截获工作状态事件并通过 TCP 转发至桌面中继程序
- `installer`: 安装脚本，自动配置 Claude Code Hook，安装所需 Python 依赖

### Modified Capabilities

<!-- 新项目，无现有 capabilities 需要修改 -->

## Impact

- **硬件**: ESP32-C3-MINI-1-N4 + 3×WS2812B LED（GPIO3 串联）+ 电池分压电路（GPIO0 ADC）
- **固件依赖**: ESP-IDF 5.5.3, led_strip 组件, NimBLE
- **电脑端依赖**: Python 3.13+, bleak (BLE), 标准库 socket/tkinter
- **Claude Code**: settings.json 中新增 hooks 配置（自动化安装）
- **无破坏性变更**: 全新增项目
