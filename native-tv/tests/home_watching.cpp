#include "service/home_watching.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace nativetv;
using namespace nativetv::service;

namespace {
const int64_t kMinute = 60 * 1000;

Json title(int id, double runtime) {
    Json item = Json::parse("{\"id\":" + std::to_string(id) +
                            ",\"type\":\"movie\",\"title\":\"Film " + std::to_string(id) +
                            "\",\"poster\":\"/p.jpg\",\"backdrop\":\"/b.jpg\"}");
    if (runtime > 0) item.set("runtime", Json::numeric(runtime));
    return item;
}
double progressOf(const Json &row, int id) {
    for (const Json &item : row["items"].items())
        if (item["id"].number() == id) return item["progress"].number();
    return -1;
}
} // namespace

int main() {
    char pattern[] = "/tmp/nativetv-home-watching-XXXXXX";
    const std::string directory = mkdtemp(pattern);
    int64_t now = 1793900000000LL;
    {
        HomeWatching watching(directory);
        assert(!watching.row("main", now).valid());

        // Opened from Home in Prime Video: the time the app stays in front is how far it got.
        watching.opened("main", title(1, 100), "prime", "amazon.global", now);
        now += kMinute;
        assert(!watching.row("main", now).valid()); // a minute is a look, not watching
        now += 9 * kMinute;
        watching.foreground("com.webos.app.hdmi1", now);
        Json row = watching.row("main", now);
        assert(row["key"].string() == "continue" && progressOf(row, 1) == .1);
        assert(row["items"][0]["service"].string() == "prime" &&
               row["items"][0]["title"].string() == "Film 1");
        now += 60 * kMinute; // elsewhere: not counted
        assert(progressOf(watching.row("main", now), 1) == .1);
        watching.foreground("amazon.global", now); // back in the app
        now += 50 * kMinute;
        assert(progressOf(watching.row("main", now), 1) == .6); // counting while in front
        watching.foreground("", now); // the screen goes off
        now += 30 * kMinute;
        assert(progressOf(watching.row("main", now), 1) == .6);

        // Another title: the newest leads, and one without a runtime is taken as two hours.
        watching.opened("main", title(2, 0), "netflix", "netflix", now);
        now += 30 * kMinute;
        watching.opened("main", title(3, 50), "netflix", "netflix", now); // counts title 2's time
        row = watching.row("main", now);
        assert(row["items"][0]["id"].number() == 2 && progressOf(row, 2) == .25);
        now += 46 * kMinute; // title 3 near its end: finished, so no longer shown
        watching.foreground("youtube.leanback.v4", now);
        assert(progressOf(watching.row("main", now), 3) == -1);
        assert(!watching.row("guest", now).valid());
    }
    struct stat info = {};
    const std::string path = directory + "/home-watching.json";
    assert(stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);
    {
        HomeWatching again(directory);
        assert(progressOf(again.row("main", now), 1) == .6);
        // A month later the titles are gone.
        assert(!again.row("main", now + 31LL * 24 * 60 * kMinute).valid());
    }
    (void)unlink(path.c_str());
    (void)rmdir(directory.c_str());
    std::printf("Home watching passed.\n");
    return 0;
}
