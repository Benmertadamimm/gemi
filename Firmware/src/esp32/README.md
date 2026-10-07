# ESP32-S3 Firmware

Kumandayla (FlySky FS-i6X + FS-iA6B, iBUS) iki thruster'ı diferansiyel sürer.
ESP-IDF 6.0, C++, PlatformIO.

## Dosyalar

| Dosya | Görev |
|-------|-------|
| `include/config.hpp` | **Tüm pinler ve ayarlar burada** (limit, trim, kanal, timeout) |
| `include/ibus_parser.hpp` | iBUS frame çözücü (donanımdan bağımsız) |
| `include/ibus.hpp`, `src/ibus.cpp` | UART okuma task'ı, son frame'i thread-safe saklar |
| `include/esc_pulse.hpp` | -1..1 komut → ESC pulse (µs), trim, yön |
| `include/motor.hpp`, `src/motor.cpp` | MCPWM ESC sürücü + komut watchdog'u + ESC kalibrasyonu |
| `include/mixer.hpp` | Deadzone, gaz/yaw mixing, rampa |
| `include/arming.hpp` | Arm / disarm / failsafe durum makinesi |
| `include/control.hpp`, `src/control.cpp` | 50 Hz kontrol döngüsü |
| `src/main.cpp` | Başlatma |
| `host_tests/` | PC'de çalışan mantık testleri |

## Derleme ve Yükleme

```bash
pio run -t upload        # derle + yükle
pio device monitor       # log (115200)
```

Mantık testleri (ESP32 gerekmez):

```bash
make -C host_tests
```

## Kumanda ile Kullanım

1. Kumandayı aç, CH5 switch'i (SwA) **kapalı** konumda olsun.
2. Tekneye güç ver. ESC'ler 2 sn nötr sinyal görür (`kEscArmDelayMs`).
3. Gaz ve yaw çubukları ortadayken CH5'i **ARM** konumuna al → log: `DISARMED -> ARMED`.
4. Sol çubuk: dikey = ileri/geri, yatay = dönüş.
5. Durdurmak için CH5'i kapat.

## Kumanda Kopunca Ne Olur? (Failsafe)

Üç bağımsız koruma katmanı var. Herhangi biri tetiklenirse motorlar **rampa
beklemeden anında** nötre çekilir.

| # | Durum | Algılama | Sonuç |
|---|-------|----------|-------|
| 1 | Alıcı frame göndermiyor (kumanda kapandı / menzil dışı ve alıcıda failsafe yok, kablo koptu, alıcı enerjisiz) | 200 ms geçerli frame yok (`kRcTimeoutMs`) | `FAILSAFE` |
| 1b | Bozuk veri | Kanal değeri 900–2100 µs dışında | `FAILSAFE` |
| 2 | Alıcı failsafe preset'i gönderiyor (kumanda kapandı / menzil dışı) | CH5 = 1000 µs gelir | `DISARMED` |
| 3 | Kontrol task'ı kilitlendi / çöktü | Motor sürücüsü 100 ms komut almadı (`kMotorCmdTimeoutMs`) | ESC'ler nötr |

Bağlantı geri geldiğinde tekne **kendiliğinden kalkmaz**: tekrar arm olması için
çubukların ortaya alınması gerekir (`kRequireSwitchCycleAfterFailsafe = true` yapılırsa
CH5'i kapatıp açmak da gerekir).

> **Önemli:** 2. katmanın çalışması için alıcı failsafe ayarı şarttır:
> kumandada Menu → System → RX Setup → Failsafe → CH3 = 1500, CH4 = 1500, CH5 = 1000.
> Bind işlemi bu ayarı sıfırlar. Ayrıntı: `Docs/Devices/FlySky_FS-i6X.md`.

## Ayarlar (`config.hpp`)

| Parametre | Varsayılan | Açıklama |
|-----------|-----------|----------|
| `kMaxThrottle` | 0.10 | Tam çubukta verilen güç (test için düşük) |
| `kMaxYaw` | 0.07 | Tam çubukta verilen dönüş gücü |
| `kMixMode` | ScaleDown | Taşmada gaz+yaw orantılı küçülür |
| `kRampEnabled` | false | Motor çıkışı rampası (`kRampRatePerSec`) |
| `kLeftTrimUs` | -13 | Sol ESC nötr kayması telafisi |
| `kUseArmSwitch` | true | false: switch olmadan, çubuklar ortadayken otomatik arm |
| `kEscCalibrationMode` | false | true: ESC kalibrasyon dizisi çalışır |

## Log Örneği

```
I control: ARMED     gaz=+0.05 yaw=+0.00 | L=+0.05 R=+0.05 | 1512/1525us | rc yas=4ms frame=1532 err=0
W control: ARMED -> FAILSAFE: 203 ms frame yok, kumanda baglantisi koptu — MOTORLAR DURDURULDU
W control: FAILSAFE -> DISARMED: baglanti geri geldi — tekrar arm icin cubuklari ortala
```
