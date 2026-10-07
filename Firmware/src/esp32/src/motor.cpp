#include "motor.hpp"

#include "config.hpp"
#include "esc_pulse.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr const char *TAG = "motor";

constexpr uint32_t kTimerResolutionHz = 1'000'000;  /* 1 tick = 1µs   */
constexpr uint32_t kPwmPeriodTicks    = 20'000;     /* 20ms = 50Hz    */
constexpr uint64_t kWatchdogPeriodUs  = 10'000;     /* 10ms'de bir kontrol */

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

uint32_t neutral_left()  { return esc::to_pulse_us(0.0f, cfg::kLeftTrimUs, cfg::kLeftReversed); }
uint32_t neutral_right() { return esc::to_pulse_us(0.0f, cfg::kRightTrimUs, cfg::kRightReversed); }

}  // namespace

esp_err_t MotorDriver::init()
{
    mcpwm_timer_config_t timer_cfg = {};
    timer_cfg.group_id      = cfg::kMcpwmGroup;
    timer_cfg.clk_src       = MCPWM_TIMER_CLK_SRC_DEFAULT;
    timer_cfg.resolution_hz = kTimerResolutionHz;
    timer_cfg.count_mode    = MCPWM_TIMER_COUNT_MODE_UP;
    timer_cfg.period_ticks  = kPwmPeriodTicks;

    mcpwm_timer_handle_t timer = nullptr;
    ESP_RETURN_ON_ERROR(mcpwm_new_timer(&timer_cfg, &timer), TAG, "mcpwm_new_timer");
    ESP_RETURN_ON_ERROR(setup_channel(timer, cfg::kMotorLeftGpio, &cmpr_left_), TAG, "sol kanal");
    ESP_RETURN_ON_ERROR(setup_channel(timer, cfg::kMotorRightGpio, &cmpr_right_), TAG, "sag kanal");

    write_pulses(neutral_left(), neutral_right());  /* timer başlamadan nötr yükle */
    ESP_RETURN_ON_ERROR(mcpwm_timer_enable(timer), TAG, "mcpwm_timer_enable");
    ESP_RETURN_ON_ERROR(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP), TAG, "mcpwm_timer_start");

    esp_timer_create_args_t wd_args = {};
    wd_args.callback        = &MotorDriver::watchdog_cb;
    wd_args.arg             = this;
    wd_args.dispatch_method = ESP_TIMER_TASK;
    wd_args.name            = "motor_wd";
    feed_watchdog();
    ESP_RETURN_ON_ERROR(esp_timer_create(&wd_args, &wd_timer_), TAG, "esp_timer_create");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(wd_timer_, kWatchdogPeriodUs), TAG, "esp_timer_start");

    ESP_LOGI(TAG, "Motor init OK — SOL:GPIO%d SAG:GPIO%d, notr L=%luus R=%luus",
             cfg::kMotorLeftGpio, cfg::kMotorRightGpio,
             static_cast<unsigned long>(neutral_left()), static_cast<unsigned long>(neutral_right()));
    return ESP_OK;
}

esp_err_t MotorDriver::setup_channel(mcpwm_timer_handle_t timer, int gpio, mcpwm_cmpr_handle_t *out_cmpr)
{
    mcpwm_operator_config_t oper_cfg = {};
    oper_cfg.group_id = cfg::kMcpwmGroup;
    mcpwm_oper_handle_t oper = nullptr;
    ESP_RETURN_ON_ERROR(mcpwm_new_operator(&oper_cfg, &oper), TAG, "mcpwm_new_operator");
    ESP_RETURN_ON_ERROR(mcpwm_operator_connect_timer(oper, timer), TAG, "connect_timer");

    /* Compare değeri periyot başında (TEZ) güncellenir → yarım pulse oluşmaz */
    mcpwm_comparator_config_t cmpr_cfg = {};
    cmpr_cfg.flags.update_cmp_on_tez = true;
    ESP_RETURN_ON_ERROR(mcpwm_new_comparator(oper, &cmpr_cfg, out_cmpr), TAG, "mcpwm_new_comparator");

    mcpwm_generator_config_t gen_cfg = {};
    gen_cfg.gen_gpio_num = gpio;
    mcpwm_gen_handle_t gen = nullptr;
    ESP_RETURN_ON_ERROR(mcpwm_new_generator(oper, &gen_cfg, &gen), TAG, "mcpwm_new_generator");

    /* Periyot başında HIGH, compare değerinde LOW → pulse genişliği = compare (µs) */
    mcpwm_gen_timer_event_action_t on_empty = {};
    on_empty.direction = MCPWM_TIMER_DIRECTION_UP;
    on_empty.event     = MCPWM_TIMER_EVENT_EMPTY;
    on_empty.action    = MCPWM_GEN_ACTION_HIGH;
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_action_on_timer_event(gen, on_empty), TAG, "timer_event");

    mcpwm_gen_compare_event_action_t on_compare = {};
    on_compare.direction  = MCPWM_TIMER_DIRECTION_UP;
    on_compare.comparator = *out_cmpr;
    on_compare.action     = MCPWM_GEN_ACTION_LOW;
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_action_on_compare_event(gen, on_compare), TAG, "compare_event");
    return ESP_OK;
}

void MotorDriver::set(float left, float right)
{
    if (!cmpr_left_ || !cmpr_right_) return;
    write_pulses(esc::to_pulse_us(left, cfg::kLeftTrimUs, cfg::kLeftReversed),
                 esc::to_pulse_us(right, cfg::kRightTrimUs, cfg::kRightReversed));
    feed_watchdog();
}

void MotorDriver::stop()
{
    if (!cmpr_left_ || !cmpr_right_) return;
    write_pulses(neutral_left(), neutral_right());
    feed_watchdog();
}

void MotorDriver::write_pulses(uint32_t left_us, uint32_t right_us)
{
    mcpwm_comparator_set_compare_value(cmpr_left_, left_us);
    mcpwm_comparator_set_compare_value(cmpr_right_, right_us);
    last_left_us_.store(left_us);
    last_right_us_.store(right_us);
}

void MotorDriver::feed_watchdog()
{
    last_cmd_ms_.store(now_ms());
    if (wd_tripped_.exchange(false)) ESP_LOGI(TAG, "Komut akisi geri geldi");
}

void MotorDriver::watchdog_cb(void *arg)
{
    auto *self = static_cast<MotorDriver *>(arg);
    const uint32_t last = self->last_cmd_ms_.load();  /* önce oku: now >= last garanti */
    if (now_ms() - last <= cfg::kMotorCmdTimeoutMs) return;

    self->write_pulses(neutral_left(), neutral_right());
    if (!self->wd_tripped_.exchange(true)) {
        ESP_LOGE(TAG, "%lu ms komut gelmedi — motorlar NOTR'e cekildi (kontrol task'i kilitlenmis olabilir)",
                 static_cast<unsigned long>(cfg::kMotorCmdTimeoutMs));
    }
}

void MotorDriver::calibrate_escs()
{
    /* Watchdog kalibrasyondaki MAX sinyalini nötre çekmesin */
    if (wd_timer_) esp_timer_stop(wd_timer_);

    ESP_LOGW(TAG, "=== ESC KALIBRASYON MODU ===");
    ESP_LOGW(TAG, "1) ESC guc kaynagi KAPALI olmali, pervane/itki guvenli konumda olmali.");
    ESP_LOGW(TAG, "2) 5 saniye icinde MAX sinyal baslayacak, o sirada ESC'ye GUC VER.");
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGW(TAG, "MAX sinyal (%dus) gonderiliyor — ESC'nin ilk beep'ini bekle...", cfg::kPulseMaxUs);
    write_pulses(cfg::kPulseMaxUs, cfg::kPulseMaxUs);
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGW(TAG, "NOTR sinyale (%dus) geciliyor — ikinci beep'i bekle...", cfg::kPulseMidUs);
    write_pulses(cfg::kPulseMidUs, cfg::kPulseMidUs);
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGW(TAG, "Kalibrasyon tamamlandi. config.hpp'deki kEscCalibrationMode'u false yapip yeniden yukle.");
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
