#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "config.h"

class LedController {
public:
    void init();
    void reinit();  // Re-initialize RMT after light sleep wake
    void set_led(int index, LedColor color);
    void set_led(int index, uint8_t r, uint8_t g, uint8_t b);
    void all_off();
    void all_on(LedColor color);

private:
    static constexpr int LED_COUNT = WS2812_LED_COUNT;
    SemaphoreHandle_t mutex_ = nullptr;  // 保护 RMT 并发访问
};
