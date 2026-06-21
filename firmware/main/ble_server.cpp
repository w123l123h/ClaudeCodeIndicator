#include "ble_server.h"
#include "config.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_bt.h"
#include "esp_sleep.h"
#include <cstring>

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble";

// 单例指针（由 init() 设置）
BleServer *BleServer::s_instance = nullptr;

// GATT characteristic UUID 定义（静态常量，不属于实例）
static const ble_uuid128_t g_svc_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00);
static const ble_uuid128_t g_char_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x01, 0xff, 0x00, 0x00);

// GATT characteristic 定义（含 arg 指向 s_instance，用于 gatt_write_cb）
static struct ble_gatt_chr_def g_chr_defs[] = {
    {
        .uuid = &g_char_uuid.u,
        .access_cb = BleServer::gatt_write_cb,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = nullptr, // 将在 init() 中设置为 &m_char_val_handle
    },
    {0}};

// GATT service 定义
static struct ble_gatt_svc_def g_svc_defs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &g_svc_uuid.u,
        .characteristics = g_chr_defs,
    },
    {0}};

// ============ BleServer static 成员函数 ============

int BleServer::gatt_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    BleServer *self = static_cast<BleServer *>(arg);
    if (!self)
        return 0;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && self->m_msg_cb && ctxt->om)
    {
        char buf[128];
        int len = ctxt->om->om_len;
        if (len > 127)
            len = 127;
        memcpy(buf, ctxt->om->om_data, len);
        buf[len] = 0;
        // 去除尾部换行
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        {
            buf[--len] = 0;
        }
        ESP_LOGI(TAG, "Received: %s", buf);
        self->m_msg_cb(buf);

        // 喂狗：收到任何数据时重置看门狗
        self->reset_watchdog();
    }
    return 0;
}

int BleServer::gap_event_cb(struct ble_gap_event *event, void *arg)
{
    BleServer *self = s_instance;
    if (!self)
        return 0;

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            bool was_in_phase = (self->m_adv_phase != AdvPhase::NONE);
            self->cancel_phase_timers(); // cancel phase1/phase2 timers
            if (was_in_phase)
            {
                ESP_LOGI(TAG, "Adv phase timers cancelled (reconnected)");
            }
            self->m_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected, handle=%d", self->m_conn_handle);
            if (self->m_conn_cb)
                self->m_conn_cb(true);

            // 设置连接参数（缩短 supervision timeout 到 4s）
            {
                struct ble_gap_upd_params params = {};
                // ms → 1.25ms units: *4/5
                params.itvl_min = (BLE_CONN_INTERVAL_MIN * 4) / 5;
                params.itvl_max = (BLE_CONN_INTERVAL_MAX * 4) / 5;
                params.latency = BLE_SLAVE_LATENCY;
                params.supervision_timeout = BLE_SUPERVISION_TIMEOUT; // *10ms
                params.min_ce_len = 0;
                params.max_ce_len = 0;
                int rc = ble_gap_update_params(self->m_conn_handle, &params);
                if (rc != 0)
                {
                    ESP_LOGW(TAG, "ble_gap_update_params failed: %d", rc);
                }
                else
                {
                    ESP_LOGI(TAG, "Connection params set: itvl=%d-%dms, sup_to=%dms",
                             BLE_CONN_INTERVAL_MIN, BLE_CONN_INTERVAL_MAX,
                             BLE_SUPERVISION_TIMEOUT * 10);
                }
            }

            // Start watchdog immediately on connect.
            // PAIR_CONFIRM will stop it if user needs time to confirm pairing.
            self->start_watchdog();
        }
        else
        {
            ESP_LOGW(TAG, "Connection failed, restarting advertise");
            if (self->m_adv_phase != AdvPhase::PHASE2_SLEEP)
            {
                self->_advertise_with_retry();
            }
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected, restarting advertise");
        self->m_conn_handle = 0;
        self->cancel_phase_timers(); // safety: clean any leftover phase state
        if (self->m_conn_cb)
            self->m_conn_cb(false);
        // 停止数据看门狗
        self->stop_watchdog();
        self->_advertise_with_retry();
        self->start_phase1(); // start 30-min phase1 timer
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (self->m_adv_phase == AdvPhase::PHASE2_SLEEP)
        {
            // BLE suspended during SLEEP — ignore stale ADV_COMPLETE
            ESP_LOGI(TAG, "Adv complete during SLEEP phase — ignored (BLE suspended)");
        }
        else
        {
            ESP_LOGW(TAG, "Advertising stopped unexpectedly, restarting");
            self->_advertise_with_retry();
        }
        break;
    default:
        break;
    }
    return 0;
}

void BleServer::on_sync()
{
    BleServer *self = s_instance;
    if (!self)
        return;

    ESP_LOGI(TAG, "NimBLE host synced");
    int rc = ble_hs_id_infer_auto(0, &self->m_own_addr_type);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "Failed to infer address type: %d, using random", rc);
        self->m_own_addr_type = BLE_OWN_ADDR_RANDOM;
    }
    self->m_ready = true;
    ESP_LOGI(TAG, "NimBLE host ready, addr_type=%d", self->m_own_addr_type);
}

void BleServer::ble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    ESP_LOGI(TAG, "NimBLE host task ended");
    nimble_port_freertos_deinit();
}

void BleServer::watchdog_cb(TimerHandle_t t)
{
    BleServer *self = static_cast<BleServer *>(pvTimerGetTimerID(t));
    if (!self)
        return;

    ESP_LOGW(TAG, "Data watchdog timeout — forcing disconnect");
    if (self->m_conn_handle != 0)
    {
        ble_gap_terminate(self->m_conn_handle, 0x13); // HCI: Remote User Terminated Connection
    }
}

void BleServer::phase1_cb(TimerHandle_t t)
{
    BleServer *self = static_cast<BleServer *>(pvTimerGetTimerID(t));
    if (!self)
        return;

    // Guard: if phase was cancelled while this callback was queued, bail out
    // Capture handle locally to prevent TOCTOU race with cancel_phase_timers
    TimerHandle_t handle = self->m_phase1_timer;
    if (self->m_adv_phase != AdvPhase::PHASE1_CONTINUOUS || !handle)
    {
        return;
    }

    // One-shot timer fired — delete it
    xTimerDelete(handle, 0);
    self->m_phase1_timer = nullptr;

    ESP_LOGI(TAG, "Adv phase 2: intermittent — AWAKE (10s)");

    // PM lock already held from disconnect — no need to re-acquire

    self->m_adv_phase = AdvPhase::PHASE2_AWAKE;
    self->_advertise_with_retry();

    // Start 10s awake timer
    self->m_phase2_timer = xTimerCreate(
        "adv_ph2",
        pdMS_TO_TICKS(PHASE2_AWAKE_MS),
        pdFALSE,
        self,
        phase2_cb);
    if (self->m_phase2_timer)
    {
        xTimerStart(self->m_phase2_timer, 0);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to create phase2 timer");
    }
}

void BleServer::phase2_cb(TimerHandle_t t)
{
    BleServer *self = static_cast<BleServer *>(pvTimerGetTimerID(t));
    if (!self)
        return;

    // Guard: capture handle locally to prevent TOCTOU race
    TimerHandle_t handle = self->m_phase2_timer;
    if (!handle)
        return;

    xTimerDelete(handle, 0);
    self->m_phase2_timer = nullptr;

    if (self->m_adv_phase == AdvPhase::PHASE2_AWAKE)
    {
        // AWAKE → SLEEP: signal main loop to suspend BLE and enter light sleep
        ESP_LOGI(TAG, "Adv phase 2: intermittent — SLEEP (%lu ms), suspend pending",
                 (unsigned long)PHASE2_SLEEP_MS);
        self->m_adv_phase = AdvPhase::PHASE2_SLEEP;
        self->m_sleep_pending = true;
        if (self->m_power_cb)
            self->m_power_cb(true); // release PM lock

        // No timer — main loop handles SLEEP→AWAKE after light sleep
    }
    else
    {
        ESP_LOGW(TAG, "Adv phase2: unexpected phase %d, stopping cycle",
                 static_cast<int>(self->m_adv_phase));
    }
}

// ============ BleServer 公有方法 ============

void BleServer::init()
{
    s_instance = this;
    ESP_LOGI(TAG, "BLE init start");

    // 生成设备名称: ClaudeCodeIndicator_<MAC>
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    // snprintf(m_device_name, sizeof(m_device_name), "%s_%02X%02X%02X%02X%02X%02X",
    //          BLE_DEVICE_NAME_PREFIX,
    //          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    // ESP_LOGI(TAG, "Device name: %s", m_device_name);

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

    ble_svc_gap_device_name_set("ClaudeCodeIndicator");

    // 绑定实例指针和成员变量（必须在 register 之前）
    g_chr_defs[0].arg = this;
    g_chr_defs[0].val_handle = &m_char_val_handle;

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

void BleServer::cancel_phase_timers()
{
    // Release PM lock if we're in a phase that holds it
    if (m_adv_phase == AdvPhase::PHASE1_CONTINUOUS ||
        m_adv_phase == AdvPhase::PHASE2_AWAKE)
    {
        if (m_power_cb)
            m_power_cb(true); // release PM lock
    }

    if (m_phase1_timer)
    {
        xTimerStop(m_phase1_timer, 0);
        xTimerDelete(m_phase1_timer, 0);
        m_phase1_timer = nullptr;
    }
    if (m_phase2_timer)
    {
        xTimerStop(m_phase2_timer, 0);
        xTimerDelete(m_phase2_timer, 0);
        m_phase2_timer = nullptr;
    }
    m_adv_phase = AdvPhase::NONE;
}

void BleServer::start_phase1()
{
    cancel_phase_timers(); // safety: clean any existing phase state first

    ESP_LOGI(TAG, "Adv phase 1: continuous advertising (30 min)");
    m_adv_phase = AdvPhase::PHASE1_CONTINUOUS;

    m_phase1_timer = xTimerCreate(
        "adv_ph1",
        pdMS_TO_TICKS(PHASE1_DURATION_MS),
        pdFALSE, // one-shot
        this,    // pvTimerID → retrieved by phase1_cb
        phase1_cb);
    if (m_phase1_timer)
    {
        xTimerStart(m_phase1_timer, 0);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to create phase1 timer");
    }
}

void BleServer::start_advertise()
{
    ESP_LOGI(TAG, "Waiting for NimBLE host ready...");
    int timeout = 200; // 2 seconds
    while (!m_ready && timeout-- > 0)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!m_ready)
    {
        ESP_LOGE(TAG, "NimBLE host not ready after 2s, cannot advertise");
        return;
    }
    ESP_LOGI(TAG, "Starting advertise...");
    _advertise_with_retry();
}

void BleServer::_advertise_with_retry()
{
    // 1. 先停止可能残留的广播，清 NimBLE 内部状态
    int rc = ble_gap_adv_stop();
    ESP_LOGI(TAG, "Adv stop returned: %d", rc);
    vTaskDelay(pdMS_TO_TICKS(50)); // 让 NimBLE 处理完停止事件

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    // 广播标志：一般发现模式
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)"ClaudeCodeIndicator";
    fields.name_len = strlen("ClaudeCodeIndicator");
    fields.name_is_complete = 1; // 表示这是完整名称  
    rc = ble_gap_adv_set_fields(&fields);
    assert(rc == 0);

    // 2. 带重试启动广播
    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    int retry = 50; // 最多等 1 秒
    while (retry-- > 0)
    {
        rc = ble_gap_adv_start(m_own_addr_type, nullptr, BLE_HS_FOREVER,
                               &adv_params, gap_event_cb, nullptr);
        if (rc == 0)
            break;
        if (rc != BLE_HS_EBUSY && rc != BLE_HS_EINVAL)
            break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (rc != 0)
    {
        ESP_LOGE(TAG, "Restart advertise failed: %d", rc);
    }
    else
    {
        ESP_LOGI(TAG, "Advertising started");
    }
}

void BleServer::enter_light_sleep()
{
    m_sleep_pending = false;

    ESP_LOGI(TAG, "Suspending BLE for light sleep...");

    // 1. Stop advertising
    int rc = ble_gap_adv_stop();
    ESP_LOGI(TAG, "Adv stop returned: %d", rc);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 2. Disable BLE controller — releases btLS lock
    rc = esp_bt_controller_disable();
    if (rc != ESP_OK)
    {
        ESP_LOGW(TAG, "bt_controller_disable failed: %d", rc);
    }
    else
    {
        ESP_LOGI(TAG, "BLE controller disabled — btLS released");
    }

    // 3. Set RTC timer and enter light sleep
    ESP_LOGI(TAG, "Entering light sleep for %lu ms...",
             (unsigned long)PHASE2_SLEEP_MS);

    // Notify main loop: turn off LEDs before sleep
    if (m_sleep_cb)
        m_sleep_cb(true);

    esp_sleep_enable_timer_wakeup(PHASE2_SLEEP_MS * 1000);
    esp_light_sleep_start();

    // Notify main loop: wake up, restore state
    if (m_sleep_cb)
        m_sleep_cb(false);

    ESP_LOGI(TAG, "Woke from light sleep");

    // 4. Re-enable BLE controller
    rc = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (rc != ESP_OK)
    {
        ESP_LOGE(TAG, "bt_controller_enable failed: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "BLE controller re-enabled");
    vTaskDelay(pdMS_TO_TICKS(200));

    // 5. Restart AWAKE cycle
    ESP_LOGI(TAG, "Adv phase 2: intermittent — AWAKE (%lu ms)",
             (unsigned long)PHASE2_AWAKE_MS);

    if (m_power_cb)
        m_power_cb(false); // re-acquire PM lock
    m_adv_phase = AdvPhase::PHASE2_AWAKE;

    vTaskDelay(pdMS_TO_TICKS(500)); // let NimBLE recover
    _advertise_with_retry();

    // Start 10s AWAKE timer → phase2_cb
    m_phase2_timer = xTimerCreate(
        "adv_ph2",
        pdMS_TO_TICKS(PHASE2_AWAKE_MS),
        pdFALSE,
        this,
        phase2_cb);
    if (m_phase2_timer)
    {
        xTimerStart(m_phase2_timer, 0);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to create phase2 timer");
    }
}

void BleServer::set_message_callback(ble_message_cb_t cb)
{
    m_msg_cb = cb;
}

void BleServer::set_connect_callback(ble_connect_cb_t cb)
{
    m_conn_cb = cb;
}

void BleServer::set_power_ctrl_callback(ble_power_ctrl_cb_t cb)
{
    m_power_cb = cb;
}

void BleServer::set_sleep_callback(ble_sleep_cb_t cb)
{
    m_sleep_cb = cb;
}

void BleServer::send_response(const char *msg)
{
    if (m_conn_handle == 0)
        return;
    char data[128];
    snprintf(data, sizeof(data), "%s\n", msg);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, strlen(data));
    int rc = ble_gattc_notify_custom(m_conn_handle, m_char_val_handle, om);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "Notify failed: %d", rc);
    }
    else
    {
        ESP_LOGI(TAG, "Sent: %s", msg);
    }
}

// ============ 数据看门狗（Task 3 实现） ============

void BleServer::start_watchdog()
{
    stop_watchdog(); // 防止重复创建
    m_watchdog = xTimerCreate(
        "ble_wd",
        pdMS_TO_TICKS(BLE_WATCHDOG_TIMEOUT_MS),
        pdFALSE, // one-shot
        this,    // 通过 pvTimerID 传递 this
        watchdog_cb);
    if (m_watchdog)
    {
        xTimerStart(m_watchdog, 0);
        ESP_LOGI(TAG, "Watchdog started (%d ms)", BLE_WATCHDOG_TIMEOUT_MS);
    }
}

void BleServer::reset_watchdog()
{
    if (m_watchdog)
    {
        xTimerReset(m_watchdog, 0);
    }
}

void BleServer::stop_watchdog()
{
    if (m_watchdog)
    {
        xTimerStop(m_watchdog, 0);
        xTimerDelete(m_watchdog, 0);
        m_watchdog = nullptr;
        ESP_LOGI(TAG, "Watchdog stopped");
    }
}
