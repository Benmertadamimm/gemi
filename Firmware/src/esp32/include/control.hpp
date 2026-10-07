#pragma once

/* Kumanda → motor kontrol döngüsü (50Hz FreeRTOS task'ı).
 *
 * Her döngüde:
 *   1. iBUS snapshot al, link sağlığını kontrol et (timeout + değer aralığı)
 *   2. Kill switch'i oku (debounce)
 *   3. Arm/failsafe durum makinesini güncelle
 *   4. ARMED değilse → motorlar anında nötr
 *      ARMED ise    → deadzone → limit → mix → güç katsayısı → (ramp) → motor yaz */

#include <cstdint>

#include "arming.hpp"
#include "esp_err.h"
#include "ibus.hpp"
#include "motor.hpp"
#include "power_monitor.hpp"

class Controller {
public:
    Controller(ibus::Receiver &rc, MotorDriver &motors, PowerMonitor &power);

    /* Kill switch GPIO'sunu kurar ve kontrol task'ını başlatır. */
    esp_err_t start();

private:
    static void task_entry(void *arg);
    [[noreturn]] void run();
    void step(uint32_t now_ms);
    bool read_kill();
    void log_transition(arming::State from, arming::State to, const ibus::Snapshot &rc, bool values_ok);
    void log_status(arming::State st, const ibus::Snapshot &rc, float throttle, float yaw, float power_scale);

    ibus::Receiver   &rc_;
    MotorDriver      &motors_;
    PowerMonitor     &power_;
    arming::Fsm       fsm_;
    arming::Debouncer kill_;

    float    out_left_    = 0.0f;
    float    out_right_   = 0.0f;
    uint32_t start_ms_    = 0;
    uint32_t last_log_ms_ = 0;
    bool     esc_ready_   = false;
};
