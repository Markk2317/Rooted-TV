#include "service/home_trailers.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <utility>
#include <vector>
#include <cstdlib>
#include <curl/curl.h>

namespace nativetv {
namespace service {
namespace {
// A film with no preview is asked about again after six hours, a failed download after ten
// minutes.
const int64_t kMissingRetryMs = 6LL * 60 * 60 * 1000;
const int64_t kFailedRetryMs = 10LL * 60 * 1000;
const size_t kQueueMax = 12;
// Less than this is not worth playing (a few seconds).
const size_t kClipMinimumBytes = 512u * 1024;
// Apple TV's 894 x 372 variant (H.264 High, about 1.6 Mb/s), 1.4 times the iTunes preview's
// width: the TV app's decoder (one C6 core, see graphics/h264_decoder.cpp) plays it at 30
// pictures a second where the 1004 px one manages 22 and the 1190 px one 18, under the 24 needed.
const int kHlsMaxWidth = 900;

bool validId(const std::string &id) {
    return !id.empty() && id.size() <= 10 && id[0] != '0' &&
           id.find_first_not_of("0123456789") == std::string::npos;
}

bool initializeCurl() {
    static std::once_flag once;
    static CURLcode result = CURLE_FAILED_INIT;
    std::call_once(once, [] { result = curl_global_init(CURL_GLOBAL_DEFAULT); });
    return result == CURLE_OK;
}

struct Target {
    FILE *file = nullptr;
    std::string *text = nullptr; // where there is no file
    size_t written = 0, limit = 0;
    const std::atomic<bool> *stopping = nullptr;
};
size_t writeLimited(char *data, size_t size, size_t count, void *context) {
    Target &target = *static_cast<Target *>(context);
    if (size && count > (target.limit - target.written) / size)
        count = (target.limit - target.written) / size;
    const size_t bytes = size * count;
    if (target.text)
        target.text->append(data, bytes);
    else if (bytes && std::fwrite(data, 1, bytes, target.file) != bytes)
        return 0;
    target.written += bytes;
    // Returning less than offered stops the transfer at the limit, which a server that ignores the
    // range needs.
    return bytes;
}
int abortWhenStopping(void *context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const Target &target = *static_cast<const Target *>(context);
    return target.stopping && *target.stopping ? 1 : 0;
}
bool startsAsMp4(const std::string &path) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file)
        return false;
    char head[8] = {};
    const bool read = std::fread(head, 1, sizeof(head), file) == sizeof(head);
    std::fclose(file);
    return read && std::string(head + 4, 4) == "ftyp";
}
// One GET into `target`, from the start up to `range` ("0-N"; none when empty), over https only.
CURLcode transfer(const std::string &url, Target &target, const std::string &range, long &status) {
    status = 0;
    CURL *curl = curl_easy_init();
    if (!curl)
        return CURLE_FAILED_INIT;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if (!range.empty())
        curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 120000L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 8192L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#pragma GCC diagnostic pop
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "tvweb");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeLimited);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &target);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, abortWhenStopping);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &target);
    const CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    return code;
}
// The file the clip is written into while it downloads, renamed into place once it is whole.
FILE *openPartial(const std::string &partial) {
    const int descriptor =
        open(partial.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0)
        return nullptr;
    FILE *file = fdopen(descriptor, "wb");
    if (!file)
        close(descriptor);
    return file;
}
bool keepClip(const std::string &partial, const std::string &path, size_t written, std::string &error) {
    if (written < kClipMinimumBytes || !startsAsMp4(partial)) {
        error = "the preview is not an MP4 file";
        (void)unlink(partial.c_str());
        return false;
    }
    if (rename(partial.c_str(), path.c_str()) != 0) {
        (void)unlink(partial.c_str());
        error = "could not keep the trailer";
        return false;
    }
    return true;
}
bool curlDownload(const std::string &url, const std::string &path, size_t bytes,
                  std::string &error, const std::atomic<bool> *stopping) {
    if (url.compare(0, 8, "https://") != 0) {
        error = "not an https URL";
        return false;
    }
    if (!initializeCurl()) {
        error = "HTTP initialization failed";
        return false;
    }
    const std::string partial = path + ".part";
    Target target;
    target.file = openPartial(partial);
    target.limit = bytes;
    target.stopping = stopping;
    if (!target.file) {
        error = "could not write the trailer";
        return false;
    }
    long status = 0;
    const CURLcode code = transfer(url, target, "0-" + std::to_string(bytes - 1), status);
    const bool flushed = std::fclose(target.file) == 0;
    const bool complete = code == CURLE_OK || (code == CURLE_WRITE_ERROR && target.written == bytes);
    if (!complete || !flushed || (status != 200 && status != 206)) {
        error = !complete ? curl_easy_strerror(code)
                          : !flushed ? "could not write the trailer"
                                     : "the preview answered HTTP " + std::to_string(status);
        (void)unlink(partial.c_str());
        return false;
    }
    return keepClip(partial, path, target.written, error);
}
// A playlist's text, at most 1 MB.
bool fetchPlaylist(const std::string &url, std::string &text, std::string &error,
                   const std::atomic<bool> *stopping) {
    text.clear();
    Target target;
    target.text = &text;
    target.limit = 1u << 20;
    target.stopping = stopping;
    long status = 0;
    const CURLcode code = transfer(url, target, "", status);
    if (code != CURLE_OK || status != 200) {
        error = code != CURLE_OK ? curl_easy_strerror(code) : "the playlist answered HTTP " + std::to_string(status);
        return false;
    }
    return true;
}
// Apple TV's trailers are HLS: the widest H.264 variant up to kHlsMaxWidth, its init segment and
// then its segments, one after another into the file, up to `bytes`: a fragmented MP4 whose last
// segment may be cut short, which the TV app's reader plays up to the last whole sample.
bool hlsDownload(const std::string &url, const std::string &path, size_t bytes,
                 std::string &error, const std::atomic<bool> *stopping) {
    if (!initializeCurl()) {
        error = "HTTP initialization failed";
        return false;
    }
    std::string master, media;
    if (!fetchPlaylist(url, master, error, stopping))
        return false;
    const std::string variant = hlsVariant(master, url, kHlsMaxWidth);
    if (variant.empty()) {
        error = "the trailer has no H.264 picture to play";
        return false;
    }
    if (!fetchPlaylist(variant, media, error, stopping))
        return false;
    const std::vector<std::string> parts = hlsSegments(media, variant);
    if (parts.size() < 2) {
        error = "the trailer's playlist lists no segments";
        return false;
    }
    const std::string partial = path + ".part";
    Target target;
    target.file = openPartial(partial);
    target.limit = bytes;
    target.stopping = stopping;
    if (!target.file) {
        error = "could not write the trailer";
        return false;
    }
    for (size_t index = 0; index < parts.size() && target.written < bytes; ++index) {
        long status = 0;
        const CURLcode code = transfer(parts[index], target, "", status);
        if ((code != CURLE_OK && !(code == CURLE_WRITE_ERROR && target.written == bytes)) || status != 200) {
            // Whole segments before a failed one still play.
            if (index >= 2)
                break;
            error = code != CURLE_OK ? curl_easy_strerror(code)
                                     : "the trailer answered HTTP " + std::to_string(status);
            std::fclose(target.file);
            (void)unlink(partial.c_str());
            return false;
        }
    }
    if (std::fclose(target.file) != 0) {
        error = "could not write the trailer";
        (void)unlink(partial.c_str());
        return false;
    }
    return keepClip(partial, path, target.written, error);
}
// An https address on one of Apple's hosts, as a playlist may name.
bool appleAddress(const std::string &url) {
    const std::string scheme = "https://";
    if (url.compare(0, scheme.size(), scheme) != 0 || url.size() > 1024 ||
        url.find_first_of(" \"\\<>\r\n") != std::string::npos)
        return false;
    const size_t slash = url.find('/', scheme.size());
    const std::string host = url.substr(scheme.size(), slash == std::string::npos ? std::string::npos
                                                                                 : slash - scheme.size());
    const std::string domain = ".apple.com";
    return host.size() > domain.size() &&
           host.compare(host.size() - domain.size(), domain.size(), domain) == 0 &&
           host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-") == std::string::npos;
}
// `uri` as the playlist at `base` names it: absolute, or beside the playlist.
std::string resolved(const std::string &base, const std::string &uri) {
    if (uri.compare(0, 8, "https://") == 0)
        return uri;
    if (uri.empty() || uri[0] == '/' || uri.find("://") != std::string::npos)
        return "";
    const size_t query = base.find('?');
    const size_t slash = base.rfind('/', query == std::string::npos ? std::string::npos : query);
    return slash == std::string::npos ? "" : base.substr(0, slash + 1) + uri;
}
std::string attribute(const std::string &line, const std::string &name) {
    size_t at = line.find(name + "=");
    while (at != std::string::npos && at && line[at - 1] != ':' && line[at - 1] != ',')
        at = line.find(name + "=", at + 1);
    if (at == std::string::npos)
        return "";
    at += name.size() + 1;
    if (at < line.size() && line[at] == '"') {
        const size_t end = line.find('"', at + 1);
        return end == std::string::npos ? "" : line.substr(at + 1, end - at - 1);
    }
    const size_t end = line.find(',', at);
    return line.substr(at, end == std::string::npos ? std::string::npos : end - at);
}
std::vector<std::string> lines(const std::string &text) {
    std::vector<std::string> result;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        result.push_back(line);
        at = end + 1;
    }
    return result;
}
} // namespace

std::string hlsVariant(const std::string &master, const std::string &base, int maxWidth) {
    const std::vector<std::string> all = lines(master);
    std::string best;
    int bestWidth = 0;
    for (size_t i = 0; i + 1 < all.size(); ++i) {
        if (all[i].compare(0, 18, "#EXT-X-STREAM-INF:") != 0)
            continue;
        const std::string codecs = attribute(all[i], "CODECS"), resolution = attribute(all[i], "RESOLUTION"),
                          range = attribute(all[i], "VIDEO-RANGE");
        const int width = std::atoi(resolution.c_str());
        if (codecs.find("avc1.") == std::string::npos || (!range.empty() && range != "SDR") ||
            width <= bestWidth || width > maxWidth)
            continue;
        const std::string url = resolved(base, all[i + 1]);
        if (!appleAddress(url))
            continue;
        best = url;
        bestWidth = width;
    }
    return best;
}

std::vector<std::string> hlsSegments(const std::string &media, const std::string &base) {
    std::vector<std::string> parts;
    std::string init;
    for (const std::string &line : lines(media)) {
        if (line.compare(0, 11, "#EXT-X-MAP:") == 0) {
            if (!init.empty() || !parts.empty())
                return {}; // one init segment, before the media
            init = resolved(base, attribute(line, "URI"));
            if (!appleAddress(init))
                return {};
            parts.push_back(init);
        } else if (!line.empty() && line[0] != '#') {
            const std::string url = resolved(base, line);
            if (init.empty() || !appleAddress(url))
                return {};
            parts.push_back(url);
        }
    }
    return parts;
}
namespace {
bool fetchClip(const std::string &url, const std::string &path, size_t bytes, std::string &error,
               const std::atomic<bool> *stopping) {
    return url.find(".m3u8") != std::string::npos ? hlsDownload(url, path, bytes, error, stopping)
                                                  : curlDownload(url, path, bytes, error, stopping);
}
} // namespace

HomeTrailers::HomeTrailers(std::string dataRoot, Resolve resolve, Download download, Clock clock)
    : directory_(std::move(dataRoot) + "/home-trailers"), resolve_(std::move(resolve)),
      download_(std::move(download)), clock_(std::move(clock)) {
    if (!clock_)
        clock_ = []() {
            return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count());
        };
    if (!download_)
        download_ = [this](const std::string &url, const std::string &path, size_t bytes,
                           std::string &error) {
            return fetchClip(url, path, bytes, error, &stopping_);
        };
    (void)mkdir(directory_.c_str(), 0700);
    // A download the daemon was stopped in the middle of, the iTunes previews kept before the
    // trailers came from Apple TV (movie-<id>.m4v), so a film's is fetched again in HD, and a clip
    // larger than read() hands out.
    if (DIR *directory = opendir(directory_.c_str())) {
        while (const dirent *entry = readdir(directory)) {
            const std::string name = entry->d_name, file = directory_ + "/" + name;
            struct stat info = {};
            if ((name.size() > 5 && name.compare(name.size() - 5, 5, ".part") == 0) ||
                (name.size() > 4 && name.compare(name.size() - 4, 4, ".m4v") == 0) ||
                (name.compare(0, 6, "movie-") == 0 && stat(file.c_str(), &info) == 0 &&
                 static_cast<unsigned long long>(info.st_size) > kClipBytes))
                (void)unlink(file.c_str());
        }
        closedir(directory);
    }
    worker_ = std::thread(&HomeTrailers::work, this);
}

HomeTrailers::~HomeTrailers() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    changed_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

std::string HomeTrailers::pathFor(const std::string &id) const {
    return directory_ + "/movie-" + id + ".mp4";
}

HomeTrailers::State HomeTrailers::request(const std::string &id, std::string &path) {
    path.clear();
    if (!validId(id))
        return State::None;
    const std::string file = pathFor(id);
    struct stat info = {};
    if (stat(file.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0) {
        // The time a clip was last played decides which one goes first.
        (void)utime(file.c_str(), nullptr);
        path = file;
        return State::Ready;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto missing = missing_.find(id);
    if (missing != missing_.end()) {
        if (clock_() - missing->second < kMissingRetryMs)
            return State::None;
        missing_.erase(missing);
    }
    if (working_ == id)
        return State::Pending;
    // The banner is waiting for this one: it goes before the prewarmed ones.
    queue_.erase(std::remove(queue_.begin(), queue_.end(), id), queue_.end());
    queue_.push_front(id);
    while (queue_.size() > kQueueMax)
        queue_.pop_back();
    changed_.notify_all();
    return State::Pending;
}

bool HomeTrailers::read(const std::string &path, std::string &bytes) {
    bytes.clear();
    const int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0)
        return false;
    char buffer[65536];
    for (;;) {
        const ssize_t count = ::read(descriptor, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0 || bytes.size() + static_cast<size_t>(count) > kClipBytes) {
            close(descriptor);
            return count == 0 && !bytes.empty();
        }
        bytes.append(buffer, static_cast<size_t>(count));
    }
}

bool HomeTrailers::enqueueLocked(const std::string &id) {
    if (!validId(id) || working_ == id || queue_.size() >= kQueueMax ||
        std::find(queue_.begin(), queue_.end(), id) != queue_.end())
        return false;
    const auto missing = missing_.find(id);
    if (missing != missing_.end() && clock_() - missing->second < kMissingRetryMs)
        return false;
    struct stat info = {};
    if (stat(pathFor(id).c_str(), &info) == 0)
        return false;
    queue_.push_back(id);
    return true;
}

void HomeTrailers::prewarm(const std::vector<std::string> &ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool queued = false;
    for (const std::string &id : ids)
        queued = enqueueLocked(id) || queued;
    if (queued)
        changed_.notify_all();
}

void HomeTrailers::settle() {
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait(lock, [this] { return stopping_ || (queue_.empty() && working_.empty()); });
}

void HomeTrailers::evict(const std::string &keep) {
    std::vector<std::pair<time_t, std::string>> files;
    if (DIR *directory = opendir(directory_.c_str())) {
        while (const dirent *entry = readdir(directory)) {
            const std::string name = entry->d_name;
            if (name.size() <= 10 || name.compare(0, 6, "movie-") != 0 ||
                name.compare(name.size() - 4, 4, ".mp4") != 0 || name == "movie-" + keep + ".mp4")
                continue;
            struct stat info = {};
            if (stat((directory_ + "/" + name).c_str(), &info) == 0 && S_ISREG(info.st_mode))
                files.push_back({info.st_mtime, name});
        }
        closedir(directory);
    }
    std::sort(files.begin(), files.end());
    for (size_t index = 0; index + kFilesMax <= files.size(); ++index)
        (void)unlink((directory_ + "/" + files[index].second).c_str());
}

void HomeTrailers::work() {
    for (;;) {
        std::string id;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            changed_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_)
                return;
            id = queue_.front();
            queue_.pop_front();
            working_ = id;
        }
        std::string url, error;
        try {
            url = resolve_ ? resolve_(id) : std::string();
        } catch (...) {
            url.clear();
        }
        bool saved = false;
        if (!url.empty()) {
            saved = download_(url, pathFor(id), kClipBytes, error);
            if (saved)
                evict(id);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        working_.clear();
        if (!saved)
            missing_[id] = url.empty() ? clock_() : clock_() - (kMissingRetryMs - kFailedRetryMs);
        changed_.notify_all();
    }
}

} // namespace service
} // namespace nativetv
