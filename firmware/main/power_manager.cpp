#include "power_manager.h"
#include "config.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

static const char* TAG = "power";

static TimerHandle_t g_idle_timer = nullptr;
static bool g_sleep_enabled = false;

void PowerManager::enable()
{
    if (g_sleep_enabled) return;
    g_sleep_enabled = true;
    if (g_idle_timer) {
        xTimerStart(g_idle_timer, 0);
    }
    ESP_LOGI(TAG, "Sleep enabled (after connection)");
}

void PowerManager::disable()
{
    g_sleep_enabled = false;
    if (g_idle_timer) {
        xTimerStop(g_idle_timer, 0);
    }
    ESP_LOGI(TAG, "Sleep disabled (no connection)");
}

// BLE 活动时调用
void PowerManager::on_activity()
{
    if (!g_sleep_enabled) return;
    if (g_idle_timer) {
        xTimerReset(g_idle_timer, 0);
    }
}

static void idle_timeout_cb(TimerHandle_t timer)
{
    if (!g_sleep_enabled) return;
    ESP_LOGI(TAG, "Entering light sleep (wake every %d ms)", SLEEP_WAKEUP_INTERVAL_MS);

    // 配置定时器唤醒
    esp_sleep_enable_timer_wakeup(SLEEP_WAKEUP_INTERVAL_MS * 1000);

    // BLE 活动也会唤醒（由 NimBLE 处理）
    // esp_light_sleep_start();

    ESP_LOGI(TAG, "Woke up from light sleep");
}

void PowerManager::start()
{
    g_idle_timer = xTimerCreate(
        "idle_timer",
        pdMS_TO_TICKS(SLEEP_IDLE_TIMEOUT_MS),
        pdFALSE,
        nullptr,
        idle_timeout_cb
    );
    // 不自动启动，等待 enable() 调用
    ESP_LOGI(TAG, "Power manager started (sleep disabled until BLE connects)");
}
