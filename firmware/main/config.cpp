#include "config.h"

// LED 状态颜色变量 — 可在运行时通过 HTTP 服务修改
LedColor led_color_advertising  = {0,   20, 0};   // BLE 广播，LED2 闪烁
LedColor led_color_battery_low   = {120, 0,   0};   // 电量低，LED0 闪烁
LedColor led_color_pair_confirm  = {0,   50, 0};   // 配对确认，全部 LED 全亮
LedColor led_color_off           = {0,   0,   0};   // 关灯/休眠/超时熄灭
