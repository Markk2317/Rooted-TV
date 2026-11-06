#include "graphics/h264_decoder.hpp"
#include <cstring>
#include <dlfcn.h>
#include <mutex>

namespace nativetv {
namespace {
// The leading fields of AVPacket and AVFrame, which libavcodec 55 to 58 (FFmpeg 2.3 to 4.4) all
// keep; mirrored here so the compiler places them as FFmpeg's own build does (on 32-bit ARM the
// packet's data is at 24 and the frame's width at 68).
struct PacketHead {
    void *buf;
    int64_t pts, dts;
    uint8_t *data;
    int size;
};
struct FrameHead {
    uint8_t *data[8];
    int linesize[8];
    uint8_t **extendedData;
    int width, height, samples, format;
};
const int kPaddingBytes = 64;   // FFmpeg 2.3 reads up to 32 bytes past a packet
const int kPacketBytes = 256;   // sizeof(AVPacket) is 80 on 32-bit ARM, under 100 elsewhere
const int kPixelYuv420 = 0, kPixelYuvJ420 = 12;
const int kLogQuiet = -8;

struct Codec {
    bool loaded = false;
    std::string error;
    void (*registerAll)() = nullptr;
    unsigned (*version)() = nullptr;
    void *(*findDecoder)(const char *) = nullptr;
    void *(*allocContext)(const void *) = nullptr;
    int (*openContext)(void *, const void *, void **) = nullptr;
    int (*decodeVideo)(void *, void *, int *, const void *) = nullptr;
    void (*freeContext)(void **) = nullptr;
    void *(*allocFrame)() = nullptr;
    void (*freeFrame)(void **) = nullptr;
    void (*initPacket)(void *) = nullptr;
    void (*logLevel)(int) = nullptr;
};
template <typename Function> bool symbol(void *library, const char *name, Function &function) {
    function = reinterpret_cast<Function>(dlsym(library, name));
    return function != nullptr;
}
const Codec &codec() {
    static Codec loaded;
    static std::once_flag once;
    std::call_once(once, []() {
        static const char *const names[] = {"libavcodec.so.55", "libavcodec.so.56",
                                            "libavcodec.so.57", "libavcodec.so.58"};
        for (unsigned major = 55; major <= 58; ++major) {
            void *library = dlopen(names[major - 55], RTLD_NOW | RTLD_LOCAL);
            if (!library)
                continue;
            Codec found;
            // Symbols of libavutil come through libavcodec's own dependencies.
            if (symbol(library, "avcodec_register_all", found.registerAll) &&
                symbol(library, "avcodec_version", found.version) &&
                symbol(library, "avcodec_find_decoder_by_name", found.findDecoder) &&
                symbol(library, "avcodec_alloc_context3", found.allocContext) &&
                symbol(library, "avcodec_open2", found.openContext) &&
                symbol(library, "avcodec_decode_video2", found.decodeVideo) &&
                symbol(library, "avcodec_free_context", found.freeContext) &&
                symbol(library, "av_frame_alloc", found.allocFrame) &&
                symbol(library, "av_frame_free", found.freeFrame) &&
                symbol(library, "av_init_packet", found.initPacket) &&
                symbol(library, "av_log_set_level", found.logLevel) &&
                found.version() >> 16 == major) {
                found.logLevel(kLogQuiet);
                found.registerAll();
                found.loaded = true;
                loaded = found;
                return;
            }
            dlclose(library);
        }
        loaded.error = "No libavcodec with avcodec_decode_video2 (55 to 58)";
    });
    return loaded;
}
} // namespace

H264Decoder::~H264Decoder() {
    const Codec &library = codec();
    if (frame_)
        library.freeFrame(&frame_);
    if (context_)
        library.freeContext(&context_);
}

bool H264Decoder::open(std::string &error) {
    const Codec &library = codec();
    if (!library.loaded) {
        error = library.error;
        return false;
    }
    const void *decoder = library.findDecoder("h264");
    if (!decoder) {
        error = "libavcodec has no H.264 decoder";
        return false;
    }
    context_ = library.allocContext(decoder);
    frame_ = library.allocFrame();
    if (!context_ || !frame_ || library.openContext(context_, decoder, nullptr) < 0) {
        error = "The H.264 decoder did not open";
        return false;
    }
    return true;
}

int H264Decoder::decode(const std::string &unit, YuvPicture &picture) {
    const Codec &library = codec();
    if (!context_ || !frame_)
        return -1;
    alignas(16) uint8_t packet[kPacketBytes] = {};
    library.initPacket(packet);
    PacketHead *head = reinterpret_cast<PacketHead *>(packet);
    if (!unit.empty()) {
        buffer_.assign(unit.size() + kPaddingBytes, 0);
        std::memcpy(buffer_.data(), unit.data(), unit.size());
        head->data = buffer_.data();
        head->size = static_cast<int>(unit.size());
    } else {
        head->data = nullptr;
        head->size = 0;
    }
    int got = 0;
    if (library.decodeVideo(context_, frame_, &got, packet) < 0)
        return -1;
    if (!got)
        return 0;
    const FrameHead *frame = static_cast<const FrameHead *>(frame_);
    if ((frame->format != kPixelYuv420 && frame->format != kPixelYuvJ420) || frame->width <= 0 ||
        frame->height <= 0 || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return -1;
    for (int i = 0; i < 3; ++i) {
        picture.plane[i] = frame->data[i];
        picture.stride[i] = frame->linesize[i];
    }
    picture.width = frame->width;
    picture.height = frame->height;
    picture.fullRange = frame->format == kPixelYuvJ420;
    return 1;
}
} // namespace nativetv
