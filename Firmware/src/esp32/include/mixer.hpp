#pragma once

/* Gaz/yaw → sol/sağ motor karışımı ve rampa — donanımdan bağımsız.
 *
 * Diferansiyel sürüş: sol = gaz + yaw, sağ = gaz - yaw
 * yaw > 0 → sağa dönüş (sol hızlanır, sağ yavaşlar) */

#include <algorithm>
#include <cmath>

#include "config.hpp"

namespace mixer {

struct Output {
    float left;
    float right;
};

/* Merkeze yakın küçük sapmaları sıfırlar, kalan aralığı 0..1'e yeniden ölçekler
 * (deadzone kenarında çıkış 0'dan başlar, sıçrama olmaz; tam çubuk yine 1.0). */
inline float apply_deadzone(float v, float deadzone)
{
    const float mag = std::fabs(v);
    if (mag <= deadzone) return 0.0f;
    return std::copysign(std::min((mag - deadzone) / (1.0f - deadzone), 1.0f), v);
}

/* throttle, yaw: -1..1. Çıkış her zaman -1..1 aralığındadır. */
inline Output mix(float throttle, float yaw, cfg::MixMode mode)
{
    throttle = std::clamp(throttle, -1.0f, 1.0f);
    yaw      = std::clamp(yaw, -1.0f, 1.0f);

    if (mode == cfg::MixMode::ThrottlePriority) {
        const float room = 1.0f - std::fabs(throttle);
        yaw = std::clamp(yaw, -room, room);
        return {throttle + yaw, throttle - yaw};
    }

    /* ScaleDown: taşma varsa ikisini aynı oranda küçült → dönüş oranı korunur */
    float left  = throttle + yaw;
    float right = throttle - yaw;
    const float peak = std::max(std::fabs(left), std::fabs(right));
    if (peak > 1.0f) {
        left  /= peak;
        right /= peak;
    }
    return {left, right};
}

/* current'ı target'a en fazla max_step kadar yaklaştırır. */
inline float ramp_toward(float current, float target, float max_step)
{
    if (target > current + max_step) return current + max_step;
    if (target < current - max_step) return current - max_step;
    return target;
}

}  // namespace mixer
