#pragma once
#include <cstdint>

namespace rootedtv {
// A remote key that taps when released and holds after kHoldMs (OK starts editing the apps, Back
// opens the recent apps). Some LG remotes hold back the release until the next key is pressed, so
// until a release has been seen to arrive in time, a press that is neither released nor repeated
// within kTapMs is taken as a tap (one press is enough), and only a press the remote repeats, as it
// does while a key is held, becomes a hold.
class KeyHold {
  public:
    enum class Action { None, Tap, Hold };
    static constexpr uint32_t kTapMs = 550, kHoldMs = 650;

    void press(uint32_t now) {
        if (down_)
            return;
        down_ = true;
        held_ = false;
        repeats_ = 0;
        at_ = now;
    }
    // A repeated press of the key that is down.
    void repeat() {
        if (down_)
            ++repeats_;
    }
    Action release(uint32_t now) {
        if (!down_)
            return Action::None; // late, after the press was taken as a tap
        down_ = false;
        if (!held_ && now - at_ < kTapMs)
            prompt_ = true;
        return held_ ? Action::None : Action::Tap;
    }
    // `holdable`: the screen has something for a hold to do.
    Action tick(uint32_t now, bool holdable) {
        if (!down_ || held_)
            return Action::None;
        const uint32_t elapsed = now - at_;
        if (holdable && elapsed >= kHoldMs && (prompt_ || repeats_ > 0)) {
            held_ = true;
            return Action::Hold;
        }
        if (!prompt_ && (repeats_ == 0 || !holdable) && elapsed >= kTapMs) {
            down_ = false;
            return Action::Tap;
        }
        return Action::None;
    }
    // When tick() next has something to decide, for a loop that sleeps until then.
    bool waiting() const {
        return down_ && !held_;
    }
    uint32_t dueAt() const {
        return at_ + (prompt_ || repeats_ > 0 ? kHoldMs : kTapMs);
    }
    void cancel() {
        down_ = false;
    }

  private:
    bool down_ = false, held_ = false;
    bool prompt_ = false; // a release has arrived in time: this remote's releases can be trusted
    unsigned repeats_ = 0;
    uint32_t at_ = 0;
};
} // namespace rootedtv
