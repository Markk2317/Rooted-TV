#include "core/json.hpp"
#include "daemon/home_key_watcher.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace nativetv;
using namespace nativetv::daemon;

namespace {
void check(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

void append(const std::string &path, const std::string &text) {
    std::ofstream output(path.c_str(), std::ios::app | std::ios::binary);
    output << text;
}

const char *kShownOverHdmi =
    "2026-11-06T11:47:37.687474Z [7677.370] user.info surface-manager-starfish [] LSM "
    "NL_HOME_SHOWN {\"CLOCK\":7677.37,\"foregroundId\":\"com.webos.app.hdmi1\"} \n";
const char *kShownOverApp =
    "2026-11-06T11:46:33.983471Z [7613.666] user.info surface-manager-starfish [] LSM "
    "NL_HOME_SHOWN {\"CLOCK\":7613.666,\"foregroundId\":\"com.tvweb.dashboard\"} \n";
const char *kOther =
    "2026-11-06T11:47:38.522431Z [7678.205] user.info sam [] SAM NL_APP_LAUNCH_BEGIN "
    "{\"app_id\":\"com.tvweb.dashboard\",\"caller_id\":\"\",\"mode\":\"normal\"} \n";

bool waitFor(const std::function<bool()> &condition) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (condition()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return condition();
}
} // namespace

int main() {
    try {
        std::string foreground;
        check(parseHomeShown(kShownOverHdmi, foreground) && foreground == "com.webos.app.hdmi1",
              "the card in front is read from the compositor's line");
        check(!parseHomeShown(kOther, foreground), "other lines are not a Home");

        char pattern[] = "/tmp/nativetv-home-key-XXXXXX";
        const int descriptor = mkstemp(pattern);
        check(descriptor >= 0, "could not create the stand-in log");
        close(descriptor);
        const std::string log = pattern;
        append(log, kShownOverHdmi); // before the start: history, not a press

        std::mutex mutex;
        std::vector<std::string> opened;
        std::atomic<bool> enabled{true};
        HomeKeyWatcherOptions options;
        options.logPath = log;
        options.pollIntervalMilliseconds = 10;
        HomeKeyWatcher watcher([&enabled]() { return enabled.load(); },
                               [&](const std::string &id) {
                                   std::lock_guard<std::mutex> lock(mutex);
                                   opened.push_back(id);
                               },
                               nullptr, options);
        const auto count = [&]() {
            std::lock_guard<std::mutex> lock(mutex);
            return opened.size();
        };
        check(watcher.start(), "the watcher starts on a log that exists");
        check(!watcher.status().valid(), "no press before the first");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        check(count() == 0, "a Home logged before the start is not taken");

        append(log, kOther);
        append(log, kShownOverApp);
        check(waitFor([&]() { return count() == 1; }), "a new Home is handed on");
        check(opened[0] == "com.tvweb.dashboard", "with the card that was in front");
        check(watcher.status()["seq"].number() == 1 && watcher.status()["agoMs"].number() < 2000,
              "the status counts it");

        // A line written in two pieces is read once whole.
        const std::string line = kShownOverHdmi;
        append(log, line.substr(0, 40));
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        append(log, line.substr(40));
        check(waitFor([&]() { return count() == 2; }) && opened[1] == "com.webos.app.hdmi1",
              "a line split across reads");

        watcher.suppressFor(std::chrono::milliseconds(300));
        append(log, kShownOverHdmi);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        check(count() == 2, "a Home the server opened itself is passed over");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        enabled = false;
        append(log, kShownOverHdmi);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        check(count() == 2 && watcher.status()["seq"].number() == 2,
              "switched off, a Home is left to LG");

        enabled = true;
        std::ofstream(log.c_str(), std::ios::trunc) << kShownOverHdmi; // rotated: shorter, all new
        check(waitFor([&]() { return count() == 3; }), "a rotated log is read from its start");

        watcher.stop();
        std::remove(log.c_str());
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "home key watcher test failed: %s\n", error.what());
        return 1;
    }
}
