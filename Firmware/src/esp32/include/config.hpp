#pragma once

/* Tüm pin atamaları ve ayar parametreleri tek yerde.
 * Bu dosya ESP-IDF başlığı içermez; host testleri de kullanabilir. */

#include <cstdint>

namespace cfg {

/* ── Pinler ───────────────────────────────────────────────────────────────── */
inline constexpr int kIbusUartNum    = 1;    /* UART1                        */
inline constexpr int kIbusRxGpio     = 16;   /* FS-iA6B iBUS SERVO → GPIO16  */
inline constexpr int kMotorLeftGpio  = 17;   /* sol thruster ESC sinyali     */
inline constexpr int kMotorRightGpio = 18;   /* sağ thruster ESC sinyali     */
inline constexpr int kMcpwmGroup     = 0;

/* ── Kumanda kanalları (0 indeksli: 2 = CH3) ──────────────────────────────── */
inline constexpr int kChThrottle = 2;   /* CH3 — sol çubuk dikey  */
inline constexpr int kChYaw      = 3;   /* CH4 — sol çubuk yatay  */
inline constexpr int kChArm      = 4;   /* CH5 — arm switch (SWA) */

/* ── Arm (motorları devreye alma) ─────────────────────────────────────────────
 * kUseArmSwitch=true : CH5 switch'i ARM konumunda + çubuklar ortada → ARMED.
 *                      Boot'ta switch ARM konumundaysa önce kapatıp açmak gerekir.
 * kUseArmSwitch=false: link gelip çubuklar ortadaysa otomatik ARMED (switch yok).
 * kRequireSwitchCycleAfterFailsafe=true: link geri gelince switch'i kapat-aç
 *                      yapmadan tekrar ARM olmaz (false: çubukları ortalamak yeter). */
inline constexpr bool     kUseArmSwitch                    = true;
inline constexpr bool     kRequireSwitchCycleAfterFailsafe = false;
inline constexpr uint16_t kArmSwitchThresholdUs            = 1700;  /* CH5 >= → ARM */
inline constexpr float    kArmStickTolerance               = 0.05f; /* "ortada" sayılan sapma */

/* ── Link kaybı / failsafe ────────────────────────────────────────────────────
 * kRcTimeoutMs      : bu süre geçerli iBUS frame gelmezse FAILSAFE → motor dur
 * kRcValidMin/MaxUs : bu aralık dışındaki kanal değeri bozuk sayılır → FAILSAFE
 * kMotorCmdTimeoutMs: motor sürücüsü bu süre komut almazsa kendini nötre çeker
 *                     (kontrol task'ı kilitlenirse son koruma katmanı) */
inline constexpr uint32_t kRcTimeoutMs       = 200;
inline constexpr uint16_t kRcValidMinUs      = 900;
inline constexpr uint16_t kRcValidMaxUs      = 2100;
inline constexpr uint32_t kMotorCmdTimeoutMs = 100;

/* ── ESC ──────────────────────────────────────────────────────────────────────
 * 50Hz PWM. 1000µs = tam geri | 1500µs = nötr | 2000µs = tam ileri
 * Trim: ESC'nin gerçek nötrü 1500µs'den kaymışsa telafi (pozitif → yukarı).
 * Reversed: motor ters dönüyorsa (veya ayna montajsa) yönü yazılımla çevir. */
inline constexpr int      kPulseMinUs     = 1000;
inline constexpr int      kPulseMidUs     = 1500;
inline constexpr int      kPulseMaxUs     = 2000;
inline constexpr int      kLeftTrimUs     = -13;
inline constexpr int      kRightTrimUs    = 0;
inline constexpr bool     kLeftReversed   = false;
inline constexpr bool     kRightReversed  = false;
inline constexpr uint32_t kEscArmDelayMs  = 2000;  /* boot'ta nötrde bekleme */

/* true yapıp yükle → normal kontrol yerine ESC kalibrasyon dizisi çalışır.
 * Kalibrasyon bitince false yapıp tekrar yüklemeyi unutma. */
inline constexpr bool kEscCalibrationMode = false;

/* ── Kontrol döngüsü ──────────────────────────────────────────────────────────
 * kDeadzone   : çubuk merkeze dönünce ±bu kadar sapma sıfır sayılır
 * kMaxThrottle: tam çubukta verilecek max ileri/geri güç (0..1)
 * kMaxYaw     : tam çubukta verilecek max dönüş gücü (0..1)
 * kRamp*      : motor çıkışının saniyedeki max değişimi (1.0 → 0'dan tam güce 1s) */
enum class MixMode : uint8_t {
    ScaleDown,        /* gaz ve yaw orantılı küçülür, yön korunur              */
    ThrottlePriority, /* gaz korunur, yaw sığdığı kadar girer                  */
};

inline constexpr uint32_t kControlPeriodMs = 20;   /* 50Hz */
inline constexpr float    kDeadzone        = 0.01f;
inline constexpr float    kMaxThrottle     = 0.10f;
inline constexpr float    kMaxYaw          = 0.07f;
inline constexpr MixMode  kMixMode         = MixMode::ScaleDown;
inline constexpr bool     kRampEnabled     = false; /* GEÇİCİ: testte kapalı tutuluyor */
inline constexpr float    kRampRatePerSec  = 1.0f;

inline constexpr uint32_t kLogPeriodMs = 250;  /* telemetri log aralığı */

}  // namespace cfg
