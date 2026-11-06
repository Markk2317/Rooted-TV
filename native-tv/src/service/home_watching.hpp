#pragma once

#include "core/json.hpp"
#include <cstdint>
#include <mutex>
#include <string>

namespace nativetv {
namespace service {

// Google TV's Continue watching, for the titles Native Tv Home opened: a streaming app tells no
// other app what it plays, so a title is known only when Home opened it, and how far it got is the
// time its app then stayed in front, against the title's runtime. Kept per profile in
// home-watching.json beside the server.
class HomeWatching {
  public:
    explicit HomeWatching(std::string dataRoot);

    // Home opened `item` (a title as the feed lists it, with its "runtime" in minutes when known) in
    // `appId` for `service` ("netflix", "prime", "disney") for `profile`, at `nowMs`.
    void opened(const std::string &profile, const Json &item, const std::string &service,
                const std::string &appId, int64_t nowMs);
    // The app in front changed, or the screen went off (""): the title opened last stops counting
    // once its app has left the front.
    void foreground(const std::string &appId, int64_t nowMs);
    // {"key": "continue", "items": [title + "service" + "progress" (0 to 1)]}, newest first, or null:
    // the titles watched for two minutes or more, not to their end, in the last 30 days.
    Json row(const std::string &profile, int64_t nowMs);

  private:
    void loadLocked();
    void saveLocked();

    std::string path_;
    std::mutex mutex_;
    bool loaded_ = false;
    Json profiles_; // {profile: [{"item", "service", "appId", "openedAt", "watchedMs", "seenAt"}]}
    // The title being watched: its profile and key, its app, and since when it is in front.
    std::string activeProfile_, activeKey_, activeApp_;
    int64_t activeSince_ = 0;
    bool activeInFront_ = false;
};

} // namespace service
} // namespace nativetv
