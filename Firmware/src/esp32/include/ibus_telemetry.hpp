#pragma once

/* iBUS sensör (telemetri) protokolü — donanımdan bağımsız, host testlerinde derlenir.
 *
 * FS-iA6B'nin SENS portu tek telli, 115200 8N1. Alıcı sensörleri sırayla sorgular,
 * ESP32 sensör gibi cevap verir; değerler kumanda (FS-i6X) ekranında görünür.
 *
 * Alıcı komutu (4 bayt):  [0x04][cmd|adr][cs_lo][cs_hi]
 *   cmd 0x80: keşif      → cevap: komutun aynısı
 *   cmd 0x90: tip sorgusu → cevap: [0x06][0x90|adr][tip][0x02][cs_lo][cs_hi]
 *   cmd 0xA0: ölçüm       → cevap: [0x06][0xA0|adr][val_lo][val_hi][cs_lo][cs_hi]
 * Checksum: 0xFFFF - (önceki baytların toplamı), little-endian.
 * Adres 0 alıcının kendisidir (Int.V); harici sensörler 1'den başlar. */

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ibus::telemetry {

/* Betaflight ibus_shared.h ile aynı tip kodları. Stok FS-i6X yazılımı Temp ve
 * Ext.V'yi gösterir; akım (0x05) bazı yazılımlarda "bilinmeyen" görünebilir. */
enum class SensorType : uint8_t {
    Temperature     = 0x01,  /* 0.1 °C, 0 = -40 °C */
    Rpm             = 0x02,
    ExternalVoltage = 0x03,  /* 0.01 V */
    CellVoltage     = 0x04,  /* 0.01 V */
    BatteryCurrent  = 0x05,  /* 0.01 A */
};

inline constexpr size_t kMaxSensors  = 15;
inline constexpr size_t kMaxResponse = 6;

inline uint16_t encode_voltage(float volts)
{
    const float v = std::round(volts * 100.0f);
    return v <= 0.0f ? 0 : v >= 65535.0f ? 65535 : static_cast<uint16_t>(v);
}

inline uint16_t encode_current(float amps) { return encode_voltage(amps); }  /* aynı ölçek: 0.01 */

inline uint16_t encode_temperature(float celsius)
{
    const float v = std::round(celsius * 10.0f + 400.0f);
    return v <= 0.0f ? 0 : v >= 65535.0f ? 65535 : static_cast<uint16_t>(v);
}

class Responder {
public:
    using Response = std::array<uint8_t, kMaxResponse>;

    /* Sensör ekler, 1'den başlayan adresini döner (0 = yer yok). */
    uint8_t add_sensor(SensorType type)
    {
        if (count_ >= kMaxSensors) return 0;
        types_[count_] = type;
        values_[count_] = 0;
        return static_cast<uint8_t>(++count_);
    }

    void set_value(uint8_t address, uint16_t value)
    {
        if (address >= 1 && address <= count_) values_[address - 1] = value;
    }

    /* Bir bayt besler. Bize ait geçerli bir komut tamamlanınca cevabı `out`a yazar
     * ve uzunluğunu döner; aksi halde 0. */
    size_t push(uint8_t byte, Response &out)
    {
        win_[0] = win_[1];
        win_[1] = win_[2];
        win_[2] = win_[3];
        win_[3] = byte;
        if (filled_ < 4 && ++filled_ < 4) return 0;

        if (win_[0] != 0x04) return 0;
        const uint16_t sum = static_cast<uint16_t>(0xFFFF - win_[0] - win_[1]);
        if (sum != static_cast<uint16_t>(win_[2] | (win_[3] << 8))) return 0;
        filled_ = 0;  /* komut tüketildi, aynı baytlarla tekrar eşleşmesin */

        const uint8_t cmd  = win_[1] & 0xF0;
        const uint8_t addr = win_[1] & 0x0F;
        if (addr == 0 || addr > count_) return 0;  /* bize ait değil: sessiz kal */

        size_t len = 0;
        switch (cmd) {
            case 0x80:
                out[0] = 0x04;
                out[1] = win_[1];
                len    = 2;
                break;
            case 0x90:
                out[0] = 0x06;
                out[1] = win_[1];
                out[2] = static_cast<uint8_t>(types_[addr - 1]);
                out[3] = 0x02;
                len    = 4;
                break;
            case 0xA0:
                out[0] = 0x06;
                out[1] = win_[1];
                out[2] = static_cast<uint8_t>(values_[addr - 1] & 0xFF);
                out[3] = static_cast<uint8_t>(values_[addr - 1] >> 8);
                len    = 4;
                break;
            default:
                return 0;
        }

        uint16_t cs = 0xFFFF;
        for (size_t i = 0; i < len; ++i) cs = static_cast<uint16_t>(cs - out[i]);
        out[len]     = static_cast<uint8_t>(cs & 0xFF);
        out[len + 1] = static_cast<uint8_t>(cs >> 8);
        return len + 2;
    }

    void reset() { filled_ = 0; }
    size_t sensor_count() const { return count_; }

private:
    std::array<SensorType, kMaxSensors> types_{};
    std::array<uint16_t, kMaxSensors>   values_{};
    std::array<uint8_t, 4>              win_{};
    size_t count_  = 0;
    size_t filled_ = 0;
};

}  // namespace ibus::telemetry
