#pragma once

/* iBUS telemetri: FS-iA6B SENS portuna sensör gibi cevap verir; batarya voltajı,
 * PDB sıcaklığı ve akım FS-i6X ekranında görünür.
 *
 * Bağlantı: FS-iA6B "SENS" sinyal pini → GPIO15 (tek tel, ESP32 open-drain sürer). */

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "ibus_telemetry.hpp"
#include "power_monitor.hpp"

class Telemetry {
public:
    explicit Telemetry(PowerMonitor &power) : power_(power) {}

    esp_err_t start(uart_port_t port, gpio_num_t pin);

private:
    static void task_entry(void *arg);
    [[noreturn]] void run();
    void refresh_values();

    PowerMonitor &power_;
    uart_port_t   port_ = UART_NUM_2;
    ibus::telemetry::Responder responder_;
    uint8_t addr_voltage_ = 0;
    uint8_t addr_temp_    = 0;
    uint8_t addr_current_ = 0;
};
