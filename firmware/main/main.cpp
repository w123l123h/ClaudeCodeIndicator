#include <stdio.h>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "config.h"
#include "led_controller.h"
#include "ble_server.h"
#include "battery_monitor.h"
#include "power_manager.h"
#include "cJSON.h"

static const char* TAG = "main";

// 运行时状态（避免静态初始化问题）
static LedController* g_led = nullptr;
static BleServer* g_ble = nullptr;

// LED 运行时状态
struct LedState {
    TimerHandle_t timer = nullptr;
    bool blink = false;
    uint16_t blink_ms = LED_BLINK_PERIOD_MS;
    uint16_t blink_counter = 0;
    bool blink_on = false;
    uint8_t r = 0, g = 0, b = 0;
};
static LedState g_led_states[3];
static bool g_battery_low = false;
static bool g_connected = false;
static PowerManager* g_power = nullptr;

// 单个 LED 超时回调
static void led_timeout_callback(TimerHandle_t t) {
    for (int i = 0; i < 3; i++) {
        if (g_led_states[i].timer == t) {
            if (i == 0 && g_battery_low) return;
            g_led->set_led(i, 0, 0, 0);
            g_led_states[i].blink = false;
            g_led_states[i].blink_counter = 0;
            g_led_states[i].blink_on = false;
            g_led_states[i].timer = nullptr;
            ESP_LOGI(TAG, "LED%d timeout — off", i);
            return;
        }
    }
}

// 执行单条 LED 指令
static void apply_led_command(int id, bool on, uint8_t r, uint8_t g, uint8_t b,
                               uint32_t timeout_s, bool blink, uint16_t blink_ms) {
    if (id < 0 || id > 2) return;

    if (id == 0 && g_battery_low) {
        ESP_LOGW(TAG, "LED0 blocked by battery protection");
        return;
    }

    LedState& state = g_led_states[id];

    if (state.timer) {
        xTimerStop(state.timer, 0);
        xTimerDelete(state.timer, 0);
        state.timer = nullptr;
    }

    if (!on) {
        g_led->set_led(id, 0, 0, 0);
        state.blink = false;
        state.blink_counter = 0;
        state.blink_on = false;
        state.r = state.g = state.b = 0;
        ESP_LOGI(TAG, "LED%d off", id);
    } else {
        state.r = r;
        state.g = g;
        state.b = b;
        state.blink = blink;
        state.blink_ms = (blink_ms > 0) ? blink_ms : LED_BLINK_PERIOD_MS;
        state.blink_counter = 0;
        state.blink_on = true;
        g_led->set_led(id, r, g, b);

        uint32_t timeout_ms = (timeout_s > 0) ? (timeout_s * 1000) : CC_LED_TIMEOUT_MS;
        state.timer = xTimerCreate(
            "led_to", pdMS_TO_TICKS(timeout_ms),
            pdFALSE,
            (void*)(uintptr_t)id,
            led_timeout_callback
        );
        if (state.timer) {
            xTimerStart(state.timer, 0);
        }
        ESP_LOGI(TAG, "LED%d on rgb=(%d,%d,%d) timeout=%us blink=%d blink_ms=%u",
                 id, r, g, b, (unsigned)(timeout_ms / 1000), blink, (unsigned)blink_ms);
    }
}

// JSON 解析
static void parse_led_json(const char* json_str) {
    cJSON* root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error: %s",
                 cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "unknown");
        g_ble->send_response("ERR:JSON_PARSE");
        return;
    }

    cJSON* leds = cJSON_GetObjectItem(root, "leds");
    if (!leds || !cJSON_IsArray(leds)) {
        ESP_LOGE(TAG, "JSON missing 'leds' array");
        cJSON_Delete(root);
        g_ble->send_response("ERR:JSON_PARSE");
        return;
    }

    int count = cJSON_GetArraySize(leds);
    for (int i = 0; i < count && i < 3; i++) {
        cJSON* item = cJSON_GetArrayItem(leds, i);
        if (!item) continue;

        cJSON* j_id = cJSON_GetObjectItem(item, "id");
        cJSON* j_on = cJSON_GetObjectItem(item, "on");
        if (!j_id || !j_on) continue;

        int id = j_id->valueint;
        bool on = cJSON_IsTrue(j_on);

        cJSON* j_r = cJSON_GetObjectItem(item, "r");
        cJSON* j_g = cJSON_GetObjectItem(item, "g");
        cJSON* j_b = cJSON_GetObjectItem(item, "b");
        uint8_t r = (uint8_t)(j_r ? j_r->valueint : 0);
        uint8_t g = (uint8_t)(j_g ? j_g->valueint : 0);
        uint8_t b = (uint8_t)(j_b ? j_b->valueint : 0);

        cJSON* j_to = cJSON_GetObjectItem(item, "timeout");
        uint32_t timeout_s = (j_to && j_to->valueint > 0) ? (uint32_t)j_to->valueint : 0;

        cJSON* j_blink = cJSON_GetObjectItem(item, "blink");
        bool blink = cJSON_IsTrue(j_blink);

        cJSON* j_blink_ms = cJSON_GetObjectItem(item, "blink_ms");
        uint16_t blink_ms = (j_blink_ms && j_blink_ms->valueint > 0)
                            ? (uint16_t)j_blink_ms->valueint
                            : LED_BLINK_PERIOD_MS;

        apply_led_command(id, on, r, g, b, timeout_s, blink, blink_ms);
    }

    cJSON_Delete(root);
    g_ble->send_response("OK");
}

// 配对处理
static void handle_pair_confirm() {
    // Pause watchdog — user needs time to confirm pairing dialog on PC
    g_ble->stop_watchdog();

    for (int i = 0; i < 3; i++) {
        if (g_led_states[i].timer) {
            xTimerStop(g_led_states[i].timer, 0);
            xTimerDelete(g_led_states[i].timer, 0);
            g_led_states[i].timer = nullptr;
        }
        g_led_states[i].blink = false;
    }
    g_led->all_on({0, 255, 0});
    ESP_LOGI(TAG, "Pair confirm — all LEDs green, watchdog paused");
}

static void handle_pair_success() {
    for (int i = 0; i < 3; i++) {
        if (g_led_states[i].timer) {
            xTimerStop(g_led_states[i].timer, 0);
            xTimerDelete(g_led_states[i].timer, 0);
            g_led_states[i].timer = nullptr;
        }
        g_led_states[i].blink = false;
        g_led_states[i].r = g_led_states[i].g = g_led_states[i].b = 0;
    }
    g_led->all_off();
    g_battery_low = false;
    g_ble->start_watchdog();  // 配对完成后启动数据看门狗
    ESP_LOGI(TAG, "Pair success — all off, state cleared, watchdog started");
}

// BLE 消息处理 (C 回调)
static void handle_ble_message(const char* msg)
{
    // JSON 指令
    if (msg[0] == '{') {
        if (g_power) g_power->on_activity();
        parse_led_json(msg);
        return;
    }

    // 保活
    if (strcmp(msg, MSG_KEEPALIVE) == 0) {
        g_ble->send_response(MSG_ALIVE);
        return;
    }

    // 配对
    if (strcmp(msg, MSG_PAIR_CONFIRM) == 0) {
        handle_pair_confirm();
        return;
    }
    if (strcmp(msg, MSG_PAIR_SUCCESS) == 0) {
        handle_pair_success();
        return;
    }

    ESP_LOGW(TAG, "Unknown BLE message: %s", msg);
}

// BLE 连接状态回调 (C 回调)
static void ble_connect_callback(bool connected)
{
    g_connected = connected;
    if (connected) {
        g_led_states[2].blink = false;
        g_led_states[2].blink_counter = 0;
        g_led_states[2].blink_on = false;
        g_led->set_led(2, 0, 0, 0);
        if (g_power) g_power->enable();
    } else {
        // 断连后广播会重启，恢复等待指示
        g_led_states[2].timer = nullptr;
        g_led_states[2].blink = true;
        g_led_states[2].blink_ms = LED_BLINK_PERIOD_MS;
        g_led_states[2].blink_counter = 0;
        g_led_states[2].blink_on = false;
        g_led_states[2].r = 0;
        g_led_states[2].g = 255;
        g_led_states[2].b = 0;
        if (g_power) g_power->disable();
    }
}

// 电源控制回调（由 BLE server 的 phase 定时器触发）
static void ble_power_ctrl_callback(bool allow_sleep)
{
    if (!g_power) return;
    if (allow_sleep) {
        g_power->enable();   // release PM lock → allow light sleep
    } else {
        g_power->disable();  // acquire PM lock → stay awake
    }
}

// 电池低电量回调
static void battery_low_callback(bool low)
{
    g_battery_low = low;
    if (low) {
        if (g_led_states[0].timer) {
            xTimerStop(g_led_states[0].timer, 0);
            xTimerDelete(g_led_states[0].timer, 0);
            g_led_states[0].timer = nullptr;
        }
        g_led_states[0].r = 255;
        g_led_states[0].g = 0;
        g_led_states[0].b = 0;
        g_led_states[0].blink = true;
        g_led_states[0].blink_ms = LED_BLINK_PERIOD_MS;
        g_led_states[0].blink_counter = 0;
        g_led_states[0].blink_on = false;
        ESP_LOGW(TAG, "Battery LOW (<= 3.5V) — LED0 red blink");
    } else {
        g_led_states[0].blink = false;
        if (g_led_states[0].timer == nullptr) {
            g_led->set_led(0, 0, 0, 0);
            g_led_states[0].r = g_led_states[0].g = g_led_states[0].b = 0;
        }
        ESP_LOGI(TAG, "Battery OK (> 3.5V) — LED0 released");
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Claude Code Indicator starting...");

    // 堆分配避免静态初始化问题
    g_led = new LedController();
    g_ble = new BleServer();

    g_led->init();

    // 自检序列
    ESP_LOGI(TAG, "Self-test start");
    g_led->set_led(0, LED_COLOR_RED);
    vTaskDelay(pdMS_TO_TICKS(SELF_TEST_DELAY_MS));
    g_led->set_led(1, LED_COLOR_ORANGE);
    vTaskDelay(pdMS_TO_TICKS(SELF_TEST_DELAY_MS));
    g_led->set_led(2, LED_COLOR_GREEN);
    vTaskDelay(pdMS_TO_TICKS(SELF_TEST_DELAY_MS));
    g_led->all_off();
    ESP_LOGI(TAG, "Self-test complete");

    // 初始化 NVS（BLE 需要）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // BLE
    g_ble->init();
    g_ble->set_message_callback(handle_ble_message);
    g_ble->set_connect_callback(ble_connect_callback);
    g_ble->set_power_ctrl_callback(ble_power_ctrl_callback);
    g_ble->start_advertise();

    // 等待连接：LED2 绿色闪烁
    g_led_states[2].timer = nullptr;
    g_led_states[2].blink = true;
    g_led_states[2].blink_ms = LED_BLINK_PERIOD_MS;
    g_led_states[2].blink_counter = 0;
    g_led_states[2].blink_on = false;
    g_led_states[2].r = 0;
    g_led_states[2].g = 255;
    g_led_states[2].b = 0;

    // 电池监测
    BatteryMonitor battery;
    battery.set_callback(battery_low_callback);
    battery.start();

    // 电源管理
    PowerManager power;
    g_power = &power;
    power.start();

    // 主循环：处理 LED 闪烁（per-LED timing via fast tick + counter）
    while (true) {
        for (int i = 0; i < 3; i++) {
            LedState& s = g_led_states[i];
            if (s.blink && s.blink_ms > 0) {
                s.blink_counter += BLINK_TICK_MS;
                if (s.blink_counter >= s.blink_ms) {
                    s.blink_counter = 0;
                    s.blink_on = !s.blink_on;
                    if (s.blink_on) {
                        g_led->set_led(i, s.r, s.g, s.b);
                    } else {
                        g_led->set_led(i, 0, 0, 0);
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BLINK_TICK_MS));
    }
}
