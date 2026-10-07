#include "power_monitor.hpp"

#include "config.hpp"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace {

constexpr const char *TAG = "power";

constexpr uint32_t    kTaskStack    = 4096;
constexpr UBaseType_t kTaskPriority = 4;  /* kontrol task'ından düşük */
constexpr adc_atten_t kAtten        = ADC_ATTEN_DB_12;

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

}  // namespace

esp_err_t PowerMonitor::start()
{
    const bool any_adc = cfg::kBattSenseEnabled || cfg::kCurrentSenseEnabled || cfg::kTempSenseEnabled;
    if (!any_adc) {
        ESP_LOGW(TAG, "Guc sensorleri kapali (config.hpp) — batarya/akim/sicaklik korumasi YOK");
        return ESP_OK;  /* status() varsayılanı: power_scale = 1.0 */
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id  = ADC_UNIT_1;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &adc_), TAG, "adc_oneshot_new_unit");

    if (cfg::kBattSenseEnabled) ESP_RETURN_ON_ERROR(setup_input(volt_in_, cfg::kBattVoltageGpio, "voltaj"), TAG, "");
    if (cfg::kCurrentSenseEnabled) ESP_RETURN_ON_ERROR(setup_input(curr_in_, cfg::kCurrentGpio, "akim"), TAG, "");
    if (cfg::kTempSenseEnabled) ESP_RETURN_ON_ERROR(setup_input(temp_in_, cfg::kTempGpio, "sicaklik"), TAG, "");

    if (xTaskCreate(&PowerMonitor::task_entry, "power", kTaskStack, this, kTaskPriority, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "task olusturulamadi");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t PowerMonitor::setup_input(AdcInput &in, int gpio, const char *name)
{
    adc_unit_t unit;
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(gpio, &unit, &in.channel), TAG, "GPIO%d ADC pini degil", gpio);
    ESP_RETURN_ON_FALSE(unit == ADC_UNIT_1, ESP_ERR_INVALID_ARG, TAG,
                        "GPIO%d ADC2'de — ADC1 pini (GPIO1..10) kullan", gpio);

    adc_oneshot_chan_cfg_t ch_cfg = {};
    ch_cfg.atten    = kAtten;
    ch_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(adc_, in.channel, &ch_cfg), TAG, "config_channel");

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id  = ADC_UNIT_1;
    cali_cfg.chan     = in.channel;
    cali_cfg.atten    = kAtten;
    cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &in.cali) != ESP_OK) in.cali = nullptr;
#endif
    if (!in.cali) ESP_LOGW(TAG, "%s: ADC kalibrasyonu yok, yaklasik donusum kullanilacak", name);

    in.enabled = true;
    ESP_LOGI(TAG, "%s sensoru: GPIO%d (ADC1_CH%d)", name, gpio, static_cast<int>(in.channel));
    return ESP_OK;
}

float PowerMonitor::read_volts(AdcInput &in)
{
    int sum = 0;
    int ok  = 0;
    for (int i = 0; i < cfg::kAdcOversample; ++i) {
        int raw = 0;
        if (adc_oneshot_read(adc_, in.channel, &raw) == ESP_OK) {
            sum += raw;
            ++ok;
        }
    }
    if (ok == 0) return NAN;

    const int raw = sum / ok;
    int mv = 0;
    if (!in.cali || adc_cali_raw_to_voltage(in.cali, raw, &mv) != ESP_OK) {
        mv = raw * 3100 / 4095;  /* 12 dB zayıflatmada yaklaşık tam skala */
    }
    in.last_v = static_cast<float>(mv) / 1000.0f;
    return in.last_v;
}

power::Status PowerMonitor::status() const
{
    portENTER_CRITICAL(&mux_);
    const power::Status s = shared_;
    portEXIT_CRITICAL(&mux_);
    return s;
}

void PowerMonitor::task_entry(void *arg)
{
    static_cast<PowerMonitor *>(arg)->run();
}

void PowerMonitor::run()
{
    const float dt_s       = static_cast<float>(cfg::kPowerPeriodMs) / 1000.0f;
    uint32_t    last_log   = now_ms();
    TickType_t  last_wake  = xTaskGetTickCount();

    for (;;) {
        power::Measurements m;
        if (volt_in_.enabled) {
            const float v   = read_volts(volt_in_);
            m.voltage_enabled = !std::isnan(v);
            m.voltage_v       = power::battery_volts(v);
        }
        if (curr_in_.enabled) {
            const float v   = read_volts(curr_in_);
            m.current_enabled = !std::isnan(v);
            m.current_a       = power::current_amps(v);
        }
        if (temp_in_.enabled) {
            const float v = read_volts(temp_in_);
            m.temp_enabled = !std::isnan(v);
            m.temp_c       = power::ntc_celsius(v);
        }

        const power::Status prev = monitor_.status();
        monitor_.update(m, dt_s);
        const power::Status &now = monitor_.status();

        portENTER_CRITICAL(&mux_);
        shared_ = now;
        portEXIT_CRITICAL(&mux_);

        log_changes(prev, now);
        if (now_ms() - last_log >= cfg::kPowerLogPeriodMs) {
            last_log = now_ms();
            log_periodic(now);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(cfg::kPowerPeriodMs));
    }
}

void PowerMonitor::log_changes(const power::Status &prev, const power::Status &now)
{
    if (now.battery != prev.battery) {
        const bool bad = now.battery >= power::BatteryLevel::Low;
        const char *action = now.battery == power::BatteryLevel::Limit    ? " — GUC SINIRLANDI"
                           : now.battery == power::BatteryLevel::Critical ? " — KRITIK, GUC KESILIYOR"
                                                                          : "";
        if (bad) {
            ESP_LOGW(TAG, "Batarya %s -> %s: %.2f V (%.2f V/hucre)%s", power::to_string(prev.battery),
                     power::to_string(now.battery), static_cast<double>(now.voltage_v),
                     static_cast<double>(now.cell_v), action);
        } else {
            ESP_LOGI(TAG, "Batarya %s: %.2f V", power::to_string(now.battery), static_cast<double>(now.voltage_v));
        }
    }
    if (volt_in_.enabled && prev.voltage_valid && !now.voltage_valid) {
        ESP_LOGE(TAG, "Voltaj olcumu gecersiz (%.2f V ADC) — kablo/kalibrasyon kontrol et",
                 static_cast<double>(volt_in_.last_v));
    }
    if (now.current_limiting != prev.current_limiting) {
        if (now.current_limiting) {
            ESP_LOGW(TAG, "Akim sinirlamasi aktif: %.1f A > %.0f A", static_cast<double>(now.current_a),
                     static_cast<double>(cfg::kCurrentLimitA));
        } else {
            ESP_LOGI(TAG, "Akim sinirlamasi kalkti");
        }
    }
    if (now.temp_warning != prev.temp_warning) {
        if (now.temp_warning) {
            ESP_LOGW(TAG, "PDB sicakligi yuksek: %.1f C", static_cast<double>(now.temp_c));
        } else {
            ESP_LOGI(TAG, "PDB sicakligi normale dondu: %.1f C", static_cast<double>(now.temp_c));
        }
    }
}

void PowerMonitor::log_periodic(const power::Status &st)
{
    ESP_LOGI(TAG, "V=%.2f(%s) I=%.1fA T=%.1fC | guc=%.2f (bat %.2f akim %.2f sic %.2f) | ham ADC: %.3f/%.3f/%.3f V",
             static_cast<double>(st.voltage_v), power::to_string(st.battery), static_cast<double>(st.current_a),
             static_cast<double>(st.temp_c), static_cast<double>(st.power_scale),
             static_cast<double>(st.battery_scale), static_cast<double>(st.current_scale),
             static_cast<double>(st.temp_scale), static_cast<double>(volt_in_.last_v),
             static_cast<double>(curr_in_.last_v), static_cast<double>(temp_in_.last_v));
}
