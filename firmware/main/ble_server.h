#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#ifdef __cplusplus
extern "C" {
#endif

// 消息回调: void on_message(const char* msg)
typedef void (*ble_message_cb_t)(const char* msg);
// 连接状态回调: void on_connect_change(bool connected)
typedef void (*ble_connect_cb_t)(bool connected);
// 休眠回调: void on_sleep(bool entering) — entering=true 即将休眠, false 已唤醒
typedef void (*ble_sleep_cb_t)(bool entering);
// 电源控制回调: void on_power_ctrl(bool allow_sleep)
// allow_sleep=true  → release PM lock → CPU can enter light sleep
// allow_sleep=false → acquire PM lock → stay awake for advertising
typedef void (*ble_power_ctrl_cb_t)(bool allow_sleep);

#ifdef __cplusplus
}
#endif

class BleServer {
public:
    static BleServer* s_instance;

    void init();
    void start_advertise();
    void set_message_callback(ble_message_cb_t cb);
    void set_connect_callback(ble_connect_cb_t cb);
    void set_power_ctrl_callback(ble_power_ctrl_cb_t cb);
    void set_sleep_callback(ble_sleep_cb_t cb);
    void send_response(const char* msg);

    // 数据看门狗
    void start_watchdog();
    void reset_watchdog();
    void stop_watchdog();

    // NimBLE 回调（static，被全局 g_chr_defs / ble_gap_adv_start 引用，须为 public）
    static int gatt_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt* ctxt, void* arg);
    static int gap_event_cb(struct ble_gap_event* event, void* arg);
    static void on_sync();
    static void ble_host_task(void* param);
    static void watchdog_cb(TimerHandle_t t);
    static void phase1_cb(TimerHandle_t t);
    static void phase2_cb(TimerHandle_t t);

    // 带重试和状态清理的广播启动
    void _advertise_with_retry();

    // Phase2 SLEEP: BLE suspend → light sleep → resume
    void enter_light_sleep();  // full cycle: suspend BLE, sleep, resume
    bool is_sleep_pending() const { return m_sleep_pending; }

private:
    // 断连后广播阶段
    enum class AdvPhase { NONE, PHASE1_CONTINUOUS, PHASE2_AWAKE, PHASE2_SLEEP };

    static constexpr uint32_t PHASE1_DURATION_MS = 30 * 60 * 1000;  // 30 min
    static constexpr uint32_t PHASE2_AWAKE_MS    = 10 * 1000;       // 10s
    static constexpr uint32_t PHASE2_SLEEP_MS    = 50 * 1000;       // 50s

    AdvPhase m_adv_phase = AdvPhase::NONE;
    TimerHandle_t m_phase1_timer = nullptr;  // 30-min one-shot
    TimerHandle_t m_phase2_timer = nullptr;  // 10s one-shot for AWAKE window
    ble_power_ctrl_cb_t m_power_cb = nullptr;
    ble_sleep_cb_t m_sleep_cb = nullptr;
    bool m_sleep_pending = false;  // set by phase2_cb, consumed by main loop

    uint16_t m_conn_handle = 0;
    uint8_t m_own_addr_type = 0;  // set in on_sync()
    uint16_t m_char_val_handle = 0;
    bool m_ready = false;
    ble_message_cb_t m_msg_cb = nullptr;
    ble_connect_cb_t m_conn_cb = nullptr;
    TimerHandle_t m_watchdog = nullptr;
    char m_device_name[64];

    void cancel_phase_timers();
    void start_phase1();
};
