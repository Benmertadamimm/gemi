#include "telemetry.hpp"

#include "config.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr const char *TAG = "telemetry";

constexpr int         kBaud         = 115200;
constexpr int         kRxBufSize    = 256;
constexpr uint32_t    kTaskStack    = 3072;
constexpr UBaseType_t kTaskPriority = 6;  /* alıcı cevabı hızlı bekler */

}  // namespace

esp_err_t Telemetry::start(uart_port_t port, gpio_num_t pin)
{
    using ibus::telemetry::SensorType;
    if (cfg::kBattSenseEnabled) addr_voltage_ = responder_.add_sensor(SensorType::ExternalVoltage);
    if (cfg::kTempSenseEnabled) addr_temp_ = responder_.add_sensor(SensorType::Temperature);
    if (cfg::kCurrentSenseEnabled) addr_current_ = responder_.add_sensor(SensorType::BatteryCurrent);
    if (responder_.sensor_count() == 0) {
        ESP_LOGW(TAG, "Telemetri acik ama hicbir guc sensoru acik degil — baslatilmadi");
        return ESP_OK;
    }

    port_ = port;
    uart_config_t cfg = {};
    cfg.baud_rate  = kBaud;
    cfg.data_bits  = UART_DATA_8_BITS;
    cfg.parity     = UART_PARITY_DISABLE;
    cfg.stop_bits  = UART_STOP_BITS_1;
    cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    ESP_RETURN_ON_ERROR(uart_driver_install(port_, kRxBufSize, 0, 0, nullptr, 0), TAG, "uart_driver_install");
    ESP_RETURN_ON_ERROR(uart_param_config(port_, &cfg), TAG, "uart_param_config");
    /* TX ve RX aynı pinde: tek telli half-duplex. Open-drain + pull-up ile iki taraf
     * aynı anda sürse bile pin zarar görmez. */
    ESP_RETURN_ON_ERROR(uart_set_pin(port_, pin, pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "uart_set_pin");
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY), TAG, "pull-up");
    ESP_RETURN_ON_ERROR(gpio_od_enable(pin), TAG, "open-drain");
    /* Komut gelir gelmez işlensin: RX timeout'u kısa tut (2 karakter süresi) */
    ESP_RETURN_ON_ERROR(uart_set_rx_timeout(port_, 2), TAG, "rx_timeout");

    if (xTaskCreate(&Telemetry::task_entry, "telemetry", kTaskStack, this, kTaskPriority, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "task olusturulamadi");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "iBUS telemetri — UART%d GPIO%d, %u sensor", static_cast<int>(port_), static_cast<int>(pin),
             static_cast<unsigned>(responder_.sensor_count()));
    return ESP_OK;
}

void Telemetry::task_entry(void *arg)
{
    static_cast<Telemetry *>(arg)->run();
}

void Telemetry::refresh_values()
{
    using namespace ibus::telemetry;
    const power::Status st = power_.status();
    if (addr_voltage_) responder_.set_value(addr_voltage_, encode_voltage(st.voltage_v));
    if (addr_temp_) responder_.set_value(addr_temp_, encode_temperature(st.temp_c));
    if (addr_current_) responder_.set_value(addr_current_, encode_current(st.current_a));
}

void Telemetry::run()
{
    uint8_t buf[32];
    ibus::telemetry::Responder::Response resp{};

    for (;;) {
        /* İlk baytı bekle, sonra tamponda ne varsa bloklamadan al (gecikme olmasın) */
        int n = uart_read_bytes(port_, buf, 1, pdMS_TO_TICKS(50));
        if (n <= 0) continue;
        size_t buffered = 0;
        uart_get_buffered_data_len(port_, &buffered);
        if (buffered > 0) {
            const size_t want = buffered < sizeof(buf) - 1 ? buffered : sizeof(buf) - 1;
            const int    m    = uart_read_bytes(port_, buf + 1, want, 0);
            if (m > 0) n += m;
        }

        refresh_values();
        for (int i = 0; i < n; ++i) {
            const size_t len = responder_.push(buf[i], resp);
            if (len == 0) continue;

            uart_write_bytes(port_, resp.data(), len);
            uart_wait_tx_done(port_, pdMS_TO_TICKS(5));
            /* Tek telde kendi gönderdiğimiz baytlar RX'e geri döner: at.
             * Alıcının bir sonraki komutu ~7 ms sonra gelir. */
            uart_flush_input(port_);
            responder_.reset();
            break;  /* tampondaki kalan baytlar eski: yeni komutu bekle */
        }
    }
}
