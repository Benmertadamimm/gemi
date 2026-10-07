#include "config.hpp"
#include "control.hpp"
#include "esp_err.h"
#include "ibus.hpp"
#include "motor.hpp"

/* Görev dağılımı:
 *   ibus task    (prio 6): UART'tan iBUS frame'lerini okur
 *   control task (prio 5): 50Hz arm/failsafe + mixing + motor yazma
 *   esp_timer    (motor_wd): komut gelmezse motorları nötre çeker */

extern "C" void app_main(void)
{
    static MotorDriver    motors;
    static ibus::Receiver rc;

    /* Önce motorlar: ESC'ler açılır açılmaz nötr sinyal görsün */
    ESP_ERROR_CHECK(motors.init());

    if constexpr (cfg::kEscCalibrationMode) {
        motors.calibrate_escs();
    }

    ESP_ERROR_CHECK(rc.start(static_cast<uart_port_t>(cfg::kIbusUartNum),
                             static_cast<gpio_num_t>(cfg::kIbusRxGpio)));

    static Controller controller(rc, motors);
    ESP_ERROR_CHECK(controller.start());
}
