#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nativetv {
// A decoded 4:2:0 picture, valid until the next decode.
struct YuvPicture {
    const uint8_t *plane[3] = {nullptr, nullptr, nullptr};
    int stride[3] = {0, 0, 0};
    int width = 0, height = 0;
    bool fullRange = false; // JPEG levels (0-255) rather than video levels (16-235)
};
// H.264 through the system's FFmpeg, opened with dlopen: webOS 3.4 ships libavcodec 55 (FFmpeg
// 2.3) with its software H.264 decoder, so the client links no codec of its own. Where there is
// none it can use (libavcodec 59 and later dropped avcodec_decode_video2), open() fails and the
// caller shows no video.
class H264Decoder {
  public:
    H264Decoder() = default;
    ~H264Decoder();
    H264Decoder(const H264Decoder &) = delete;
    H264Decoder &operator=(const H264Decoder &) = delete;
    // False, with the reason, when no usable libavcodec is found.
    bool open(std::string &error);
    // One Annex B access unit in: 1 with `picture` set when a picture comes out, 0 when none does
    // yet, -1 when the unit could not be decoded. An empty unit drains the pictures still held.
    int decode(const std::string &unit, YuvPicture &picture);

  private:
    void *context_ = nullptr, *frame_ = nullptr;
    std::vector<uint8_t> buffer_; // the unit and FFmpeg's zeroed padding after it
};
} // namespace nativetv
