#include "service/home_picks.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

using nativetv::Json;
using nativetv::service::Assistant;
using nativetv::service::HomePicks;

namespace {
Json title(int id, const char *type, const char *name, const char *service = nullptr) {
    Json item = Json::object();
    item.set("id", Json::numeric(id));
    item.set("type", Json::text(type));
    item.set("title", Json::text(name));
    item.set("year", Json::text("2026"));
    if (service) item.set("service", Json::text(service));
    return item;
}
Json watched(const char *name, const char *why) {
    Json entry = Json::object();
    entry.set("title", Json::text(name));
    entry.set("type", Json::text("movie"));
    entry.set("year", Json::text("2008"));
    entry.set("why", Json::text(why));
    return entry;
}
} // namespace

int main() {
    Json candidates = Json::array();
    candidates.add(title(1, "movie", "Heat", "netflix"));
    candidates.add(title(2, "tv", "Slow Horses"));
    candidates.add(title(3, "movie", "Dune"));
    Json history = Json::array();
    history.add(watched("The Dark Knight", "watched"));

    Json services = Json::parse(R"([{"service":"netflix","playedMs":19200000},
                                     {"service":"prime","playedMs":2400000},{"service":"other","playedMs":1}])");
    const std::string text = HomePicks::prompt(history, services, candidates, "IT");
    assert(text.find("- The Dark Knight (film, 2008): watched") != std::string::npos);
    assert(text.find("movie:1 | Heat (film, 2026) [Netflix]") != std::string::npos);
    assert(text.find("tv:2 | Slow Horses (series, 2026)\n") != std::string::npos);
    assert(text.find("country IT") != std::string::npos);
    assert(text.find("a short reason in English") != std::string::npos);
    assert(HomePicks::prompt(history, services, candidates, "IT", "it-IT").find("a short reason in the language it-IT") !=
           std::string::npos);
    assert(text.find("in the last 30 days: Netflix (5 h 20 min), Prime Video (40 min).\n") !=
           std::string::npos);
    assert(HomePicks::prompt(history, Json::array(), candidates, "IT").find("last 30 days") ==
           std::string::npos);

    // Only titles on offer, once each, with a reason of sensible length.
    const Json parsed = HomePicks::parse(
        "```json\n[{\"key\":\"tv:2\",\"because\":\"Because you watched The Dark Knight\"},"
        "{\"key\":\"movie:9\",\"because\":\"x\"},{\"key\":\"tv:2\",\"because\":\"again\"},"
        "{\"key\":\"movie:3\",\"because\":\"Trending this week\"}]\n```",
        candidates);
    assert(parsed.size() == 2);
    assert(parsed[0]["key"].string() == "tv:2" &&
           parsed[0]["because"].string() == "Because you watched The Dark Knight");
    assert(parsed[1]["key"].string() == "movie:3");
    assert(!HomePicks::parse("no picks today", candidates).size());

    char directory[] = "/tmp/nativetv-home-picks-XXXXXX";
    assert(mkdtemp(directory));
    int asked = 0;
    std::string sentKey;
    int64_t clock = 1000;
    {
        HomePicks picks(
            directory,
            [&](const std::string &url, const std::string &key, const std::string &body,
                Assistant::Reply &reply, std::string &) {
                ++asked;
                sentKey = key;
                assert(url.find("gemini-flash-latest:generateContent") != std::string::npos);
                assert(body.find("\"responseMimeType\"") != std::string::npos);
                reply.status = 200;
                reply.body = Json::parse(
                    R"({"candidates":[{"content":{"parts":[{"text":"[{\"key\":\"movie:3\",\"because\":\"Because you watched The Dark Knight\"}]"}]}}]})");
                return true;
            },
            [&clock]() { return clock; });
        assert(!picks.picks("", "", "main", history, services, candidates, "IT").size());
        assert(asked == 0);
        assert(!picks.picks("key", "", "main", history, services, candidates, "IT").size());
        picks.wait();
        assert(asked == 1 && sentKey == "key");
        const Json kept = picks.picks("key", "", "main", history, services, candidates, "IT");
        assert(kept.size() == 1 && kept[0]["key"].string() == "movie:3");
        picks.wait();
        assert(asked == 1); // kept: not asked again
        clock += HomePicks::kLifetimeMs + 1;
        assert(picks.picks("key", "", "main", history, services, candidates, "IT").size() == 1);
        picks.wait();
        assert(asked == 2); // a day on, asked again, the old picks shown meanwhile
        history.add(watched("Heat", "liked"));
        (void)picks.picks("key", "", "main", history, services, candidates, "IT");
        picks.wait();
        assert(asked == 3); // the profile's titles changed
        (void)picks.picks("key", "", "kids", history, services, candidates, "IT");
        picks.wait();
        assert(asked == 4); // another profile has its own
        (void)picks.picks("key", "", "kids", history, services, candidates, "IT");
        picks.wait();
        assert(asked == 4); // the same
        services = Json::parse(R"([{"service":"netflix","playedMs":19260000},
                                  {"service":"prime","playedMs":2400000},{"service":"other","playedMs":1}])");
        (void)picks.picks("key", "", "kids", history, services, candidates, "IT");
        picks.wait();
        assert(asked == 4); // a minute more played: not asked again
        Json reordered = Json::parse(R"([{"service":"prime","playedMs":9}])");
        (void)picks.picks("key", "", "kids", history, reordered, candidates, "IT");
        picks.wait();
        assert(asked == 5); // another service watched most
    }
    {
        int failures = 0;
        HomePicks failing(
            directory,
            [&](const std::string &, const std::string &, const std::string &, Assistant::Reply &reply,
                std::string &) {
                ++failures;
                reply.status = 400;
                return true;
            },
            [&clock]() { return clock; });
        Json other = Json::array();
        other.add(watched("Alien", "watched"));
        (void)failing.picks("key", "", "main", other, services, candidates, "IT");
        failing.wait();
        (void)failing.picks("key", "", "main", other, services, candidates, "IT");
        failing.wait();
        assert(failures == 1); // a refusal is not asked again at once
        clock += HomePicks::kRetryMs + 1;
        (void)failing.picks("key", "", "main", other, services, candidates, "IT");
        failing.wait();
        assert(failures == 2);
    }
    std::system((std::string("rm -rf ") + directory).c_str());
    std::puts("home picks checks passed");
    return 0;
}
