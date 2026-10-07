#pragma once

/* Kumanda → motor kontrol döngüsü (50Hz FreeRTOS task'ı).
 *
 * Her döngüde:
 *   1. iBUS snapshot al, link sağlığını kontrol et (timeout + değer aralığı)
 *   2. Arm/failsafe durum makinesini güncelle
 *   3. ARMED değilse → motorlar anında nötr
 *      ARMED ise    → deadzone → limit → mix → (ramp) → motor yaz */

#include <cstdint>

#include "arming.hpp"
#include "esp_err.h"
#include "ibus.hpp"
#include "motor.hpp"

class Controller {
public:
    Controller(ibus::Receiver &rc, MotorDriver &motors);

    /* Kontrol task'ını başlatır. */
    esp_err_t start();

private:
    static void task_entry(void *arg);
    [[noreturn]] void run();
    void step(uint32_t now_ms);
    void log_transition(arming::State from, arming::State to, const ibus::Snapshot &rc, bool values_ok);
    void log_status(arming::State st, const ibus::Snapshot &rc, float throttle, float yaw);

    ibus::Receiver &rc_;
    MotorDriver    &motors_;
    arming::Fsm     fsm_;

    float    out_left_     = 0.0f;
    float    out_right_    = 0.0f;
    uint32_t start_ms_     = 0;
    uint32_t last_log_ms_  = 0;
    bool     esc_ready_    = false;
};
