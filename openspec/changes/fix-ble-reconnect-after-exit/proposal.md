## Why

`main.py` 退出后（正常退出、Ctrl+C 或强制终止），再次启动无法连接 ESP32 硬件，必须重启硬件才能恢复。根因在 ESP32 端：PC 断开后，BLE 协议层 supervision timeout（默认 20+ 秒）检测太慢，且可能因连接参数未协商而不触发，ESP32 长期卡在"已连接"状态不广播。Python 侧退出时不主动断开、启动时不清理适配器状态，加剧了问题。

## What Changes

- **ESP32 BLE 协议层**：设置 `ble_gap_set_prefered_le_params` 将 supervision timeout 缩短到 4 秒，硬件层快速检测断连
- **ESP32 应用层**：添加数据看门狗，20 秒无数据则主动断开连接并重启广播
- **Python 侧优雅退出**：捕获 KeyboardInterrupt/SIGTERM，主动调用 BLE disconnect 通知远端
- **Python 侧启动清理**：连接前调用 Bleak 的适配器 reset/cleanup，清除 OS 层旧连接状态

## Capabilities

### New Capabilities

- `ble-fast-reconnect`: ESP32 固件通过协议层参数 + 应用层看门狗双保险，确保 PC 端断连后 4-20 秒内恢复广播
- `desktop-relay-resilient-startup`: Python 端启动时清理 BLE 适配器状态，退出时主动断开，减少旧连接残留

### Modified Capabilities

<!-- No existing specs -->

## Impact

- `firmware/main/ble_server.cpp`: 添加 `set_prefered_conn_params()` 调用和数据看门狗定时器
- `firmware/main/ble_server.h`: 新增 `set_data_watchdog(bool active)` 接口
- `firmware/main/config.h`: 新增常量 `BLE_WATCHDOG_TIMEOUT_MS = 20000`
- `desktop-relay/ble_client.py`: 添加 `cleanup()` 方法，启动时清理适配器
- `desktop-relay/main.py`: 添加 shutdown 流程
