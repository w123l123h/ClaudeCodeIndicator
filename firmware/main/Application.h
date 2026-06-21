#pragma once

#include "led_controller.h"
#include "led_state_manager.h"
#include "ble_server.h"
#include "battery_monitor.h"
#include "power_manager.h"
#include "cJSON.h"

class Application {
public:
    // 单例模式
    static Application& instance();

    // 生命周期方法
    void init();
    void run();

    // 获取组件实例（供外部回调使用）
    LedStateManager* led_state_manager() { return &led_state_manager_; }
    BleServer* ble_server() { return &ble_server_; }
    PowerManager* power_manager() { return &power_manager_; }

private:
    Application();
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // 组件成员变量
    LedController led_controller_;
    LedStateManager led_state_manager_;
    BleServer ble_server_;
    BatteryMonitor battery_monitor_;
    PowerManager power_manager_;

    // GPIO 充电检测相关
    void init_charge_detect();
    static void IRAM_ATTR charge_detect_isr_handler(void* arg);
    void check_charge_status();
    
    static volatile bool s_charge_detected;
    static volatile bool s_last_charge_state;
    static bool s_current_state_stable;  // 稳定后的状态
    static uint32_t s_last_change_time;
    static constexpr uint32_t DEBOUNCE_MS = 500;  // 增加防抖时间到500ms

    // 私有回调方法
    void handle_ble_message(const char* msg);
    void handle_ble_connect(bool connected);
    void handle_power_ctrl(bool allow_sleep);
    void handle_sleep(bool entering);
    void handle_battery_low(bool low);

    // JSON 解析
    void parse_led_json(const char* json_str);

    // 静态回调桥接（C 函数接口）
    static void ble_message_callback(const char* msg);
    static void ble_connect_callback(bool connected);
    static void ble_power_ctrl_callback(bool allow_sleep);
    static void ble_sleep_callback(bool entering);
    static void battery_low_callback(bool low);
};