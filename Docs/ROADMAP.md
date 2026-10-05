# Posedion — Yol Haritası

Kumandayla denizde sürülen, ileride kamerayla çöpü kendisi bulup toplayan katamaran.

| Birim | Görev | Dil |
|-------|-------|-----|
| **ESP32-S3** | Gerçek zamanlı işler: kumanda (iBUS), motor/ESC sürme, failsafe, sensörler, toplama mekanizması | C++ (ESP-IDF) |
| **Raspberry Pi 5** | Kamera, görüntü işleme (çöp tespiti), otonom karar | Python (OpenCV + YOLO) |

---

## Temel Güvenlik İlkeleri

Projenin her aşamasında geçerli. Yeni bir özellik eklenirken bunlar bozulmamalı.

1. **Kumanda her zaman önceliklidir.** Otonom mod ne yaparsa yapsın kumanda araya girebilir.
2. **Her bağlantının bir zaman aşımı vardır.** Kumanda, RPi5 ya da kontrol task'ı sessiz kalırsa motorlar durur.
3. **Motorlar sadece ARM durumunda döner.** Boot, bağlantı kopması ve bozuk veri durumlarında motorlar nötrdedir.
4. **Ani kalkış olmaz.** Arm olmak ya da bağlantı geri geldikten sonra devam etmek için çubuklar ortada olmalıdır.
5. **Kalibrasyon, trim ve limit değerleri tek yerde tutulur** (`Firmware/src/esp32/include/config.hpp`).

---

## Faz 0 — Altyapı ✅

- [x] Repo yapısı (`Cad/`, `Docs/`, `Firmware/src/esp32`, `Firmware/src/rpi5`, `Firmware/Schemes`)
- [x] Bileşen dokümanları (`Docs/Devices/`)
- [x] ESP-IDF + PlatformIO projesi
- [ ] Kablolama şeması (`Firmware/Schemes/`): güç zinciri, sinyal hatları, sigorta
- [ ] Pin haritasının şemayla eşleştirilmesi

## Faz 1 — ESP32: Kumandayla Sürüş ✅ (kod hazır, donanımda test edilecek)

- [x] Kodun C'den C++'a taşınması (modüler sınıflar, `config.hpp`)
- [x] iBUS alıcı: akış çözücü, checksum, gecikmesiz okuma
- [x] ESC sürücü: MCPWM 50 Hz, trim, yön çevirme, kalibrasyon modu
- [x] Diferansiyel mixing (ScaleDown / ThrottlePriority), deadzone, hız limiti, rampa
- [x] Arm/disarm durum makinesi (CH5 switch, ani kalkış koruması)
- [x] **Kumanda bağlantısı kopunca motorların durması** (3 katmanlı failsafe; bkz. firmware README)
- [x] PC'de çalışan mantık testleri (`host_tests/`)

## Faz 2 — Tezgah ve Havuz Testleri

- [ ] FS-i6X: CH5 = SwA ataması, alıcı failsafe ayarı (CH3/CH4 = 1500, CH5 = 1000)
- [ ] ESC kalibrasyonu (`kEscCalibrationMode`), nötr trim ayarı (`kLeftTrimUs` / `kRightTrimUs`)
- [ ] Motor yön kontrolü (`kLeftReversed` / `kRightReversed`)
- [ ] Failsafe testleri: kumandayı kapat, alıcı kablosunu çek, alıcının enerjisini kes → her durumda motorlar durmalı
- [ ] Havuzda düşük limitle (`kMaxThrottle = 0.10`) sürüş, sonra limitlerin kademeli artırılması
- [ ] Rampa açık/kapalı karşılaştırması (`kRampEnabled`)

## Faz 3 — Güç ve Sağlık İzleme (ESP32)

- [ ] PDB voltaj/akım/sıcaklık okuma (ADC1 pinleri; ADC2 Wi-Fi ile çakışır)
- [ ] Düşük batarya: önce güç sınırlama, sonra güvenli durdurma (6S cutoff 19.8 V)
- [ ] Fiziksel kill switch / PDB On-Off hattı
- [ ] Telemetri: iBUS-sens ile kumandaya batarya voltajı (opsiyonel)

## Faz 4 — Çöp Toplama Mekanizması

- [ ] Mekanizma tasarımı (konveyör / kepçe / ağ) — `Cad/`
- [ ] ESP32'den mekanizma motorunun sürülmesi (kumandada CH6 ile aç/kapa)
- [ ] Hazne doluluk sensörü
- [ ] Mekanizma için ayrı arm kuralı ve akım limiti

## Faz 5 — RPi5 Görüntü İşleme

- [ ] Kamera seçimi ve kurulumu (Pi Camera Module 3 / USB kamera), su geçirmez muhafaza
- [ ] Ortam: Raspberry Pi OS 64-bit, Python, OpenCV, Picamera2
- [ ] Veri seti: şişe, poşet, kutu, köpük vb. (kendi çekimlerimiz + açık veri setleri)
- [ ] Model: YOLO (nano boyut), NCNN/ONNX formatına çevirerek Pi5 CPU'da ≥10 FPS hedefi
      (hızlandırıcı gerekirse: Raspberry Pi AI Kit / Hailo)
- [ ] Çıktı: hedefin görüntüdeki açısı, tahmini mesafe ve güven skoru

## Faz 6 — ESP32 ↔ RPi5 Haberleşme

- [ ] UART protokolü: başlık + uzunluk + mesaj tipi + veri + CRC16
- [ ] Mesajlar: `HEARTBEAT`, `DRIVE_CMD (gaz, yaw)`, `COLLECT_CMD`, `TELEMETRY`
- [ ] RPi5 heartbeat'i 300 ms kesilirse otonom komutlar yok sayılır, motorlar durur
- [ ] ESP32 son sözü söyler: RPi5'ten gelen komut da `config.hpp` limitleriyle sınırlanır

## Faz 7 — Yarı Otonom ve Otonom Mod

- [ ] Mod seçimi (CH6 / SwC 3 konum): **MANUEL** → **ASİSTLİ** → **OTONOM**
- [ ] Asistli: kumandayla gaz, kamera hedefe doğru yaw düzeltmesi yapar
- [ ] Otonom: çöpü bul → yaklaş → topla → aramaya devam et
- [ ] GPS (NEO-7M) + IMU: rota tutma, sanal çit (geofence), eve dönüş
- [ ] Kumanda çubuğu hareket ederse otonom mod anında devre dışı kalır

## Faz 8 — Saha ve Dayanıklılık

- [ ] Su geçirmezlik (IP67 muhafazalar, kablo rakorları)
- [ ] Isı yönetimi (ESC, regülatör)
- [ ] Deniz suyu sonrası bakım prosedürü
- [ ] Uzun süreli deniz testi ve kayıt (log) analizi
