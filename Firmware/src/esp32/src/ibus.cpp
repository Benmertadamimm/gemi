#include "ibus.hpp"

#include <algorithm>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace ibus {

namespace {

constexpr const char *TAG = "ibus";

constexpr int         kBaud          = 115200;
constexpr int         kRxBufSize     = 1024;
constexpr uint32_t    kTaskStack     = 4096;
constexpr UBaseType_t kTaskPriority  = 6;  /* kontrol task'ından yüksek */
constexpr uint32_t    kReadTimeoutMs = 50;

}  // namespace

esp_err_t Receiver::start(uart_port_t port, gpio_num_t rx_pin)
{
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
    ESP_RETURN_ON_ERROR(uart_set_pin(port_, UART_PIN_NO_CHANGE, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                        TAG, "uart_set_pin");
    /* Kablo koparsa hat boşta kalmasın (gürültü yerine sessizlik → timeout) */
    gpio_set_pull_mode(rx_pin, GPIO_PULLUP_ONLY);

    if (xTaskCreate(&Receiver::task_entry, "ibus", kTaskStack, this, kTaskPriority, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "task olusturulamadi");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "iBUS dinleniyor — UART%d RX:GPIO%d", static_cast<int>(port_), static_cast<int>(rx_pin));
    return ESP_OK;
}

Snapshot Receiver::snapshot() const
{
    Snapshot s;
    int64_t last_us;
    portENTER_CRITICAL(&mux_);
    s.ch          = shared_;
    s.has_frame   = has_frame_;
    s.frame_count = frame_count_;
    last_us       = last_frame_us_;
    portEXIT_CRITICAL(&mux_);

    s.error_count = error_count_.load(std::memory_order_relaxed);
    s.age_ms      = s.has_frame ? static_cast<uint32_t>((esp_timer_get_time() - last_us) / 1000) : UINT32_MAX;
    return s;
}

void Receiver::task_entry(void *arg)
{
    static_cast<Receiver *>(arg)->run();
}

void Receiver::run()
{
    uint8_t  buf[64];
    Channels frame{};

    for (;;) {
        /* İlk baytı bekle, sonra tamponda ne varsa bloklamadan al.
         * Böylece frame gelir gelmez işlenir, gecikme birikmez. */
        int n = uart_read_bytes(port_, buf, 1, pdMS_TO_TICKS(kReadTimeoutMs));
        if (n <= 0) continue;

        size_t buffered = 0;
        uart_get_buffered_data_len(port_, &buffered);
        if (buffered > 0) {
            const size_t want = std::min(buffered, sizeof(buf) - 1);
            const int    m    = uart_read_bytes(port_, buf + 1, want, 0);
            if (m > 0) n += m;
        }

        for (int i = 0; i < n; ++i) {
            if (parser_.push(buf[i], frame)) publish(frame);
        }
        error_count_.store(parser_.checksum_errors(), std::memory_order_relaxed);
    }
}

void Receiver::publish(const Channels &frame)
{
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&mux_);
    shared_        = frame;
    last_frame_us_ = now;
    has_frame_     = true;
    ++frame_count_;
    portEXIT_CRITICAL(&mux_);
}

}  // namespace ibus
