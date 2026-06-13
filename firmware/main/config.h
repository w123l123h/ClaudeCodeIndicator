#pragma once

#include "hal/gpio_types.h"
#include "hal/adc_types.h"

// 硬件引脚
#define LED_GPIO            GPIO_NUM_3
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0  // GPIO0
#define WS2812_LED_COUNT    3

// LED 颜色预设 (RGB)
#define LED_COLOR_RED       {255, 0, 0}
#define LED_COLOR_ORANGE    {255, 128, 0}
#define LED_COLOR_GREEN     {0, 255, 0}
#define LED_COLOR_OFF       {0, 0, 0}

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
#define BATTERY_LOW_THRESHOLD_MV    3500
#define BATTERY_ADC_ATTEN           ADC_ATTEN_DB_11
#define BATTERY_ADC_WIDTH           ADC_BITWIDTH_12
#define BATTERY_VOLTAGE_DIVIDER     2.0f  // 分压 50%
#define BATTERY_MOVING_AVG_SAMPLES  10

// BLE 参数
#define BLE_DEVICE_NAME_PREFIX      "ClaudeCodeIndicator"
#define BLE_SERVICE_UUID            "0000ff00-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_UUID               "0000ff01-0000-1000-8000-00805f9b34fb"
#define BLE_CONN_INTERVAL_MIN       100   // ms
#define BLE_CONN_INTERVAL_MAX       100   // ms
#define BLE_SUPERVISION_TIMEOUT     400  // *10ms = 4000ms
#define BLE_SLAVE_LATENCY           0

// 数据看门狗超时 (ms)
#define BLE_WATCHDOG_TIMEOUT_MS     20000

// BLE 消息定义
#define MSG_KEEPALIVE       "KEEPALIVE"
#define MSG_PAIR_CONFIRM    "PAIR_CONFIRM"
#define MSG_PAIR_SUCCESS    "PAIR_SUCCESS"
#define MSG_ALIVE           "ALIVE"

// LED 指令日志标记
#define MSG_LED_CMD         "LED_CMD"
