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

        // Opened from Home in Prime Video: on a TV whose player says nothing, the time the app
        // stays in front is how far it got.
        watching.opened("main", title(1, 100), "prime", "amazon.global", now);
        watching.foreground("amazon.global", "prime", "main", now);
        now += kMinute;
        assert(!watching.row("main", now).valid()); // a minute is a look, not watching
        now += 9 * kMinute;
        const Json ended = watching.foreground("com.webos.app.hdmi1", "", "main", now);
        assert(ended["service"].string() == "prime" && ended["playedMs"].number() == 10 * kMinute &&
               ended["titled"].boolean());
        Json row = watching.row("main", now);
        assert(row["key"].string() == "continue" && progressOf(row, 1) == .1);
        assert(row["items"][0]["service"].string() == "prime" &&
               row["items"][0]["title"].string() == "Film 1");
        assert(row["items"].size() == 1); // the play had the title: no card of the app's own
        now += 20 * kMinute; // elsewhere: not counted
        assert(progressOf(watching.row("main", now), 1) == .1);
        watching.foreground("amazon.global", "prime", "main", now); // back in the app soon
        now += 50 * kMinute;
        assert(progressOf(watching.row("main", now), 1) == .6); // counting while in front
        watching.foreground("", "", "main", now); // the screen goes off
        now += 20 * kMinute;
        assert(progressOf(watching.row("main", now), 1) == .6);

        // Once LG's player has said anything, only the time a picture plays counts: not the
        // app's menus, nor a pause.
        watching.playing(false, now);
        watching.opened("main", title(2, 0), "netflix", "netflix", now);
        watching.foreground("netflix", "netflix", "main", now);
        now += 5 * kMinute; // the profiles, the title's page
        watching.playing(true, now);
        now += 24 * kMinute;
        watching.playing(false, now); // paused
        now += 10 * kMinute;
        watching.playing(true, now);
        now += 6 * kMinute;
        row = watching.row("main", now);
        assert(row["items"][0]["id"].number() == 2 && progressOf(row, 2) == .25); // of two hours
        watching.opened("main", title(3, 50), "netflix", "netflix", now); // counts title 2's time
        now += 46 * kMinute; // title 3 near its end: finished, so no longer shown
        watching.foreground("youtube.leanback.v4", "", "main", now);
        assert(progressOf(watching.row("main", now), 3) == -1);
        assert(!watching.row("guest", now).valid());

        // Netflix opened from its own button, a day later: what it played has no title, so the
        // row has Netflix's own card, newest first.
        now += 24 * 60 * kMinute;
        watching.playing(false, now);
        watching.foreground("netflix", "netflix", "main", now);
        now += 2 * kMinute;
        watching.playing(true, now);
        now += 47 * kMinute;
        watching.playing(false, now);
        const Json untitled = watching.foreground("com.tvweb.dashboard", "", "main", now);
        assert(untitled["playedMs"].number() == 47 * kMinute && !untitled["titled"].boolean());
        row = watching.row("main", now);
        assert(row["items"][0]["kind"].string() == "app" &&
               row["items"][0]["appId"].string() == "netflix" &&
               row["items"][0]["playedMs"].number() == 47 * kMinute);
        assert(progressOf(row, 2) == .25); // title 2 was not counted: it was opened a day ago
        // A short look in Prime Video is not a play.
        watching.foreground("amazon.global", "prime", "main", now);
        watching.playing(true, now);
        now += kMinute;
        assert(!watching.foreground("", "", "main", now).valid());

        const Json services = watching.services("main", now);
        assert(services.size() == 2 && services[0]["service"].string() == "netflix" &&
               services[0]["playedMs"].number() == (30 + 46 + 47) * kMinute &&
               services[1]["service"].string() == "prime");
        assert(!watching.services("guest", now).size());
    }
    struct stat info = {};
    const std::string path = directory + "/home-watching.json";
    const std::string playsPath = directory + "/home-plays.json";
    assert(stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);
    assert(stat(playsPath.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);
    {
        HomeWatching again(directory);
        assert(progressOf(again.row("main", now), 1) == .6);
        assert(again.row("main", now)["items"][0]["kind"].string() == "app");
        // A week later Netflix's own card is gone; a month later the titles too.
        assert(again.row("main", now + 8LL * 24 * 60 * kMinute)["items"][0]["kind"].string() != "app");
        assert(!again.row("main", now + 31LL * 24 * 60 * kMinute).valid());
    }
    (void)unlink(path.c_str());
    (void)unlink(playsPath.c_str());
    (void)rmdir(directory.c_str());
    {
        char finishedPattern[] = "/tmp/nativetv-home-finished-XXXXXX";
        const std::string finishedDirectory = mkdtemp(finishedPattern);
        HomeWatching watching(finishedDirectory);
        // A title played to 90% is handed over once, to be marked watched; one marked watched by
        // hand leaves the row.
        assert(!watching.takeFinished("main", now).size());
        watching.opened("main", title(1, 100), "prime", "amazon.global", now);
        watching.opened("main", title(7, 20), "prime", "amazon.global", now);
        watching.foreground("amazon.global", "prime", "main", now);
        watching.playing(true, now);
        now += 19 * kMinute;
        const Json finished = watching.takeFinished("main", now);
        assert(finished.size() == 1 && finished[0]["id"].number() == 7);
        assert(!watching.takeFinished("main", now).size());
        watching.forget("main", "movie:7");
        assert(!watching.takeFinished("main", now + kMinute).size());
        watching.foreground("", "", "main", now);
        (void)unlink((finishedDirectory + "/home-watching.json").c_str());
        (void)unlink((finishedDirectory + "/home-plays.json").c_str());
        (void)rmdir(finishedDirectory.c_str());
    }
    std::printf("Home watching passed.\n");
    return 0;
}
