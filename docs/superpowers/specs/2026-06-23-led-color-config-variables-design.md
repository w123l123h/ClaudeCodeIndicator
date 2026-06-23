# LED 颜色配置变量提取

## 目标

将 `LedStateManager` 中硬编码的 RGB 颜色值提取到 `config.h` / `config.cpp` 中，定义为可修改的 `extern LedColor` 变量，为后续通过 HTTP 服务在运行时修改颜色参数做准备。

## 范围

仅涉及 `LedStateManager` 使用的颜色。以下**不在范围内**：
- `config.h` 现有 `LED_COLOR_*` 宏（保持不变）
- `Application.cpp` 中的自检颜色和彩虹效果颜色

## 变量清单

| 变量名 | 初始值 | 语义 |
|--------|--------|------|
| `led_color_disconnected` | `{0, 255, 0}` | BLE 断开/广播，LED2 闪烁 |
| `led_color_battery_low` | `{255, 0, 0}` | 电量低，LED0 闪烁 |
| `led_color_pair_confirm` | `{0, 255, 0}` | 配对确认，全部 LED 全亮 |
| `led_color_off` | `{0, 0, 0}` | 关灯/休眠/超时熄灭 |

## 文件变更

### 新增：`firmware/main/config.cpp`

颜色变量定义（定义初始值）：

```cpp
#include "config.h"

LedColor led_color_disconnected  = {0,   255, 0};
LedColor led_color_battery_low   = {255, 0,   0};
LedColor led_color_pair_confirm  = {0,   255, 0};
LedColor led_color_off           = {0,   0,   0};
```

### 修改：`firmware/main/config.h`

在现有 `LED_COLOR_*` 宏下方新增 `extern` 声明：

```cpp
extern LedColor led_color_disconnected;
extern LedColor led_color_battery_low;
extern LedColor led_color_pair_confirm;
extern LedColor led_color_off;
```

### 修改：`firmware/main/led_state_manager.cpp`

替换以下位置的硬编码值为变量引用：

| 函数 | 行号 | 旧值 | 新引用 |
|------|------|------|--------|
| `set_connected()` 断开分支 | 159-161 | `{0, 255, 0}` | `led_color_disconnected` |
| `set_connected()` 连接分支 | 149 | `{0, 0, 0}` | `led_color_off` |
| `set_connected()` 关闭 LED0/LED1 | 164-165 | `{0, 0, 0}` | `led_color_off` |
| `set_battery_low()` 低电分支 | 121-123 | `{255, 0, 0}` | `led_color_battery_low` |
| `set_battery_low()` 恢复分支 | 135-136 | `{0, 0, 0}` | `led_color_off` |
| `handle_pair_confirm()` | 202 | `{0, 255, 0}` | `led_color_pair_confirm` |
| `handle_pair_success()` | 213 | `{0, 0, 0}` | `led_color_off` |
| `tick()` 闪烁关闭相 | 103 | `{0, 0, 0}` | `led_color_off` |
| `timer_callback()` 超时熄灭 | 304 | `{0, 0, 0}` | `led_color_off` |
| `turn_off_led()` | 325 | `{0, 0, 0}` | `led_color_off` |
| `prepare_sleep()` | 225 | `all_off()` | 不变 |
| `set_charge_detected()` | 181-189 | 死代码 | 不动 |

## 构建

需将 `config.cpp` 加入 CMakeLists.txt 的源文件列表。

## 验证

1. 编译固件通过
2. 运行固件，确认 BLE 连接/断开、电量低、配对等场景的 LED 颜色与修改前一致
