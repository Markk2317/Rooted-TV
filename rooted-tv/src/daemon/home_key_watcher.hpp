#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace rootedtv {
class Json;
namespace daemon {
class Logger;

// The card in front when the compositor logged that it showed LG's Home, from a line of
// /var/log/messages such as `LSM NL_HOME_SHOWN {"CLOCK":7613.666,"foregroundId":"com.webos.app.hdmi1"}`.
// False for any other line.
bool parseHomeShown(const std::string &line, std::string &foregroundId);
// The app the TV opens as it is switched on, from the boot manager's launch of it:
// `SAM NL_APP_LAUNCH_BEGIN {"app_id":"com.webos.app.hdmi1","caller_id":"com.webos.bootManager",...}`,
// logged at a cold start and at every wake from Quick Start's standby. False for any other line.
bool parsePowerOnLaunch(const std::string &line, std::string &appId);
// An input that LG's external device service opened, as it does when a device on HDMI announces
// itself the active source over CEC: `SAM NL_APP_LAUNCH_BEGIN {"app_id":"com.webos.app.hdmi3",
// "caller_id":"com.webos.service.tv.externaldevice",...}`. False for any other line.
bool parseDeviceLaunch(const std::string &line, std::string &appId);
// The kernel time of the TV's last switch-on in the log (the bracketed seconds of its line, the
// clock /proc/uptime counts, which leaves out Quick Start's standby), or a negative number when the
// log has none.
double lastPowerOnUptime(const std::string &logPath);

struct HomeKeyWatcherOptions {
    std::string logPath = "/var/log/messages";
    unsigned pollIntervalMilliseconds = 200;
};

// webOS 3.4 has no Home app to put Rooted TV Home in the place of: LG's Home is a ribbon the
// compositor draws over the card in front. The watcher follows the compositor's log for the ribbon
// being shown and, while `enabled` says so, hands each one to `opened` with the card that was in
// front, which brings Rooted TV Home forward. A Home the server opens itself (the LG Home tile, the
// dashboard's Home key) is passed over for the time given to suppressFor.
class HomeKeyWatcher {
  public:
    using Opened = std::function<void(const std::string &foregroundId)>;

    HomeKeyWatcher(std::function<bool()> enabled, Opened opened, Logger *logger = nullptr,
                   HomeKeyWatcherOptions options = HomeKeyWatcherOptions());
    ~HomeKeyWatcher();
    HomeKeyWatcher(const HomeKeyWatcher &) = delete;
    HomeKeyWatcher &operator=(const HomeKeyWatcher &) = delete;

    // With `enabled` saying so, the TV being switched on is handed to `opened` too, with the input
    // the TV opened, as a Home press would be. Set before start().
    void setPowerOn(std::function<bool()> enabled) { powerOnEnabled_ = std::move(enabled); }
    // Told of every switch-on, whatever the switches. Set before start().
    void setPoweredOn(std::function<void()> poweredOn) { poweredOn_ = std::move(poweredOn); }
    // Asked at a switch-on that would be handed on: true when a device switched the TV on (over
    // HDMI-CEC, to show itself), which leaves its input in front. Set before start().
    void setPoweredOnByDevice(std::function<bool()> byDevice) { byDevice_ = std::move(byDevice); }
    bool start();
    void stop();
    void suppressFor(std::chrono::milliseconds duration);
    // {"seq": Home presses handed on, "agoMs": since the last}, or null before the first: the TV
    // app asks when it comes forward, and opens its Home if the Home button brought it there.
    Json status() const;

  private:
    void run();
    void readUpdates();
    void handle(const std::string &foregroundId, bool powerOn);
    void deviceLaunched(const std::string &appId);

    std::function<bool()> enabled_, powerOnEnabled_;
    std::function<void()> poweredOn_;
    std::function<bool()> byDevice_;
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
    std::chrono::steady_clock::time_point deviceTakeoverUntil_;
    std::atomic<unsigned> seq_{0};
};

} // namespace daemon
} // namespace rootedtv
