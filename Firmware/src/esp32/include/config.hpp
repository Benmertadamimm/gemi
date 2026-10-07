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

/* Faz 3 — güç izleme ve güvenlik (ADC1: GPIO1..10, Wi-Fi ile çakışmaz) */
inline constexpr int kBattVoltageGpio = 4;   /* PDB voltaj çıkışı   → ADC1_CH3 */
inline constexpr int kCurrentGpio     = 5;   /* PDB Hall akım çıkışı → ADC1_CH4 */
inline constexpr int kTempGpio        = 6;   /* PDB termistör       → ADC1_CH5 */
inline constexpr int kKillSwitchGpio  = 7;   /* acil durdurma butonu (NC kontak → GND) */
inline constexpr int kTelemetryUartNum = 2;  /* UART2, tek tel (half-duplex)  */
inline constexpr int kTelemetryGpio    = 15; /* FS-iA6B iBUS SENS → GPIO15    */

/* ── Kumanda kanalları (0 indeksli: 1 = CH2) ──────────────────────────────────
 * FS-i6X'te CH3 (gaz) çubuğu yaylı DEĞİLDİR: bırakınca ortaya dönmez, olduğu yerde
 * kalır. Çift yönlü (ileri/geri) ESC'de bu, çubuk bırakılınca teknenin gitmeye devam
 * etmesi demektir. Bu yüzden sürüş, kendiliğinden ortalanan çubuğa alındı:
 *   CH2 (elevator) = gaz,  CH1 (aileron) = dönüş  → Mode 2'de SAĞ çubuk tek başına sürer.
 * Kumanda modundan (Mode 1/2) bağımsız olarak CH1, CH2, CH4 yaylıdır; CH3 değildir.
 * Dönüşü sol çubuğa (CH4, rudder) almak istersen kChYaw = 3 yap.
 * *Reversed: çubuk ileri itilince log'da gaz negatif görünüyorsa true yap
 *            (ya da kumandada Functions setup → Reverse). */
inline constexpr int  kChThrottle       = 1;      /* CH2 — sağ çubuk dikey (yaylı)  */
inline constexpr int  kChYaw            = 0;      /* CH1 — sağ çubuk yatay (yaylı)  */
inline constexpr int  kChArm            = 4;      /* CH5 — arm switch (SwA)         */
inline constexpr bool kThrottleReversed = false;
inline constexpr bool kYawReversed      = false;

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
 * kDeadzone   : çubuk merkeze dönünce ±bu kadar sapma sıfır sayılır (sonrası
 *               yeniden ölçeklenir, deadzone kenarında sıçrama olmaz)
 * kMaxThrottle: tam çubukta verilecek max ileri/geri güç (0..1)
 * kMaxYaw     : tam çubukta verilecek max dönüş gücü (0..1)
 * kRamp*      : motor çıkışının saniyedeki max değişimi (1.0 → 0'dan tam güce 1s) */
enum class MixMode : uint8_t {
    ScaleDown,        /* gaz ve yaw orantılı küçülür, yön korunur              */
    ThrottlePriority, /* gaz korunur, yaw sığdığı kadar girer                  */
};

inline constexpr uint32_t kControlPeriodMs = 20;   /* 50Hz */
inline constexpr float    kDeadzone        = 0.03f; /* yaylı çubuk tam 1500'e dönmeyebilir */
inline constexpr float    kMaxThrottle     = 0.10f;
inline constexpr float    kMaxYaw          = 0.07f;
inline constexpr MixMode  kMixMode         = MixMode::ScaleDown;
inline constexpr bool     kRampEnabled     = false; /* GEÇİCİ: testte kapalı tutuluyor */
inline constexpr float    kRampRatePerSec  = 1.0f;

inline constexpr uint32_t kLogPeriodMs = 250;  /* kontrol log aralığı */

/* ════════════════════════════════════════════════════════════════════════════
 * FAZ 3 — GÜÇ VE SAĞLIK İZLEME
 * Sensör bağlanmadan bir özelliği açma: boşta kalan ADC pini rastgele değer okur.
 * Devreye alma sırası ve kalibrasyon: Firmware/src/esp32/README.md
 * ════════════════════════════════════════════════════════════════════════════ */

/* ── Hangi sensörler bağlı? ───────────────────────────────────────────────── */
inline constexpr bool kBattSenseEnabled    = false;
inline constexpr bool kCurrentSenseEnabled = false;
inline constexpr bool kTempSenseEnabled    = false;
inline constexpr bool kKillSwitchEnabled   = false;
inline constexpr bool kTelemetryEnabled    = false;  /* kumanda ekranına voltaj/akım/sıcaklık */

inline constexpr uint32_t kPowerPeriodMs    = 100;   /* ölçüm aralığı (10 Hz)     */
inline constexpr int      kAdcOversample    = 16;    /* her ölçümde ortalama örnek */
inline constexpr uint32_t kPowerLogPeriodMs = 5000;  /* periyodik güç logu         */

/* ── Kalibrasyon (TODO: DEGZ PDB çıkışları ölçülüp doğrulanmalı) ───────────────
 * ESP32 ADC girişi en fazla ~3.1 V. PDB çıkışı bunu aşıyorsa gerilim bölücü şart.
 *
 * Voltaj: V_bat = V_adc × kBattVoltsPerVolt + kBattOffsetV
 *         Kalibrasyon: multimetreyle batarya voltajını ölç, log'daki ham mV ile böl.
 * Akım  : I = (V_adc − kCurrentZeroV) / kCurrentVoltsPerAmp
 *         kCurrentZeroV: motorlar dururken log'daki ham mV (Hall sensör sıfır noktası)
 * Sıcaklık: NTC termistör, seri direnç 3.3V'a (pull-up), NTC GND'ye bağlı varsayılır. */
inline constexpr float kBattVoltsPerVolt   = 11.0f;   /* örn. 100k/10k bölücü        */
inline constexpr float kBattOffsetV        = 0.0f;
inline constexpr float kCurrentZeroV       = 1.65f;   /* çift yönlü Hall: Vcc/2      */
inline constexpr float kCurrentVoltsPerAmp = 0.0132f; /* 13.2 mV/A (örnek değer)     */
inline constexpr float kNtcR25Ohm          = 10000.0f;
inline constexpr float kNtcBeta            = 3950.0f;
inline constexpr float kNtcSeriesOhm       = 10000.0f;
inline constexpr float kNtcSupplyV         = 3.3f;

/* ── Batarya (2× 6S LiPo paralel) ─────────────────────────────────────────────
 * Voltaj yük altında düşer, yük kalkınca toparlanır. Bu yüzden:
 *  - ölçüm filtrelenir (kBattFilterTauS),
 *  - eşik kBattDebounceS boyunca kesintisiz aşılmadan seviye düşmez,
 *  - seviye bir kez düştü mü geri yükselmez (batarya değişene kadar, yani reset).
 * LOW      : sadece uyarı
 * LIMIT    : güç kBattLimitScale ile çarpılır
 * CRITICAL : güç kBattCriticalScale ile çarpılır (0 = dur; "eve dönüş" için örn. 0.2) */
inline constexpr int   kBattCellCount      = 6;
inline constexpr float kCellLowV           = 3.50f;  /* 21.0 V */
inline constexpr float kCellLimitV         = 3.40f;  /* 20.4 V */
inline constexpr float kCellCriticalV      = 3.30f;  /* 19.8 V */
inline constexpr float kBattLimitScale     = 0.5f;
inline constexpr float kBattCriticalScale  = 0.0f;
inline constexpr float kBattFilterTauS     = 2.0f;
inline constexpr float kBattDebounceS      = 3.0f;
inline constexpr float kBattPlausibleMinV  = 12.0f;  /* dışı → sensör arızası, güç kesilmez */
inline constexpr float kBattPlausibleMaxV  = 27.0f;

/* ── Akım sınırlama (toplam batarya akımı) ────────────────────────────────────
 * 2 thruster × 35 A sürekli. Ölçülen akım sınırı aşarsa güç yavaşça kısılır,
 * altına inince yavaşça geri verilir. Kısa devre koruması sigortanın (ANL) işidir. */
inline constexpr float kCurrentLimitA          = 60.0f;
inline constexpr float kCurrentMinScale        = 0.2f;
inline constexpr float kCurrentDropPerSec      = 1.0f;
inline constexpr float kCurrentRecoverPerSec   = 0.2f;
inline constexpr float kCurrentFilterTauS      = 0.2f;
inline constexpr float kCurrentPlausibleMaxA   = 250.0f;

/* ── Sıcaklık (PDB termistörü) ────────────────────────────────────────────────
 * kTempDerateStartC → kTempDerateEndC arasında güç doğrusal olarak kTempMinScale'e iner. */
inline constexpr float kTempWarnC         = 60.0f;
inline constexpr float kTempDerateStartC  = 70.0f;
inline constexpr float kTempDerateEndC    = 85.0f;
inline constexpr float kTempMinScale      = 0.0f;
inline constexpr float kTempFilterTauS    = 2.0f;

/* ── Acil durdurma (kill switch) ──────────────────────────────────────────────
 * NC (normalde kapalı) buton GPIO ile GND arasında, dahili pull-up açık:
 * butona basılırsa VEYA kablo koparsa pin HIGH olur → KILLED.
 * Bırakıldıktan sonra tekrar sürmek için CH5 switch kapat-aç gerekir. */
inline constexpr bool     kKillActiveHigh   = true;
inline constexpr uint32_t kKillDebounceMs   = 60;

}  // namespace cfg
