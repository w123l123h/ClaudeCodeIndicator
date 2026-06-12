## 1. ESP32 固件 — C++ 重构：全局状态移入 BleServer 成员

- [ ] 1.1 将 `g_conn_handle`, `g_msg_cb`, `g_conn_cb` 移入 `ble_server.h` 的 `BleServer` private 成员变量
- [ ] 1.2 将 `gatt_write_cb` 和 `gap_event_cb` 改为 `BleServer` 的 private static 成员函数，通过 `arg` 指针接收 `this`
- [ ] 1.3 更新 `BleServer::init()`, `send_response()`, `set_message_callback()`, `set_connect_callback()` 使用成员变量
- [ ] 1.4 编译固件确认重构无回归

## 2. ESP32 固件 — BLE 协议层参数

- [ ] 2.1 在 `firmware/main/config.h` 中添加常量 `BLE_SUPERVISION_TIMEOUT`（400 = 4000ms）及 conn interval 常量
- [ ] 2.2 在 `BleServer::start_advertise()` 成功后调用 `ble_gap_set_prefered_le_params()` 设置连接参数
- [ ] 2.3 编译烧录固件，串口日志确认参数设置无报错

## 3. ESP32 固件 — 应用层数据看门狗

- [ ] 3.1 在 `BleServer` 中添加成员 `TimerHandle_t m_watchdog`，及 `start_watchdog()` / `reset_watchdog()` / `stop_watchdog()` 方法
- [ ] 3.2 在 `BLE_GAP_EVENT_CONNECT`（status=0）中调用 `start_watchdog()`
- [ ] 3.3 在 `BLE_GAP_EVENT_DISCONNECT` 中调用 `stop_watchdog()`
- [ ] 3.4 在 `gatt_write_cb` 中每次收到数据时调用 `reset_watchdog()`
- [ ] 3.5 看门狗超时回调（static, 通过 `pvTimerID` 取 `this`）中调用 `ble_gap_terminate()` 主动断开
- [ ] 3.6 编译烧录，验证：连接后 kill Python 进程，观察 ESP32 日志确认 20 秒内 watchdog 触发并重启广播

## 4. Python 侧 — 优雅退出

- [ ] 4.1 在 `ble_client.py` 的 `BleClientManager` 中添加 `cleanup()` 方法：断开连接、清理 `_client` 引用
- [ ] 4.2 在 `main.py` 的 `DesktopRelay.run()` 中用 try/finally 确保退出时调用 `ble.cleanup()` 和 `http.stop()`
- [ ] 4.3 KeyboardInterrupt handler 中调用 shutdown 流程

## 5. Python 侧 — 启动适配器清理

- [ ] 5.1 在 `ble_client.py` 的 `scan_and_connect()` 开头添加 500ms 稳定等待
- [ ] 5.2 在 `_connect_and_verify()` 开头加强旧连接对象清理（确保 `self._client` 变为 None 前先调用 disconnect）
- [ ] 5.3 验证：多次 kill + 重启 Python 进程，确认每次都能自动重连 ESP32

## 6. 集成验证

- [ ] 6.1 端到端测试：启动 ESP32 + Python → Ctrl+C Python → 立即重启 Python → 确认无需重启硬件即可重连
- [ ] 6.2 端到端测试：启动 ESP32 + Python → kill -9 Python → 立即重启 Python → 确认 20 秒内自动重连
- [ ] 6.3 端到端测试：正常通信 5 分钟后 kill Python → 重启 Python → 确认 LED 指令正常收发
