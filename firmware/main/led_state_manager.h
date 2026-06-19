#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "led_controller.h"
#include "config.h"

class LedStateManager {
public:
    LedStateManager();
    ~LedStateManager();

    // 初始化
    void init(LedController* led);

    // LED 指令执行
    void apply_command(int id, bool on, uint8_t r, uint8_t g, uint8_t b,
                       uint32_t timeout_s, bool blink, uint16_t blink_ms);

    // 主循环 tick，处理闪烁
    void tick();

    // 状态设置
    void set_battery_low(bool low);
    void set_connected(bool connected);

    // 配对处理
    void handle_pair_confirm();
    void handle_pair_success();

    // 休眠准备
    void prepare_sleep(bool entering);

    // 状态查询
    bool is_blinking(int id) const;
    void get_color(int id, uint8_t& r, uint8_t& g, uint8_t& b) const;

private:
    // LED 状态结构体
    struct LedState {
        TimerHandle_t timer;
        bool blink;
        uint16_t blink_ms;
        uint16_t blink_counter;
        bool blink_on;
        uint8_t r, g, b;

        LedState() : timer(nullptr), blink(false), blink_ms(LED_BLINK_PERIOD_MS),
                     blink_counter(0), blink_on(false), r(0), g(0), b(0) {}
    };

    static constexpr int LED_COUNT = 3;

    LedController* led_;
    LedState states_[LED_COUNT];
    bool battery_low_;
    bool in_pair_;

    // 定时器回调（静态函数）
    static void timer_callback(TimerHandle_t timer);

    // 内部辅助方法
    void stop_and_delete_timer(int id);
    void turn_off_led(int id);
};