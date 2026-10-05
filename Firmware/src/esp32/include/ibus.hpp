#pragma once

/* FS-iA6B iBUS alıcısı: arka plan task'ı UART'tan frame okur, son frame'i saklar.
 *
 * Kullanım:
 *   ibus::Receiver rc;
 *   rc.start(UART_NUM_1, GPIO_NUM_16);   // başlangıçta bir kez
 *   auto s = rc.snapshot();              // herhangi bir task'tan, bloklamaz
 *   if (s.has_frame && s.age_ms < 200) { float gaz = ibus::normalize(s.ch[2]); } */

#include <atomic>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "ibus_parser.hpp"

namespace ibus {

struct Snapshot {
    Channels ch{};                  /* son geçerli frame                      */
    bool     has_frame   = false;   /* boot'tan beri en az bir frame geldi mi */
    uint32_t age_ms      = 0;       /* son geçerli frame'den bu yana geçen    */
    uint32_t frame_count = 0;
    uint32_t error_count = 0;       /* checksum hatası sayısı                 */
};

class Receiver {
public:
    /* UART'ı yapılandırır ve okuma task'ını başlatır. */
    esp_err_t start(uart_port_t port, gpio_num_t rx_pin);

    /* Son frame'in kopyası. Thread-safe. */
    Snapshot snapshot() const;

private:
    static void task_entry(void *arg);
    [[noreturn]] void run();
    void publish(const Channels &frame);

    uart_port_t port_ = UART_NUM_1;
    FrameParser parser_;

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    Channels shared_{};
    int64_t  last_frame_us_ = 0;
    bool     has_frame_     = false;
    uint32_t frame_count_   = 0;

    std::atomic<uint32_t> error_count_{0};
};

}  // namespace ibus
