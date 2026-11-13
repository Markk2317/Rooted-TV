#include "daemon/home_key_watcher.hpp"

#include "core/json.hpp"
#include "daemon/logger.hpp"

#include <cstdlib>
#include <fstream>
#include <sys/stat.h>

namespace rootedtv {
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

namespace {
// How long after a switch-on a device's taking the screen is undone (deviceLaunched).
const std::chrono::milliseconds kDeviceTakeover(10000);

// The app a SAM NL_APP_LAUNCH_BEGIN line names, when `caller` launched it.
bool parseLaunchBy(const std::string &line, const std::string &caller, std::string &appId) {
    const size_t marker = line.find("SAM NL_APP_LAUNCH_BEGIN");
    if (marker == std::string::npos ||
        line.find("\"caller_id\":\"" + caller + "\"", marker) == std::string::npos)
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
} // namespace

bool parsePowerOnLaunch(const std::string &line, std::string &appId) {
    return parseLaunchBy(line, "com.webos.bootManager", appId);
}

bool parseDeviceLaunch(const std::string &line, std::string &appId) {
    return parseLaunchBy(line, "com.webos.service.tv.externaldevice", appId);
}

double lastPowerOnUptime(const std::string &logPath) {
    std::ifstream input(logPath.c_str(), std::ios::binary);
    if (!input) return -1;
    double found = -1;
    std::string line, appId;
    while (std::getline(input, line)) {
        if (!parsePowerOnLaunch(line, appId)) continue;
        const size_t open = line.find(" [");
        if (open == std::string::npos) continue;
        char *end = nullptr;
        const double seconds = std::strtod(line.c_str() + open + 2, &end);
        if (end && *end == ']') found = seconds;
    }
    return found;
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
        else if (parseDeviceLaunch(line, foregroundId))
            deviceLaunched(foregroundId);
        start = end + 1;
    }
    partial_.erase(0, start);
    // A line longer than any the log writes is not waited for.
    if (partial_.size() > 65536) partial_.clear();
}

void HomeKeyWatcher::handle(const std::string &foregroundId, bool powerOn) {
    if (powerOn && poweredOn_) poweredOn_();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (std::chrono::steady_clock::now() < suppressedUntil_) return;
    }
    const std::function<bool()> &enabled = powerOn ? powerOnEnabled_ : enabled_;
    if (!enabled || !enabled()) return;
    if (powerOn && byDevice_ && byDevice_()) {
        if (logger_)
            logger_->info("power on: a device switched the TV on over HDMI-CEC; leaving " +
                          foregroundId + " in front");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastAt_ = std::chrono::steady_clock::now();
        // The device on the input, woken with the TV, may take the screen back (see deviceLaunched).
        deviceTakeoverUntil_ =
            powerOn ? lastAt_ + kDeviceTakeover : std::chrono::steady_clock::time_point();
    }
    seq_.fetch_add(1);
    if (logger_)
        logger_->info(powerOn ? "power on: the TV opened " + foregroundId +
                                    "; bringing Rooted TV Home forward"
                              : "home key: LG's Home opened over " +
                                    (foregroundId.empty() ? std::string("unknown") : foregroundId) +
                                    "; bringing Rooted TV Home forward");
    if (opened_) opened_(foregroundId);
}

// A Chromecast on HDMI 3 woken over CEC with the TV (an OLED55C6V on webOS 3.4.3) announces itself
// as the active source and tv.externaldevice relaunches its input 3.5 s after Rooted TV Home came
// forward. Once in the seconds after a switch-on, Rooted TV Home is brought back over it.
void HomeKeyWatcher::deviceLaunched(const std::string &appId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        if (now >= deviceTakeoverUntil_) return;
        deviceTakeoverUntil_ = std::chrono::steady_clock::time_point();
        lastAt_ = now;
    }
    seq_.fetch_add(1);
    if (logger_)
        logger_->info("power on: the device on " + appId +
                      " took the screen as it woke; bringing Rooted TV Home forward again");
    if (opened_) opened_(appId);
}

} // namespace daemon
} // namespace rootedtv
