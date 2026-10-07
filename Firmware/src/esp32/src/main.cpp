#include "config.hpp"
#include "control.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "ibus.hpp"
#include "motor.hpp"
#include "power_monitor.hpp"
#include "telemetry.hpp"

/* Görev dağılımı:
 *   ibus task      (prio 6): UART1'den iBUS kanal frame'lerini okur
 *   telemetry task (prio 6): UART2 tek tel, alıcının sensör sorgularına cevap verir
 *   control task   (prio 5): 50Hz arm/failsafe/kill + mixing + motor yazma
 *   power task     (prio 4): 10Hz voltaj/akım/sıcaklık ölçümü ve güç sınırı
 *   esp_timer      (motor_wd): komut gelmezse motorları nötre çeker */

extern "C" void app_main(void)
{
    static MotorDriver    motors;
    static ibus::Receiver rc;
    static PowerMonitor   power;

    /* Önce motorlar: ESC'ler açılır açılmaz nötr sinyal görsün */
    ESP_ERROR_CHECK(motors.init());

    if constexpr (cfg::kEscCalibrationMode) {
        motors.calibrate_escs();
    }

    ESP_ERROR_CHECK(rc.start(static_cast<uart_port_t>(cfg::kIbusUartNum),
                             static_cast<gpio_num_t>(cfg::kIbusRxGpio)));

    /* Güç izleme hatası sürüşü engellemesin: logla, korumasız devam et */
    if (power.start() != ESP_OK) ESP_LOGE("main", "Guc izleme baslatilamadi — koruma YOK");

    static Controller controller(rc, motors, power);
    ESP_ERROR_CHECK(controller.start());

    if constexpr (cfg::kTelemetryEnabled) {
        static Telemetry telemetry(power);
        if (telemetry.start(static_cast<uart_port_t>(cfg::kTelemetryUartNum),
                            static_cast<gpio_num_t>(cfg::kTelemetryGpio)) != ESP_OK) {
            ESP_LOGE("main", "Telemetri baslatilamadi");
        }
    }
}
