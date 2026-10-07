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
#include "ibus_telemetry.hpp"
#include "mixer.hpp"
#include "power.hpp"

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
    CHECK(near(mixer::apply_deadzone(0.03f, 0.03f), 0.0f));       /* kenar: hâlâ 0 */
    CHECK(near(mixer::apply_deadzone(0.515f, 0.03f), 0.5f));      /* yeniden ölçek */
    CHECK(near(mixer::apply_deadzone(-0.515f, 0.03f), -0.5f));
    CHECK(near(mixer::apply_deadzone(1.0f, 0.03f), 1.0f));        /* tam çubuk = tam */
    CHECK(near(mixer::apply_deadzone(0.031f, 0.03f), 0.001f / 0.97f));  /* sıçrama yok */

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

void test_arming_kill()
{
    arming::Fsm fsm({true, false});
    fsm.update(in(true, false, true));
    CHECK(fsm.update(in(true, true, true)) == State::Armed);

    arming::Input k = in(true, true, false);
    k.kill = true;
    CHECK(fsm.update(k) == State::Killed);                    /* gaz verilirken bile durur */
    CHECK(fsm.update(k) == State::Killed);
    /* bırakıldı, switch hâlâ ARM ve çubuklar ortada → kendiliğinden kalkmaz */
    CHECK(fsm.update(in(true, true, true)) == State::Disarmed);
    CHECK(fsm.update(in(true, true, true)) == State::Disarmed);
    CHECK(fsm.update(in(true, false, true)) == State::Disarmed);  /* switch kapat */
    CHECK(fsm.update(in(true, true, true)) == State::Armed);      /* aç → ARMED */

    /* kill, link yokken de geçerli; bırakılınca link yoksa FAILSAFE */
    arming::Input k2 = in(false, true, true);
    k2.kill = true;
    CHECK(fsm.update(k2) == State::Killed);
    CHECK(fsm.update(in(false, true, true)) == State::Failsafe);

    /* boot'ta basılı */
    arming::Fsm boot({true, false});
    CHECK(boot.update(k) == State::Killed);
}

void test_debouncer()
{
    arming::Debouncer d(3, true);
    CHECK(d.update(false) == true);
    CHECK(d.update(false) == true);
    CHECK(d.update(true) == true);    /* sıçrama sayacı sıfırlar */
    CHECK(d.update(false) == true);
    CHECK(d.update(false) == true);
    CHECK(d.update(false) == false);  /* 3 ardışık */
    CHECK(d.update(true) == false);
}

void test_arming_no_switch()
{
    arming::Fsm fsm({false, false});
    CHECK(fsm.update(in(true, false, false)) == State::Disarmed);  /* gaz ortada değil */
    CHECK(fsm.update(in(true, false, true)) == State::Armed);
    CHECK(fsm.update(in(false, false, true)) == State::Failsafe);
    CHECK(fsm.update(in(true, false, true)) == State::Armed);      /* link + ortada → tekrar */
}

/* ── Güç izleme (Faz 3) ───────────────────────────────────────────────────── */

/* Monitor'ü `seconds` boyunca sabit ölçümle çalıştırır (10 Hz). */
void run_for(power::Monitor &mon, const power::Measurements &m, float seconds)
{
    const int steps = static_cast<int>(seconds / 0.1f + 0.5f);
    for (int i = 0; i < steps; ++i) mon.update(m, 0.1f);
}

power::Measurements volts(float v)
{
    power::Measurements m;
    m.voltage_enabled = true;
    m.voltage_v       = v;
    return m;
}

void test_power_conversions()
{
    CHECK(near(power::battery_volts(2.0f), 2.0f * cfg::kBattVoltsPerVolt));
    CHECK(near(power::current_amps(cfg::kCurrentZeroV), 0.0f));
    CHECK(near(power::current_amps(cfg::kCurrentZeroV + 10.0f * cfg::kCurrentVoltsPerAmp), 10.0f, 1e-3f));
    /* NTC: R_ntc = R_seri → 25 °C (yarı besleme) */
    CHECK(near(power::ntc_celsius(cfg::kNtcSupplyV / 2.0f), 25.0f, 0.01f));
    CHECK(power::ntc_celsius(cfg::kNtcSupplyV / 2.0f - 0.3f) > 25.0f);  /* NTC: düşük direnç = sıcak */
    CHECK(std::isnan(power::ntc_celsius(0.0f)));                       /* kısa devre */
    CHECK(std::isnan(power::ntc_celsius(cfg::kNtcSupplyV)));           /* açık devre */
}

void test_battery()
{
    using power::BatteryLevel;
    power::Monitor mon;
    CHECK(mon.status().battery == BatteryLevel::Unknown);
    CHECK(near(mon.status().power_scale, 1.0f));

    run_for(mon, volts(24.0f), 1.0f);
    CHECK(mon.status().battery == BatteryLevel::Ok);
    CHECK(near(mon.status().voltage_v, 24.0f));                /* ilk ölçümle başlar */

    /* kısa süreli çökme (hızlanma anı) → seviye düşmez */
    run_for(mon, volts(19.0f), 1.0f);
    run_for(mon, volts(24.0f), 2.0f);
    CHECK(mon.status().battery == BatteryLevel::Ok);

    /* 20.6 V (3.43 V/hücre) uzun süre → DUSUK, güç sınırı yok */
    run_for(mon, volts(20.6f), 10.0f);
    CHECK(mon.status().battery == BatteryLevel::Low);
    CHECK(near(mon.status().power_scale, 1.0f));

    /* 20.2 V (3.37 V/hücre) → LIMIT */
    run_for(mon, volts(20.2f), 10.0f);
    CHECK(mon.status().battery == BatteryLevel::Limit);
    CHECK(near(mon.status().power_scale, cfg::kBattLimitScale));

    /* yük kalkınca voltaj toparlansa da seviye geri yükselmez (kilit) */
    run_for(mon, volts(22.0f), 20.0f);
    CHECK(mon.status().battery == BatteryLevel::Limit);

    /* 19.5 V → KRITIK */
    run_for(mon, volts(19.5f), 15.0f);
    CHECK(mon.status().battery == BatteryLevel::Critical);
    CHECK(near(mon.status().power_scale, cfg::kBattCriticalScale));

    /* sensör arızası (kablo koptu, 0 V okuyor) → seviye korunur, yeni karar yok */
    power::Monitor mon2;
    run_for(mon2, volts(24.0f), 1.0f);
    run_for(mon2, volts(0.3f), 10.0f);
    CHECK(mon2.status().battery == BatteryLevel::Ok);
    CHECK(!mon2.status().voltage_valid);
    CHECK(near(mon2.status().power_scale, 1.0f));

    /* boot'ta zaten boş batarya → debounce sonrası KRITIK */
    power::Monitor mon3;
    run_for(mon3, volts(19.0f), 1.0f);
    CHECK(mon3.status().battery == BatteryLevel::Ok);         /* henüz debounce dolmadı */
    run_for(mon3, volts(19.0f), 5.0f);
    CHECK(mon3.status().battery == BatteryLevel::Critical);
}

void test_current_limit()
{
    power::Monitor mon;
    power::Measurements m;
    m.current_enabled = true;

    m.current_a = 30.0f;
    run_for(mon, m, 2.0f);
    CHECK(near(mon.status().current_scale, 1.0f));

    m.current_a = 80.0f;  /* sınır üstü → güç kısılır, ama alt sınırın altına inmez */
    run_for(mon, m, 0.5f);
    CHECK(mon.status().current_scale < 1.0f);
    CHECK(mon.status().current_limiting);
    run_for(mon, m, 10.0f);
    CHECK(near(mon.status().current_scale, cfg::kCurrentMinScale));

    m.current_a = 20.0f;  /* düşünce yavaşça geri gelir */
    run_for(mon, m, 1.0f);
    CHECK(mon.status().current_scale > cfg::kCurrentMinScale);
    CHECK(mon.status().current_scale < 1.0f);
    run_for(mon, m, 10.0f);
    CHECK(near(mon.status().current_scale, 1.0f));
}

void test_temperature()
{
    CHECK(near(power::temp_scale_for(25.0f), 1.0f));
    CHECK(near(power::temp_scale_for(cfg::kTempDerateStartC), 1.0f));
    CHECK(near(power::temp_scale_for((cfg::kTempDerateStartC + cfg::kTempDerateEndC) / 2.0f),
               (1.0f + cfg::kTempMinScale) / 2.0f));
    CHECK(near(power::temp_scale_for(cfg::kTempDerateEndC + 10.0f), cfg::kTempMinScale));

    power::Monitor mon;
    power::Measurements m;
    m.temp_enabled = true;
    m.temp_c       = 65.0f;
    run_for(mon, m, 1.0f);
    CHECK(mon.status().temp_warning);
    CHECK(near(mon.status().power_scale, 1.0f));

    m.temp_c = NAN;  /* termistör koptu → uyarı yok, güç kesilmez */
    run_for(mon, m, 1.0f);
    CHECK(!mon.status().temp_valid);
    CHECK(near(mon.status().power_scale, 1.0f));
}

void test_power_scale_is_minimum()
{
    power::Monitor mon;
    power::Measurements m = volts(20.2f);  /* LIMIT → 0.5 */
    m.temp_enabled = true;
    m.temp_c       = (cfg::kTempDerateStartC + cfg::kTempDerateEndC) / 2.0f;
    run_for(mon, m, 30.0f);
    const float expected = std::min(cfg::kBattLimitScale, power::temp_scale_for(m.temp_c));
    CHECK(near(mon.status().power_scale, expected, 1e-3f));
}

/* ── iBUS telemetri ───────────────────────────────────────────────────────── */

std::vector<uint8_t> sensor_cmd(uint8_t cmd, uint8_t addr)
{
    const uint8_t  b1 = static_cast<uint8_t>(cmd | addr);
    const uint16_t cs = static_cast<uint16_t>(0xFFFF - 0x04 - b1);
    return {0x04, b1, static_cast<uint8_t>(cs & 0xFF), static_cast<uint8_t>(cs >> 8)};
}

size_t feed_cmd(ibus::telemetry::Responder &r, const std::vector<uint8_t> &bytes,
                ibus::telemetry::Responder::Response &out)
{
    size_t len = 0;
    for (uint8_t b : bytes) {
        const size_t l = r.push(b, out);
        if (l) len = l;
    }
    return len;
}

bool checksum_ok(const ibus::telemetry::Responder::Response &out, size_t len)
{
    uint16_t cs = 0xFFFF;
    for (size_t i = 0; i < len - 2; ++i) cs = static_cast<uint16_t>(cs - out[i]);
    return out[len - 2] == (cs & 0xFF) && out[len - 1] == (cs >> 8);
}

void test_telemetry()
{
    using namespace ibus::telemetry;
    CHECK(encode_voltage(22.2f) == 2220);
    CHECK(encode_voltage(-1.0f) == 0);
    CHECK(encode_temperature(25.0f) == 650);
    CHECK(encode_temperature(-40.0f) == 0);
    CHECK(encode_current(12.34f) == 1234);

    Responder r;
    const uint8_t a_v = r.add_sensor(SensorType::ExternalVoltage);
    const uint8_t a_t = r.add_sensor(SensorType::Temperature);
    CHECK(a_v == 1 && a_t == 2);
    r.set_value(a_v, 2220);

    Responder::Response out{};
    /* keşif: komutun aynısı geri döner */
    auto disc = sensor_cmd(0x80, a_v);
    CHECK(feed_cmd(r, disc, out) == 4);
    CHECK(out[0] == disc[0] && out[1] == disc[1] && out[2] == disc[2] && out[3] == disc[3]);

    /* tip sorgusu */
    CHECK(feed_cmd(r, sensor_cmd(0x90, a_t), out) == 6);
    CHECK(out[0] == 0x06 && out[1] == (0x90 | a_t) && out[2] == 0x01 && out[3] == 0x02);
    CHECK(checksum_ok(out, 6));

    /* ölçüm */
    CHECK(feed_cmd(r, sensor_cmd(0xA0, a_v), out) == 6);
    CHECK(out[0] == 0x06 && out[1] == (0xA0 | a_v) && (out[2] | (out[3] << 8)) == 2220);
    CHECK(checksum_ok(out, 6));

    /* adres 0 (alıcının kendisi) ve tanımsız adres → sessiz kal */
    CHECK(feed_cmd(r, sensor_cmd(0x80, 0), out) == 0);
    CHECK(feed_cmd(r, sensor_cmd(0x80, 3), out) == 0);

    /* bozuk checksum → cevap yok; önde çöp bayt olsa da senkronlanır */
    auto bad = sensor_cmd(0xA0, a_v);
    bad[3] ^= 0x01;
    CHECK(feed_cmd(r, bad, out) == 0);
    std::vector<uint8_t> noisy = {0x55, 0x04, 0x13};
    auto good = sensor_cmd(0xA0, a_v);
    noisy.insert(noisy.end(), good.begin(), good.end());
    CHECK(feed_cmd(r, noisy, out) == 6);
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
    test_arming_kill();
    test_debouncer();
    test_power_conversions();
    test_battery();
    test_current_limit();
    test_temperature();
    test_power_scale_is_minimum();
    test_telemetry();

    if (g_failures == 0) {
        std::printf("Tum testler gecti.\n");
        return EXIT_SUCCESS;
    }
    std::printf("%d test BASARISIZ.\n", g_failures);
    return EXIT_FAILURE;
}
