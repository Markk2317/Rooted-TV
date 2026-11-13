#include "platform/pulse_sounds.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <iterator>

namespace nativetv {
namespace {
// libpulse's simple API, declared here so the build needs no PulseAudio headers; the layouts are
// PulseAudio's stable ABI.
struct pa_simple;
struct PaSampleSpec {
    int format;
    uint32_t rate;
    uint8_t channels;
};
struct PaBufferAttr {
    uint32_t maxlength, tlength, prebuf, minreq, fragsize;
};
constexpr int kSampleS16le = 3;    // PA_SAMPLE_S16LE
constexpr int kStreamPlayback = 1; // PA_STREAM_PLAYBACK
constexpr uint32_t kRate = 44100;
constexpr size_t kChunkFrames = kRate / 100; // 10 ms per write
constexpr size_t kVoices = 4;                // SoundEffectsHelper's NUM_SOUNDPOOL_CHANNELS
// Android plays sound effects 6 dB down (config_soundEffectVolumeDb, -6 on the Chromecast too).
constexpr float kEffectGain = 0.5012f;

struct Pulse {
    pa_simple *(*open)(const char *, const char *, int, const char *, const char *,
                       const PaSampleSpec *, const void *, const PaBufferAttr *, int *) = nullptr;
    int (*write)(pa_simple *, const void *, size_t, int *) = nullptr;
    void (*close)(pa_simple *) = nullptr;
    const char *(*describe)(int) = nullptr;
};
template <typename Function> bool symbol(void *library, const char *name, Function &function) {
    function = reinterpret_cast<Function>(dlsym(library, name));
    return function != nullptr;
}
// Null pointers when there is no libpulse-simple: the sounds stay off.
const Pulse &pulse() {
    static Pulse loaded = [] {
        Pulse found;
        void *library = dlopen("libpulse-simple.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!library) {
            std::fprintf(stderr, "System sounds off: %s\n", dlerror());
            return found;
        }
        // pa_strerror is libpulse's, reached through libpulse-simple's own dependency.
        if (!symbol(library, "pa_simple_new", found.open) ||
            !symbol(library, "pa_simple_write", found.write) ||
            !symbol(library, "pa_simple_free", found.close) ||
            !symbol(library, "pa_strerror", found.describe)) {
            std::fprintf(stderr, "System sounds off: libpulse-simple lacks the simple API\n");
            dlclose(library);
            return Pulse();
        }
        return found;
    }();
    return loaded;
}
pa_simple *openStream(const Pulse &api) {
    const PaSampleSpec spec = {kSampleS16le, kRate, 2};
    // 40 ms ahead: short enough that a tick follows its key at once.
    const uint32_t ahead = static_cast<uint32_t>(kRate * 4 / 25);
    // Playback starts once one chunk is in. The C6's pcm_output has a fixed 93 ms latency, so the
    // server stretches the 40 ms to about 113 ms, and the default start level (about 103 ms) is
    // more than one 94 ms tick: each tick waited for the next and the two played together.
    const uint32_t start = static_cast<uint32_t>(kChunkFrames * 2 * sizeof(int16_t));
    const PaBufferAttr buffer = {static_cast<uint32_t>(-1), ahead, start,
                                 static_cast<uint32_t>(-1), static_cast<uint32_t>(-1)};
    int error = 0;
    // The default server first, then webOS's system-wide one: an app's environment names none.
    for (const char *server : {static_cast<const char *>(nullptr), "unix:/var/run/pulse/native"}) {
        if (pa_simple *stream = api.open(server, "Native Tv", kStreamPlayback, nullptr,
                                         "System sounds", &spec, nullptr, &buffer, &error))
            return stream;
    }
    std::fprintf(stderr, "System sounds: PulseAudio refused a stream: %s\n", api.describe(error));
    return nullptr;
}
uint32_t little32(const std::string &data, size_t at) {
    return static_cast<uint32_t>(static_cast<unsigned char>(data[at])) |
           static_cast<uint32_t>(static_cast<unsigned char>(data[at + 1])) << 8 |
           static_cast<uint32_t>(static_cast<unsigned char>(data[at + 2])) << 16 |
           static_cast<uint32_t>(static_cast<unsigned char>(data[at + 3])) << 24;
}
uint16_t little16(const std::string &data, size_t at) {
    return static_cast<uint16_t>(static_cast<unsigned char>(data[at]) |
                                 static_cast<unsigned char>(data[at + 1]) << 8);
}
// The samples of a 16-bit stereo 44.1 kHz WAV, at the effect level; empty for any other file.
std::vector<int16_t> readTick(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<int16_t> samples;
    if (data.size() < 12 || data.compare(0, 4, "RIFF") != 0 || data.compare(8, 4, "WAVE") != 0)
        return samples;
    bool format = false;
    for (size_t at = 12; at + 8 <= data.size();) {
        const std::string id = data.substr(at, 4);
        const size_t size = little32(data, at + 4), body = at + 8;
        if (size > data.size() - body)
            break;
        if (id == "fmt " && size >= 16)
            format = little16(data, body) == 1 && little16(data, body + 2) == 2 &&
                     little32(data, body + 4) == kRate && little16(data, body + 14) == 16;
        if (id == "data" && format) {
            for (size_t i = 0; i + 1 < size; i += 2)
                samples.push_back(static_cast<int16_t>(
                    static_cast<int16_t>(little16(data, body + i)) * kEffectGain));
            return samples;
        }
        at = body + size + (size & 1);
    }
    return samples;
}
} // namespace

PulseSounds::PulseSounds(const std::string &wav) : samples_(readTick(wav)) {
    if (samples_.empty())
        std::fprintf(stderr, "System sounds off: %s is not 16-bit stereo 44.1 kHz PCM\n", wav.c_str());
    else
        thread_ = std::thread(&PulseSounds::run, this);
}
PulseSounds::~PulseSounds() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_one();
    if (thread_.joinable())
        thread_.join();
}
void PulseSounds::tick() {
    if (samples_.empty())
        return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++requested_;
    }
    wake_.notify_one();
}
void PulseSounds::run() {
    const Pulse &api = pulse();
    pa_simple *stream = nullptr;
    std::vector<size_t> voices; // where each tick playing has got to, in samples
    std::vector<int16_t> chunk(kChunkFrames * 2);
    bool refused = false;
    auto refusedAt = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
        if (voices.empty() && !requested_) {
            // Quiet: wait for a tick, and let go of the stream after ten seconds of none.
            if (!wake_.wait_for(lock, std::chrono::seconds(10),
                                [this] { return requested_ || stopping_; }) &&
                stream) {
                api.close(stream);
                stream = nullptr;
            }
            continue;
        }
        for (; requested_; --requested_) {
            if (voices.size() == kVoices)
                voices.erase(voices.begin());
            voices.push_back(0);
        }
        lock.unlock();
        // A server that refused is asked again only after a while, not at every key.
        if (!stream && api.open &&
            (!refused || std::chrono::steady_clock::now() - refusedAt > std::chrono::seconds(30))) {
            stream = openStream(api);
            refused = !stream;
            refusedAt = std::chrono::steady_clock::now();
        }
        if (!stream) {
            voices.clear();
            lock.lock();
            continue;
        }
        std::fill(chunk.begin(), chunk.end(), 0);
        for (size_t &voice : voices) {
            for (size_t i = 0; i < chunk.size() && voice < samples_.size(); ++i, ++voice)
                chunk[i] = static_cast<int16_t>(
                    std::max(-32768, std::min(32767, chunk[i] + samples_[voice])));
        }
        voices.erase(std::remove_if(voices.begin(), voices.end(),
                                    [this](size_t voice) { return voice >= samples_.size(); }),
                     voices.end());
        int error = 0;
        // Blocks while the 40 ms ahead are full, which paces the loop.
        if (api.write(stream, chunk.data(), chunk.size() * sizeof(int16_t), &error) < 0) {
            std::fprintf(stderr, "System sounds: PulseAudio write failed: %s\n", api.describe(error));
            api.close(stream);
            stream = nullptr;
            voices.clear();
        }
        lock.lock();
    }
    if (stream)
        api.close(stream);
}
} // namespace nativetv
