# ESP32-S3 Firmware

Kumandayla (FlySky FS-i6X + FS-iA6B, iBUS) iki thruster'ı diferansiyel sürer;
batarya, akım ve sıcaklığı izleyip gücü sınırlar. ESP-IDF 6.0, C++, PlatformIO.

## Dosyalar

| Dosya | Görev |
|-------|-------|
| `include/config.hpp` | **Tüm pinler ve ayarlar burada** (kanal, limit, trim, timeout, Faz 3 eşikleri) |
| `include/ibus_parser.hpp` | iBUS kanal frame çözücü (donanımdan bağımsız) |
| `include/ibus.hpp`, `src/ibus.cpp` | UART1 okuma task'ı, son frame'i thread-safe saklar |
| `include/esc_pulse.hpp` | -1..1 komut → ESC pulse (µs), trim, yön |
| `include/motor.hpp`, `src/motor.cpp` | MCPWM ESC sürücü + komut watchdog'u + ESC kalibrasyonu |
| `include/mixer.hpp` | Deadzone, gaz/yaw mixing, rampa |
| `include/arming.hpp` | Arm / disarm / failsafe / kill durum makinesi, debounce |
| `include/control.hpp`, `src/control.cpp` | 50 Hz kontrol döngüsü |
| `include/power.hpp` | Faz 3: batarya / akım / sıcaklık koruma mantığı (donanımdan bağımsız) |
| `include/power_monitor.hpp`, `src/power_monitor.cpp` | Faz 3: ADC ölçümü, 10 Hz task |
| `include/ibus_telemetry.hpp` | Faz 3: iBUS sensör protokolü (donanımdan bağımsız) |
| `include/telemetry.hpp`, `src/telemetry.cpp` | Faz 3: alıcının SENS portuna cevap veren task |
| `src/main.cpp` | Başlatma |
| `host_tests/` | PC'de çalışan mantık testleri |

## Derleme ve Yükleme

VS Code + PlatformIO: **File → Open Folder** ile bu klasörü (`Firmware/src/esp32`,
`platformio.ini`'nin olduğu yer) aç; alttaki çubukta ✓ = derle, → = yükle, 🔌 = log.

```bash
pio run -t upload        # derle + yükle
pio device monitor       # log (115200)
```

> **Klasör yolu sadece İngilizce harf, rakam, `-` ve `_` içermeli.**
> - Türkçe karakter (`Masaüstü`, `Kaan'ın`...) → GCC yolu bozar, derleme başarısız olur.
> - Boşluk → PlatformIO "whitespace character" hatası verir.
> - `[` `]` → eskiden "Couldn't find the main target" hatasına yol açıyordu.
>
> Önerilen: `C:\projeler\gemi`. Klasörü taşıdıktan sonra eski `.pio` klasörünü sil.
>
> Yeni bir `.cpp` dosyası eklersen `src/CMakeLists.txt` listesine de ekle.

Mantık testleri (ESP32 gerekmez):

```bash
make -C host_tests
```

## Kumanda ile Kullanım

| Kumanda | Görev |
|---------|-------|
| Sağ çubuk dikey (CH2) | İleri / geri — **bırakınca durur** |
| Sağ çubuk yatay (CH1) | Dönüş |
| SwA (CH5) | ARM / DISARM |

> FS-i6X'in sol dikey çubuğu (CH3, gaz) yaylı değildir, bırakınca ortaya dönmez.
> Çift yönlü ESC'de bu, teknenin durmaması demek; bu yüzden sürüş sağ çubukta.
> Kumanda ayarları: `Docs/Devices/FlySky_FS-i6X.md`.

1. Kumandayı aç, SwA **kapalı** konumda olsun.
2. Tekneye güç ver. ESC'ler 2 sn nötr sinyal görür (`kEscArmDelayMs`).
3. Sağ çubuk ortadayken SwA'yı **ARM** konumuna al → log: `DISARMED -> ARMED`.
4. Durdurmak için SwA'yı kapat ya da çubuğu bırak.

## Kumanda Kopunca Ne Olur? (Failsafe)

Herhangi bir katman tetiklenirse motorlar **rampa beklemeden anında** nötre çekilir.

| # | Durum | Algılama | Sonuç |
|---|-------|----------|-------|
| 1 | Alıcı frame göndermiyor (kumanda kapalı/menzil dışı ve alıcıda failsafe yok, kablo koptu, alıcı enerjisiz) | 200 ms geçerli frame yok (`kRcTimeoutMs`) | `FAILSAFE` |
| 1b | Bozuk veri | Kanal değeri 900–2100 µs dışında | `FAILSAFE` |
| 2 | Alıcı failsafe preset'i gönderiyor (kumanda kapalı / menzil dışı) | CH5 = 1000 µs gelir | `DISARMED` |
| 3 | Kontrol task'ı kilitlendi / çöktü | Motor sürücüsü 100 ms komut almadı (`kMotorCmdTimeoutMs`) | ESC'ler nötr |
| 4 | Acil durdurma butonu (Faz 3) | GPIO7, 60 ms debounce | `KILLED` |

Bağlantı geri geldiğinde tekne **kendiliğinden kalkmaz**: çubuk ortaya alınmalı.
Acil durdurmadan sonra SwA'yı kapatıp açmak da gerekir.

> **Önemli:** 2. katman için alıcı failsafe ayarı şarttır:
> Menu → System → RX Setup → Failsafe → CH1 = 1500, CH2 = 1500, CH5 = 1000.
> Bind işlemi bu ayarı sıfırlar.

## Faz 3 — Güç ve Sağlık İzleme

Tüm Faz 3 özellikleri **varsayılan olarak kapalıdır**: bağlı olmayan bir ADC pini
rastgele değer okur ve sahte "batarya bitti" kararına yol açar. Her sensörü
bağladıkça `config.hpp`'den tek tek aç.

### Ne yapar?

| Koruma | Kural | Güç |
|--------|-------|-----|
| Batarya DUSUK | < 3.50 V/hücre (21.0 V) | Sadece uyarı |
| Batarya LIMIT | < 3.40 V/hücre (20.4 V) | %50 (`kBattLimitScale`) |
| Batarya KRITIK | < 3.30 V/hücre (19.8 V) | Dur (`kBattCriticalScale = 0`; eve dönüş için örn. 0.2) |
| Aşırı akım | Toplam > 60 A | Yavaşça kısılır (en az %20), düşünce geri verilir |
| Sıcaklık | PDB 70 °C → 85 °C | %100'den %0'a doğrusal |

- Motorlara giden güç = üç katsayının **en küçüğü**. Log'da `guc=` olarak görünür.
- Voltaj yük altında düşer: ölçüm 2 sn filtrelenir, eşik **3 sn kesintisiz** aşılmadan
  seviye düşmez. Bir kez düşen seviye geri yükselmez (yük kalkınca voltajın toparlanması yanıltıcıdır).
- **Sensör arızası gücü kesmez:** voltaj 12–27 V dışındaysa ya da termistör açık/kısa
  devreyse ölçüm geçersiz sayılır, uyarı loglanır. Denizde bir kablo koptu diye tekne mahsur kalmaz.

### Devreye alma sırası

1. **PDB çıkışlarını ölç.** JST pin sırasını ve voltaj aralığını multimetreyle doğrula.
   ESP32 ADC'si en fazla ~3.1 V okur; üstündeyse gerilim bölücü kullan.
2. **Voltaj:** GPIO4'e bağla, `kBattSenseEnabled = true`. Log'daki
   `ham ADC: X/.../...` değerini ve multimetredeki batarya voltajını not et:
   `kBattVoltsPerVolt = batarya_V / X`.
3. **Akım:** GPIO5'e bağla, `kCurrentSenseEnabled = true`. Motorlar dururken ham değer
   → `kCurrentZeroV`. Bilinen bir akımda (pens ampermetre) eğimi hesapla → `kCurrentVoltsPerAmp`.
4. **Sıcaklık:** GPIO6'ya bağla, `kTempSenseEnabled = true`. Oda sıcaklığında ~25 °C
   okumalı; değilse `kNtcR25Ohm` / `kNtcBeta` / `kNtcSeriesOhm` değerlerini termistöre göre düzelt.
5. **Acil durdurma butonu:** NC (normalde kapalı) buton GPIO7 ile GND arasına,
   `kKillSwitchEnabled = true`. Butona basınca ve kabloyu çekince log'da `KILLED` görünmeli.
6. **Telemetri:** FS-iA6B SENS sinyal pini → GPIO15, `kTelemetryEnabled = true`.
   Kumandada Menu → System → Display sensors ile voltaj/sıcaklık görünmeli.
7. Eşikleri havuzda doğrula: düşük voltajı test etmek için `kCellLimitV`'yi geçici
   olarak yükseltip LIMIT'e düştüğünü gör, sonra eski değere al.

## Ayarlar (`config.hpp`)

| Parametre | Varsayılan | Açıklama |
|-----------|-----------|----------|
| `kChThrottle` / `kChYaw` | CH2 / CH1 | Sağ çubuk (yaylı). Dönüş sol çubukta istenirse `kChYaw = 3` |
| `kThrottleReversed` / `kYawReversed` | false | Çubuk yönü ters ise |
| `kDeadzone` | 0.03 | Merkezde ölü bölge, sonrası yeniden ölçeklenir |
| `kMaxThrottle` | 0.10 | Tam çubukta verilen güç (test için düşük) |
| `kMaxYaw` | 0.07 | Tam çubukta verilen dönüş gücü |
| `kMixMode` | ScaleDown | Taşmada gaz+yaw orantılı küçülür |
| `kRampEnabled` | false | Motor çıkışı rampası (`kRampRatePerSec`) |
| `kLeftTrimUs` | -13 | Sol ESC nötr kayması telafisi |
| `kUseArmSwitch` | true | false: switch olmadan, çubuk ortadayken otomatik arm |
| `kEscCalibrationMode` | false | true: ESC kalibrasyon dizisi çalışır |
| `k*SenseEnabled`, `kKillSwitchEnabled`, `kTelemetryEnabled` | false | Faz 3 özellikleri |

## Log Örneği

```
I control: ARMED     gaz=+0.05 yaw=+0.00 guc=1.00 | L=+0.05 R=+0.05 | 1512/1525us | rc yas=4ms frame=1532 err=0
W control: ARMED -> FAILSAFE: 203 ms frame yok, kumanda baglantisi koptu — MOTORLAR DURDURULDU
W control: FAILSAFE -> DISARMED: baglanti geri geldi — tekrar arm icin cubuklari ortala
I power: V=22.31(OK) I=12.4A T=34.2C | guc=1.00 (bat 1.00 akim 1.00 sic 1.00) | ham ADC: 2.028/1.814/1.320 V
W power: Batarya DUSUK -> LIMIT: 20.38 V (3.40 V/hucre) — GUC SINIRLANDI
```
