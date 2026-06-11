#pragma once

#include <stdint.h>
#include "config.h"

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
    static constexpr int LED_COUNT = WS2812_LED_COUNT;
};
