#include "graphics/video_player.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace nativetv;

namespace {
std::string be16(uint32_t value) {
    return {static_cast<char>(value >> 8), static_cast<char>(value)};
}
std::string be32(uint32_t value) {
    return be16(value >> 16) + be16(value & 0xffff);
}
std::string box(const std::string &type, const std::string &payload) {
    return be32(static_cast<uint32_t>(8 + payload.size())) + type + payload;
}
std::string full(const std::string &type, const std::string &payload) {
    return box(type, std::string(4, '\0') + payload);
}
const std::string kSps("\x67\x42\xc0\x1e", 4), kPps("\x68\xce", 2);
const std::string kKey("\x65\x88\x84", 3), kOther("\x41\x9a", 2);

// An MP4 the way Apple's previews are laid out (ftyp, moov, then mdat): four 24000/1001 fps
// samples, two to a chunk, key frames 1 and 3, 640x458 at a 4:3 pixel aspect.
std::string movie() {
    std::vector<std::string> samples = {be32(3) + kKey, be32(2) + kOther, be32(3) + kKey,
                                        be32(2) + kOther};
    std::string media;
    for (const std::string &sample : samples)
        media += sample;
    auto index = [&](uint32_t firstOffset) {
        std::string entry = std::string(6, '\0') + be16(1) + std::string(16, '\0') + be16(640) +
                            be16(458) + std::string(50, '\0');
        const std::string avcc = std::string("\x01\x42\xc0\x1e\xff\xe1", 6) + be16(4) + kSps +
                                 std::string("\x01", 1) + be16(2) + kPps;
        entry += box("avcC", avcc) + box("pasp", be32(4) + be32(3));
        std::string sizes = be32(0) + be32(4);
        for (const std::string &sample : samples)
            sizes += be32(static_cast<uint32_t>(sample.size()));
        const uint32_t second = firstOffset + static_cast<uint32_t>(samples[0].size() + samples[1].size());
        const std::string stbl =
            box("stbl", full("stsd", be32(1) + box("avc1", entry)) +
                            full("stts", be32(1) + be32(4) + be32(1001)) +
                            full("stss", be32(2) + be32(1) + be32(3)) + full("stsz", sizes) +
                            full("stsc", be32(1) + be32(1) + be32(2) + be32(1)) +
                            full("stco", be32(2) + be32(firstOffset) + be32(second)));
        const std::string mdia =
            box("mdia", full("mdhd", be32(0) + be32(0) + be32(24000) + be32(4004) + be32(0)) +
                            full("hdlr", be32(0) + "vide" + std::string(13, '\0')) +
                            box("minf", stbl));
        // An audio track first, as Apple's previews have, which the reader passes over.
        const std::string sound =
            box("trak", box("mdia", full("mdhd", be32(0) + be32(0) + be32(44100) + be32(0) + be32(0)) +
                                        full("hdlr", be32(0) + "soun" + std::string(13, '\0'))));
        return box("moov", sound + box("trak", mdia));
    };
    const std::string ftyp = box("ftyp", "M4V " + be32(0) + "M4V mp42isom");
    const uint32_t mediaAt = static_cast<uint32_t>(ftyp.size() + index(0).size() + 8);
    return ftyp + index(mediaAt) + box("mdat", media);
}

class Store final : public TextureStore {
  public:
    TextureId create(int, int, TextureFormat, bool) override {
        return ++created;
    }
    void update(TextureId, int, int, int, int, const void *) override {}
    void destroy(TextureId) override {
        ++destroyed;
    }
    size_t uploadBudget() const override {
        return 1u << 20;
    }
    unsigned created = 0, destroyed = 0;
};

struct Planes {
    std::vector<uint8_t> y, u, v;
    YuvPicture picture(int width, int height) {
        YuvPicture result;
        result.plane[0] = y.data();
        result.plane[1] = u.data();
        result.plane[2] = v.data();
        result.stride[0] = width;
        result.stride[1] = result.stride[2] = width / 2;
        result.width = width;
        result.height = height;
        return result;
    }
};
} // namespace

int main() {
    // The track: samples, times, key frames, the pixel aspect and the parameter sets.
    const std::string bytes = movie();
    Mp4Video video;
    std::string error;
    assert(parseMp4Video(bytes, video, error));
    assert(video.width == 640 && video.height == 458 && video.aspectNum == 4 && video.aspectDen == 3);
    assert(video.nalLength == 4 && video.samples.size() == 4);
    assert(video.samples[0].key && !video.samples[1].key && video.samples[2].key);
    assert(video.samples[1].offset == video.samples[0].offset + 7);
    assert(video.samples[2].offset == video.samples[1].offset + 6);
    assert(video.samples[3].atMs > 125.1 && video.samples[3].atMs < 125.2);
    assert(video.durationMs > 166.8 && video.durationMs < 166.9);
    const std::string sets = std::string("\0\0\0\1", 4) + kSps + std::string("\0\0\0\1", 4) + kPps;
    assert(video.parameterSets == sets);
    std::string unit;
    assert(annexBSample(bytes, video, 0, unit) && unit == sets + std::string("\0\0\0\1", 4) + kKey);
    assert(annexBSample(bytes, video, 1, unit) && unit == std::string("\0\0\0\1", 4) + kOther);
    assert(!annexBSample(bytes, video, 4, unit));

    // The first megabytes of a longer file: the clip ends at the first sample they do not hold.
    Mp4Video cut;
    assert(parseMp4Video(bytes.substr(0, bytes.size() - 3), cut, error) && cut.samples.size() == 3);
    assert(cut.durationMs > 125.1 && cut.durationMs < 125.2);
    // A NAL length that runs past its sample.
    std::string damaged = bytes;
    damaged[static_cast<size_t>(video.samples[1].offset) + 3] = 9;
    assert(!annexBSample(damaged, video, 1, unit));
    assert(!parseMp4Video("not a movie", cut, error) && !error.empty());
    assert(!parseMp4Video(bytes.substr(0, 200), cut, error));

    // The part that fills 16:9: Apple's 640x458 at 4:3 loses its sides, a 4:3 picture its top and
    // bottom, a letterboxed one its bars first.
    PixelRect crop = coverCrop(640, 458, 4, 3, 0, 0);
    assert(crop.x == 14 && crop.y == 0 && crop.w == 610 && crop.h == 458);
    crop = coverCrop(1920, 1080, 1, 1, 0, 0);
    assert(crop.x == 0 && crop.y == 0 && crop.w == 1920 && crop.h == 1080);
    crop = coverCrop(640, 480, 1, 1, 0, 0);
    assert(crop.x == 0 && crop.y == 60 && crop.w == 640 && crop.h == 360);
    crop = coverCrop(640, 480, 1, 1, 60, 60);
    assert(crop.x == 0 && crop.y == 60 && crop.w == 640 && crop.h == 360);
    crop = coverCrop(640, 480, 1, 1, 200, 200); // bars that leave too little are not bars
    assert(crop.y == 60 && crop.h == 360);

    // BT.601 at video levels: black, white, grey and red.
    Planes planes;
    planes.y = {16, 235, 126, 81};
    planes.u = {128, 90};
    planes.v = {128, 240};
    YuvPicture picture = planes.picture(4, 1);
    std::vector<uint8_t> rgba(16);
    yuvToRgba(picture, {0, 0, 4, 1}, rgba.data());
    assert(rgba[0] == 0 && rgba[1] == 0 && rgba[2] == 0 && rgba[3] == 255);
    assert(rgba[4] == 255 && rgba[5] == 255 && rgba[6] == 255);
    assert(rgba[12] == 255 && rgba[13] == 0 && rgba[14] == 0 && rgba[15] == 255);
    planes.y[2] = 126;
    planes.u[1] = planes.v[1] = 128;
    yuvToRgba(planes.picture(4, 1), {0, 0, 4, 1}, rgba.data());
    assert(rgba[8] == 128 && rgba[9] == 128 && rgba[10] == 128);

    // Letterbox bars and how dark a picture is.
    Planes boxed;
    boxed.y.assign(8 * 12, 120);
    for (int x = 0; x < 8; ++x)
        boxed.y[x] = boxed.y[8 + x] = boxed.y[80 + x] = boxed.y[88 + x] = 16;
    boxed.u.assign(4 * 6, 128);
    boxed.v.assign(4 * 6, 128);
    int top = 0, bottom = 0;
    blackBars(boxed.picture(8, 12), top, bottom);
    assert(top == 2 && bottom == 2);
    assert(meanLuma(boxed.picture(8, 12)) > 24);

    // The loader: a clip is fetched on the first ask, asked again while the server prepares it,
    // dropped when a drawn frame did not ask for it, and given up when it cannot play.
    Store store;
    std::vector<std::string> fetched;
    VideoLoader loader(store, [&fetched](const std::string &id) {
        fetched.push_back(id);
        return true;
    });
    const std::string id = "trailer:movie:603";
    ImageView view = loader.get(id, 0);
    assert(!view.texture && !view.failed && fetched.size() == 1);
    assert(loader.pump(true, 0));
    loader.provide(id, 202, "", 100);
    assert(!loader.get(id, 200).failed && fetched.size() == 1);
    assert(!loader.get(id, 1700).failed && fetched.size() == 2);
    loader.fail(id, 1800);
    assert(loader.get(id, 1900).failed);
    assert(!loader.pump(true, 1900));
    // Not asked during a drawn frame: dropped, and fetched anew when asked again.
    assert(!loader.pump(true, 2000));
    assert(!loader.get(id, 2100).failed && fetched.size() == 3);
    loader.provide(id, 200, "not a movie", 2200);
    assert(loader.get(id, 2300).failed);
    assert(!loader.get("trailer:movie:1", 2300).failed);
    assert(loader.get("trailer:movie:2", 2300).failed); // two clips at most
    loader.stopAll();
    // A movie this host cannot decode (no libavcodec 55 to 58 here, or samples that are not real
    // pictures) ends as failed, with nothing drawn.
    assert(!loader.get(id, 3000).failed);
    loader.provide(id, 200, bytes, 3000);
    bool failed = false;
    for (int wait = 0; wait < 400 && !failed; ++wait) {
        view = loader.get(id, 3000 + wait * 10);
        failed = view.failed;
        if (!failed)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(failed && !view.texture && store.created == 0);
    std::printf("Video passed.\n");
    return 0;
}
