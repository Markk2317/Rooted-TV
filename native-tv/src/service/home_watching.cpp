#include "service/home_watching.hpp"
#include "service/home_titles.hpp"
#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace nativetv {
namespace service {
namespace {
const size_t kKeptMax = 20;
const size_t kPlaysKeptMax = 40;
const size_t kRowMax = 12;
const int64_t kMinuteMs = 60LL * 1000;
const int64_t kSessionMaxMs = 4LL * 60 * kMinuteMs;   // a TV left on in an app is not four films
const int64_t kKeptForMs = 30LL * 24 * 60 * kMinuteMs;
const int64_t kPlayShownForMs = 7LL * 24 * 60 * kMinuteMs;
// Back in the app of the title Home opened after this long, something else is being watched.
const int64_t kResumedWithinMs = 30 * kMinuteMs;
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

Json loadDocument(const std::string &path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) return Json::object();
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const Json parsed = Json::parse(contents);
    return parsed.isObject() ? parsed : Json::object();
}
} // namespace

HomeWatching::HomeWatching(std::string dataRoot) {
    const std::string root = dataRoot.empty() ? "/var/lib/tvweb" : std::move(dataRoot);
    path_ = root + "/home-watching.json";
    playsPath_ = root + "/home-plays.json";
}

void HomeWatching::loadLocked() {
    if (loaded_) return;
    loaded_ = true;
    profiles_ = loadDocument(path_);
    plays_ = loadDocument(playsPath_);
}

void HomeWatching::saveLocked(const std::string &path, const Json &document) const {
    const std::string temporary = path + ".tmp";
    const std::string contents = document.dump();
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
    if (!ok || rename(temporary.c_str(), path.c_str()) != 0) (void)unlink(temporary.c_str());
}

bool HomeWatching::countsLocked() const {
    return !activeKey_.empty() && frontApp_ == activeApp_ && (!knowsPlayback_ || playing_);
}

// The time counted up to `nowMs` goes to the title Home opened and to the play.
void HomeWatching::settleLocked(int64_t nowMs) {
    if (countingSince_) {
        const int64_t counted = std::min(kSessionMaxMs, std::max<int64_t>(0, nowMs - countingSince_));
        Json kept = Json::array();
        for (const Json &entry : profiles_[activeProfile_].items()) {
            Json copy = copyOf(entry);
            if (HomeTitles::keyOf(entry["item"]) == activeKey_) {
                copy.set("watchedMs", Json::numeric(entry["watchedMs"].number() +
                                                    static_cast<double>(counted)));
                copy.set("seenAt", Json::numeric(static_cast<double>(nowMs)));
            }
            kept.add(copy);
        }
        profiles_.set(activeProfile_, kept);
        countingSince_ = 0;
        dirty_ = true;
    }
    if (play_.since) {
        play_.playedMs += std::min(kSessionMaxMs, std::max<int64_t>(0, nowMs - play_.since));
        play_.since = 0;
    }
}

// After a change, counting starts again where it applies.
void HomeWatching::resumeLocked(int64_t nowMs) {
    if (countsLocked()) {
        countingSince_ = nowMs;
        if (play_.appId == activeApp_) play_.titled = true;
    }
    if (!play_.appId.empty() && (!knowsPlayback_ || playing_)) play_.since = nowMs;
}

Json HomeWatching::endPlayLocked(int64_t nowMs) {
    Json ended;
    if (!play_.appId.empty() && play_.playedMs >= kShownAfterMs) {
        Json entry = Json::object();
        entry.set("service", Json::text(play_.service));
        entry.set("appId", Json::text(play_.appId));
        entry.set("startedAt", Json::numeric(static_cast<double>(play_.startedAt)));
        entry.set("endedAt", Json::numeric(static_cast<double>(nowMs)));
        entry.set("playedMs", Json::numeric(static_cast<double>(play_.playedMs)));
        entry.set("titled", Json::flag(play_.titled));
        Json list = Json::array();
        list.add(entry);
        for (const Json &other : plays_[play_.profile].items())
            if (list.size() < kPlaysKeptMax &&
                nowMs - static_cast<int64_t>(other["endedAt"].number()) <= kKeptForMs)
                list.add(copyOf(other));
        plays_.set(play_.profile, list);
        saveLocked(playsPath_, plays_);
        ended = Json::object();
        ended.set("profile", Json::text(play_.profile));
        ended.set("service", entry["service"]);
        ended.set("appId", entry["appId"]);
        ended.set("playedMs", entry["playedMs"]);
        ended.set("endedAt", entry["endedAt"]);
        ended.set("titled", entry["titled"]);
    }
    play_ = Play();
    return ended;
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
    // The time the title before it was watching is counted first.
    settleLocked(nowMs);
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
    activeLeftAt_ = 0;
    resumeLocked(nowMs);
    saveLocked(path_, profiles_);
    dirty_ = false;
}

void HomeWatching::seen(const std::string &profile, const Json &item, const std::string &service,
                        const std::string &appId, int64_t watchedMs, int64_t nowMs) {
    const Json title = HomeTitles::trimmed(item);
    const std::string key = HomeTitles::keyOf(item);
    if (!title.valid() || key.empty() || profile.empty() || watchedMs <= 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    loadLocked();
    if (appId == play_.appId && profile == play_.profile) play_.titled = true;
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
    saveLocked(path_, profiles_);
}

Json HomeWatching::foreground(const std::string &appId, const std::string &service,
                              const std::string &profile, int64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    loadLocked();
    if (appId == frontApp_) return Json();
    settleLocked(nowMs);
    if (!activeApp_.empty() && frontApp_ == activeApp_) activeLeftAt_ = nowMs;
    const Json ended = endPlayLocked(nowMs);
    frontApp_ = appId;
    // The title stays the one Home opened last until it opens another, so coming back to its app
    // soon goes on counting; back much later, the app is playing something else.
    if (!activeKey_.empty() && appId == activeApp_ && activeLeftAt_ &&
        nowMs - activeLeftAt_ > kResumedWithinMs) {
        activeKey_.clear();
        activeApp_.clear();
    }
    if (!appId.empty() && !service.empty() && !profile.empty()) {
        play_.profile = profile;
        play_.appId = appId;
        play_.service = service;
        play_.startedAt = nowMs;
    }
    resumeLocked(nowMs);
    if (dirty_) saveLocked(path_, profiles_);
    dirty_ = false;
    return ended;
}

void HomeWatching::playing(bool playing, int64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    loadLocked();
    if (knowsPlayback_ && playing == playing_) return;
    settleLocked(nowMs);
    knowsPlayback_ = true;
    playing_ = playing;
    resumeLocked(nowMs);
    if (dirty_) saveLocked(path_, profiles_);
    dirty_ = false;
}

Json HomeWatching::watched(const std::string &profile, size_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    loadLocked();
    Json items = Json::array();
    for (const Json &entry : profiles_[profile].items())
        if (items.size() < limit && entry["watchedMs"].number() >= kShownAfterMs)
            items.add(entry["item"]);
    return items;
}

Json HomeWatching::row(const std::string &profile, int64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    loadLocked();
    std::vector<std::pair<double, Json>> shown;
    for (const Json &entry : profiles_[profile].items()) {
        double watched = entry["watchedMs"].number();
        const std::string key = HomeTitles::keyOf(entry["item"]);
        if (countingSince_ && profile == activeProfile_ && key == activeKey_)
            watched += static_cast<double>(std::min(kSessionMaxMs, std::max<int64_t>(0, nowMs - countingSince_)));
        const double progress = watched / runtimeMs(entry);
        if (watched < kShownAfterMs || progress >= kFinished ||
            nowMs - static_cast<int64_t>(entry["openedAt"].number()) > kKeptForMs)
            continue;
        Json item = copyOf(entry["item"]);
        item.set("service", entry["service"]);
        item.set("progress", Json::numeric(std::round(progress * 1000) / 1000));
        shown.emplace_back(std::max(entry["openedAt"].number(), entry["seenAt"].number()), item);
    }
    // A service's last play decides: with a title, the title's card stands for it.
    std::set<std::string> services;
    for (const Json &play : plays_[profile].items()) {
        const std::string service = play["service"].string();
        const double endedAt = play["endedAt"].number();
        if (!services.insert(service).second || play["titled"].boolean() ||
            nowMs - static_cast<int64_t>(endedAt) > kPlayShownForMs)
            continue;
        Json item = Json::object();
        item.set("kind", Json::text("app"));
        item.set("id", play["appId"]);
        item.set("appId", play["appId"]);
        item.set("service", play["service"]);
        item.set("playedMs", play["playedMs"]);
        item.set("endedAt", play["endedAt"]);
        shown.emplace_back(endedAt, item);
    }
    std::stable_sort(shown.begin(), shown.end(),
                     [](const std::pair<double, Json> &a, const std::pair<double, Json> &b) {
                         return a.first > b.first;
                     });
    Json items = Json::array();
    for (const auto &entry : shown)
        if (items.size() < kRowMax) items.add(entry.second);
    if (!items.size()) return Json();
    Json result = Json::object();
    result.set("key", Json::text("continue"));
    result.set("items", items);
    return result;
}

Json HomeWatching::services(const std::string &profile, int64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    loadLocked();
    std::map<std::string, double> played;
    for (const Json &play : plays_[profile].items())
        if (nowMs - static_cast<int64_t>(play["endedAt"].number()) <= kKeptForMs)
            played[play["service"].string()] += play["playedMs"].number();
    std::vector<std::pair<std::string, double>> ranked(played.begin(), played.end());
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const std::pair<std::string, double> &a,
                        const std::pair<std::string, double> &b) { return a.second > b.second; });
    Json result = Json::array();
    for (const auto &entry : ranked) {
        Json service = Json::object();
        service.set("service", Json::text(entry.first));
        service.set("playedMs", Json::numeric(entry.second));
        result.add(service);
    }
    return result;
}

} // namespace service
} // namespace nativetv
