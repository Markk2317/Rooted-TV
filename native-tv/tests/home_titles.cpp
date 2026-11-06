#include "service/home_titles.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace nativetv;
using namespace nativetv::service;

namespace {
Json title(int id, const std::string &type = "movie") {
    return Json::parse("{\"id\":" + std::to_string(id) + ",\"type\":\"" + type +
                       "\",\"title\":\"Title " + std::to_string(id) +
                       "\",\"year\":\"2026\",\"rating\":7.25,\"overview\":\"About it.\","
                       "\"poster\":\"/p" + std::to_string(id) + ".jpg\",\"backdrop\":\"/b.jpg\","
                       "\"service\":\"prime\"}");
}
size_t count(const Json &rows, const std::string &key) {
    for (const Json &row : rows.items())
        if (row["key"].string() == key) return row["items"].size();
    return 0;
}
} // namespace

int main() {
    char pattern[] = "/tmp/nativetv-home-titles-XXXXXX";
    const std::string directory = mkdtemp(pattern);
    std::string error;
    {
        HomeTitles titles(directory);
        // Only a film or series of TMDB's is kept, and only the fields Home shows.
        assert(!titles.apply("main", "watch", Json::parse(R"({"id":"x","type":"movie","title":"A"})"), error));
        assert(!titles.apply("main", "watch", Json::parse(R"({"id":1,"type":"person","title":"A"})"), error));
        assert(!titles.apply("main", "hide", title(1), error));
        Json hostile = title(9);
        hostile.set("poster", Json::text("https://elsewhere.example/x.jpg"));
        assert(titles.apply("main", "watch", hostile, error));
        assert(titles.state("main")["watchlist"][0]["poster"].string().empty());
        assert(!titles.state("main")["watchlist"][0].has("service"));
        assert(titles.state("main")["watchlist"][0]["rating"].number() == 7.3);

        // The newest first, once each; unwatch takes a title off.
        assert(titles.apply("main", "watch", title(1), error));
        assert(titles.apply("main", "watch", title(2, "tv"), error));
        assert(titles.apply("main", "watch", title(1), error));
        Json state = titles.state("main");
        assert(state["watchlist"].size() == 3 && state["watchlist"][0]["id"].number() == 1 &&
               state["watchlist"][1]["type"].string() == "tv");
        assert(titles.apply("main", "unwatch", title(9), error));
        assert(titles.state("main")["watchlist"].size() == 2);

        // A like keeps its similar titles; a dislike replaces it, and unrate clears both.
        Json liked = title(3);
        Json similar = Json::array();
        similar.add(title(4));
        similar.add(title(3)); // the title itself is not like itself
        similar.add(title(5, "tv"));
        liked.set("similar", similar);
        assert(titles.apply("main", "like", liked, error));
        state = titles.state("main");
        assert(state["liked"].size() == 1 && state["liked"][0]["similar"].size() == 2);
        assert(titles.apply("main", "dislike", title(3), error));
        state = titles.state("main");
        assert(state["liked"].size() == 0 && state["disliked"][0].string() == "movie:3");
        assert(titles.apply("main", "like", liked, error));
        assert(titles.apply("main", "dislike", title(5, "tv"), error));
        state = titles.state("main");
        assert(state["liked"].size() == 1 && state["disliked"].size() == 1 &&
               state["disliked"][0].string() == "tv:5");

        // Each profile has its own.
        assert(titles.state("guest")["watchlist"].size() == 0);

        // Home: what was not for the profile is left out of every row, then its watchlist and
        // the titles like the one it liked last follow.
        Json rows = Json::array();
        Json trending = Json::object();
        trending.set("key", Json::text("trending"));
        Json items = Json::array();
        items.add(title(5, "tv"));
        items.add(title(6));
        trending.set("items", items);
        rows.add(trending);
        Json onlyDisliked = Json::object();
        onlyDisliked.set("key", Json::text("series"));
        Json lone = Json::array();
        lone.add(title(5, "tv"));
        onlyDisliked.set("items", lone);
        rows.add(onlyDisliked);
        const Json personal = HomeTitles::personalRows(rows, state);
        // Trending: the pick like title 3 (title 4), then title 6; title 5 is not for it.
        assert(count(personal, "trending") == 2 && count(personal, "series") == 0);
        assert(count(personal, "watchlist") == 2);
        assert(count(personal, "liked") == 1); // title 4: title 5 is not for it
        for (const Json &row : personal.items())
            if (row["key"].string() == "liked")
                assert(row["title"].string() == "Because you liked Title 3");
        assert(trending["items"].size() == 2); // the feed's own rows are not changed

        // The top picks lead with titles like the ones liked and put on the watchlist, one from
        // each in turn, none already marked, then the week's trending titles.
        Json watched = title(2, "tv");
        Json watchedSimilar = Json::array();
        watchedSimilar.add(title(30));
        watchedSimilar.add(title(31));
        watched.set("similar", watchedSimilar);
        assert(titles.apply("main", "watch", watched, error));
        state = titles.state("main");
        Json picksFeed = Json::array();
        Json trendingRow = Json::object();
        trendingRow.set("key", Json::text("trending"));
        Json trendingItems = Json::array();
        trendingItems.add(title(40));
        trendingItems.add(title(1)); // on the watchlist already
        trendingItems.add(title(30)); // also a pick: listed once
        trendingRow.set("items", trendingItems);
        picksFeed.add(trendingRow);
        const Json picked = HomeTitles::personalRows(picksFeed, state);
        const Json picks = picked[0]["items"];
        assert(picked[0]["key"].string() == "trending");
        assert(picks.size() == 4 && picks[0]["id"].number() == 4 && picks[1]["id"].number() == 30 &&
               picks[2]["id"].number() == 31 && picks[3]["id"].number() == 40);
        for (const Json &row : picked.items())
            if (row["key"].string() == "watchlist")
                assert(!row["items"][0].has("similar"));

        const Json summary = HomeTitles::summary(state);
        assert(summary["watchlist"][0].string() == "tv:2" && summary["liked"][0].string() == "movie:3" &&
               summary["disliked"][0].string() == "tv:5");
    }
    // Kept for the next start, readable by root alone.
    struct stat info = {};
    const std::string path = directory + "/home-titles.json";
    assert(stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);
    {
        HomeTitles again(directory);
        assert(again.state("main")["watchlist"].size() == 2 &&
               again.state("main")["watchlist"][0]["similar"].size() == 2);
    }
    (void)unlink(path.c_str());
    (void)rmdir(directory.c_str());
    std::printf("Home titles passed.\n");
    return 0;
}
