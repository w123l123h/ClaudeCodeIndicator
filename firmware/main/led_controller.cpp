#include "led_controller.h"
#include "led_strip.h"
#include "driver/rmt_types.h"
#include "esp_log.h"

static const char* TAG = "led";

static led_strip_handle_t g_strip = nullptr;

void LedController::init()
{
    if (!mutex_) {
        mutex_ = xSemaphoreCreateRecursiveMutex();
    }

    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = LED_GPIO;  // GPIO3
    strip_config.max_leds = WS2812_LED_COUNT;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    strip_config.flags.invert_out = false;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000; // 10MHz
    rmt_config.flags.with_dma = false;

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &g_strip));
    ESP_ERROR_CHECK(led_strip_clear(g_strip));
    ESP_LOGI(TAG, "Initialized %d WS2812B LEDs on GPIO3", WS2812_LED_COUNT);
}

void LedController::reinit()
{
    if (g_strip) {
        led_strip_del(g_strip);
        g_strip = nullptr;
    }
    init();
}

void LedController::set_led(int index, LedColor color)
{
    if (!g_strip || index < 0 || index >= WS2812_LED_COUNT) return;

    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    ESP_ERROR_CHECK(led_strip_set_pixel(g_strip, index, color.r, color.g, color.b));
    ESP_ERROR_CHECK(led_strip_refresh(g_strip));
    xSemaphoreGiveRecursive(mutex_);
}

void LedController::set_led(int index, uint8_t r, uint8_t g, uint8_t b)
{
    set_led(index, LedColor{r, g, b});
}

void LedController::all_off()
{
    if (!g_strip) return;

    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    ESP_ERROR_CHECK(led_strip_clear(g_strip));
    xSemaphoreGiveRecursive(mutex_);

    ESP_LOGI(TAG, "All LEDs off");
}

void LedController::all_on(LedColor color)
{
    if (!g_strip) return;

    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    for (int i = 0; i < WS2812_LED_COUNT; i++) {
        ESP_ERROR_CHECK(led_strip_set_pixel(g_strip, i, color.r, color.g, color.b));
    }
    ESP_ERROR_CHECK(led_strip_refresh(g_strip));
    xSemaphoreGiveRecursive(mutex_);
}
