/* Donanımdan bağımsız mantık testleri (PC'de çalışır, ESP32 gerekmez).
 *
 * Çalıştırma:  make -C Firmware/src/esp32/host_tests */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "arming.hpp"
#include "config.hpp"
#include "esc_pulse.hpp"
#include "ibus_parser.hpp"
#include "mixer.hpp"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

/* ── iBUS ─────────────────────────────────────────────────────────────────── */

std::vector<uint8_t> make_frame(const ibus::Channels &ch)
{
    std::vector<uint8_t> f(ibus::kFrameLen);
    f[0] = ibus::kHeader0;
    f[1] = ibus::kHeader1;
    for (size_t i = 0; i < ibus::kNumChannels; ++i) {
        f[2 + i * 2] = static_cast<uint8_t>(ch[i] & 0xFF);
        f[3 + i * 2] = static_cast<uint8_t>(ch[i] >> 8);
    }
    uint16_t sum = 0xFFFF;
    for (size_t i = 0; i < ibus::kFrameLen - 2; ++i) sum = static_cast<uint16_t>(sum - f[i]);
    f[30] = static_cast<uint8_t>(sum & 0xFF);
    f[31] = static_cast<uint8_t>(sum >> 8);
    return f;
}

int feed(ibus::FrameParser &p, const std::vector<uint8_t> &bytes, ibus::Channels &out)
{
    int frames = 0;
    for (uint8_t b : bytes) frames += p.push(b, out) ? 1 : 0;
    return frames;
}

void test_ibus()
{
    ibus::Channels ch{};
    for (size_t i = 0; i < ibus::kNumChannels; ++i) ch[i] = static_cast<uint16_t>(1000 + i * 50);

    {   /* tek geçerli frame */
        ibus::FrameParser p;
        ibus::Channels out{};
        CHECK(feed(p, make_frame(ch), out) == 1);
        CHECK(out == ch);
    }
    {   /* önde çöp bayt + yanlış header ile başlayan akış → yine senkronlanır */
        ibus::FrameParser p;
        ibus::Channels out{};
        std::vector<uint8_t> s = {0x00, 0x20, 0x11, 0x20, 0x20, 0x55};
        auto f = make_frame(ch);
        s.insert(s.end(), f.begin(), f.end());
        s.insert(s.end(), f.begin(), f.end());
        CHECK(feed(p, s, out) == 2);
        CHECK(out == ch);
    }
    {   /* bozuk checksum → atılır, sayılır; sonraki frame geçerli */
        ibus::FrameParser p;
        ibus::Channels out{};
        auto bad = make_frame(ch);
        bad[10] ^= 0x01;
        CHECK(feed(p, bad, out) == 0);
        CHECK(p.checksum_errors() == 1);
        CHECK(feed(p, make_frame(ch), out) == 1);
    }
    {   /* üst nibble (CH15-18 verisi) maskelenir */
        ibus::FrameParser p;
        ibus::Channels out{};
        ibus::Channels hi = ch;
        hi[0] = static_cast<uint16_t>(0x5000 | 1500);
        CHECK(feed(p, make_frame(hi), out) == 1);
        CHECK(out[0] == 1500);
    }

    CHECK(near(ibus::normalize(1000), -1.0f));
    CHECK(near(ibus::normalize(1500), 0.0f));
    CHECK(near(ibus::normalize(2000), 1.0f));
    CHECK(near(ibus::normalize(0), -1.0f));     /* clamp */
    CHECK(near(ibus::normalize(2500), 1.0f));   /* clamp */
}

/* ── Mixer ────────────────────────────────────────────────────────────────── */

void test_mixer()
{
    using cfg::MixMode;

    CHECK(near(mixer::apply_deadzone(0.005f, 0.01f), 0.0f));
    CHECK(near(mixer::apply_deadzone(-0.5f, 0.01f), -0.5f));

    auto o = mixer::mix(0.5f, 0.0f, MixMode::ScaleDown);
    CHECK(near(o.left, 0.5f) && near(o.right, 0.5f));

    o = mixer::mix(0.0f, 0.3f, MixMode::ScaleDown);          /* yerinde sağa dönüş */
    CHECK(near(o.left, 0.3f) && near(o.right, -0.3f));

    o = mixer::mix(1.0f, 0.5f, MixMode::ScaleDown);          /* taşma → orantılı küçült */
    CHECK(near(o.left, 1.0f) && near(o.right, 0.5f / 1.5f));

    o = mixer::mix(-0.5f, -1.0f, MixMode::ScaleDown);        /* eski kodda yanlış ölçeklenen durum */
    CHECK(near(o.left, -1.0f) && near(o.right, 0.5f / 1.5f));

    o = mixer::mix(0.8f, 0.5f, MixMode::ThrottlePriority);   /* gaz korunur, yaw kırpılır */
    CHECK(near(o.left, 1.0f) && near(o.right, 0.6f));

    CHECK(near(mixer::ramp_toward(0.0f, 1.0f, 0.1f), 0.1f));
    CHECK(near(mixer::ramp_toward(0.0f, -1.0f, 0.1f), -0.1f));
    CHECK(near(mixer::ramp_toward(0.95f, 1.0f, 0.1f), 1.0f));
}

/* ── ESC pulse ────────────────────────────────────────────────────────────── */

void test_esc_pulse()
{
    CHECK(esc::to_pulse_us(0.0f, 0, false) == 1500);
    CHECK(esc::to_pulse_us(1.0f, 0, false) == 2000);
    CHECK(esc::to_pulse_us(-1.0f, 0, false) == 1000);
    CHECK(esc::to_pulse_us(0.1f, 0, false) == 1550);
    CHECK(esc::to_pulse_us(5.0f, 0, false) == 2000);          /* clamp */
    CHECK(esc::to_pulse_us(0.0f, -13, false) == 1487);        /* trim */
    CHECK(esc::to_pulse_us(-1.0f, -13, false) == 1000);       /* trim sonrası clamp */
    CHECK(esc::to_pulse_us(0.5f, 0, true) == 1250);           /* ters yön */
    CHECK(esc::to_pulse_us(NAN, 0, false) == 1500);           /* bozuk girdi → nötr */
}

/* ── Arm / failsafe ───────────────────────────────────────────────────────── */

using arming::State;

arming::Input in(bool link, bool sw, bool centered) { return {link, sw, centered}; }

void test_arming_switch()
{
    arming::Fsm fsm({true, false});
    CHECK(fsm.state() == State::WaitLink);
    CHECK(fsm.update(in(false, false, true)) == State::WaitLink);  /* link yok → bekle */

    /* boot'ta switch zaten ARM konumunda → arm olmamalı */
    CHECK(fsm.update(in(true, true, true)) == State::Disarmed);
    CHECK(fsm.update(in(true, true, true)) == State::Disarmed);

    /* switch kapat → aç, ama gaz ortada değil → arm yok */
    CHECK(fsm.update(in(true, false, true)) == State::Disarmed);
    CHECK(fsm.update(in(true, true, false)) == State::Disarmed);
    /* çubuklar ortada → ARMED */
    CHECK(fsm.update(in(true, true, true)) == State::Armed);
    /* gaz verilirken ARMED kalır */
    CHECK(fsm.update(in(true, true, false)) == State::Armed);

    /* LINK KOPTU → FAILSAFE (motorlar durur) */
    CHECK(fsm.update(in(false, true, false)) == State::Failsafe);
    CHECK(fsm.update(in(false, true, false)) == State::Failsafe);

    /* link geri geldi ama gaz hâlâ ileride → kendiliğinden kalkmaz */
    CHECK(fsm.update(in(true, true, false)) == State::Disarmed);
    CHECK(fsm.update(in(true, true, false)) == State::Disarmed);
    /* çubuklar ortalanınca tekrar ARMED */
    CHECK(fsm.update(in(true, true, true)) == State::Armed);

    /* switch kapatılınca DISARMED */
    CHECK(fsm.update(in(true, false, true)) == State::Disarmed);
    /* DISARMED iken link kopması da FAILSAFE'e gider */
    CHECK(fsm.update(in(false, false, true)) == State::Failsafe);
}

void test_arming_switch_cycle_after_failsafe()
{
    arming::Fsm fsm({true, true});
    fsm.update(in(true, false, true));
    CHECK(fsm.update(in(true, true, true)) == State::Armed);
    CHECK(fsm.update(in(false, true, true)) == State::Failsafe);
    /* switch kapat-aç yapılmadan tekrar arm olmaz */
    CHECK(fsm.update(in(true, true, true)) == State::Disarmed);
    CHECK(fsm.update(in(true, false, true)) == State::Disarmed);
    CHECK(fsm.update(in(true, true, true)) == State::Armed);
}

void test_arming_no_switch()
{
    arming::Fsm fsm({false, false});
    CHECK(fsm.update(in(true, false, false)) == State::Disarmed);  /* gaz ortada değil */
    CHECK(fsm.update(in(true, false, true)) == State::Armed);
    CHECK(fsm.update(in(false, false, true)) == State::Failsafe);
    CHECK(fsm.update(in(true, false, true)) == State::Armed);      /* link + ortada → tekrar */
}

}  // namespace

int main()
{
    test_ibus();
    test_mixer();
    test_esc_pulse();
    test_arming_switch();
    test_arming_switch_cycle_after_failsafe();
    test_arming_no_switch();

    if (g_failures == 0) {
        std::printf("Tum testler gecti.\n");
        return EXIT_SUCCESS;
    }
    std::printf("%d test BASARISIZ.\n", g_failures);
    return EXIT_FAILURE;
}
