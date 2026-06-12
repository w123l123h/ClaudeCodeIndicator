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

    // 带重试和状态清理的广播启动
    void _advertise_with_retry();

private:
    uint16_t m_conn_handle = 0;
    uint8_t m_own_addr_type = 0;  // set in on_sync()
    uint16_t m_char_val_handle = 0;
    bool m_ready = false;
    ble_message_cb_t m_msg_cb = nullptr;
    ble_connect_cb_t m_conn_cb = nullptr;
    TimerHandle_t m_watchdog = nullptr;
    char m_device_name[64];
};
