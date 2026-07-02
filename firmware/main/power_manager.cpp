#include "power_manager.h"
#include "config.h"
#include "esp_log.h"
#include "esp_pm.h"

static const char* TAG = "power";

#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
static esp_err_t on_sleep_enter(int64_t sleep_time_us, void* arg)
{
    ESP_LOGI(TAG, "💤 Light sleep enter (expected %lld us)", sleep_time_us);
    return ESP_OK;
}

static esp_err_t on_sleep_exit(int64_t sleep_time_us, void* arg)
{
    ESP_LOGI(TAG, "⏰ Light sleep exit (slept %lld us)", sleep_time_us);
    return ESP_OK;
}
#endif

void PowerManager::start()
{
    // Configure ESP-IDF PM framework for light sleep
    esp_pm_config_t pm_cfg = {
        .max_freq_mhz = 160,          // ESP32-C3 default max
        .min_freq_mhz = 160,          // Keep max when awake (BLE needs it)
        .light_sleep_enable = true,   // Allow automatic light sleep
    };
    esp_err_t ret = esp_pm_configure(&pm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_configure failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "PM framework configured (light_sleep_enable=true)");

    // Create lock — acquired = prevent sleep, released = allow sleep
    ret = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "ble_state", &m_pm_lock);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_lock_create failed: %s", esp_err_to_name(ret));
        return;
    }

    // Acquire immediately: no BLE connection yet, stay awake for advertising
    esp_pm_lock_acquire(m_pm_lock);
    ESP_LOGI(TAG, "PM started: light sleep blocked (no BLE connection)");

#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
    // Register sleep callbacks for observability
    esp_pm_sleep_cbs_register_config_t cbs = {
        .enter_cb = on_sleep_enter,
        .exit_cb = on_sleep_exit,
    };
    ret = esp_pm_light_sleep_register_cbs(&cbs);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register sleep callbacks: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Sleep callbacks registered");
    }
#endif
}

void PowerManager::enable()
{
    // BLE connected: keep PM lock acquired to prevent automatic CPU light sleep.
    // CPU light sleep wake-up latency (~1ms+) can cause missed BLE connection
    // events, eventually triggering supervision timeout (20s) on the Central.
    // The BLE controller's modem sleep still saves RF power safely.
    // Phase 2 manual light sleep (enter_light_sleep) handles power saving
    // during disconnect independently.
    ESP_LOGI(TAG, "PM lock held — light sleep blocked (BLE connected)");
}

void PowerManager::disable()
{
    // BLE disconnected: keep PM lock acquired.
    // Automatic light sleep stays blocked; Phase 2 manual sleep cycle
    // (10s awake / 50s light sleep) handles power saving explicitly.
    ESP_LOGI(TAG, "PM lock held — light sleep blocked (BLE disconnected)");
}

void PowerManager::on_activity()
{
    // No-op: BLE controller's internal PM lock handles connection events.
    // When data arrives, the controller holds its lock during the connection
    // event, preventing sleep. After processing, the lock releases and the
    // PM framework can re-enter light sleep. No manual coordination needed.
}
