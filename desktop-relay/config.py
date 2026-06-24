# BLE
BLE_SERVICE_UUID = "0000ff00-0000-1000-8000-00805f9b34fb"
BLE_CHAR_UUID = "0000ff01-0000-1000-8000-00805f9b34fb"
BLE_SCAN_TIMEOUT = 10  # 秒
BLE_DEVICE_NAME_PREFIX = "ClaudeCodeIndicator"

# HTTP
HTTP_HOST = "127.0.0.1"
HTTP_PORT = 54321

# 保活
KEEPALIVE_INTERVAL = 10  # 秒
KEEPALIVE_RESPONSE_TIMEOUT = 3  # 秒
KEEPALIVE_MAX_FAILURES = 3

# 重连
CONNECT_TIMEOUT = 5.0               # BleakClient 连接超时（秒）
RECONNECT_BASE_DELAY = 1            # 重连失败基础延迟（秒）
RECONNECT_MAX_DELAY = 30            # 重连最大延迟（秒）
RECONNECT_BACKOFF_MULTIPLIER = 2    # 指数退避倍数

# 配对
DEVICE_CONFIG_FILE = "device.json"

# 事件 → LED 指令映射（由 main.py 的 _translate_event 使用）
EVENT_LED_MAP = {
    "WORKING": {
        "leds": [
            {"id": 1, "on": True,  "r": 10, "g": 5, "b": 0, "timeout": 30},
            {"id": 2, "on": False},
        ]
    },
    "WAITING_USER": {
        "leds": [
            {"id": 1, "on": True,  "r": 10, "g": 5, "b": 0, "blink": True, "blink_ms": 300, "timeout": 30},
            {"id": 2, "on": False},
        ]
    },
    "COMPLETED": {
        "leds": [
            {"id": 1, "on": False},
            {"id": 2, "on": True,  "r": 0, "g": 10, "b": 0, "timeout": 30},
        ]
    },
    "ERROR": {
        "leds": [
            {"id": 0, "on": True,  "r": 1, "g": 0, "b": 0, "timeout": 30},
            {"id": 1, "on": False},
            {"id": 2, "on": False},
        ]
    },
}

# 已保存设备直连重试间隔（秒）
SAVED_RETRY_INTERVAL = 3
# 扫描全部失败后重试间隔（秒）
SCAN_RETRY_INTERVAL = 5
