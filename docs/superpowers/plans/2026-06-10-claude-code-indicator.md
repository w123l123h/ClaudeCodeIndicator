# Claude Code Indicator — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建 ESP32-C3 + WS2812B LED 物理指示器及配套 Python 软件，实时显示 Claude Code 工作状态。

**Architecture:** 自底向上实现：ESP32 固件 → 桌面中继 → Hook 通知 → 安装/打包。固件用 ESP-IDF 5.5.3 + NimBLE，Python 端用 asyncio + bleak，全部参数通过配置变量管理。

**Tech Stack:** C++ (ESP-IDF 5.5.3), Python 3.13 (bleak, asyncio, tkinter)

---

## 文件结构

```
/start.bat                  ← 启动桌面中继
/install.bat                ← 安装 Hook
/package.py                 ← 打包分发 zip
/firmware/                  ← ESP-IDF 项目
  CMakeLists.txt
  main/
    CMakeLists.txt
    main.cpp
    config.h
    led_controller.h/cpp
    ble_server.h/cpp
    battery_monitor.h/cpp
    power_manager.h/cpp
/desktop-relay/
  main.py
  config.py
  ble_client.py
  tcp_server.py
  pairing.py
/hook-notifier/
  notify.py
/installer/
  install.py
```

---

### Task 1: 项目骨架

**Files:**
- Create: `firmware/CMakeLists.txt`
- Create: `firmware/main/CMakeLists.txt`
- Create: `firmware/main/config.h`
- Create: `firmware/main/main.cpp` (最小入口)
- Create: `desktop-relay/config.py`
- Create: `hook-notifier/notify.py` (空壳)
- Create: `installer/install.py` (空壳)
- Create: `start.bat`
- Create: `install.bat`

- [ ] **Step 1: 创建固件外层 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(claude_code_indicator)
```

- [ ] **Step 2: 创建固件 main/CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "main.cpp" "led_controller.cpp" "ble_server.cpp" "battery_monitor.cpp" "power_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES led_strip nvs_flash bt nimble
)
```

- [ ] **Step 3: 创建固件 config.h，集中定义所有参数宏**

```cpp
#pragma once

// 硬件引脚
#define LED_GPIO            GPIO_NUM_3
#define BATTERY_ADC_CHANNEL ADC1_CHANNEL_0  // GPIO0
#define LED_COUNT           3

// LED 颜色预设 (RGB)
#define LED_COLOR_RED       {255, 0, 0}
#define LED_COLOR_ORANGE    {255, 128, 0}
#define LED_COLOR_GREEN     {0, 255, 0}
#define LED_COLOR_OFF       {0, 0, 0}

// 时序参数 (ms)
#define SELF_TEST_DELAY_MS      200
#define LED_BLINK_PERIOD_MS     500
#define CC_LED_TIMEOUT_MS       600000  // 10 分钟
#define BATTERY_CHECK_INTERVAL_MS  5000
#define SLEEP_IDLE_TIMEOUT_MS   5000
#define SLEEP_WAKEUP_INTERVAL_MS 4000

// 电池阈值 (mV)
#define BATTERY_LOW_THRESHOLD_MV    3500
#define BATTERY_ADC_ATTEN           ADC_ATTEN_DB_11
#define BATTERY_ADC_WIDTH           ADC_WIDTH_BIT_12
#define BATTERY_VOLTAGE_DIVIDER     2.0f  // 分压 50%
#define BATTERY_MOVING_AVG_SAMPLES  10

// BLE 参数
#define BLE_DEVICE_NAME_PREFIX      "ClaudeCodeIndicator"
#define BLE_SERVICE_UUID            "0000ff00-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_UUID               "0000ff01-0000-1000-8000-00805f9b34fb"
#define BLE_CONN_INTERVAL_MIN       30   // ms
#define BLE_CONN_INTERVAL_MAX       50   // ms
#define BLE_SUPERVISION_TIMEOUT     400  // ms*10 = 4000ms
#define BLE_SLAVE_LATENCY           0

// BLE 消息定义
#define MSG_KEEPALIVE       "KEEPALIVE"
#define MSG_WORKING         "WORKING"
#define MSG_WAITING_USER    "WAITING_USER"
#define MSG_COMPLETED       "COMPLETED"
#define MSG_ERROR           "ERROR"
#define MSG_PAIR_CONFIRM    "PAIR_CONFIRM"
#define MSG_PAIR_SUCCESS    "PAIR_SUCCESS"
#define MSG_ALIVE           "ALIVE"
```

- [ ] **Step 4: 创建固件最小 main.cpp（只有入口 + 自检）**

```cpp
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "config.h"
#include "led_controller.h"
#include "ble_server.h"
#include "battery_monitor.h"
#include "power_manager.h"

static const char* TAG = "main";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Claude Code Indicator starting...");

    // 初始化 LED
    LedController led;
    led.init();

    // 自检序列
    ESP_LOGI(TAG, "Self-test: LED1 RED");
    led.set_led(0, LED_COLOR_RED);
    vTaskDelay(pdMS_TO_TICKS(SELF_TEST_DELAY_MS));

    ESP_LOGI(TAG, "Self-test: LED2 ORANGE");
    led.set_led(1, LED_COLOR_ORANGE);
    vTaskDelay(pdMS_TO_TICKS(SELF_TEST_DELAY_MS));

    ESP_LOGI(TAG, "Self-test: LED3 GREEN");
    led.set_led(2, LED_COLOR_GREEN);
    vTaskDelay(pdMS_TO_TICKS(SELF_TEST_DELAY_MS));

    led.all_off();
    ESP_LOGI(TAG, "Self-test complete");

    // TODO: 后续任务接入 BLE、电池、电源管理
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

- [ ] **Step 5: 创建 desktop-relay/config.py**

```python
# BLE
BLE_SERVICE_UUID = "0000ff00-0000-1000-8000-00805f9b34fb"
BLE_CHAR_UUID = "0000ff01-0000-1000-8000-00805f9b34fb"
BLE_SCAN_TIMEOUT = 10  # 秒
BLE_DEVICE_NAME_PREFIX = "ClaudeCodeIndicator"

# TCP
TCP_HOST = "127.0.0.1"
TCP_PORT = 54321

# 保活
KEEPALIVE_INTERVAL = 10  # 秒
KEEPALIVE_RESPONSE_TIMEOUT = 3  # 秒
KEEPALIVE_MAX_FAILURES = 3

# 重连
RECONNECT_BASE_DELAY = 1   # 秒
RECONNECT_MAX_DELAY = 30   # 秒
RECONNECT_BACKOFF_MULTIPLIER = 2

# 配对
DEVICE_CONFIG_FILE = "device.json"

# Python 路径 (用于 .bat)
PYTHON_EXE = r"C:\Users\joe06\AppData\Local\Programs\Python\Python313\python.exe"
```

- [ ] **Step 6: 创建根目录 start.bat**

```bat
@echo off
set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"

tasklist /FI "IMAGENAME eq python.exe" | findstr /I "desktop-relay" >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo Another instance is already running.
    exit /b 0
)

C:\Users\joe06\AppData\Local\Programs\Python\Python313\python.exe "%SCRIPT_DIR%desktop-relay\main.py"
pause
```

- [ ] **Step 7: 创建根目录 install.bat**

```bat
@echo off
set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"

C:\Users\joe06\AppData\Local\Programs\Python\Python313\python.exe "%SCRIPT_DIR%installer\install.py"
pause
```

- [ ] **Step 8: 创建 hook-notifier/notify.py 空壳 + install.py 空壳**

`hook-notifier/notify.py`:
```python
# Claude Code Hook 通知脚本
# 在后续任务中实现具体逻辑
import sys
print("notify placeholder", file=sys.stderr)
```

`installer/install.py`:
```python
# Claude Code Indicator 安装程序
# 在后续任务中实现具体逻辑
print("install placeholder")
```

- [ ] **Step 9: 验证**

```bash
ls -R
```

Expected: 目录结构符合设计文档。

---

### Task 2: ESP32 LED 控制器

**Files:**
- Create: `firmware/main/led_controller.h`
- Create: `firmware/main/led_controller.cpp`

- [ ] **Step 1: 创建 led_controller.h**

```cpp
#pragma once

#include <stdint.h>

struct LedColor {
    uint8_t r, g, b;
};

class LedController {
public:
    void init();
    void set_led(int index, LedColor color);
    void set_led(int index, uint8_t r, uint8_t g, uint8_t b);
    void all_off();
    void all_on(LedColor color);

private:
    static constexpr int LED_COUNT = 3;
    // led_strip 句柄将在 init 中创建
};
```

- [ ] **Step 2: 创建 led_controller.cpp**

```cpp
#include "led_controller.h"
#include "led_strip.h"
#include "esp_log.h"

static const char* TAG = "led";

static led_strip_handle_t g_strip = nullptr;

void LedController::init()
{
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = 3;  // GPIO3
    strip_config.max_leds = LED_COUNT;
    strip_config.led_pixel_format = LED_PIXEL_FORMAT_GRB;
    strip_config.led_model = LED_MODEL_WS2812;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10 * 1000 * 1000; // 10MHz

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &g_strip));
    ESP_ERROR_CHECK(led_strip_clear(g_strip));
    ESP_LOGI(TAG, "Initialized %d WS2812B LEDs on GPIO3", LED_COUNT);
}

void LedController::set_led(int index, LedColor color)
{
    if (!g_strip || index < 0 || index >= LED_COUNT) return;
    ESP_ERROR_CHECK(led_strip_set_pixel(g_strip, index, color.r, color.g, color.b));
    ESP_ERROR_CHECK(led_strip_refresh(g_strip));
}

void LedController::set_led(int index, uint8_t r, uint8_t g, uint8_t b)
{
    set_led(index, LedColor{r, g, b});
}

void LedController::all_off()
{
    if (!g_strip) return;
    ESP_ERROR_CHECK(led_strip_clear(g_strip));
    ESP_LOGI(TAG, "All LEDs off");
}

void LedController::all_on(LedColor color)
{
    for (int i = 0; i < LED_COUNT; i++) {
        set_led(i, color);
    }
}
```

- [ ] **Step 3: 更新 main.cpp，接入 LedController**

用 Task 1 Step 4 中的代码（已包含 led_controller.h 引入），编译验证放在 Task 7。

---

### Task 3: ESP32 BLE Server

**Files:**
- Create: `firmware/main/ble_server.h`
- Create: `firmware/main/ble_server.cpp`
- Modify: `firmware/main/main.cpp` (接入 BLE)

- [ ] **Step 1: 创建 ble_server.h**

```cpp
#pragma once

#include <functional>
#include <string>

// 消息回调类型: void on_message(const std::string& msg)
using BleMessageCallback = std::function<void(const std::string&)>;

class BleServer {
public:
    void init();
    void start_advertise();
    void set_message_callback(BleMessageCallback cb);
    void send_response(const std::string& msg);
    int get_mtu();

private:
    BleMessageCallback m_callback;
    std::string m_device_name;
    // NimBLE 内部句柄在 cpp 中定义
};
```

- [ ] **Step 2: 创建 ble_server.cpp**

```cpp
#include "ble_server.h"
#include "config.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <cstring>

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char* TAG = "ble";

// NimBLE 全局变量
static uint16_t g_conn_handle = 0;
static uint8_t g_own_addr_type;
static BleMessageCallback g_msg_cb = nullptr;

// GATT characteristic 定义
static const ble_uuid128_t g_svc_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00);
static const ble_uuid128_t g_char_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x01, 0xff, 0x00, 0x00);

// GATT 写回调
static int gatt_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && g_msg_cb) {
        std::string msg((char*)ctxt->om->om_data, ctxt->om->om_len);
        // 去除尾部换行/空白
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
            msg.pop_back();
        }
        ESP_LOGI(TAG, "Received: %s", msg.c_str());
        g_msg_cb(msg);
    }
    return 0;
}

// GAP 事件回调
static int gap_event_cb(struct ble_gap_event* event, void* arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            g_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected, handle=%d", g_conn_handle);
        } else {
            ESP_LOGW(TAG, "Connection failed, restarting advertise");
            ble_svc_gap_device_start();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected, restarting advertise");
        g_conn_handle = 0;
        ble_svc_gap_device_start();
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: %d", event->mtu.mtu);
        break;
    default:
        break;
    }
    return 0;
}

void BleServer::init()
{
    // 生成设备名称: ClaudeCodeIndicator_<MAC>
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    char name[64];
    snprintf(name, sizeof(name), "%s_%02X%02X%02X%02X%02X%02X",
             BLE_DEVICE_NAME_PREFIX,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    m_device_name = name;

    nimble_port_init();

    // 配置 GAP
    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_svc_gap_device_name_set(m_device_name.c_str());

    // 注册 GATT service
    ble_gatts_count_cfg(ble_svc_gatt_chr_cfg());
    ble_gatts_add_svcs((ble_gatt_svc_def[]){
        {
            .type = BLE_GATT_SVC_TYPE_PRIMARY,
            .uuid = &g_svc_uuid.u,
            .characteristics = (ble_gatt_chr_def[]){
                {
                    .uuid = &g_char_uuid.u,
                    .access_cb = gatt_write_cb,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                },
                { 0 }
            },
        },
        { 0 }
    });

    // 配置连接参数
    ble_gap_preferred_conn_params_t conn_params = {};
    conn_params.conn_interval_min = BLE_CONN_INTERVAL_MIN;
    conn_params.conn_interval_max = BLE_CONN_INTERVAL_MAX;
    conn_params.supervision_timeout = BLE_SUPERVISION_TIMEOUT;
    conn_params.slave_latency = BLE_SLAVE_LATENCY;
    ble_gap_set_preferred_conn_params(&conn_params);

    // 注册 GAP 事件回调
    ble_gap_event_register(gap_event_cb, nullptr);

    ESP_LOGI(TAG, "BLE initialized, device name: %s", m_device_name.c_str());
}

void BleServer::start_advertise()
{
    ble_svc_gap_device_start();
    ESP_LOGI(TAG, "Advertising started");
}

void BleServer::set_message_callback(BleMessageCallback cb)
{
    g_msg_cb = cb;
}

void BleServer::send_response(const std::string& msg)
{
    if (g_conn_handle == 0) return;
    std::string data = msg + "\n";
    struct os_mbuf* om = ble_hs_mbuf_from_flat(data.c_str(), data.size());
    ble_gattc_notify_custom(g_conn_handle, ble_svc_gatt_chr_handle(), om);
    ESP_LOGI(TAG, "Sent: %s", msg.c_str());
}
```

- [ ] **Step 3: 更新 main.cpp 接入 BLE**

```cpp
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Claude Code Indicator starting...");

    // LED 初始化 + 自检
    LedController led;
    led.init();
    // ... 自检代码保持不变 ...

    // BLE 初始化
    BleServer ble;
    ble.init();
    ble.set_message_callback([&led](const std::string& msg) {
        // 消息路由：后续任务实现 LED 状态映射
        ESP_LOGI(TAG, "BLE message: %s", msg.c_str());
    });
    ble.start_advertise();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

### Task 4: ESP32 消息路由与 LED 状态机

**Files:**
- Modify: `firmware/main/main.cpp` (消息路由 + LED 状态表 + 超时逻辑)

- [ ] **Step 1: 更新 main.cpp — 完整的消息处理与 LED 状态机**

```cpp
#include <stdio.h>
#include <map>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "config.h"
#include "led_controller.h"
#include "ble_server.h"
#include "battery_monitor.h"
#include "power_manager.h"

static const char* TAG = "main";

// 全局对象
static LedController g_led;
static BleServer g_ble;
static TimerHandle_t g_cc_timeout_timer = nullptr;
static bool g_battery_low = false;
static bool is_waiting_user = false;

// 清除所有 CC 触发的 LED 状态（超时回调）
static void cc_timeout_callback(TimerHandle_t timer)
{
    ESP_LOGI(TAG, "CC LED timeout — resetting LEDs");
    is_waiting_user = false;
    if (!g_battery_low) {
        g_led.set_led(0, LED_COLOR_OFF);  // LED1 off
    }
    g_led.set_led(1, LED_COLOR_OFF);  // LED2 off
    g_led.set_led(2, LED_COLOR_OFF);  // LED3 off
}

// 启动 CC LED 超时定时器
static void start_cc_timeout()
{
    if (g_cc_timeout_timer) {
        xTimerReset(g_cc_timeout_timer, 0);
    } else {
        g_cc_timeout_timer = xTimerCreate(
            "cc_timeout",
            pdMS_TO_TICKS(CC_LED_TIMEOUT_MS),
            pdFALSE,  // 单次
            nullptr,
            cc_timeout_callback
        );
    }
    if (g_cc_timeout_timer) {
        xTimerStart(g_cc_timeout_timer, 0);
    }
}

// BLE 消息处理
static void handle_ble_message(const std::string& msg)
{
    if (msg == MSG_KEEPALIVE) {
        g_ble.send_response(MSG_ALIVE);
        return;
    }

    if (msg == MSG_WORKING) {
        is_waiting_user = false;
        g_led.set_led(1, LED_COLOR_ORANGE);  // LED2 橙常亮
        if (!g_battery_low) g_led.set_led(0, LED_COLOR_OFF);
        g_led.set_led(2, LED_COLOR_OFF);
        start_cc_timeout();
    }
    else if (msg == MSG_WAITING_USER) {
        is_waiting_user = true;  // 驱动主循环闪烁
        if (!g_battery_low) g_led.set_led(0, LED_COLOR_OFF);
        g_led.set_led(2, LED_COLOR_OFF);
        start_cc_timeout();
    }
    else if (msg == MSG_COMPLETED) {
        is_waiting_user = false;
        g_led.set_led(2, LED_COLOR_GREEN);  // LED3 绿常亮
        g_led.set_led(1, LED_COLOR_OFF);
        if (!g_battery_low) g_led.set_led(0, LED_COLOR_OFF);
        start_cc_timeout();
    }
    else if (msg == MSG_ERROR) {
        is_waiting_user = false;
        g_led.set_led(0, LED_COLOR_RED);    // LED1 红常亮
        g_led.set_led(1, LED_COLOR_OFF);
        g_led.set_led(2, LED_COLOR_OFF);
        start_cc_timeout();
    }
    else if (msg == MSG_PAIR_CONFIRM) {
        g_led.all_on(LED_COLOR_GREEN);       // 全部绿
        if (g_cc_timeout_timer) xTimerStop(g_cc_timeout_timer, 0);
    }
    else if (msg == MSG_PAIR_SUCCESS) {
        g_led.all_off();
        g_battery_low = false;
        if (g_cc_timeout_timer) xTimerStop(g_cc_timeout_timer, 0);
    }
}

// 电池低电量回调
static void battery_low_callback(bool low)
{
    g_battery_low = low;
    if (low) {
        g_led.set_led(0, LED_COLOR_RED);  // LED1 红 (永久保持)
        ESP_LOGW(TAG, "Battery LOW (<= 3.5V)");
    } else if (g_cc_timeout_timer == nullptr || !xTimerIsTimerActive(g_cc_timeout_timer)) {
        g_led.set_led(0, LED_COLOR_OFF);
        ESP_LOGI(TAG, "Battery OK (> 3.5V)");
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Claude Code Indicator starting...");

    g_led.init();

    // 自检序列
    g_led.set_led(0, LED_COLOR_RED);
    vTaskDelay(pdMS_TO_TICKS(SELF_TEST_DELAY_MS));
    g_led.set_led(1, LED_COLOR_ORANGE);
    vTaskDelay(pdMS_TO_TICKS(SELF_TEST_DELAY_MS));
    g_led.set_led(2, LED_COLOR_GREEN);
    vTaskDelay(pdMS_TO_TICKS(SELF_TEST_DELAY_MS));
    g_led.all_off();

    // BLE
    g_ble.init();
    g_ble.set_message_callback(handle_ble_message);
    g_ble.start_advertise();

    // 电池监测
    BatteryMonitor battery;
    battery.set_callback(battery_low_callback);
    battery.start();

    // 电源管理
    PowerManager power;
    power.start();

    // 主循环：处理 LED 闪烁（WAITING_USER）
    bool blink_state = false;
    bool is_waiting_user = false;
    // 注：is_waiting_user 需在 handle_ble_message 中设置
    // 为简洁此处略去互斥，后续可改为 event-driven

    while (true) {
        if (is_waiting_user) {
            blink_state = !blink_state;
            if (blink_state) {
                g_led.set_led(1, LED_COLOR_ORANGE);
            } else {
                g_led.set_led(1, LED_COLOR_OFF);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(LED_BLINK_PERIOD_MS));
    }
}
```

闪烁由 `is_waiting_user` 标志驱动主循环切换 LED2 开关，500ms 周期；收到其他 CC 消息或超时时清零。

---

### Task 5: ESP32 电池监测

**Files:**
- Create: `firmware/main/battery_monitor.h`
- Create: `firmware/main/battery_monitor.cpp`

- [ ] **Step 1: 创建 battery_monitor.h**

```cpp
#pragma once

#include <functional>

using BatteryCallback = std::function<void(bool low)>;

class BatteryMonitor {
public:
    void set_callback(BatteryCallback cb);
    void start();

private:
    BatteryCallback m_callback;
    float read_voltage();
};
```

- [ ] **Step 2: 创建 battery_monitor.cpp**

```cpp
#include "battery_monitor.h"
#include "config.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "battery";

static adc_oneshot_unit_handle_t g_adc_handle = nullptr;
static adc_cali_handle_t g_cali_handle = nullptr;
static BatteryCallback g_battery_cb = nullptr;

void BatteryMonitor::set_callback(BatteryCallback cb)
{
    g_battery_cb = cb;
}

float BatteryMonitor::read_voltage()
{
    // 移动平均采样
    int sum = 0;
    for (int i = 0; i < BATTERY_MOVING_AVG_SAMPLES; i++) {
        int raw;
        adc_oneshot_read(g_adc_handle, BATTERY_ADC_CHANNEL, &raw);
        sum += raw;
    }
    int avg_raw = sum / BATTERY_MOVING_AVG_SAMPLES;

    // 校准 + 换算电压 (mV)
    int voltage_mv;
    adc_cali_raw_to_voltage(g_cali_handle, avg_raw, &voltage_mv);

    // 分压换算实际电压
    float actual_voltage = voltage_mv * BATTERY_VOLTAGE_DIVIDER / 1000.0f;
    return actual_voltage;
}

static void battery_task(void* arg)
{
    bool last_low = false;
    while (true) {
        float voltage = static_cast<BatteryMonitor*>(arg)->read_voltage();
        bool low = (voltage <= (BATTERY_LOW_THRESHOLD_MV / 1000.0f));

        ESP_LOGI(TAG, "Battery: %.2fV, low=%d", voltage, low);

        if (low != last_low && g_battery_cb) {
            g_battery_cb(low);
        }
        last_low = low;

        vTaskDelay(pdMS_TO_TICKS(BATTERY_CHECK_INTERVAL_MS));
    }
}

void BatteryMonitor::start()
{
    // ADC1 初始化
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_new_unit(&init_cfg, &g_adc_handle);

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_WIDTH,
    };
    adc_oneshot_config_channel(g_adc_handle, BATTERY_ADC_CHANNEL, &chan_cfg);

    // 校准
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_WIDTH,
    };
    adc_cali_create_scheme_line_fitting(&cali_cfg, &g_cali_handle);

    xTaskCreate(battery_task, "battery", 4096, this, 5, nullptr);
    ESP_LOGI(TAG, "Battery monitor started (ADC1 CH%d)", BATTERY_ADC_CHANNEL);
}
```

---

### Task 6: ESP32 电源管理 (Light-sleep)

**Files:**
- Create: `firmware/main/power_manager.h`
- Create: `firmware/main/power_manager.cpp`

- [ ] **Step 1: 创建 power_manager.h**

```cpp
#pragma once

class PowerManager {
public:
    void start();
    void on_activity();
};
```

- [ ] **Step 2: 创建 power_manager.cpp**

```cpp
#include "power_manager.h"
#include "config.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

static const char* TAG = "power";

static TimerHandle_t g_idle_timer = nullptr;
static bool g_active = false;

// BLE 活动时调用
void PowerManager::on_activity()
{
    g_active = true;
    if (g_idle_timer) {
        xTimerReset(g_idle_timer, 0);
    }
}

static void idle_timeout_cb(TimerHandle_t timer)
{
    g_active = false;
    ESP_LOGI(TAG, "Entering light sleep (wake every %d ms)", SLEEP_WAKEUP_INTERVAL_MS);

    // 配置定时器唤醒
    esp_sleep_enable_timer_wakeup(SLEEP_WAKEUP_INTERVAL_MS * 1000);

    // BLE 活动也会唤醒（由 NimBLE 处理）
    // 进入 light sleep
    esp_light_sleep_start();

    ESP_LOGI(TAG, "Woke up from light sleep");
}

static void power_task(void* arg)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // 任务仅用于保持 FreeRTOS tick
    }
}

void PowerManager::start()
{
    g_idle_timer = xTimerCreate(
        "idle_timer",
        pdMS_TO_TICKS(SLEEP_IDLE_TIMEOUT_MS),
        pdFALSE,
        nullptr,
        idle_timeout_cb
    );
    if (g_idle_timer) {
        xTimerStart(g_idle_timer, 0);
    }

    xTaskCreate(power_task, "power_mgr", 2048, nullptr, 3, nullptr);
    ESP_LOGI(TAG, "Power manager started");
}
```

---

### Task 7: ESP32 编译验证

使用 `/idf-build` 技能编译固件，修复编译错误。

- [ ] **Step 1: 编译固件**

```
/idf-build firmware
```

- [ ] **Step 2: 修复所有编译错误，确认 build 成功**
- [ ] **Step 3: 烧录到 ESP32-C3 验证自检序列和 BLE 广播**

---

### Task 8: 电脑端主程序 — BLE 客户端

**Files:**
- Create: `desktop-relay/ble_client.py`

- [ ] **Step 1: 创建 ble_client.py**

```python
import asyncio
import logging
from bleak import BleakScanner, BleakClient

from config import (
    BLE_SERVICE_UUID, BLE_CHAR_UUID, BLE_SCAN_TIMEOUT,
    BLE_DEVICE_NAME_PREFIX, DEVICE_CONFIG_FILE,
    RECONNECT_BASE_DELAY, RECONNECT_MAX_DELAY, RECONNECT_BACKOFF_MULTIPLIER,
)

logger = logging.getLogger(__name__)


class BleClientManager:
    def __init__(self):
        self._client: BleakClient | None = None
        self._char = None
        self._connected = False
        self._message_callback = None
        self._target_device = None

    def set_message_callback(self, cb):
        self._message_callback = cb

    @property
    def is_connected(self):
        return self._connected and self._client and self._client.is_connected

    def _get_saved_device(self):
        import os
        if os.path.exists(DEVICE_CONFIG_FILE):
            import json
            with open(DEVICE_CONFIG_FILE, 'r') as f:
                data = json.load(f)
                return data.get('device_name')
        return None

    def _save_device(self, name):
        import json
        with open(DEVICE_CONFIG_FILE, 'w') as f:
            json.dump({'device_name': name}, f)

    async def scan_and_connect(self):
        saved = self._get_saved_device()

        if saved:
            logger.info(f"Looking for saved device: {saved}")
            device = await self._scan_for_device(saved)
        else:
            logger.info("No saved device, scanning all...")
            device = await self._scan_for_device(BLE_DEVICE_NAME_PREFIX)

        if device:
            self._target_device = device.name
            await self._connect(device.address)
            return True
        return False

    async def _scan_for_device(self, name_filter):
        logger.info(f"Scanning for '{name_filter}'...")
        devices = await BleakScanner.discover(timeout=BLE_SCAN_TIMEOUT)

        for d in devices:
            if d.name and name_filter in d.name:
                logger.info(f"Found: {d.name} ({d.address})")
                return d
        return None

    async def _connect(self, address):
        self._client = BleakClient(address, disconnected_callback=self._on_disconnect)
        await self._client.connect()
        self._connected = True
        logger.info(f"Connected to {address}")

        # 发现 service/characteristic
        for service in self._client.services:
            if service.uuid == BLE_SERVICE_UUID:
                for char in service.characteristics:
                    if char.uuid == BLE_CHAR_UUID:
                        self._char = char
                        # 启用 notify
                        await self._client.start_notify(char.uuid, self._on_notify)
                        logger.info("Characteristic found, notify enabled")
                        break

    def _on_disconnect(self, client):
        logger.warning("BLE disconnected")
        self._connected = False

    def _on_notify(self, sender, data):
        msg = data.decode().strip()
        logger.info(f"Notify: {msg}")
        if self._message_callback:
            self._message_callback(msg)

    async def send(self, message):
        if self._client and self._char:
            data = (message + '\n').encode()
            await self._client.write_gatt_char(self._char.uuid, data)
            logger.info(f"Sent: {message}")

    async def disconnect(self):
        if self._client:
            await self._client.disconnect()
            self._connected = False

    async def reconnect_loop(self):
        delay = RECONNECT_BASE_DELAY
        while True:
            if not self.is_connected:
                logger.info(f"Reconnecting in {delay}s...")
                await asyncio.sleep(delay)
                success = await self.scan_and_connect()
                if success:
                    delay = RECONNECT_BASE_DELAY
                else:
                    delay = min(delay * RECONNECT_BACKOFF_MULTIPLIER, RECONNECT_MAX_DELAY)
            else:
                await asyncio.sleep(1)
```

---

### Task 9: 电脑端主程序 — TCP 服务器

**Files:**
- Create: `desktop-relay/tcp_server.py`

- [ ] **Step 1: 创建 tcp_server.py**

```python
import asyncio
import logging

from config import TCP_HOST, TCP_PORT

logger = logging.getLogger(__name__)

VALID_MESSAGES = {"WORKING", "WAITING_USER", "COMPLETED", "ERROR"}


class TcpRelayServer:
    def __init__(self, ble_send_callback):
        self._ble_send = ble_send_callback
        self._server = None

    async def start(self):
        self._server = await asyncio.start_server(
            self._handle_client, TCP_HOST, TCP_PORT
        )
        logger.info(f"TCP server listening on {TCP_HOST}:{TCP_PORT}")

    async def _handle_client(self, reader, writer):
        try:
            data = await asyncio.wait_for(reader.read(1024), timeout=5.0)
            msg = data.decode().strip()
            peer = writer.get_extra_info('peername')
            logger.info(f"TCP from {peer}: {msg}")

            if msg in VALID_MESSAGES:
                await self._ble_send(msg)
            else:
                logger.warning(f"Unknown TCP message: {msg}")
        except asyncio.TimeoutError:
            pass
        except Exception as e:
            logger.error(f"TCP error: {e}")
        finally:
            writer.close()
            await writer.wait_closed()

    async def stop(self):
        if self._server:
            self._server.close()
            await self._server.wait_closed()
```

---

### Task 10: 电脑端主程序 — 配对与保活

**Files:**
- Create: `desktop-relay/pairing.py`
- Create: `desktop-relay/main.py`

- [ ] **Step 1: 创建 pairing.py**

```python
import tkinter as tk
from tkinter import messagebox


def show_pairing_dialog(device_name):
    root = tk.Tk()
    root.withdraw()

    result = messagebox.askyesno(
        "Claude Code Indicator — 配对确认",
        f"已连接到设备: {device_name}\n\n"
        "请确认指示器上 3 个 LED 是否都亮绿色？\n\n"
        "选择「是」完成配对\n选择「否」取消"
    )

    root.destroy()
    return result
```

- [ ] **Step 2: 创建 main.py**

```python
import asyncio
import logging
import sys
import os

# 确保工作目录为项目根
os.chdir(os.path.dirname(os.path.abspath(__file__)))

from config import (
    KEEPALIVE_INTERVAL, KEEPALIVE_RESPONSE_TIMEOUT, KEEPALIVE_MAX_FAILURES,
    DEVICE_CONFIG_FILE,
)
from ble_client import BleClientManager
from tcp_server import TcpRelayServer
from pairing import show_pairing_dialog

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(name)s] %(levelname)s: %(message)s'
)
logger = logging.getLogger("main")


class DesktopRelay:
    def __init__(self):
        self.ble = BleClientManager()
        self.tcp = TcpRelayServer(self.ble.send)
        self.alive_event = asyncio.Event()
        self.keepalive_failures = 0

        self.ble.set_message_callback(self._on_ble_message)

    def _on_ble_message(self, msg):
        logger.info(f"BLE notify: {msg}")
        if msg == "ALIVE":
            self.alive_event.set()

    async def _pairing_flow(self):
        """首次配对流程"""
        saved = self.ble._get_saved_device()
        if saved:
            logger.info(f"Already paired with: {saved}")
            return True

        # 连接后发送 PAIR_CONFIRM
        await self.ble.send("PAIR_CONFIRM")
        await asyncio.sleep(0.5)

        # 弹出对话框
        loop = asyncio.get_event_loop()
        confirmed = await loop.run_in_executor(
            None, show_pairing_dialog, self.ble._target_device
        )

        if confirmed:
            self.ble._save_device(self.ble._target_device)
            await self.ble.send("PAIR_SUCCESS")
            logger.info("Pairing successful")
            return True
        else:
            await self.ble.disconnect()
            logger.info("Pairing cancelled by user")
            return False

    async def _keepalive_task(self):
        while True:
            await asyncio.sleep(KEEPALIVE_INTERVAL)
            if not self.ble.is_connected:
                continue

            try:
                self.alive_event.clear()
                await self.ble.send("KEEPALIVE")

                await asyncio.wait_for(
                    self.alive_event.wait(),
                    timeout=KEEPALIVE_RESPONSE_TIMEOUT
                )
                self.keepalive_failures = 0
                logger.debug("Keepalive OK")
            except asyncio.TimeoutError:
                self.keepalive_failures += 1
                logger.warning(f"Keepalive timeout ({self.keepalive_failures}/{KEEPALIVE_MAX_FAILURES})")

                if self.keepalive_failures >= KEEPALIVE_MAX_FAILURES:
                    logger.error("Keepalive failed — forcing reconnect")
                    await self.ble.disconnect()
                    self.keepalive_failures = 0

    async def run(self):
        # 扫描 + 连接
        connected = await self.ble.scan_and_connect()
        if not connected:
            logger.error("No device found")
            return

        # 配对
        paired = await self._pairing_flow()
        if not paired:
            return

        # 启动 TCP
        await self.tcp.start()

        # 并发：保活 + 重连监视
        await asyncio.gather(
            self._keepalive_task(),
            self.ble.reconnect_loop(),
        )


if __name__ == "__main__":
    relay = DesktopRelay()
    try:
        asyncio.run(relay.run())
    except KeyboardInterrupt:
        logger.info("Shutting down...")
```

---

### Task 11: Hook 通知脚本

**Files:**
- Modify: `hook-notifier/notify.py` (完整实现)

- [ ] **Step 1: 实现 notify.py**

```python
#!/usr/bin/env python3
"""
Claude Code Hook 通知脚本
由 Claude Code settings.json hooks 调用，将状态事件通过 TCP 发送给桌面中继。
"""
import socket
import sys
import os

TCP_HOST = "127.0.0.1"
TCP_PORT = 54321
TIMEOUT = 2  # 秒


def send_message(msg):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(TIMEOUT)
        sock.connect((TCP_HOST, TCP_PORT))
        sock.sendall((msg + "\n").encode())
        sock.close()
    except (socket.timeout, ConnectionRefusedError, OSError):
        # 主程序未运行，静默忽略
        pass


def main():
    if len(sys.argv) < 2:
        sys.exit(0)

    event_type = sys.argv[1]

    # 事件 → 消息映射
    event_map = {
        "tool_start": "WORKING",
        "waiting_user": "WAITING_USER",
        "complete": "COMPLETED",
        "error": "ERROR",
    }

    msg = event_map.get(event_type)
    if msg:
        send_message(msg)


if __name__ == "__main__":
    main()
```

---

### Task 12: 安装程序

**Files:**
- Modify: `installer/install.py` (完整实现)

- [ ] **Step 1: 实现 install.py**

```python
#!/usr/bin/env python3
"""
Claude Code Indicator 安装程序
1. 安装 Python 依赖 (bleak)
2. 配置 Claude Code hooks
"""
import subprocess
import sys
import json
import os
from pathlib import Path

PYTHON_EXE = r"C:\Users\joe06\AppData\Local\Programs\Python\Python313\python.exe"
SCRIPT_DIR = Path(__file__).parent.parent.resolve()
HOOK_SCRIPT = str(SCRIPT_DIR / "hook-notifier" / "notify.py")


def install_deps():
    """安装所需 Python 依赖"""
    print("Checking dependencies...")
    try:
        import bleak
        print("  bleak: installed")
    except ImportError:
        print("  bleak: installing...")
        subprocess.check_call(
            [PYTHON_EXE, "-m", "pip", "install", "bleak"],
            stdout=sys.stdout, stderr=sys.stderr
        )
        print("  bleak: installed successfully")


def setup_hooks():
    """配置 Claude Code hooks"""
    settings_path = Path.home() / ".claude" / "settings.json"

    # 读取现有配置
    settings = {}
    if settings_path.exists():
        with open(settings_path, 'r') as f:
            settings = json.load(f)

    # 确保 hooks 段存在
    if "hooks" not in settings:
        settings["hooks"] = {}

    # 添加 hook 配置
    settings["hooks"]["PostToolUse"] = [
        {
            "matcher": "*",
            "hooks": [
                {
                    "type": "command",
                    "command": f'"{PYTHON_EXE}" "{HOOK_SCRIPT}" tool_start'
                }
            ]
        }
    ]

    # 写入
    os.makedirs(settings_path.parent, exist_ok=True)
    with open(settings_path, 'w') as f:
        json.dump(settings, f, indent=2, ensure_ascii=False)

    print(f"Hooks configured in: {settings_path}")


def verify():
    """验证安装"""
    errors = []

    # 检查 bleak
    try:
        import bleak
        print("  [OK] bleak available")
    except ImportError:
        errors.append("bleak not installed")

    # 检查 settings.json
    settings_path = Path.home() / ".claude" / "settings.json"
    if settings_path.exists():
        with open(settings_path) as f:
            data = json.load(f)
            if "hooks" in data:
                print(f"  [OK] settings.json configured")
            else:
                errors.append("hooks not found in settings.json")
    else:
        errors.append("settings.json not found")

    if errors:
        print(f"\nWARNING: {len(errors)} issue(s):")
        for e in errors:
            print(f"  - {e}")
    else:
        print("\nInstallation successful!")


if __name__ == "__main__":
    print("=== Claude Code Indicator Installer ===\n")
    install_deps()
    setup_hooks()
    verify()
```

---

### Task 13: 打包脚本

**Files:**
- Create: `package.py`

- [ ] **Step 1: 实现 package.py**

```python
#!/usr/bin/env python3
"""
打包脚本：将客户分发文件打包为 ClaudeCodeIndicator.zip
解压后 start.bat 和 install.bat 在根目录，其余保持子目录结构。
"""
import zipfile
import os
from pathlib import Path

ROOT = Path(__file__).parent
OUTPUT = ROOT / "ClaudeCodeIndicator.zip"

INCLUDE_FILES = [
    "start.bat",
    "install.bat",
]

INCLUDE_DIRS = [
    "desktop-relay",
    "hook-notifier",
    "installer",
]


def package():
    with zipfile.ZipFile(OUTPUT, 'w', zipfile.ZIP_DEFLATED) as zf:
        for f in INCLUDE_FILES:
            path = ROOT / f
            if path.exists():
                zf.write(path, f)
                print(f"  + {f}")

        for d in INCLUDE_DIRS:
            dir_path = ROOT / d
            if not dir_path.exists():
                continue
            for file in dir_path.rglob("*"):
                if file.is_file():
                    arcname = str(file.relative_to(ROOT))
                    zf.write(file, arcname)
                    print(f"  + {arcname}")

    print(f"\nPackaged: {OUTPUT} ({os.path.getsize(OUTPUT)} bytes)")


if __name__ == "__main__":
    package()
```

---

### Task 14: 端到端验证

- [ ] **Step 1: 烧录固件，确认自检序列正常**
- [ ] **Step 2: 启动 desktop-relay/main.py，验证 BLE 扫描/连接/配对**
- [ ] **Step 3: 用 netcat 手动发送消息到 TCP 54321，确认 LED 状态切换**

```bash
echo "WORKING" | nc 127.0.0.1 54321
echo "COMPLETED" | nc 127.0.0.1 54321
echo "ERROR" | nc 127.0.0.1 54321
```

- [ ] **Step 4: 等待 10 分钟确认 CC LED 超时复位**
- [ ] **Step 5: 运行 install.py 确认 Hook 配置写入 settings.json**
- [ ] **Step 6: 运行 package.py 确认 zip 生成，解压验证结构正确**
- [ ] **Step 7: 低电量模拟: 调整阈值 > 当前电压，确认红色 LED 常亮** (电池告警不受超时影响)
