#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nativetv {
namespace service {

// Trailers for the details pages of Native Tv Home: the start of a film's trailer (about 30 s of
// Apple TV's HLS one, or of the iTunes Store's preview) is kept on the TV under
// home-trailers/, so the page plays it from the TV. Finding the trailer (Wikidata, then Apple TV's
// page or the iTunes lookup) is the caller's; this keeps the files, fetches them one at a time in
// the background and drops the least recently played.
// HLS, as Apple TV serves its trailers. The variant whose H.264 SDR picture is the widest up to
// `maxWidth`, its address resolved against the master playlist's at `base`; "" when there is none
// on an https apple.com address.
std::string hlsVariant(const std::string &master, const std::string &base, int maxWidth);
// A media playlist's init segment (EXT-X-MAP) and then its segments, resolved against `base`;
// empty unless there is one init segment first and every address is https on apple.com.
std::vector<std::string> hlsSegments(const std::string &media, const std::string &base);

class HomeTrailers {
  public:
    // The preview's https URL for a film's TMDB id; "" when it has none.
    using Resolve = std::function<std::string(const std::string &id)>;
    // Saves the first `bytes` of `url` at `path`; false, with the reason, when it could not.
    using Download = std::function<bool(const std::string &url, const std::string &path,
                                        size_t bytes, std::string &error)>;
    using Clock = std::function<int64_t()>;
    enum class State { Ready, Pending, None };

    // Under the 8 MB the TV app takes in one answer (adapters/http_services.cpp): about 33 s of
    // Apple TV's 894 px trailers.
    static const size_t kClipBytes = 7u << 20;
    static const size_t kFilesMax = 6;

    HomeTrailers(std::string dataRoot, Resolve resolve, Download download = Download(),
                 Clock clock = Clock());
    ~HomeTrailers();
    HomeTrailers(const HomeTrailers &) = delete;
    HomeTrailers &operator=(const HomeTrailers &) = delete;

    // A film's trailer: Ready with the file's path, Pending while it is being fetched (the first
    // call starts that), or None when the film has none or the id is not one.
    State request(const std::string &id, std::string &path);
    // A clip's bytes, at most kClipBytes; false when the file is gone.
    static bool read(const std::string &path, std::string &bytes);
    // Starts fetching these films' trailers, the first ones first, without waiting.
    void prewarm(const std::vector<std::string> &ids);
    // Waits until nothing is queued or being fetched (tests).
    void settle();

  private:
    void work();
    bool enqueueLocked(const std::string &id);
    void evict(const std::string &keep);
    std::string pathFor(const std::string &id) const;

    std::string directory_;
    Resolve resolve_;
    Download download_;
    Clock clock_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::string> queue_;
    std::string working_;
    std::map<std::string, int64_t> missing_; // id -> when it was found to have none
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};

} // namespace service
} // namespace nativetv
