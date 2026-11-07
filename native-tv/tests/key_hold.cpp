#include "platform/key_hold.hpp"
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    using nativetv::KeyHold;
    using Action = KeyHold::Action;
    try {
        {
            // A remote that holds back its release until the next press: one press is a tap.
            KeyHold late;
            late.press(1000);
            check(late.tick(1300, true) == Action::None, "A press waits a moment for its release");
            check(late.tick(1000 + KeyHold::kTapMs, true) == Action::Tap,
                  "A press neither released nor repeated is a tap, so it is never taken as a hold");
            check(late.release(5000) == Action::None, "The late release that follows does nothing");
            // Held, that remote repeats the key: a hold.
            late.press(6000);
            late.repeat();
            check(late.tick(6000 + KeyHold::kTapMs, true) == Action::None &&
                      late.tick(6000 + KeyHold::kHoldMs, true) == Action::Hold,
                  "A repeated press becomes a hold");
            check(late.release(9000) == Action::None, "The hold's release does nothing more");
            // Repeated where a hold does nothing, it is a tap.
            late.press(10000);
            late.repeat();
            check(late.tick(10000 + KeyHold::kTapMs, false) == Action::Tap,
                  "Where a hold does nothing a repeated press is a tap");
        }
        {
            // A remote that releases in time: a tap on release, a hold by time alone.
            KeyHold prompt;
            prompt.press(0);
            check(prompt.release(120) == Action::Tap, "A press released in time is a tap");
            prompt.press(1000);
            check(prompt.tick(1000 + KeyHold::kTapMs, true) == Action::None &&
                      prompt.tick(1000 + KeyHold::kHoldMs, true) == Action::Hold,
                  "Once releases arrive in time, a press held without repeats is a hold");
            check(prompt.release(2000) == Action::None, "The release after a hold does nothing");
            prompt.press(3000);
            check(prompt.tick(3000 + KeyHold::kHoldMs + 500, false) == Action::None &&
                      prompt.release(4000) == Action::Tap,
                  "Where a hold does nothing, a long press of such a remote taps on release");
        }
        {
            KeyHold idle;
            check(!idle.waiting() && idle.release(10) == Action::None &&
                      idle.tick(10000, true) == Action::None,
                  "A key that is not down does nothing");
            idle.press(0);
            check(idle.waiting() && idle.dueAt() == KeyHold::kTapMs,
                  "A press is due its decision after kTapMs");
            idle.cancel();
            check(!idle.waiting(), "Losing the focus forgets the press");
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    std::puts("Key hold checks passed");
    return 0;
}
