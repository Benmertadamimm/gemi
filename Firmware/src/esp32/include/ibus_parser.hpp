#pragma once

/* iBUS protokol çözücü — donanımdan bağımsız, host testlerinde de derlenir.
 *
 * iBUS: 115200 baud, 8N1, 32 byte frame, ~7ms'de bir frame.
 * Frame: [0x20][0x40][ch1_lo][ch1_hi]...[ch14_lo][ch14_hi][csum_lo][csum_hi]
 * Kanal: 1000 (min) .. 1500 (merkez) .. 2000 (max), uint16 little-endian.
 * Checksum: 0xFFFF - sum(byte[0..29]) */

#include <array>
#include <cstddef>
#include <cstdint>

namespace ibus {

inline constexpr size_t   kNumChannels = 14;
inline constexpr size_t   kFrameLen    = 32;
inline constexpr uint8_t  kHeader0     = 0x20;
inline constexpr uint8_t  kHeader1     = 0x40;
inline constexpr uint16_t kChMin       = 1000;
inline constexpr uint16_t kChMid       = 1500;
inline constexpr uint16_t kChMax       = 2000;

using Channels = std::array<uint16_t, kNumChannels>;  /* [0] = CH1 .. [13] = CH14 */

/* Kanal değerini -1.0 .. 1.0 aralığına çevirir (1000 → -1, 1500 → 0, 2000 → 1). */
inline float normalize(uint16_t us)
{
    float v = (static_cast<float>(us) - kChMid) / static_cast<float>(kChMax - kChMid);
    if (v < -1.0f) v = -1.0f;
    if (v >  1.0f) v =  1.0f;
    return v;
}

/* Bayt bayt beslenen akış çözücü. Header'a göre senkronlanır, checksum'ı
 * tutmayan frame'leri atar. */
class FrameParser {
public:
    /* Bir bayt besler. Geçerli bir frame tamamlanınca true döner ve out'u doldurur. */
    bool push(uint8_t byte, Channels &out)
    {
        if (idx_ == 0) {
            if (byte != kHeader0) return false;
        } else if (idx_ == 1 && byte != kHeader1) {
            /* 0x20 ise yeni bir frame başlangıcı olabilir: idx_ 1'de kalır */
            if (byte != kHeader0) idx_ = 0;
            return false;
        }

        buf_[idx_++] = byte;
        if (idx_ < kFrameLen) return false;
        idx_ = 0;

        uint16_t sum = 0xFFFF;
        for (size_t i = 0; i < kFrameLen - 2; ++i) sum -= buf_[i];
        const uint16_t rx_sum = static_cast<uint16_t>(buf_[30] | (buf_[31] << 8));
        if (sum != rx_sum) {
            ++checksum_errors_;
            return false;
        }

        for (size_t i = 0; i < kNumChannels; ++i) {
            /* Üst 4 bit bazı firmware'lerde CH15-18 için kullanılır → maskele */
            out[i] = static_cast<uint16_t>(buf_[2 + i * 2] | ((buf_[3 + i * 2] & 0x0F) << 8));
        }
        return true;
    }

    void reset() { idx_ = 0; }
    uint32_t checksum_errors() const { return checksum_errors_; }

private:
    std::array<uint8_t, kFrameLen> buf_{};
    size_t   idx_             = 0;
    uint32_t checksum_errors_ = 0;
};

}  // namespace ibus
