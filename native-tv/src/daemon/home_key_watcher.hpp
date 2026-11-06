#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace nativetv {
class Json;
namespace daemon {
class Logger;

// The card in front when the compositor logged that it showed LG's Home, from a line of
// /var/log/messages such as `LSM NL_HOME_SHOWN {"CLOCK":7613.666,"foregroundId":"com.webos.app.hdmi1"}`.
// False for any other line.
bool parseHomeShown(const std::string &line, std::string &foregroundId);

struct HomeKeyWatcherOptions {
    std::string logPath = "/var/log/messages";
    unsigned pollIntervalMilliseconds = 200;
};

// webOS 3.4 has no Home app to put Native Tv Home in the place of: LG's Home is a ribbon the
// compositor draws over the card in front. The watcher follows the compositor's log for the ribbon
// being shown and, while `enabled` says so, hands each one to `opened` with the card that was in
// front, which brings Native Tv Home forward. A Home the server opens itself (the LG Home tile, the
// dashboard's Home key) is passed over for the time given to suppressFor.
class HomeKeyWatcher {
  public:
    using Opened = std::function<void(const std::string &foregroundId)>;

    HomeKeyWatcher(std::function<bool()> enabled, Opened opened, Logger *logger = nullptr,
                   HomeKeyWatcherOptions options = HomeKeyWatcherOptions());
    ~HomeKeyWatcher();
    HomeKeyWatcher(const HomeKeyWatcher &) = delete;
    HomeKeyWatcher &operator=(const HomeKeyWatcher &) = delete;

    bool start();
    void stop();
    void suppressFor(std::chrono::milliseconds duration);
    // {"seq": Home presses handed on, "agoMs": since the last}, or null before the first: the TV
    // app asks when it comes forward, and opens its Home if the Home button brought it there.
    Json status() const;

  private:
    void run();
    void readUpdates();
    void handle(const std::string &foregroundId);

    std::function<bool()> enabled_;
    Opened opened_;
    Logger *logger_;
    HomeKeyWatcherOptions options_;
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
    long long offset_ = -1;
    std::string partial_;
    std::chrono::steady_clock::time_point suppressedUntil_;
    std::chrono::steady_clock::time_point lastAt_;
    std::atomic<unsigned> seq_{0};
};

} // namespace daemon
} // namespace nativetv
