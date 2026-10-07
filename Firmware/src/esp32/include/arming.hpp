#pragma once

/* Arm / failsafe durum makinesi — donanımdan bağımsız.
 *
 *            link yok                     link var
 *   WAIT_LINK ──────────► (bekler) ──────────────► DISARMED
 *                                                   │   ▲
 *                     switch ARM + çubuklar ortada  │   │ switch kapalı
 *                                                   ▼   │
 *                                                  ARMED
 *   DISARMED / ARMED ── link koptu ──► FAILSAFE ── link geldi ──► DISARMED
 *   herhangi bir durum ── kill switch ──► KILLED ── bırakıldı ──► DISARMED
 *                                         (tekrar arm için switch kapat-aç şart)
 *
 * Motorlar sadece ARMED durumunda döner; diğer tüm durumlarda nötr. */

#include <cstdint>

namespace arming {

enum class State : uint8_t { WaitLink, Disarmed, Armed, Failsafe, Killed };

inline const char *to_string(State s)
{
    switch (s) {
        case State::WaitLink: return "WAIT_LINK";
        case State::Disarmed: return "DISARMED";
        case State::Armed:    return "ARMED";
        case State::Failsafe: return "FAILSAFE";
        case State::Killed:   return "KILLED";
    }
    return "?";
}

struct Input {
    bool link_ok;          /* taze ve geçerli iBUS frame var mı   */
    bool arm_switch_on;    /* CH5 ARM konumunda mı                 */
    bool sticks_centered;  /* gaz ve yaw çubukları ortada mı       */
    bool kill = false;     /* acil durdurma butonu aktif mi        */
};

struct Options {
    bool use_arm_switch;
    bool require_switch_cycle_after_failsafe;
};

class Fsm {
public:
    explicit Fsm(Options opt) : opt_(opt) {}

    State update(const Input &in)
    {
        if (in.kill) {
            state_           = State::Killed;
            switch_seen_off_ = false;  /* bırakınca kendiliğinden kalkmasın */
            return state_;
        }
        if (state_ == State::Killed) state_ = in.link_ok ? State::Disarmed : State::Failsafe;

        if (!in.link_ok) {
            if (state_ == State::Armed || state_ == State::Disarmed) {
                state_ = State::Failsafe;
                if (opt_.require_switch_cycle_after_failsafe) switch_seen_off_ = false;
            }
            return state_;
        }

        if (state_ == State::WaitLink || state_ == State::Failsafe) state_ = State::Disarmed;

        /* Boot'ta switch zaten ARM'daysa motorlar hemen kalkmasın: önce OFF görülmeli */
        if (!in.arm_switch_on) switch_seen_off_ = true;

        if (state_ == State::Disarmed && can_arm(in)) {
            state_ = State::Armed;
        } else if (state_ == State::Armed && opt_.use_arm_switch && !in.arm_switch_on) {
            state_ = State::Disarmed;
        }
        return state_;
    }

    State state() const { return state_; }

private:
    bool can_arm(const Input &in) const
    {
        if (!in.sticks_centered) return false;  /* ani kalkışı önler */
        if (!opt_.use_arm_switch) return true;
        return in.arm_switch_on && switch_seen_off_;
    }

    Options opt_;
    State   state_           = State::WaitLink;
    bool    switch_seen_off_ = false;
};

/* Dijital giriş için debounce: değer ancak `required` ardışık örnek boyunca
 * aynı kalırsa değişir. Başlangıç değeri `initial`. */
class Debouncer {
public:
    Debouncer(uint32_t required, bool initial) : required_(required), stable_(initial) {}

    bool update(bool raw)
    {
        if (raw == stable_) {
            count_ = 0;
        } else if (++count_ >= required_) {
            stable_ = raw;
            count_  = 0;
        }
        return stable_;
    }

    bool value() const { return stable_; }

private:
    uint32_t required_;
    uint32_t count_ = 0;
    bool     stable_;
};

}  // namespace arming
