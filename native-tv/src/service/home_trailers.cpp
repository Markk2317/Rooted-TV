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
    size_t written = 0, limit = 0;
    const std::atomic<bool> *stopping = nullptr;
};
size_t writeLimited(char *data, size_t size, size_t count, void *context) {
    Target &target = *static_cast<Target *>(context);
    if (size && count > (target.limit - target.written) / size)
        count = (target.limit - target.written) / size;
    const size_t bytes = size * count;
    if (bytes && std::fwrite(data, 1, bytes, target.file) != bytes)
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
    const int descriptor =
        open(partial.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0) {
        error = "could not write the trailer";
        return false;
    }
    Target target;
    target.file = fdopen(descriptor, "wb");
    target.limit = bytes;
    target.stopping = stopping;
    CURL *curl = target.file ? curl_easy_init() : nullptr;
    if (!curl) {
        if (target.file)
            std::fclose(target.file);
        else
            close(descriptor);
        (void)unlink(partial.c_str());
        error = "Could not create a connection";
        return false;
    }
    const std::string range = "0-" + std::to_string(bytes - 1);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
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
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    const bool flushed = std::fclose(target.file) == 0;
    const bool complete = code == CURLE_OK || (code == CURLE_WRITE_ERROR && target.written == bytes);
    if (!complete || !flushed || (status != 200 && status != 206) ||
        target.written < kClipMinimumBytes || !startsAsMp4(partial)) {
        error = !complete ? curl_easy_strerror(code)
                          : status != 200 && status != 206
                                ? "the preview answered HTTP " + std::to_string(status)
                                : "the preview is not an MP4 file";
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
            return curlDownload(url, path, bytes, error, &stopping_);
        };
    (void)mkdir(directory_.c_str(), 0700);
    // A download the daemon was stopped in the middle of.
    if (DIR *directory = opendir(directory_.c_str())) {
        while (const dirent *entry = readdir(directory)) {
            const std::string name = entry->d_name;
            if (name.size() > 5 && name.compare(name.size() - 5, 5, ".part") == 0)
                (void)unlink((directory_ + "/" + name).c_str());
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
    return directory_ + "/movie-" + id + ".m4v";
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
                name.compare(name.size() - 4, 4, ".m4v") != 0 || name == "movie-" + keep + ".m4v")
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
