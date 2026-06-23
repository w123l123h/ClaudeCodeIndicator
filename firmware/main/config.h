#pragma once

#include <stdint.h>
#include "hal/gpio_types.h"
#include "hal/adc_types.h"

// 硬件引脚
#define LED_GPIO            GPIO_NUM_3
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0  // GPIO0
#define CHARGE_DETECT_GPIO  GPIO_NUM_1    // GPIO1 充电检测
#define WS2812_LED_COUNT    3

// LED 颜色预设 (RGB)
#define LED_COLOR_RED       {20, 0, 0}
#define LED_COLOR_ORANGE    {20, 10, 0}
#define LED_COLOR_YELLOW    {20, 10, 0}
#define LED_COLOR_GREEN     {0, 20, 0}
#define LED_COLOR_CYAN      {0, 20, 20}
#define LED_COLOR_BLUE      {0, 0, 20}
#define LED_COLOR_PURPLE    {10, 0, 20}
#define LED_COLOR_OFF       {0, 0, 0}

// RGB 颜色结构体
struct LedColor {
    uint8_t r, g, b;
};

// LED 状态颜色变量 — 定义在 config.cpp，可在运行时修改
extern LedColor led_color_advertising;
extern LedColor led_color_battery_low;
extern LedColor led_color_pair_confirm;
extern LedColor led_color_off;

// 时序参数 (ms)
#define SELF_TEST_DELAY_MS       500
#define LED_BLINK_PERIOD_MS      500
#define BLINK_TICK_MS            50   // Fast tick resolution for per-LED blink timing
#define CC_LED_TIMEOUT_MS        600000  // 10 分钟
#define MIN_WAITING_BLINK_MS     3000    // WAITING_USER 最短闪烁保护
#define BATTERY_CHECK_INTERVAL_MS  5000
#define SLEEP_IDLE_TIMEOUT_MS   5000
#define SLEEP_WAKEUP_INTERVAL_MS 4000

// 电池阈值 (mV)
#define BATTERY_LOW_THRESHOLD_MV    3700
#define BATTERY_ADC_ATTEN           ADC_ATTEN_DB_11
#define BATTERY_ADC_WIDTH           ADC_BITWIDTH_12
#define BATTERY_VOLTAGE_DIVIDER     2.0f  // 分压 50%
#define BATTERY_MOVING_AVG_SAMPLES  10

// BLE 参数 - 优化连接稳定性
#define BLE_DEVICE_NAME_PREFIX      "ClaudeCodeIndicator"
#define BLE_SERVICE_UUID            "0000ff00-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_UUID               "0000ff01-0000-1000-8000-00805f9b34fb"
#define BLE_CONN_INTERVAL_MIN       75    // ms - 最小连接间隔，降低延迟
#define BLE_CONN_INTERVAL_MAX       150   // ms - 最大连接间隔，增加灵活性
#define BLE_SUPERVISION_TIMEOUT     3000   // *10ms = 6000ms - 延长监督超时，避免误断连
#define BLE_SLAVE_LATENCY           2     // 允许从机跳过2个连接事件，降低功耗

// 数据看门狗超时 (ms)
#define BLE_WATCHDOG_TIMEOUT_MS     30000

// BLE 消息定义
#define MSG_KEEPALIVE       "KEEPALIVE"
#define MSG_PAIR_CONFIRM    "PAIR_CONFIRM"
#define MSG_PAIR_SUCCESS    "PAIR_SUCCESS"
#define MSG_ALIVE           "ALIVE"

// LED 指令日志标记
#define MSG_LED_CMD         "LED_CMD"
