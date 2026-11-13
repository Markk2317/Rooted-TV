#pragma once

#include "core/json.hpp"
#include <cstdint>
#include <mutex>
#include <string>

namespace rootedtv {
namespace service {

// Google TV's Continue watching. A streaming app tells no other app what it plays: LG's player
// says only that a picture plays full screen, and when it pauses or stops. So a title is known when
// Home opened it, and how far it got is the time its app then played, against the title's runtime.
// What was played inside a service's app is kept too, as plays without a title: the service's card
// on the row, and which services the profile watches, for its recommendations. Kept per profile in
// home-watching.json (titles) and home-plays.json (plays) beside the server.
class HomeWatching {
  public:
    static constexpr int64_t kShownAfterMs = 2 * 60 * 1000; // less is a look, not watching

    explicit HomeWatching(std::string dataRoot);

    // Home opened `item` (a title as the feed lists it, with its "runtime" in minutes when known) in
    // `appId` for `service` ("netflix", "prime", "disney") for `profile`, at `nowMs`.
    void opened(const std::string &profile, const Json &item, const std::string &service,
                const std::string &appId, int64_t nowMs);
    // A title read off the screen once it had played `watchedMs` in a service's own app
    // (service::screen_title): put first, its time added to what it had. Nothing for the title
    // Home opened last, whose time its app's playing already counts.
    void seen(const std::string &profile, const Json &item, const std::string &service,
              const std::string &appId, int64_t watchedMs, int64_t nowMs);
    // The app in front changed, or the screen went off (""): `service` is the streaming service it
    // is ("" for any other app), `profile` Home's profile then. Returns the play this ended in a
    // service's app, once it was watching ({"profile", "service", "appId", "playedMs",
    // "endedAt"}), else null.
    Json foreground(const std::string &appId, const std::string &service, const std::string &profile,
                    int64_t nowMs);
    // LG's player: whether a picture plays full screen in the app in front. Once it has said so,
    // time counts only while one plays (not in the app's menus); until then (a TV whose player says
    // nothing), all the time the app is in front.
    void playing(bool playing, int64_t nowMs);
    // {"key": "continue", "items": [...]}, newest first, or null: the titles watched for two
    // minutes or more, not to their end, in the last 30 days (title + "service" + "progress" from 0
    // to 1), and the last play without a title in each service's app in the last week ({"kind":
    // "app", "appId", "service", "playedMs", "endedAt"}).
    Json row(const std::string &profile, int64_t nowMs);
    // Every title the profile has watched for two minutes or more, finished or not, newest first:
    // [item], at most `limit`.
    Json watched(const std::string &profile, size_t limit);
    // The services the profile played in the last 30 days, most first: [{"service", "playedMs"}].
    Json services(const std::string &profile, int64_t nowMs);
    // The titles that have played to 90% since the last call, each once: [item]. Home marks them
    // watched for the profile, as Google TV does when a film ends.
    Json takeFinished(const std::string &profile, int64_t nowMs);
    // The title leaves Continue watching: marked watched, or no longer being watched.
    void forget(const std::string &profile, const std::string &key);

  private:
    void loadLocked();
    void saveLocked(const std::string &path, const Json &document) const;
    void settleLocked(int64_t nowMs);
    void resumeLocked(int64_t nowMs);
    Json endPlayLocked(int64_t nowMs);
    bool countsLocked() const;

    std::string path_, playsPath_;
    std::mutex mutex_;
    bool loaded_ = false;
    // {profile: [{"item", "service", "appId", "openedAt", "watchedMs", "seenAt", "finished"}]}
    Json profiles_;
    Json plays_;    // {profile: [{"service", "appId", "startedAt", "endedAt", "playedMs", "titled"}]}
    std::string frontApp_;
    bool knowsPlayback_ = false, playing_ = false;
    // The title Home opened last: its profile and key, its app, and since when its time counts.
    std::string activeProfile_, activeKey_, activeApp_;
    int64_t countingSince_ = 0, activeLeftAt_ = 0;
    // The play in the service's app in front: since when it plays (0 while it does not).
    struct Play {
        std::string profile, appId, service;
        int64_t startedAt = 0, playedMs = 0, since = 0;
        bool titled = false;
    } play_;
    bool dirty_ = false;
};

} // namespace service
} // namespace rootedtv
