#pragma once

#include <stdint.h>

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
    void init();
    void start_advertise();
    void set_message_callback(ble_message_cb_t cb);
    void set_connect_callback(ble_connect_cb_t cb);
    void send_response(const char* msg);

private:
    ble_message_cb_t m_callback = nullptr;
    ble_connect_cb_t m_connect_cb = nullptr;
    char m_device_name[64];
};
