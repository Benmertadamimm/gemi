#pragma once

/* PDB voltaj / akım / sıcaklık ölçümü (ADC1) ve güç koruması (10 Hz task'ı).
 *
 * Kullanım:
 *   PowerMonitor power;
 *   power.start();                       // config.hpp'de açık sensörleri kurar
 *   float k = power.status().power_scale; // motor çıkışıyla çarpılır */

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "power.hpp"

class PowerMonitor {
public:
    esp_err_t start();

    /* Son durumun kopyası. Thread-safe. */
    power::Status status() const;

private:
    struct AdcInput {
        bool              enabled = false;
        adc_channel_t     channel = ADC_CHANNEL_0;
        adc_cali_handle_t cali    = nullptr;
        float             last_v  = 0.0f;  /* son ham ADC voltajı (kalibrasyon logu için) */
    };

    esp_err_t setup_input(AdcInput &in, int gpio, const char *name);
    float     read_volts(AdcInput &in);
    static void task_entry(void *arg);
    [[noreturn]] void run();
    void log_changes(const power::Status &prev, const power::Status &now);
    void log_periodic(const power::Status &st);

    adc_oneshot_unit_handle_t adc_ = nullptr;
    AdcInput volt_in_;
    AdcInput curr_in_;
    AdcInput temp_in_;

    power::Monitor monitor_;
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    power::Status shared_;
};
