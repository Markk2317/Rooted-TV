#pragma once
#include "graphics/h264_decoder.hpp"
#include "graphics/image_loader.hpp"
#include "graphics/mp4.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rootedtv {
struct PixelRect {
    int x = 0, y = 0, w = 0, h = 0;
};
// Rows of black at the top and bottom of a picture (a film letterboxed in the frame).
void blackBars(const YuvPicture &picture, int &top, int &bottom);
// The part of a picture, bars left out, that fills a 16:9 box once its pixel aspect is applied:
// the middle, cut at the sides or at the top and bottom. Even, so chroma rows and columns line up.
PixelRect coverCrop(int width, int height, int aspectNum, int aspectDen, int top, int bottom);
// That part as RGBA (BT.601), `crop.w` texels a row.
void yuvToRgba(const YuvPicture &picture, const PixelRect &crop, uint8_t *rgba);
// The mean of the luma plane, sampled: a black or near-black picture is under 24.
int meanLuma(const YuvPicture &picture);

// One clip playing: a thread decodes it a few pictures ahead of the clock and converts each to
// RGBA; the render thread takes the newest one that is due.
class VideoClip {
  public:
    struct Picture {
        std::vector<uint8_t> rgba;
        int width = 0, height = 0;
        double atMs = 0; // from the first picture shown
    };
    VideoClip(std::shared_ptr<const std::string> bytes, Mp4Video video);
    ~VideoClip();
    VideoClip(const VideoClip &) = delete;
    VideoClip &operator=(const VideoClip &) = delete;
    // The newest picture due at `elapsedMs` since the first one, if newer than the one taken last.
    bool take(double elapsedMs, Picture &picture);
    // A picture is ready to start with.
    bool ready() const;
    // The decoder could not run, or every picture has been taken.
    bool finished() const;
    bool failed() const;
    // When the last picture stops showing, from the first; negative until the first is chosen.
    double endMs() const;

  private:
    void run();
    std::shared_ptr<const std::string> bytes_;
    Mp4Video video_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Picture> queue_;
    std::vector<std::vector<uint8_t>> spare_;
    bool stopping_ = false, done_ = false, failed_ = false;
    double endMs_ = -1;
    std::thread thread_;
};

// Trailers by id ("trailer:movie:603"), as the Pictures interface hands them out: the first ask
// fetches the clip, it plays from its first picture, and one nobody asked for during a drawn frame
// stops. A clip that ended keeps its last picture, with `failed` set so the caller fades it out.
class VideoLoader {
  public:
    // Asks the app for the clip's bytes, to arrive through provide() or fail().
    using Fetch = std::function<bool(const std::string &id)>;
    VideoLoader(TextureStore &store, Fetch fetch);
    ~VideoLoader();
    VideoLoader(const VideoLoader &) = delete;
    VideoLoader &operator=(const VideoLoader &) = delete;
    ImageView get(const std::string &id, double nowMs);
    // `status` 202: the server is still fetching the clip; asked again shortly.
    void provide(const std::string &id, long status, std::string bytes, double nowMs);
    void fail(const std::string &id, double nowMs);
    // After each loop turn: `drew` when a frame was built, so the clips it did not ask for stop.
    // True while a clip plays, so the loop keeps drawing.
    bool pump(bool drew, double nowMs);
    void stopAll();
    // How long, from now, until the clip's last picture; negative when it is not known yet.
    double remainingMs(const std::string &id, double nowMs) const;

  private:
    struct Entry {
        enum class State { Fetching, Waiting, Playing, Failed } state = State::Fetching;
        std::unique_ptr<VideoClip> clip;
        TextureId texture = 0;
        int width = 0, height = 0;
        double startedMs = -1, retryAtMs = 0, firstAskMs = 0;
        bool asked = false;
    };
    void drop(Entry &entry);
    TextureStore &store_;
    Fetch fetch_;
    std::map<std::string, Entry> entries_;
    VideoClip::Picture picture_;
};
} // namespace rootedtv
