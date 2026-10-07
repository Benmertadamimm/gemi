#pragma once

/* Güç ve sağlık izleme mantığı — donanımdan bağımsız, host testlerinde derlenir.
 *
 * Girdi : filtrelenmemiş ölçümler (voltaj, akım, sıcaklık) + geçen süre
 * Çıktı : batarya seviyesi, uyarılar ve motorlara uygulanacak güç katsayısı
 *         power_scale = min(batarya, akım, sıcaklık)   (1.0 = sınırsız, 0.0 = dur) */

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "config.hpp"

namespace power {

/* ── ADC voltajından fiziksel değere çevirme ─────────────────────────────────── */

inline float battery_volts(float adc_v)
{
    return adc_v * cfg::kBattVoltsPerVolt + cfg::kBattOffsetV;
}

inline float current_amps(float adc_v)
{
    return (adc_v - cfg::kCurrentZeroV) / cfg::kCurrentVoltsPerAmp;
}

/* NTC (GND'ye) + seri direnç (besleme'ye). Uçlar açık/kısa devreyse NAN döner. */
inline float ntc_celsius(float adc_v)
{
    const float vs = cfg::kNtcSupplyV;
    if (adc_v <= vs * 0.02f || adc_v >= vs * 0.98f) return NAN;
    const float r_ntc  = cfg::kNtcSeriesOhm * adc_v / (vs - adc_v);
    const float t0_k   = 298.15f;
    const float inv_t  = 1.0f / t0_k + std::log(r_ntc / cfg::kNtcR25Ohm) / cfg::kNtcBeta;
    return 1.0f / inv_t - 273.15f;
}

/* Birinci dereceden alçak geçiren filtre katsayısı. */
inline float ema_alpha(float dt_s, float tau_s)
{
    return tau_s <= 0.0f ? 1.0f : dt_s / (tau_s + dt_s);
}

/* ── Batarya ──────────────────────────────────────────────────────────────────── */

/* Sıralama önemli: büyük değer = daha kötü */
enum class BatteryLevel : uint8_t { Unknown, Ok, Low, Limit, Critical };

inline const char *to_string(BatteryLevel l)
{
    switch (l) {
        case BatteryLevel::Unknown:  return "BILINMIYOR";
        case BatteryLevel::Ok:       return "OK";
        case BatteryLevel::Low:      return "DUSUK";
        case BatteryLevel::Limit:    return "LIMIT";
        case BatteryLevel::Critical: return "KRITIK";
    }
    return "?";
}

inline BatteryLevel classify_cell(float cell_v)
{
    if (cell_v < cfg::kCellCriticalV) return BatteryLevel::Critical;
    if (cell_v < cfg::kCellLimitV)    return BatteryLevel::Limit;
    if (cell_v < cfg::kCellLowV)      return BatteryLevel::Low;
    return BatteryLevel::Ok;
}

class BatteryGuard {
public:
    void update(bool valid, float volts, float dt_s)
    {
        valid = valid && volts >= cfg::kBattPlausibleMinV && volts <= cfg::kBattPlausibleMaxV;
        fault_ = !valid;
        if (!valid) return;  /* sensör arızası: son seviye korunur, güç kesilmez */

        if (!initialized_) {
            filtered_    = volts;  /* ilk ölçümle başla, yavaş yükselme olmasın */
            initialized_ = true;
            if (level_ == BatteryLevel::Unknown) level_ = BatteryLevel::Ok;
        } else {
            filtered_ += ema_alpha(dt_s, cfg::kBattFilterTauS) * (volts - filtered_);
        }

        const BatteryLevel target = classify_cell(cell_volts());
        if (target > level_) {
            pending_s_ += dt_s;
            if (pending_s_ >= cfg::kBattDebounceS) {
                level_     = target;  /* kilitlenir: geri yükselmez */
                pending_s_ = 0.0f;
            }
        } else {
            pending_s_ = 0.0f;
        }
    }

    float scale() const
    {
        switch (level_) {
            case BatteryLevel::Limit:    return cfg::kBattLimitScale;
            case BatteryLevel::Critical: return cfg::kBattCriticalScale;
            default:                     return 1.0f;
        }
    }

    BatteryLevel level() const { return level_; }
    float volts() const { return filtered_; }
    float cell_volts() const { return filtered_ / static_cast<float>(cfg::kBattCellCount); }
    bool  valid() const { return initialized_ && !fault_; }
    bool  fault() const { return fault_; }

private:
    BatteryLevel level_       = BatteryLevel::Unknown;
    float        filtered_    = 0.0f;
    float        pending_s_   = 0.0f;
    bool         initialized_ = false;
    bool         fault_       = false;
};

/* ── Akım sınırlama ───────────────────────────────────────────────────────────── */

class CurrentLimiter {
public:
    void update(bool valid, float amps, float dt_s)
    {
        valid  = valid && std::fabs(amps) <= cfg::kCurrentPlausibleMaxA;
        fault_ = !valid;

        if (valid) {
            filtered_ = initialized_ ? filtered_ + ema_alpha(dt_s, cfg::kCurrentFilterTauS) * (amps - filtered_)
                                     : amps;
            initialized_ = true;
        }

        if (valid && filtered_ > cfg::kCurrentLimitA) {
            scale_ -= cfg::kCurrentDropPerSec * dt_s;
        } else if (!valid || filtered_ < cfg::kCurrentLimitA * 0.9f) {
            scale_ += cfg::kCurrentRecoverPerSec * dt_s;  /* arızada da sınırı bırak */
        }
        scale_ = std::clamp(scale_, cfg::kCurrentMinScale, 1.0f);
    }

    float scale() const { return scale_; }
    float amps() const { return filtered_; }
    bool  limiting() const { return scale_ < 1.0f; }
    bool  valid() const { return initialized_ && !fault_; }
    bool  fault() const { return fault_; }

private:
    float scale_       = 1.0f;
    float filtered_    = 0.0f;
    bool  initialized_ = false;
    bool  fault_       = false;
};

/* ── Sıcaklık ─────────────────────────────────────────────────────────────────── */

inline float temp_scale_for(float celsius)
{
    if (celsius <= cfg::kTempDerateStartC) return 1.0f;
    if (celsius >= cfg::kTempDerateEndC) return cfg::kTempMinScale;
    const float k = (celsius - cfg::kTempDerateStartC) / (cfg::kTempDerateEndC - cfg::kTempDerateStartC);
    return 1.0f + k * (cfg::kTempMinScale - 1.0f);
}

class TempGuard {
public:
    void update(bool valid, float celsius, float dt_s)
    {
        valid  = valid && !std::isnan(celsius) && celsius > -40.0f && celsius < 150.0f;
        fault_ = !valid;
        if (!valid) return;  /* arızada güç kesilmez (scale() = 1) */

        filtered_ = initialized_ ? filtered_ + ema_alpha(dt_s, cfg::kTempFilterTauS) * (celsius - filtered_)
                                 : celsius;
        initialized_ = true;
    }

    float scale() const { return valid() ? temp_scale_for(filtered_) : 1.0f; }
    float celsius() const { return filtered_; }
    bool  warning() const { return valid() && filtered_ >= cfg::kTempWarnC; }
    bool  valid() const { return initialized_ && !fault_; }
    bool  fault() const { return fault_; }

private:
    float filtered_    = 0.0f;
    bool  initialized_ = false;
    bool  fault_       = false;
};

/* ── Hepsi bir arada ──────────────────────────────────────────────────────────── */

struct Measurements {
    bool  voltage_enabled = false;
    bool  current_enabled = false;
    bool  temp_enabled    = false;
    float voltage_v       = 0.0f;
    float current_a       = 0.0f;
    float temp_c          = 0.0f;
};

struct Status {
    BatteryLevel battery       = BatteryLevel::Unknown;
    float        voltage_v     = 0.0f;   /* filtrelenmiş */
    float        cell_v        = 0.0f;
    float        current_a     = 0.0f;
    float        temp_c        = 0.0f;
    bool         voltage_valid = false;
    bool         current_valid = false;
    bool         temp_valid    = false;
    bool         current_limiting = false;
    bool         temp_warning  = false;
    float        battery_scale = 1.0f;
    float        current_scale = 1.0f;
    float        temp_scale    = 1.0f;
    float        power_scale   = 1.0f;   /* motorlara uygulanacak */
};

class Monitor {
public:
    void update(const Measurements &m, float dt_s)
    {
        if (m.voltage_enabled) battery_.update(true, m.voltage_v, dt_s);
        if (m.current_enabled) current_.update(true, m.current_a, dt_s);
        if (m.temp_enabled) temp_.update(true, m.temp_c, dt_s);

        status_.battery          = battery_.level();
        status_.voltage_v        = battery_.volts();
        status_.cell_v           = battery_.cell_volts();
        status_.voltage_valid    = battery_.valid();
        status_.current_a        = current_.amps();
        status_.current_valid    = current_.valid();
        status_.current_limiting = current_.limiting();
        status_.temp_c           = temp_.celsius();
        status_.temp_valid       = temp_.valid();
        status_.temp_warning     = temp_.warning();
        status_.battery_scale    = battery_.scale();
        status_.current_scale    = current_.scale();
        status_.temp_scale       = temp_.scale();
        status_.power_scale      = std::min({status_.battery_scale, status_.current_scale, status_.temp_scale});
    }

    const Status &status() const { return status_; }

private:
    BatteryGuard   battery_;
    CurrentLimiter current_;
    TempGuard      temp_;
    Status         status_;
};

}  // namespace power
