#include "service/home_watching.hpp"
#include "service/home_titles.hpp"
#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace nativetv {
namespace service {
namespace {
const size_t kKeptMax = 20;
const size_t kRowMax = 12;
const int64_t kMinuteMs = 60LL * 1000;
const int64_t kShownAfterMs = 2 * kMinuteMs;          // less is a look, not watching
const int64_t kSessionMaxMs = 4LL * 60 * kMinuteMs;   // a TV left on in an app is not four films
const int64_t kKeptForMs = 30LL * 24 * 60 * kMinuteMs;
const double kFinished = .9;
// Without a runtime, a film is taken as two hours and an episode as three quarters of one.
const double kFilmMinutes = 120, kEpisodeMinutes = 45;

Json copyOf(const Json &value) { return value.valid() ? Json::parse(value.dump()) : Json(); }

double runtimeMs(const Json &entry) {
    const Json runtime = entry["item"]["runtime"];
    const double minutes = runtime.isNumber() && runtime.number() > 0
                               ? runtime.number()
                               : entry["item"]["type"].string() == "tv" ? kEpisodeMinutes
                                                                       : kFilmMinutes;
    return minutes * kMinuteMs;
}
} // namespace

HomeWatching::HomeWatching(std::string dataRoot)
    : path_((dataRoot.empty() ? "/var/lib/tvweb" : std::move(dataRoot)) + "/home-watching.json") {}

void HomeWatching::loadLocked() {
    if (loaded_) return;
    loaded_ = true;
    profiles_ = Json::object();
    std::ifstream input(path_.c_str(), std::ios::binary);
    if (!input) return;
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const Json parsed = Json::parse(contents);
    if (parsed.isObject()) profiles_ = parsed;
}

void HomeWatching::saveLocked() {
    const std::string temporary = path_ + ".tmp";
    const std::string contents = profiles_.dump();
    const int fd = open(temporary.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) return;
    size_t written = 0;
    bool ok = true;
    while (written < contents.size()) {
        const ssize_t count = write(fd, contents.data() + written, contents.size() - written);
        if (count <= 0) { ok = false; break; }
        written += static_cast<size_t>(count);
    }
    if (close(fd) != 0) ok = false;
    if (!ok || rename(temporary.c_str(), path_.c_str()) != 0) (void)unlink(temporary.c_str());
}

void HomeWatching::opened(const std::string &profile, const Json &item, const std::string &service,
                          const std::string &appId, int64_t nowMs) {
    Json title = HomeTitles::trimmed(item);
    const std::string key = HomeTitles::keyOf(item);
    if (!title.valid() || key.empty() || profile.empty() || appId.empty()) return;
    const Json runtime = item["runtime"];
    title.set("runtime", runtime.isNumber() && runtime.number() > 0 && runtime.number() < 2000
                             ? runtime : Json());
    std::lock_guard<std::mutex> lock(mutex_);
    loadLocked();
    // The time the title before it was in front is counted first.
    if (activeInFront_ && !activeKey_.empty()) {
        Json entries = profiles_[activeProfile_];
        Json kept = Json::array();
        for (const Json &entry : entries.items()) {
            Json copy = copyOf(entry);
            if (HomeTitles::keyOf(entry["item"]) == activeKey_)
                copy.set("watchedMs", Json::numeric(entry["watchedMs"].number() +
                                                    std::min(kSessionMaxMs, std::max<int64_t>(0, nowMs - activeSince_))));
            kept.add(copy);
        }
        profiles_.set(activeProfile_, kept);
    }
    Json previous;
    Json rest = Json::array();
    for (const Json &entry : profiles_[profile].items()) {
        if (HomeTitles::keyOf(entry["item"]) == key) previous = entry;
        else if (rest.size() < kKeptMax - 1) rest.add(copyOf(entry));
    }
    Json entry = Json::object();
    entry.set("item", title);
    entry.set("service", Json::text(service));
    entry.set("appId", Json::text(appId));
    entry.set("openedAt", Json::numeric(static_cast<double>(nowMs)));
    entry.set("watchedMs", Json::numeric(previous.valid() ? previous["watchedMs"].number() : 0));
    Json list = Json::array();
    list.add(entry);
    for (const Json &other : rest.items()) list.add(other);
    profiles_.set(profile, list);
    activeProfile_ = profile;
    activeKey_ = key;
    activeApp_ = appId;
    activeSince_ = nowMs;
    // The app is on its way to the front; it counts from now unless another app takes it.
    activeInFront_ = true;
    saveLocked();
}

void HomeWatching::seen(const std::string &profile, const Json &item, const std::string &service,
                        const std::string &appId, int64_t watchedMs, int64_t nowMs) {
    const Json title = HomeTitles::trimmed(item);
    const std::string key = HomeTitles::keyOf(item);
    if (!title.valid() || key.empty() || profile.empty() || watchedMs <= 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    loadLocked();
    if (key == activeKey_ && profile == activeProfile_) return;
    Json previous;
    Json rest = Json::array();
    for (const Json &entry : profiles_[profile].items()) {
        if (HomeTitles::keyOf(entry["item"]) == key) previous = entry;
        else if (rest.size() < kKeptMax - 1) rest.add(copyOf(entry));
    }
    Json entry = Json::object();
    entry.set("item", previous.valid() && previous["item"]["runtime"].isNumber() ? previous["item"] : title);
    entry.set("service", Json::text(service));
    entry.set("appId", Json::text(appId));
    entry.set("openedAt", Json::numeric(static_cast<double>(nowMs - watchedMs)));
    entry.set("watchedMs", Json::numeric((previous.valid() ? previous["watchedMs"].number() : 0) +
                                         static_cast<double>(std::min(kSessionMaxMs, watchedMs))));
    entry.set("seenAt", Json::numeric(static_cast<double>(nowMs)));
    Json list = Json::array();
    list.add(entry);
    for (const Json &other : rest.items()) list.add(other);
    profiles_.set(profile, list);
    saveLocked();
}

void HomeWatching::foreground(const std::string &appId, int64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (activeKey_.empty()) return;
    loadLocked();
    const bool inFront = !appId.empty() && appId == activeApp_;
    if (inFront == activeInFront_) return;
    if (inFront) {
        // Back in the app, as Home left it: counting again.
        activeSince_ = nowMs;
        activeInFront_ = true;
        return;
    }
    Json kept = Json::array();
    for (const Json &entry : profiles_[activeProfile_].items()) {
        Json copy = copyOf(entry);
        if (HomeTitles::keyOf(entry["item"]) == activeKey_) {
            copy.set("watchedMs",
                     Json::numeric(entry["watchedMs"].number() +
                                   std::min(kSessionMaxMs, std::max<int64_t>(0, nowMs - activeSince_))));
            copy.set("seenAt", Json::numeric(static_cast<double>(nowMs)));
        }
        kept.add(copy);
    }
    profiles_.set(activeProfile_, kept);
    activeInFront_ = false;
    // Another app, or the screen off: the title stays the one Home opened last until it opens
    // another, so coming back to its app goes on counting.
    saveLocked();
}

Json HomeWatching::watched(const std::string &profile, size_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    loadLocked();
    Json items = Json::array();
    for (const Json &entry : profiles_[profile].items())
        if (items.size() < limit && entry["watchedMs"].number() >= 2 * kMinuteMs)
            items.add(entry["item"]);
    return items;
}

Json HomeWatching::row(const std::string &profile, int64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    loadLocked();
    Json items = Json::array();
    for (const Json &entry : profiles_[profile].items()) {
        if (items.size() == kRowMax) break;
        double watched = entry["watchedMs"].number();
        const std::string key = HomeTitles::keyOf(entry["item"]);
        if (activeInFront_ && profile == activeProfile_ && key == activeKey_)
            watched += static_cast<double>(std::min(kSessionMaxMs, std::max<int64_t>(0, nowMs - activeSince_)));
        const double progress = watched / runtimeMs(entry);
        if (watched < kShownAfterMs || progress >= kFinished ||
            nowMs - static_cast<int64_t>(entry["openedAt"].number()) > kKeptForMs)
            continue;
        Json item = copyOf(entry["item"]);
        item.set("service", entry["service"]);
        item.set("progress", Json::numeric(std::round(progress * 1000) / 1000));
        items.add(item);
    }
    if (!items.size()) return Json();
    Json result = Json::object();
    result.set("key", Json::text("continue"));
    result.set("items", items);
    return result;
}

} // namespace service
} // namespace nativetv
