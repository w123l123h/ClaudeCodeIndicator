#include "led_state_manager.h"
#include "esp_log.h"

static const char *TAG = "LedStateManager";

LedStateManager::LedStateManager()
    : led_(nullptr), battery_low_(false), in_pair_(false)
{
}

LedStateManager::~LedStateManager()
{
    for (int i = 0; i < LED_COUNT; i++)
    {
        if (states_[i].timer)
        {
            xTimerStop(states_[i].timer, 0);
            xTimerDelete(states_[i].timer, 0);
            states_[i].timer = nullptr;
        }
    }
}

void LedStateManager::init(LedController *led)
{
    led_ = led;
    for (int i = 0; i < LED_COUNT; i++)
    {
        states_[i] = LedState();
    }
}

void LedStateManager::apply_command(int id, bool on, uint8_t r, uint8_t g, uint8_t b,
                                    uint32_t timeout_s, bool blink, uint16_t blink_ms)
{
    if (id < 0 || id >= LED_COUNT)
        return;

    if (id == 0 && battery_low_)
    {
        ESP_LOGW(TAG, "LED0 blocked by battery protection");
        return;
    }

    LedState &state = states_[id];

    // 停止并删除现有定时器
    stop_and_delete_timer(id);

    if (!on)
    {
        turn_off_led(id);
        state.blink = false;
        state.blink_counter = 0;
        state.blink_on = false;
        state.r = state.g = state.b = 0;
        ESP_LOGI(TAG, "LED%d off", id);
    }
    else
    {
        state.r = r;
        state.g = g;
        state.b = b;
        state.blink = blink;
        state.blink_ms = (blink_ms > 0) ? blink_ms : LED_BLINK_PERIOD_MS;
        state.blink_counter = 0;
        state.blink_on = true;
        led_->set_led(id, r, g, b);

        uint32_t timeout_ms = (timeout_s > 0) ? (timeout_s * 1000) : CC_LED_TIMEOUT_MS;
        state.timer = xTimerCreate(
            "led_to", pdMS_TO_TICKS(timeout_ms),
            pdFALSE,
            (void *)(uintptr_t)id,
            timer_callback);
        if (state.timer)
        {
            xTimerStart(state.timer, 0);
        }
        ESP_LOGI(TAG, "LED%d on rgb=(%d,%d,%d) timeout=%us blink=%d blink_ms=%u",
                 id, r, g, b, (unsigned)(timeout_ms / 1000), blink, (unsigned)blink_ms);
    }
}

void LedStateManager::tick()
{
    for (int i = 0; i < LED_COUNT; i++)
    {
        LedState &s = states_[i];
        if (s.blink && s.blink_ms > 0)
        {
            s.blink_counter += BLINK_TICK_MS;
            if (s.blink_counter >= s.blink_ms)
            {
                s.blink_counter = 0;
                s.blink_on = !s.blink_on;
                if (s.blink_on)
                {
                    led_->set_led(i, s.r, s.g, s.b);
                }
                else
                {
                    led_->set_led(i, led_color_off);
                }
            }
        }
    }
}

void LedStateManager::set_battery_low(bool low)
{
    battery_low_ = low;
    if (low)
    {
        if (states_[0].timer)
        {
            xTimerStop(states_[0].timer, 0);
            xTimerDelete(states_[0].timer, 0);
            states_[0].timer = nullptr;
        }
        states_[0].r = led_color_battery_low.r;
        states_[0].g = led_color_battery_low.g;
        states_[0].b = led_color_battery_low.b;
        states_[0].blink = true;
        states_[0].blink_ms = LED_BLINK_PERIOD_MS;
        states_[0].blink_counter = 0;
        states_[0].blink_on = false;
        ESP_LOGW(TAG, "Battery LOW (<= 3.5V) — LED0 red blink");
    }
    else
    {
        states_[0].blink = false;
        if (states_[0].timer == nullptr)
        {
            led_->set_led(0, led_color_off);
            states_[0].r = states_[0].g = states_[0].b = 0;
        }
        ESP_LOGI(TAG, "Battery OK (> 3.5V) — LED0 released");
    }
}

void LedStateManager::set_connected(bool connected)
{
    if (connected)
    {
        states_[2].blink = false;
        states_[2].blink_counter = 0;
        states_[2].blink_on = false;
        led_->set_led(2, led_color_off);
    }
    else
    {
        // 断连后广播会重启，恢复等待指示
        states_[2].timer = nullptr;
        states_[2].blink = true;
        states_[2].blink_ms = LED_BLINK_PERIOD_MS;
        states_[2].blink_counter = 0;
        states_[2].blink_on = false;
        states_[2].r = led_color_advertising.r;
        states_[2].g = led_color_advertising.g;
        states_[2].b = led_color_advertising.b;
        if (in_pair_)
        {
            led_->set_led(0, led_color_off);
            led_->set_led(1, led_color_off);
        }
    }
}

void LedStateManager::set_charge_detected(bool detected)
{
    for (int i = 0; i < LED_COUNT; i++)
    {
        states_[i].blink = true;
        states_[i].blink_on = false;
        states_[i].blink_ms = LED_BLINK_PERIOD_MS;
        states_[i].blink_counter = 0;
        if (detected)
        {
            states_[i].r = 20;
            states_[i].g = 0;
            states_[i].b = 0;
        }
        else
        {
            states_[i].r = 0;
            states_[i].g = 20;
            states_[i].b = 0;
        }
        set_timeout(i, 5);
    }
}

void LedStateManager::handle_pair_confirm()
{
    for (int i = 0; i < LED_COUNT; i++)
    {
        stop_and_delete_timer(i);
        states_[i].blink = false;
    }
    led_->all_on(led_color_pair_confirm);
    in_pair_ = true;
    ESP_LOGI(TAG, "Pair confirm — all LEDs green");
}

void LedStateManager::handle_pair_success()
{
    for (int i = 0; i < LED_COUNT; i++)
    {
        stop_and_delete_timer(i);
        states_[i].blink = false;
        states_[i].r = states_[i].g = states_[i].b = 0;
    }
    led_->all_off();
    battery_low_ = false;
    in_pair_ = false;
    ESP_LOGI(TAG, "Pair success — all off, state cleared");
}

void LedStateManager::prepare_sleep(bool entering)
{
    if (entering)
    {
        led_->all_off();
        for (int i = 0; i < LED_COUNT; i++)
        {
            if (states_[i].timer)
            {
                xTimerStop(states_[i].timer, 0);
                xTimerDelete(states_[i].timer, 0);
                states_[i].timer = nullptr;
            }
            states_[i].blink = false;
            states_[i].blink_counter = 0;
        }
        ESP_LOGI(TAG, "LEDs off for light sleep");
    }
    else
    {
        // Wake from light sleep: re-init RMT (loses state during sleep),
        // then restore LED2 to advertising blink
        led_->reinit();

        states_[2].timer = nullptr;
        states_[2].blink = true;
        states_[2].blink_ms = LED_BLINK_PERIOD_MS;
        states_[2].blink_counter = 0;
        states_[2].blink_on = true;   // 立即点亮，避免唤醒后长暗
        states_[2].r = led_color_advertising.r;
        states_[2].g = led_color_advertising.g;
        states_[2].b = led_color_advertising.b;
        led_->set_led(2, states_[2].r, states_[2].g, states_[2].b);  // 立即亮灯
        ESP_LOGI(TAG, "LED2 restored to advertising blink after wake");
    }
}

bool LedStateManager::is_blinking(int id) const
{
    if (id < 0 || id >= LED_COUNT)
        return false;
    return states_[id].blink;
}

void LedStateManager::get_color(int id, uint8_t &r, uint8_t &g, uint8_t &b) const
{
    if (id < 0 || id >= LED_COUNT)
    {
        r = g = b = 0;
        return;
    }
    r = states_[id].r;
    g = states_[id].g;
    b = states_[id].b;
}

void LedStateManager::set_timeout(int id, uint32_t timeout_s)
{
    if (id < 0 || id >= LED_COUNT)
        return;

    LedState &state = states_[id];

    // 停止并删除现有定时器
    stop_and_delete_timer(id);

    // 如果超时时间大于0，创建新的定时器
    if (timeout_s > 0)
    {
        uint32_t timeout_ms = timeout_s * 1000;
        state.timer = xTimerCreate(
            "led_to", pdMS_TO_TICKS(timeout_ms),
            pdFALSE,
            (void *)(uintptr_t)id,
            timer_callback);
        if (state.timer)
        {
            xTimerStart(state.timer, 0);
            ESP_LOGI(TAG, "LED%d timeout set to %us", id, (unsigned)timeout_s);
        }
    }
    else
    {
        ESP_LOGI(TAG, "LED%d timeout cleared", id);
    }
}

void LedStateManager::timer_callback(TimerHandle_t timer)
{
    // 通过定时器 ID 找到对应的 LED
    int id = (int)(uintptr_t)pvTimerGetTimerID(timer);
    if (id < 0 || id >= LED_COUNT)
        return;

    // 需要通过实例访问，这里使用全局实例
    // 由于 FreeRTOS 定时器回调是 C 函数，我们需要一个桥接方案
    // 暂时使用全局指针
    extern LedStateManager *g_led_state_manager;
    if (g_led_state_manager)
    {
        if (id == 0 && g_led_state_manager->battery_low_)
            return;
        g_led_state_manager->led_->set_led(id, led_color_off);
        g_led_state_manager->states_[id].blink = false;
        g_led_state_manager->states_[id].blink_counter = 0;
        g_led_state_manager->states_[id].blink_on = false;
        g_led_state_manager->states_[id].timer = nullptr;
        ESP_LOGI(TAG, "LED%d timeout — off", id);
    }
}

void LedStateManager::stop_and_delete_timer(int id)
{
    if (states_[id].timer)
    {
        xTimerStop(states_[id].timer, 0);
        xTimerDelete(states_[id].timer, 0);
        states_[id].timer = nullptr;
    }
}

void LedStateManager::turn_off_led(int id)
{
    led_->set_led(id, led_color_off);
}