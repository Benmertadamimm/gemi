#pragma once

/* -1..1 motor komutunu ESC pulse genişliğine (µs) çevirir — donanımdan bağımsız. */

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "config.hpp"

namespace esc {

/* -1.0 → kPulseMinUs | 0.0 → kPulseMidUs | 1.0 → kPulseMaxUs
 * Trim eklenir, sonuç min..max aralığına kırpılır. */
inline uint32_t to_pulse_us(float value, int trim_us, bool reversed)
{
    if (std::isnan(value)) value = 0.0f;  /* bozuk girdi → nötr */
    value = std::clamp(value, -1.0f, 1.0f);
    if (reversed) value = -value;

    const float half_range = value >= 0.0f ? static_cast<float>(cfg::kPulseMaxUs - cfg::kPulseMidUs)
                                           : static_cast<float>(cfg::kPulseMidUs - cfg::kPulseMinUs);
    int pulse = cfg::kPulseMidUs + static_cast<int>(std::lround(value * half_range)) + trim_us;
    pulse = std::clamp(pulse, cfg::kPulseMinUs, cfg::kPulseMaxUs);
    return static_cast<uint32_t>(pulse);
}

}  // namespace esc
