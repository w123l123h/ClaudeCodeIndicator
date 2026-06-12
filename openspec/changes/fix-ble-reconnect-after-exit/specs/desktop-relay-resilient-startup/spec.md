## ADDED Requirements

### Requirement: Graceful BLE disconnect on exit

desktop-relay SHALL 在进程退出时主动执行 BLE 断开流程，通知 ESP32 端连接即将终止。

#### Scenario: KeyboardInterrupt 时主动断开

- **WHEN** 用户按下 Ctrl+C 触发 KeyboardInterrupt
- **THEN** `DesktopRelay.run()` 方法调用 `ble.disconnect()` 并等待断开完成，然后清理 HTTP 服务器和 BLE 资源

#### Scenario: 程序正常结束

- **WHEN** `run()` 方法因配对取消或其他正常原因返回
- **THEN** BLE 连接被主动断开，HTTP 服务器停止

### Requirement: BLE adapter state cleanup on startup

desktop-relay SHALL 在启动 BLE 扫描前给适配器足够时间稳定，并尝试清理任何残留的连接状态。

#### Scenario: 启动前等待适配器稳定

- **WHEN** `scan_and_connect()` 被调用（无论是否已配对）
- **THEN** 方法先等待 500ms 让 OS BLE 适配器进入稳定状态，再开始扫描

#### Scenario: 旧连接对象清理

- **WHEN** `scan_and_connect()` 检测到 `self._client` 已存在但未连接
- **THEN** 调用 `self._client.disconnect()` 并置 `self._client = None`，清除旧状态后再执行新扫描

### Requirement: Resilient reconnect with extended retry

desktop-relay 的 `reconnect_loop` SHALL 在连接失败时等待 ESP32 固件完成断连检测和广播恢复后继续重试。

#### Scenario: 连接失败后持续重试

- **WHEN** `scan_and_connect()` 返回 False（设备未找到或连接失败）
- **THEN** `reconnect_loop` 按指数退避延迟后重试（基础延迟 1s，最大 30s，退避因子 2x），直到连接成功

#### Scenario: 连接成功恢复后重置延迟

- **WHEN** `scan_and_connect()` 返回 True（连接成功）
- **THEN** `reconnect_loop` 将退避延迟重置为基础值
