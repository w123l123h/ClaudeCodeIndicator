#include "battery_monitor.h"
#include "config.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "hal/adc_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "battery";

static adc_oneshot_unit_handle_t g_adc_handle = nullptr;
static adc_cali_handle_t g_cali_handle = nullptr;
static BatteryCallback g_battery_cb = nullptr;

void BatteryMonitor::set_callback(BatteryCallback cb)
{
    g_battery_cb = cb;
}

float BatteryMonitor::read_voltage()
{
    // 移动平均采样
    int sum = 0;
    for (int i = 0; i < BATTERY_MOVING_AVG_SAMPLES; i++) {
        int raw;
        adc_oneshot_read(g_adc_handle, BATTERY_ADC_CHANNEL, &raw);
        sum += raw;
    }
    int avg_raw = sum / BATTERY_MOVING_AVG_SAMPLES;

    // 校准 + 换算电压 (mV)
    int voltage_mv;
    adc_cali_raw_to_voltage(g_cali_handle, avg_raw, &voltage_mv);

    // 分压换算实际电压
    float actual_voltage = voltage_mv * BATTERY_VOLTAGE_DIVIDER / 1000.0f;
    return actual_voltage;
}

static void battery_task(void* arg)
{
    BatteryMonitor* self = static_cast<BatteryMonitor*>(arg);
    bool last_low = false;
    while (true) {
        float voltage = self->read_voltage();
        bool low = (voltage <= (BATTERY_LOW_THRESHOLD_MV / 1000.0f));

        ESP_LOGI(TAG, "Battery: %.2fV, low=%d", voltage, low);

        if (low != last_low && g_battery_cb) {
            g_battery_cb(low);
        }
        last_low = low;

        vTaskDelay(pdMS_TO_TICKS(BATTERY_CHECK_INTERVAL_MS));
    }
}

void BatteryMonitor::start()
{
    // ADC1 初始化
    adc_oneshot_unit_init_cfg_t init_cfg = {};
    init_cfg.unit_id = ADC_UNIT_1;
    init_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;
    adc_oneshot_new_unit(&init_cfg, &g_adc_handle);

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = BATTERY_ADC_ATTEN;
    chan_cfg.bitwidth = BATTERY_ADC_WIDTH;
    adc_oneshot_config_channel(g_adc_handle, BATTERY_ADC_CHANNEL, &chan_cfg);

    // 校准
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = ADC_UNIT_1;
    cali_cfg.atten = BATTERY_ADC_ATTEN;
    cali_cfg.bitwidth = BATTERY_ADC_WIDTH;
    adc_cali_create_scheme_curve_fitting(&cali_cfg, &g_cali_handle);

    xTaskCreate(battery_task, "battery", 4096, this, 5, nullptr);
    ESP_LOGI(TAG, "Battery monitor started (ADC1 CH%d)", BATTERY_ADC_CHANNEL);
}
