#include "daemon/home_key_watcher.hpp"

#include "core/json.hpp"
#include "daemon/logger.hpp"

#include <fstream>
#include <sys/stat.h>

namespace nativetv {
namespace daemon {

bool parseHomeShown(const std::string &line, std::string &foregroundId) {
    const size_t marker = line.find("LSM NL_HOME_SHOWN");
    if (marker == std::string::npos) return false;
    const std::string key = "\"foregroundId\":\"";
    const size_t start = line.find(key, marker);
    foregroundId.clear();
    if (start != std::string::npos) {
        const size_t value = start + key.size();
        const size_t end = line.find('"', value);
        if (end != std::string::npos) foregroundId = line.substr(value, end - value);
    }
    return true;
}

bool parsePowerOnLaunch(const std::string &line, std::string &appId) {
    const size_t marker = line.find("SAM NL_APP_LAUNCH_BEGIN");
    if (marker == std::string::npos ||
        line.find("\"caller_id\":\"com.webos.bootManager\"", marker) == std::string::npos)
        return false;
    const std::string key = "\"app_id\":\"";
    const size_t start = line.find(key, marker);
    if (start == std::string::npos) return false;
    const size_t value = start + key.size();
    const size_t end = line.find('"', value);
    if (end == std::string::npos) return false;
    appId = line.substr(value, end - value);
    return !appId.empty();
}

HomeKeyWatcher::HomeKeyWatcher(std::function<bool()> enabled, Opened opened, Logger *logger,
                               HomeKeyWatcherOptions options)
    : enabled_(std::move(enabled)), opened_(std::move(opened)), logger_(logger),
      options_(std::move(options)) {}

HomeKeyWatcher::~HomeKeyWatcher() { stop(); }

bool HomeKeyWatcher::start() {
    struct stat info;
    if (stat(options_.logPath.c_str(), &info) != 0) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // What the log held before the server started is history, not a press.
        offset_ = static_cast<long long>(info.st_size);
        stopping_ = false;
    }
    thread_ = std::thread([this]() { run(); });
    return true;
}

void HomeKeyWatcher::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void HomeKeyWatcher::suppressFor(std::chrono::milliseconds duration) {
    std::lock_guard<std::mutex> lock(mutex_);
    suppressedUntil_ = std::chrono::steady_clock::now() + duration;
}

Json HomeKeyWatcher::status() const {
    const unsigned seq = seq_.load();
    if (!seq) return Json();
    std::chrono::steady_clock::time_point at;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        at = lastAt_;
    }
    Json result = Json::object();
    result.set("seq", Json::numeric(seq));
    result.set("agoMs", Json::numeric(static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - at).count())));
    return result;
}

void HomeKeyWatcher::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
        lock.unlock();
        readUpdates();
        lock.lock();
        wake_.wait_for(lock, std::chrono::milliseconds(options_.pollIntervalMilliseconds),
                       [this]() { return stopping_; });
    }
}

void HomeKeyWatcher::readUpdates() {
    struct stat info;
    if (stat(options_.logPath.c_str(), &info) != 0) return;
    const long long size = static_cast<long long>(info.st_size);
    // A shorter file is a rotated one: its lines are all new.
    if (size < offset_) {
        offset_ = 0;
        partial_.clear();
    }
    if (size == offset_) return;
    std::ifstream input(options_.logPath.c_str(), std::ios::binary);
    if (!input) return;
    input.seekg(offset_);
    std::string chunk(static_cast<size_t>(size - offset_), '\0');
    input.read(&chunk[0], static_cast<std::streamsize>(chunk.size()));
    chunk.resize(static_cast<size_t>(input.gcount()));
    offset_ += static_cast<long long>(chunk.size());
    partial_ += chunk;
    size_t start = 0, end;
    while ((end = partial_.find('\n', start)) != std::string::npos) {
        std::string foregroundId;
        const std::string line = partial_.substr(start, end - start);
        if (parseHomeShown(line, foregroundId))
            handle(foregroundId, false);
        else if (parsePowerOnLaunch(line, foregroundId))
            handle(foregroundId, true);
        start = end + 1;
    }
    partial_.erase(0, start);
    // A line longer than any the log writes is not waited for.
    if (partial_.size() > 65536) partial_.clear();
}

void HomeKeyWatcher::handle(const std::string &foregroundId, bool powerOn) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (std::chrono::steady_clock::now() < suppressedUntil_) return;
    }
    const std::function<bool()> &enabled = powerOn ? powerOnEnabled_ : enabled_;
    if (!enabled || !enabled()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastAt_ = std::chrono::steady_clock::now();
    }
    seq_.fetch_add(1);
    if (logger_)
        logger_->info(powerOn ? "power on: the TV opened " + foregroundId +
                                    "; bringing Native Tv Home forward"
                              : "home key: LG's Home opened over " +
                                    (foregroundId.empty() ? std::string("unknown") : foregroundId) +
                                    "; bringing Native Tv Home forward");
    if (opened_) opened_(foregroundId);
}

} // namespace daemon
} // namespace nativetv
