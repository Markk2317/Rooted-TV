#pragma once
#include "ui/system_sounds.hpp"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rootedtv {
// The tick through PulseAudio, which webOS runs for every app's sound (the C6's sinks all end on
// pcm_output, under the TV's volume). libpulse-simple is opened with dlopen, so the client builds
// without it and stays silent where there is none. A thread mixes up to four ticks at once, as
// Android's sound pool does, and closes the stream after ten quiet seconds so the TV's sound
// output can go idle.
class PulseSounds : public SystemSounds {
  public:
    // `wav`: 16-bit stereo PCM at 44.1 kHz; anything else leaves the sounds off.
    explicit PulseSounds(const std::string &wav);
    ~PulseSounds() override;
    PulseSounds(const PulseSounds &) = delete;
    PulseSounds &operator=(const PulseSounds &) = delete;
    void tick() override;

  private:
    void run();
    std::vector<int16_t> samples_; // interleaved stereo, at Android's sound-effect level
    std::mutex mutex_;
    std::condition_variable wake_;
    unsigned requested_ = 0;
    bool stopping_ = false;
    std::thread thread_;
};
} // namespace rootedtv
