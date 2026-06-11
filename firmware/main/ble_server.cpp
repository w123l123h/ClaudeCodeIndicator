#include "ble_server.h"
#include "config.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <cstring>

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char* TAG = "ble";

// NimBLE 全局变量
static uint16_t g_conn_handle = 0;
static uint8_t g_own_addr_type = BLE_OWN_ADDR_PUBLIC;
static ble_message_cb_t g_msg_cb = nullptr;
static ble_connect_cb_t g_conn_cb = nullptr;
static bool g_ready = false;

// GATT characteristic 定义
static const ble_uuid128_t g_svc_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00);
static const ble_uuid128_t g_char_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x01, 0xff, 0x00, 0x00);

static uint16_t g_char_val_handle;

// 前向声明
static int gatt_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt* ctxt, void* arg);

// GATT service 静态定义
static struct ble_gatt_chr_def g_chr_defs[] = {
    {
        .uuid = &g_char_uuid.u,
        .access_cb = gatt_write_cb,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &g_char_val_handle,
    },
    { 0 }
};

static struct ble_gatt_svc_def g_svc_defs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &g_svc_uuid.u,
        .characteristics = g_chr_defs,
    },
    { 0 }
};

// GATT 写回调
static int gatt_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && g_msg_cb && ctxt->om) {
        char buf[128];
        int len = ctxt->om->om_len;
        if (len > 127) len = 127;
        memcpy(buf, ctxt->om->om_data, len);
        buf[len] = 0;
        // 去除尾部换行
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            buf[--len] = 0;
        }
        ESP_LOGI(TAG, "Received: %s", buf);
        g_msg_cb(buf);
    }
    return 0;
}

// GAP 事件回调
static int gap_event_cb(struct ble_gap_event* event, void* arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            g_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected, handle=%d", g_conn_handle);
            if (g_conn_cb) g_conn_cb(true);
        } else {
            ESP_LOGW(TAG, "Connection failed, restarting advertise");
            ble_gap_adv_start(g_own_addr_type, nullptr, BLE_HS_FOREVER,
                              nullptr, gap_event_cb, nullptr);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected, restarting advertise");
        g_conn_handle = 0;
        if (g_conn_cb) g_conn_cb(false);
        ble_gap_adv_start(g_own_addr_type, nullptr, BLE_HS_FOREVER,
                          nullptr, gap_event_cb, nullptr);
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGW(TAG, "Advertising stopped, restarting");
        ble_gap_adv_start(g_own_addr_type, nullptr, BLE_HS_FOREVER,
                          nullptr, gap_event_cb, nullptr);
        break;
    default:
        break;
    }
    return 0;
}

// NimBLE host 同步回调
static void on_sync(void)
{
    ESP_LOGI(TAG, "NimBLE host synced");
    int rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to infer address type: %d, using random", rc);
        g_own_addr_type = BLE_OWN_ADDR_RANDOM;
    }
    g_ready = true;
    ESP_LOGI(TAG, "NimBLE host ready, addr_type=%d", g_own_addr_type);
}

// NimBLE host 任务
static void ble_host_task(void* param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    ESP_LOGI(TAG, "NimBLE host task ended");
    nimble_port_freertos_deinit();
}

void BleServer::init()
{
    ESP_LOGI(TAG, "BLE init start");

    // 生成设备名称: ClaudeCodeIndicator_<MAC>
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(m_device_name, sizeof(m_device_name), "%s_%02X%02X%02X%02X%02X%02X",
             BLE_DEVICE_NAME_PREFIX,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Device name: %s", m_device_name);

    // 初始化 NimBLE
    ESP_LOGI(TAG, "Calling nimble_port_init...");
    nimble_port_init();
    ESP_LOGI(TAG, "nimble_port_init done");

    // 配置 sync/reset 回调
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = nullptr;

    // 配置 GAP
    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_svc_gap_device_name_set(m_device_name);

    // 注册 GATT service
    ESP_LOGI(TAG, "Registering GATT services...");
    ble_gatts_count_cfg(g_svc_defs);
    ble_gatts_add_svcs(g_svc_defs);
    ESP_LOGI(TAG, "GATT services registered");

    // 启动 NimBLE host 任务
    ESP_LOGI(TAG, "Starting NimBLE host task...");
    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE init complete");
}

void BleServer::start_advertise()
{
    ESP_LOGI(TAG, "Waiting for NimBLE host ready...");
    int timeout = 200;  // 2 seconds
    while (!g_ready && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!g_ready) {
        ESP_LOGE(TAG, "NimBLE host not ready after 2s, cannot advertise");
        return;
    }
    ESP_LOGI(TAG, "Starting advertise...");
    // 设置广播数据
    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    int rc = ble_gap_adv_start(g_own_addr_type, nullptr, BLE_HS_FOREVER,
                               &adv_params, gap_event_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising started");
    }
}

void BleServer::set_message_callback(ble_message_cb_t cb)
{
    g_msg_cb = cb;
}

void BleServer::set_connect_callback(ble_connect_cb_t cb)
{
    g_conn_cb = cb;
}

void BleServer::send_response(const char* msg)
{
    if (g_conn_handle == 0) return;
    int len = strlen(msg);
    char data[128];
    snprintf(data, sizeof(data), "%s\n", msg);
    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, strlen(data));
    int rc = ble_gattc_notify_custom(g_conn_handle, g_char_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Notify failed: %d", rc);
    } else {
        ESP_LOGI(TAG, "Sent: %s", msg);
    }
}
