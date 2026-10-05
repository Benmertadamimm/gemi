#include "control.hpp"

#include <cmath>

#include "config.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mixer.hpp"

namespace {

constexpr const char *TAG = "control";

constexpr uint32_t    kTaskStack    = 4096;
constexpr UBaseType_t kTaskPriority = 5;

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

/* Kullanılan kanallar mantıklı aralıkta mı? Bozuk veri → link yok say. */
bool channels_valid(const ibus::Channels &ch)
{
    for (int idx : {cfg::kChThrottle, cfg::kChYaw, cfg::kChArm}) {
        if (ch[idx] < cfg::kRcValidMinUs || ch[idx] > cfg::kRcValidMaxUs) return false;
    }
    return true;
}

}  // namespace

Controller::Controller(ibus::Receiver &rc, MotorDriver &motors)
    : rc_(rc),
      motors_(motors),
      fsm_({cfg::kUseArmSwitch, cfg::kRequireSwitchCycleAfterFailsafe})
{
}

esp_err_t Controller::start()
{
    if (xTaskCreate(&Controller::task_entry, "control", kTaskStack, this, kTaskPriority, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "task olusturulamadi");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void Controller::task_entry(void *arg)
{
    static_cast<Controller *>(arg)->run();
}

void Controller::run()
{
    start_ms_ = now_ms();
    ESP_LOGI(TAG, "ESC arm icin %lu ms notrde bekleniyor...", static_cast<unsigned long>(cfg::kEscArmDelayMs));

    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        step(now_ms());
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(cfg::kControlPeriodMs));
    }
}

void Controller::step(uint32_t now)
{
    const ibus::Snapshot rc = rc_.snapshot();
    const bool values_ok = channels_valid(rc.ch);
    const bool link_ok   = rc.has_frame && rc.age_ms <= cfg::kRcTimeoutMs && values_ok;

    const float throttle_raw = ibus::normalize(rc.ch[cfg::kChThrottle]);
    const float yaw_raw      = ibus::normalize(rc.ch[cfg::kChYaw]);

    const arming::Input in{
        link_ok,
        rc.ch[cfg::kChArm] >= cfg::kArmSwitchThresholdUs,
        std::fabs(throttle_raw) <= cfg::kArmStickTolerance && std::fabs(yaw_raw) <= cfg::kArmStickTolerance,
    };

    if (!esc_ready_ && now - start_ms_ >= cfg::kEscArmDelayMs) {
        esc_ready_ = true;
        ESP_LOGI(TAG, "Hazir — %s", cfg::kUseArmSwitch ? "arm icin: CH5 switch ARM + cubuklar ortada"
                                                      : "cubuklar ortadayken otomatik arm olur");
    }

    const arming::State prev = fsm_.state();
    const arming::State st   = esc_ready_ ? fsm_.update(in) : prev;
    if (st != prev) log_transition(prev, st, rc, values_ok);

    float throttle = 0.0f;
    float yaw      = 0.0f;

    if (st != arming::State::Armed) {
        /* FAILSAFE / DISARMED / WAIT_LINK: rampa yok, anında nötr */
        out_left_  = 0.0f;
        out_right_ = 0.0f;
        motors_.stop();
    } else {
        throttle = mixer::apply_deadzone(throttle_raw, cfg::kDeadzone) * cfg::kMaxThrottle;
        yaw      = mixer::apply_deadzone(yaw_raw, cfg::kDeadzone) * cfg::kMaxYaw;
        const mixer::Output target = mixer::mix(throttle, yaw, cfg::kMixMode);

        if (cfg::kRampEnabled) {
            const float step_max = cfg::kRampRatePerSec * (cfg::kControlPeriodMs / 1000.0f);
            out_left_  = mixer::ramp_toward(out_left_, target.left, step_max);
            out_right_ = mixer::ramp_toward(out_right_, target.right, step_max);
        } else {
            out_left_  = target.left;
            out_right_ = target.right;
        }
        motors_.set(out_left_, out_right_);
    }

    if (now - last_log_ms_ >= cfg::kLogPeriodMs) {
        last_log_ms_ = now;
        log_status(st, rc, throttle, yaw);
    }
}

void Controller::log_transition(arming::State from, arming::State to, const ibus::Snapshot &rc, bool values_ok)
{
    using arming::State;
    const char *f = arming::to_string(from);
    const char *t = arming::to_string(to);

    if (to == State::Failsafe) {
        if (!values_ok && rc.age_ms <= cfg::kRcTimeoutMs) {
            ESP_LOGW(TAG, "%s -> %s: kanal degeri gecersiz — MOTORLAR DURDURULDU", f, t);
        } else {
            ESP_LOGW(TAG, "%s -> %s: %lu ms frame yok, kumanda baglantisi koptu — MOTORLAR DURDURULDU", f, t,
                     static_cast<unsigned long>(rc.age_ms));
        }
    } else if (from == State::Failsafe) {
        ESP_LOGW(TAG, "%s -> %s: baglanti geri geldi — tekrar arm icin cubuklari ortala", f, t);
    } else if (to == State::Armed) {
        ESP_LOGW(TAG, "%s -> %s: motorlar AKTIF", f, t);
    } else {
        ESP_LOGI(TAG, "%s -> %s", f, t);
    }
}

void Controller::log_status(arming::State st, const ibus::Snapshot &rc, float throttle, float yaw)
{
    const MotorDriver::Pulses p = motors_.last_pulses();
    ESP_LOGI(TAG, "%-9s gaz=%+.2f yaw=%+.2f | L=%+.2f R=%+.2f | %lu/%luus | rc yas=%ldms frame=%lu err=%lu",
             arming::to_string(st), static_cast<double>(throttle), static_cast<double>(yaw),
             static_cast<double>(out_left_), static_cast<double>(out_right_),
             static_cast<unsigned long>(p.left_us), static_cast<unsigned long>(p.right_us),
             rc.has_frame ? static_cast<long>(rc.age_ms) : -1L,
             static_cast<unsigned long>(rc.frame_count), static_cast<unsigned long>(rc.error_count));
}
