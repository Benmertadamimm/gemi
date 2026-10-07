#pragma once

/* İki thruster ESC'si için MCPWM sürücüsü (50Hz, 1000..2000µs).
 *
 * Kullanım:
 *   MotorDriver motors;
 *   motors.init();                 // app_main başında bir kez, ESC'ler nötr görür
 *   motors.set(gaz + yaw, gaz - yaw);  // -1.0..1.0, clamp otomatik
 *   motors.stop();                 // failsafe / disarm
 *
 * Komut watchdog'u: set()/stop() cfg::kMotorCmdTimeoutMs boyunca çağrılmazsa
 * (kontrol task'ı kilitlendi, çöktü vb.) sürücü motorları kendiliğinden nötre çeker. */

#include <atomic>
#include <cstdint>

#include "driver/mcpwm_prelude.h"
#include "esp_err.h"
#include "esp_timer.h"

class MotorDriver {
public:
    struct Pulses {
        uint32_t left_us;
        uint32_t right_us;
    };

    /* MCPWM timer + operator + comparator + generator kurar, nötr pulse ile başlar,
     * komut watchdog'unu çalıştırır. */
    esp_err_t init();

    /* left/right: -1.0..1.0 (sınır dışı clamp edilir). */
    void set(float left, float right);

    /* Her iki motoru (trim dahil) nötre çeker. */
    void stop();

    /* ESC kalibrasyon dizisi: MAX → bekle → NÖTR → bekle. Geri dönmez.
     * DİKKAT: pervane/itki suda değilken ve güvenli bir ortamda çalıştır. */
    [[noreturn]] void calibrate_escs();

    Pulses last_pulses() const { return {last_left_us_.load(), last_right_us_.load()}; }
    bool   watchdog_tripped() const { return wd_tripped_.load(); }

private:
    esp_err_t setup_channel(mcpwm_timer_handle_t timer, int gpio, mcpwm_cmpr_handle_t *out_cmpr);
    void      write_pulses(uint32_t left_us, uint32_t right_us);
    void      feed_watchdog();
    static void watchdog_cb(void *arg);

    mcpwm_cmpr_handle_t cmpr_left_  = nullptr;
    mcpwm_cmpr_handle_t cmpr_right_ = nullptr;
    esp_timer_handle_t  wd_timer_   = nullptr;

    std::atomic<uint32_t> last_cmd_ms_{0};
    std::atomic<bool>     wd_tripped_{false};
    std::atomic<uint32_t> last_left_us_{0};
    std::atomic<uint32_t> last_right_us_{0};
};
