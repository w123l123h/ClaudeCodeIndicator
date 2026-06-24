#include "application.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include <cstring>

static const char *TAG = "Application";

// 全局指针供 LedStateManager 定时器回调使用
LedStateManager *g_led_state_manager = nullptr;

// 充电检测标志位
volatile bool Application::s_charge_detected = false;
volatile bool Application::s_last_charge_state = false;
bool Application::s_current_state_stable = false;
uint32_t Application::s_last_change_time = 0;

Application &Application::instance()
{
    static Application app;
    return app;
}

Application::Application() {}

Application::~Application() {}

void Application::init()
{
    ESP_LOGI(TAG, "Claude Code Indicator starting...");

    // 初始化 GPIO10 充电检测
    init_charge_detect();

    // 初始化 LED
    led_controller_.init();
    led_state_manager_.init(&led_controller_);
    g_led_state_manager = &led_state_manager_;

    // 自检序列
    ESP_LOGI(TAG, "Self-test start");
    led_controller_.set_led(0, LED_COLOR_RED);
    vTaskDelay(pdMS_TO_TICKS(SELF_TEST_DELAY_MS));
    led_controller_.set_led(1, LED_COLOR_ORANGE);
    vTaskDelay(pdMS_TO_TICKS(SELF_TEST_DELAY_MS));
    led_controller_.set_led(2, LED_COLOR_GREEN);
    vTaskDelay(pdMS_TO_TICKS(SELF_TEST_DELAY_MS));
    led_controller_.all_off();
    ESP_LOGI(TAG, "Self-test complete");

    // 初始化 NVS（BLE 需要）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化 BLE
    ble_server_.init();
    ble_server_.set_message_callback(ble_message_callback);
    ble_server_.set_connect_callback(ble_connect_callback);
    ble_server_.set_power_ctrl_callback(ble_power_ctrl_callback);
    ble_server_.set_sleep_callback(ble_sleep_callback);

    // 初始化电池监测
    battery_monitor_.set_callback(battery_low_callback);

    // 初始化电源管理
    power_manager_.start();
}

void Application::run()
{
    // 启动 BLE 广播
    ble_server_.start_advertise();

    // 等待连接：LED2 绿色闪烁
    led_state_manager_.apply_command(2, true, led_color_advertising.r, led_color_advertising.g, led_color_advertising.b, 0, true, LED_BLINK_PERIOD_MS);

    // 启动电池监测
    battery_monitor_.start();

    // 主循环：处理 LED 闪烁 + Phase2 BLE light sleep
    while (true)
    {
        // Phase2 SLEEP: suspend BLE → light sleep → resume
        if (ble_server_.is_sleep_pending())
        {
            ble_server_.enter_light_sleep();
            continue;
        }
        led_state_manager_.tick();

        // 检查充电状态
        check_charge_status();

        vTaskDelay(pdMS_TO_TICKS(BLINK_TICK_MS));
    }
}

void Application::handle_ble_message(const char *msg)
{
    // JSON 指令
    if (msg[0] == '{')
    {
        power_manager_.on_activity();
        parse_led_json(msg);
        return;
    }

    // 保活
    if (strcmp(msg, MSG_KEEPALIVE) == 0)
    {
        ble_server_.send_response(MSG_ALIVE);
        return;
    }

    // 配对
    if (strcmp(msg, MSG_PAIR_CONFIRM) == 0)
    {
        ble_server_.stop_watchdog();
        led_state_manager_.handle_pair_confirm();
        return;
    }
    if (strcmp(msg, MSG_PAIR_SUCCESS) == 0)
    {
        led_state_manager_.handle_pair_success();
        ble_server_.start_watchdog();
        play_rainbow_effect();  // 配对成功后播放7彩灯
        return;
    }

    ESP_LOGW(TAG, "Unknown BLE message: %s", msg);
}

void Application::handle_ble_connect(bool connected)
{
    led_state_manager_.set_connected(connected);
    if (connected)
    {
        power_manager_.enable();
    }
    else
    {
        power_manager_.disable();
    }
}

void Application::handle_power_ctrl(bool allow_sleep)
{
    if (allow_sleep)
    {
        power_manager_.enable();
    }
    else
    {
        power_manager_.disable();
    }
}

void Application::handle_sleep(bool entering)
{
    led_state_manager_.prepare_sleep(entering);
}

void Application::handle_battery_low(bool low)
{
    led_state_manager_.set_battery_low(low);
}

void Application::parse_led_json(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root)
    {
        ESP_LOGE(TAG, "JSON parse error: %s",
                 cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "unknown");
        ble_server_.send_response("ERR:JSON_PARSE");
        return;
    }

    cJSON *leds = cJSON_GetObjectItem(root, "leds");
    if (!leds || !cJSON_IsArray(leds))
    {
        ESP_LOGE(TAG, "JSON missing 'leds' array");
        cJSON_Delete(root);
        ble_server_.send_response("ERR:JSON_PARSE");
        return;
    }

    int count = cJSON_GetArraySize(leds);
    for (int i = 0; i < count && i < 3; i++)
    {
        cJSON *item = cJSON_GetArrayItem(leds, i);
        if (!item)
            continue;

        cJSON *j_id = cJSON_GetObjectItem(item, "id");
        cJSON *j_on = cJSON_GetObjectItem(item, "on");
        if (!j_id || !j_on)
            continue;

        int id = j_id->valueint;
        bool on = cJSON_IsTrue(j_on);

        cJSON *j_r = cJSON_GetObjectItem(item, "r");
        cJSON *j_g = cJSON_GetObjectItem(item, "g");
        cJSON *j_b = cJSON_GetObjectItem(item, "b");
        uint8_t r = (uint8_t)(j_r ? j_r->valueint : 0);
        uint8_t g = (uint8_t)(j_g ? j_g->valueint : 0);
        uint8_t b = (uint8_t)(j_b ? j_b->valueint : 0);

        cJSON *j_to = cJSON_GetObjectItem(item, "timeout");
        uint32_t timeout_s = (j_to && j_to->valueint > 0) ? (uint32_t)j_to->valueint : 0;

        cJSON *j_blink = cJSON_GetObjectItem(item, "blink");
        bool blink = cJSON_IsTrue(j_blink);

        cJSON *j_blink_ms = cJSON_GetObjectItem(item, "blink_ms");
        uint16_t blink_ms = (j_blink_ms && j_blink_ms->valueint > 0)
                                ? (uint16_t)j_blink_ms->valueint
                                : LED_BLINK_PERIOD_MS;

        led_state_manager_.apply_command(id, on, r, g, b, timeout_s, blink, blink_ms);
    }

    cJSON_Delete(root);
    ble_server_.send_response("OK");
}

// 静态回调桥接
void Application::ble_message_callback(const char *msg)
{
    instance().handle_ble_message(msg);
}

void Application::ble_connect_callback(bool connected)
{
    instance().handle_ble_connect(connected);
}

void Application::ble_power_ctrl_callback(bool allow_sleep)
{
    instance().handle_power_ctrl(allow_sleep);
}

void Application::ble_sleep_callback(bool entering)
{
    instance().handle_sleep(entering);
}

void Application::battery_low_callback(bool low)
{
    instance().handle_battery_low(low);
}

void Application::init_charge_detect()
{
    gpio_config_t io_conf;
    // 双边沿触发（上升沿和下降沿都触发）
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    // 设置为输入模式
    io_conf.mode = GPIO_MODE_INPUT;
    // 配置 GPIO1
    io_conf.pin_bit_mask = (1ULL << CHARGE_DETECT_GPIO);
    // 禁用上拉电阻
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    // 启用下拉电阻
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    // 应用配置
    gpio_config(&io_conf);

    // 安装 GPIO 中断服务
    gpio_install_isr_service(0);

    // 添加中断处理回调
    gpio_isr_handler_add(CHARGE_DETECT_GPIO, charge_detect_isr_handler, nullptr);

    // 初始化状态
    s_last_charge_state = gpio_get_level(CHARGE_DETECT_GPIO);
    s_current_state_stable = s_last_charge_state;

    ESP_LOGI(TAG, "GPIO10 charge detect initialized (initial state: %s)",
             s_current_state_stable ? "charging" : "not charging");
}

void IRAM_ATTR Application::charge_detect_isr_handler(void *arg)
{
    s_charge_detected = true;
}

void Application::check_charge_status()
{
    if (s_charge_detected)
    {
        // static int i = 0;
        // if(i ++ < 1000 / 50)
        // {
        //     return;
        // }
        // i = 0;

        s_charge_detected = false;

        uint32_t now = esp_log_timestamp();

        // 防抖：只在超过防抖时间后才检查状态
        if (now - s_last_change_time >= DEBOUNCE_MS)
        {
            bool current_state = gpio_get_level(CHARGE_DETECT_GPIO);

            // 只有当当前读取的状态与稳定状态不同时，才认为是真正的状态变化
            if (current_state != s_current_state_stable)
            {
                if (current_state)
                {
                    // 上升沿：开始充电
                    // led_state_manager_.set_charge_detected(true);
                    ESP_LOGI(TAG, "接入USB");
                }
                else
                {
                    // 下降沿：停止充电
                    // led_state_manager_.set_charge_detected(false);
                    ESP_LOGI(TAG, "断开USB");
                }

                s_current_state_stable = current_state;
                s_last_change_time = now;
            }

            s_last_charge_state = current_state;
        }
    }
}

void Application::play_rainbow_effect()
{
    // 7种颜色：红、橙、黄、绿、青、蓝、紫
    const LedColor rainbow_colors[] = {
        LED_COLOR_RED,     // 红
        LED_COLOR_ORANGE,   // 橙
        LED_COLOR_YELLOW,   // 黄
        LED_COLOR_GREEN,     // 绿
        LED_COLOR_CYAN,   // 青
        LED_COLOR_BLUE,     // 蓝
        LED_COLOR_PURPLE    // 紫
    };
    
    // 总共执行3秒，每200ms切换一次，共15次
    const int total_cycles = 3000 / 200;
    
    for (int i = 0; i < total_cycles; i++) {
        // 关闭所有灯
        // led_controller_.all_off();
        
        // 计算当前颜色索引和灯号
        int color_idx = i % 7;      // 循环7种颜色
        int led_idx = i % WS2812_LED_COUNT;  // 循环3个LED
        
        // 设置当前灯的颜色
        led_controller_.set_led(led_idx, rainbow_colors[color_idx]);
        
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    // 3秒结束后关闭所有灯
    led_controller_.all_off();
}