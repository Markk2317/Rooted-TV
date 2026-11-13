#include "graphics/video_player.hpp"
#include <algorithm>
#include <cmath>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace rootedtv {
namespace {
const size_t kQueuePictures = 3;
const int kDarkLuma = 24;
const double kSkipDarkMs = 4000;
// Trailers open on black or a rating card: playback starts at the first key frame this far in,
// when at least kStartLeftMs of the clip follows it.
const double kStartAtMs = 2000, kStartLeftMs = 8000;
const double kRetryMs = 1500, kGiveUpMs = 30000;
const size_t kClipsMax = 2;

inline uint8_t clampByte(int value) {
    return static_cast<uint8_t>(value < 0 ? 0 : value > 255 ? 255 : value);
}
// A black row: dark on average and nowhere bright (letterboxing, not a night scene's sky).
bool blackRow(const YuvPicture &picture, int row) {
    const uint8_t *luma = picture.plane[0] + static_cast<size_t>(row) * picture.stride[0];
    const int floor = picture.fullRange ? 0 : 16;
    int total = 0, count = 0;
    for (int x = 0; x < picture.width; x += 4, ++count) {
        if (luma[x] > floor + 24)
            return false;
        total += luma[x];
    }
    return count && total / count < floor + 6;
}
} // namespace

void blackBars(const YuvPicture &picture, int &top, int &bottom) {
    top = bottom = 0;
    const int limit = picture.height / 3;
    while (top < limit && blackRow(picture, top))
        ++top;
    while (bottom < limit && blackRow(picture, picture.height - 1 - bottom))
        ++bottom;
    top = (top + 1) & ~1;
    bottom = (bottom + 1) & ~1;
}

PixelRect coverCrop(int width, int height, int aspectNum, int aspectDen, int top, int bottom) {
    PixelRect crop;
    if (aspectNum <= 0 || aspectDen <= 0)
        aspectNum = aspectDen = 1;
    int content = height - top - bottom;
    if (top < 0 || bottom < 0 || content < height / 2) {
        top = 0;
        content = height;
    }
    // Display width over height, against 16:9.
    const double shown = static_cast<double>(width) * aspectNum / (static_cast<double>(content) * aspectDen);
    if (shown > 16.0 / 9) {
        crop.h = content & ~1;
        crop.w = std::min(width, static_cast<int>(std::lround(content * 16.0 * aspectDen /
                                                              (9.0 * aspectNum)))) & ~1;
        crop.x = ((width - crop.w) / 2) & ~1;
        crop.y = top & ~1;
    } else {
        crop.w = width & ~1;
        crop.h = std::min(content, static_cast<int>(std::lround(width * 9.0 * aspectNum /
                                                                (16.0 * aspectDen)))) & ~1;
        crop.x = 0;
        crop.y = (top + (content - crop.h) / 2) & ~1;
    }
    return crop;
}

void yuvToRgba(const YuvPicture &picture, const PixelRect &crop, uint8_t *rgba) {
    // Fixed point, eight fractional bits: video levels scale Y by 255/219 and narrow the chroma.
    const int luma = picture.fullRange ? 256 : 298, offset = picture.fullRange ? 0 : 16;
    const int rv = picture.fullRange ? 359 : 409, gu = picture.fullRange ? 88 : 100,
              gv = picture.fullRange ? 183 : 208, bu = picture.fullRange ? 454 : 516;
    for (int row = 0; row < crop.h; ++row) {
        const int y = crop.y + row;
        const uint8_t *ys = picture.plane[0] + static_cast<size_t>(y) * picture.stride[0] + crop.x;
        const uint8_t *us = picture.plane[1] + static_cast<size_t>(y / 2) * picture.stride[1] + crop.x / 2;
        const uint8_t *vs = picture.plane[2] + static_cast<size_t>(y / 2) * picture.stride[2] + crop.x / 2;
        uint8_t *out = rgba + static_cast<size_t>(row) * crop.w * 4;
        int x = 0;
#if defined(__ARM_NEON)
        // Sixteen texels a turn, six fractional bits so the products fit 16-bit lanes (sums that
        // pass them saturate, as the colour would): 8.4 ms a 610x458 picture in C on the C6's
        // Cortex-A53, 2.2 ms this way.
        const int16_t scale = picture.fullRange ? 64 : 75, red6 = picture.fullRange ? 90 : 102,
                      greenU6 = picture.fullRange ? 22 : 25, greenV6 = picture.fullRange ? 46 : 52,
                      blue6 = picture.fullRange ? 113 : 129;
        const uint8x8_t floor8 = vdup_n_u8(static_cast<uint8_t>(offset)), middle = vdup_n_u8(128);
        const uint8x8_t opaque = vdup_n_u8(255);
        for (; x + 16 <= crop.w; x += 16) {
            const uint8x16_t y16 = vld1q_u8(ys + x);
            const int16x8_t u = vreinterpretq_s16_u16(vsubl_u8(vld1_u8(us + x / 2), middle));
            const int16x8_t v = vreinterpretq_s16_u16(vsubl_u8(vld1_u8(vs + x / 2), middle));
            const int16x8x2_t red = vzipq_s16(vmulq_n_s16(v, red6), vmulq_n_s16(v, red6));
            const int16x8_t greenTerm = vnegq_s16(vaddq_s16(vmulq_n_s16(u, greenU6), vmulq_n_s16(v, greenV6)));
            const int16x8x2_t green = vzipq_s16(greenTerm, greenTerm);
            const int16x8x2_t blue = vzipq_s16(vmulq_n_s16(u, blue6), vmulq_n_s16(u, blue6));
            const uint8x8_t halves[2] = {vget_low_u8(y16), vget_high_u8(y16)};
            for (int half = 0; half < 2; ++half) {
                const int16x8_t luma8 = vmulq_n_s16(
                    vreinterpretq_s16_u16(vsubl_u8(vmax_u8(halves[half], floor8), floor8)), scale);
                uint8x8x4_t texels;
                texels.val[0] = vqrshrun_n_s16(vqaddq_s16(luma8, red.val[half]), 6);
                texels.val[1] = vqrshrun_n_s16(vqaddq_s16(luma8, green.val[half]), 6);
                texels.val[2] = vqrshrun_n_s16(vqaddq_s16(luma8, blue.val[half]), 6);
                texels.val[3] = opaque;
                vst4_u8(out, texels);
                out += 32;
            }
        }
#endif
        for (; x < crop.w; x += 2) {
            const int u = us[x / 2] - 128, v = vs[x / 2] - 128;
            const int red = rv * v + 128, green = -gu * u - gv * v + 128, blue = bu * u + 128;
            for (int i = 0; i < 2; ++i) {
                const int c = (ys[x + i] - offset) * luma;
                out[0] = clampByte((c + red) >> 8);
                out[1] = clampByte((c + green) >> 8);
                out[2] = clampByte((c + blue) >> 8);
                out[3] = 255;
                out += 4;
            }
        }
    }
}

int meanLuma(const YuvPicture &picture) {
    long total = 0, count = 0;
    for (int y = 0; y < picture.height; y += 8) {
        const uint8_t *row = picture.plane[0] + static_cast<size_t>(y) * picture.stride[0];
        for (int x = 0; x < picture.width; x += 8, ++count)
            total += row[x];
    }
    return count ? static_cast<int>(total / count) : 0;
}

VideoClip::VideoClip(std::shared_ptr<const std::string> bytes, Mp4Video video)
    : bytes_(std::move(bytes)), video_(std::move(video)) {
    thread_ = std::thread(&VideoClip::run, this);
}

VideoClip::~VideoClip() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    changed_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void VideoClip::run() {
#ifdef __linux__
    // Decoding gives way to the frames being drawn.
    (void)setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#endif
    auto finish = [this](bool failed, double endMs) {
        std::lock_guard<std::mutex> lock(mutex_);
        done_ = true;
        failed_ = failed;
        endMs_ = endMs;
        changed_.notify_all();
    };
    H264Decoder decoder;
    std::string error;
    if (!decoder.open(error)) {
        finish(true, 0);
        return;
    }
    const std::vector<Mp4Sample> &samples = video_.samples;
    size_t start = 0;
    for (size_t i = 0; i < samples.size(); ++i)
        if (samples[i].key && samples[i].atMs >= kStartAtMs) {
            if (video_.durationMs - samples[i].atMs >= kStartLeftMs)
                start = i;
            break;
        }
    // Pictures come out in display order, later than their samples went in where High profile's
    // B-frames are reordered; at a constant frame rate each takes the next sample's time in turn.
    std::deque<double> times;
    std::string unit;
    YuvPicture decoded;
    bool started = false;
    double originMs = 0;
    PixelRect crop;
    for (size_t index = start; index <= samples.size(); ++index) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
                return;
        }
        const bool draining = index == samples.size();
        if (draining)
            unit.clear();
        else if (!annexBSample(*bytes_, video_, index, unit))
            break;
        else
            times.push_back(samples[index].atMs);
        // Draining repeats until the decoder has nothing left; a sample is decoded once.
        for (bool again = true; again;) {
            again = draining;
            const int result = decoder.decode(unit, decoded);
            if (result < 0 && !draining && !times.empty())
                times.pop_back();
            if (result <= 0)
                break;
            const double at = times.empty() ? 0 : times.front();
            if (!times.empty())
                times.pop_front();
            if (!started) {
                if (meanLuma(decoded) < kDarkLuma && at - samples[start].atMs < kSkipDarkMs)
                    continue;
                int top = 0, bottom = 0;
                blackBars(decoded, top, bottom);
                crop = coverCrop(decoded.width, decoded.height, video_.aspectNum, video_.aspectDen,
                                 top, bottom);
                if (crop.w <= 0 || crop.h <= 0) {
                    finish(true, 0);
                    return;
                }
                started = true;
                originMs = at;
                std::lock_guard<std::mutex> lock(mutex_);
                endMs_ = video_.durationMs - originMs;
            }
            Picture picture;
            picture.width = crop.w;
            picture.height = crop.h;
            picture.atMs = at - originMs;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!spare_.empty()) {
                    picture.rgba.swap(spare_.back());
                    spare_.pop_back();
                }
            }
            picture.rgba.resize(static_cast<size_t>(crop.w) * crop.h * 4);
            yuvToRgba(decoded, crop, picture.rgba.data());
            std::unique_lock<std::mutex> lock(mutex_);
            changed_.wait(lock, [this] { return stopping_ || queue_.size() < kQueuePictures; });
            if (stopping_)
                return;
            queue_.push_back(std::move(picture));
        }
    }
    finish(!started, started ? video_.durationMs - originMs : 0);
}

bool VideoClip::take(double elapsedMs, Picture &picture) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool taken = false;
    while (!queue_.empty() && queue_.front().atMs <= elapsedMs) {
        if (taken || !picture.rgba.empty())
            spare_.push_back(std::move(picture.rgba));
        picture = std::move(queue_.front());
        queue_.pop_front();
        taken = true;
    }
    if (taken)
        changed_.notify_all();
    return taken;
}

bool VideoClip::ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !queue_.empty();
}

bool VideoClip::finished() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return done_ && queue_.empty();
}

bool VideoClip::failed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failed_;
}

double VideoClip::endMs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return endMs_;
}

VideoLoader::VideoLoader(TextureStore &store, Fetch fetch) : store_(store), fetch_(std::move(fetch)) {}

VideoLoader::~VideoLoader() {
    stopAll();
}

void VideoLoader::drop(Entry &entry) {
    entry.clip.reset();
    if (entry.texture)
        store_.destroy(entry.texture);
    entry.texture = 0;
}

ImageView VideoLoader::get(const std::string &id, double nowMs) {
    ImageView view;
    auto found = entries_.find(id);
    if (found == entries_.end()) {
        if (entries_.size() >= kClipsMax) {
            view.failed = true;
            return view;
        }
        Entry &entry = entries_[id];
        entry.firstAskMs = nowMs;
        entry.asked = true;
        if (!fetch_ || !fetch_(id))
            entry.state = Entry::State::Failed;
        view.failed = entry.state == Entry::State::Failed;
        return view;
    }
    Entry &entry = found->second;
    entry.asked = true;
    switch (entry.state) {
    case Entry::State::Fetching:
        return view;
    case Entry::State::Waiting:
        if (nowMs >= entry.retryAtMs) {
            entry.state = Entry::State::Fetching;
            if (!fetch_(id))
                entry.state = Entry::State::Failed;
        }
        view.failed = entry.state == Entry::State::Failed;
        return view;
    case Entry::State::Failed:
        view.failed = true;
        break;
    case Entry::State::Playing:
        if (entry.clip->failed()) {
            entry.state = Entry::State::Failed;
            view.failed = true;
            break;
        }
        if (entry.startedMs < 0) {
            if (!entry.clip->ready())
                return view;
            entry.startedMs = nowMs;
        }
        if (entry.clip->take(nowMs - entry.startedMs, picture_)) {
            if (!entry.texture || entry.width != picture_.width || entry.height != picture_.height) {
                if (entry.texture)
                    store_.destroy(entry.texture);
                entry.texture = store_.create(picture_.width, picture_.height, TextureFormat::Rgba8);
                entry.width = picture_.width;
                entry.height = picture_.height;
            }
            if (entry.texture)
                store_.update(entry.texture, 0, 0, picture_.width, picture_.height,
                              picture_.rgba.data());
        }
        view.failed = entry.clip->finished();
        view.remainingMs = remainingMs(id, nowMs);
        break;
    }
    view.texture = entry.texture;
    view.width = entry.width;
    view.height = entry.height;
    view.fade = 1;
    return view;
}

void VideoLoader::provide(const std::string &id, long status, std::string bytes, double nowMs) {
    auto found = entries_.find(id);
    if (found == entries_.end() || found->second.state != Entry::State::Fetching)
        return;
    Entry &entry = found->second;
    if (status == 202) {
        entry.state = nowMs - entry.firstAskMs > kGiveUpMs ? Entry::State::Failed
                                                            : Entry::State::Waiting;
        entry.retryAtMs = nowMs + kRetryMs;
        return;
    }
    Mp4Video video;
    std::string error;
    std::shared_ptr<const std::string> shared = std::make_shared<const std::string>(std::move(bytes));
    if (status != 200 || !parseMp4Video(*shared, video, error)) {
        entry.state = Entry::State::Failed;
        return;
    }
    entry.clip.reset(new VideoClip(shared, std::move(video)));
    entry.state = Entry::State::Playing;
}

void VideoLoader::fail(const std::string &id, double) {
    auto found = entries_.find(id);
    if (found != entries_.end() && found->second.state == Entry::State::Fetching)
        found->second.state = Entry::State::Failed;
}

bool VideoLoader::pump(bool drew, double) {
    bool busy = false;
    for (auto it = entries_.begin(); it != entries_.end();) {
        Entry &entry = it->second;
        if (drew && !entry.asked) {
            drop(entry);
            it = entries_.erase(it);
            continue;
        }
        if (drew)
            entry.asked = false;
        busy = busy || entry.state == Entry::State::Fetching || entry.state == Entry::State::Waiting ||
               (entry.state == Entry::State::Playing && !entry.clip->finished() && !entry.clip->failed());
        ++it;
    }
    return busy;
}

void VideoLoader::stopAll() {
    for (auto &pair : entries_)
        drop(pair.second);
    entries_.clear();
}

double VideoLoader::remainingMs(const std::string &id, double nowMs) const {
    auto found = entries_.find(id);
    if (found == entries_.end() || found->second.state != Entry::State::Playing ||
        found->second.startedMs < 0)
        return -1;
    const double end = found->second.clip->endMs();
    return end < 0 ? -1 : end - (nowMs - found->second.startedMs);
}
} // namespace rootedtv
