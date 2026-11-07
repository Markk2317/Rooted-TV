#include "service/home_discover.hpp"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        char pattern[] = "/tmp/nativetv-home-discover-XXXXXX";
        char *created = mkdtemp(pattern);
        assert(created);
        path_ = created;
    }
    ~TemporaryDirectory() {
        (void)unlink((path_ + "/home-discover.json").c_str());
        (void)unlink((path_ + "/home-discover.json.tmp").c_str());
        (void)rmdir(path_.c_str());
    }
    const std::string &path() const { return path_; }

  private:
    std::string path_;
};

nativetv::Json title(int id, const std::string &type = "movie") {
    using nativetv::Json;
    Json item = Json::object();
    item.set("id", Json::numeric(id));
    item.set("media_type", Json::text(type));
    item.set("title", Json::text("Title " + std::to_string(id)));
    item.set("name", Json::text("Show " + std::to_string(id)));
    item.set("release_date", Json::text("2026-04-03"));
    item.set("vote_average", Json::numeric(8.26));
    item.set("vote_count", Json::numeric(200));
    item.set("overview", Json::text("A description."));
    item.set("poster_path", Json::text("/poster-abcdefghijkl.jpg"));
    item.set("backdrop_path", Json::text("/backdrop-abcdefghi.jpg"));
    return item;
}

nativetv::Json titleReply(int firstId, const std::string &type = "movie") {
    using nativetv::Json;
    Json results = Json::array();
    results.add(title(firstId, type));
    results.add(title(firstId, type)); // Each row de-duplicates its own titles.
    results.add(title(firstId + 1, type));
    results.add(title(firstId + 2, type));
    Json missing = title(firstId + 3, type);
    missing.set("poster_path", Json::parse("null"));
    missing.set("backdrop_path", Json::parse("null"));
    results.add(missing);
    Json document = Json::object();
    document.set("results", results);
    return document;
}

nativetv::Json youtubeReply() {
    using nativetv::Json;
    Json items = Json::array();
    items.add(Json::parse(
        "{\"id\":\"dQw4w9WgXcQ\",\"snippet\":{\"title\":\"Video\",\"channelTitle\":\"Channel\"}}"));
    items.add(Json::parse(
        "{\"id\":\"invalid id\",\"snippet\":{\"title\":\"Invalid\"}}"));
    Json document = Json::object();
    document.set("items", items);
    return document;
}

int64_t localMilliseconds(int year, int month, int day, int hour) {
    std::tm date = {};
    date.tm_year = year - 1900;
    date.tm_mon = month - 1;
    date.tm_mday = day;
    date.tm_hour = hour;
    return static_cast<int64_t>(std::mktime(&date)) * 1000;
}
} // namespace

int main() {
    using namespace nativetv;
    using namespace nativetv::service;
    TemporaryDirectory directory;
    int64_t clock = localMilliseconds(2026, 10, 20, 9);
    std::mutex askedMutex;
    std::condition_variable askedChanged;
    std::vector<std::string> asked;
    bool failTrending = false;
    bool failAll = false;
    auto fetcher = [&](const std::string &url, Json &document, std::string &error) {
        std::lock_guard<std::mutex> lock(askedMutex);
        asked.push_back(url);
        askedChanged.notify_all();
        if (failAll || (failTrending && url.find("trending/all/week") != std::string::npos)) {
            error = "offline";
            return false;
        }
        if (url.find("googleapis.com") != std::string::npos) {
            document = youtubeReply();
        } else if (url.find("trending/all/week") != std::string::npos) {
            document = titleReply(1);
        } else if (url.find("trending/movie/week") != std::string::npos) {
            document = titleReply(11);
        } else if (url.find("trending/tv/week") != std::string::npos) {
            document = titleReply(21, "tv");
        } else if (url.find("discover/movie") != std::string::npos) {
            document = titleReply(41);
        } else if (url.find("discover/tv") != std::string::npos) {
            document = titleReply(51, "tv");
        } else {
            document = titleReply(31, "tv");
        }
        return true;
    };
    const auto waitForAsked = [&](size_t count) {
        std::unique_lock<std::mutex> lock(askedMutex);
        return askedChanged.wait_for(lock, std::chrono::seconds(2),
                                     [&asked, count] { return asked.size() >= count; });
    };

    {
        HomeDiscover discover(directory.path(), fetcher, [&clock] { return clock; });
        const Json disabled = discover.feed("", "");
        assert(disabled["ok"].boolean() && !disabled["enabled"].boolean());
        assert(disabled["rows"].isArray() && disabled["rows"].size() == 0);
        assert(asked.empty());

        const Json first = discover.feed("private-tmdb-key", "youtube-key");
        assert(first["enabled"].boolean() && first["titlesEnabled"].boolean());
        assert(first["rows"].size() == 0); // A cache miss never waits for network.
        (void)discover.feed("private-tmdb-key", "youtube-key");
        assert(waitForAsked(4));
    } // Joining the worker makes the completed refresh observable below.
    assert(asked.size() == 4);
    for (const std::string &url : asked)
        assert(url.find("private-tmdb-key") == std::string::npos ||
               url.find("https://api.themoviedb.org/3/") == 0);
    assert(asked[0].find("api_key=private-tmdb-key") != std::string::npos);
    assert(asked[0].find("trending/all/week") != std::string::npos);
    assert(asked[3].find("key=youtube-key") != std::string::npos);

    struct stat cacheInfo = {};
    assert(stat((directory.path() + "/home-discover.json").c_str(), &cacheInfo) == 0);
    assert((cacheInfo.st_mode & 0777) == 0600);
    {
        std::ifstream input((directory.path() + "/home-discover.json").c_str());
        std::string saved((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());
        assert(saved.find("private-tmdb-key") == std::string::npos);
        assert(saved.find("youtube-key") == std::string::npos);
        const Json cache = Json::parse(saved);
        assert(!cache["partial"].boolean());
        assert(cache["rows"].size() == 4);
    }

    {
        HomeDiscover cached(directory.path(), fetcher, [&clock] { return clock; });
        const Json feed = cached.feed("private-tmdb-key", "youtube-key");
        assert(feed["rows"].size() == 4); // Three trending rows and YouTube.
        assert(feed["rows"][0]["key"].string() == "trending");
        assert(feed["rows"][0]["items"].size() == 3);
        assert(feed["rows"][0]["items"][0]["rating"].number() == 8.3);
        assert(feed["rows"][3]["key"].string() == "youtube");
        assert(feed["rows"][3]["items"][0]["image"].string() ==
               "https://i.ytimg.com/vi/dQw4w9WgXcQ/mqdefault.jpg");
    }
    assert(asked.size() == 4); // A fresh cache does not refetch.

    failTrending = true;
    const std::string partialDir = directory.path() + "/partial";
    assert(mkdir(partialDir.c_str(), 0700) == 0);
    const size_t beforePartial = asked.size();
    {
        HomeDiscover partial(partialDir, fetcher, [&clock] { return clock; });
        assert(partial.feed("tmdb", "")["rows"].size() == 0);
        assert(waitForAsked(beforePartial + 3));
    }
    Json partialCache;
    {
        std::ifstream input((partialDir + "/home-discover.json").c_str());
        std::string saved((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());
        partialCache = Json::parse(saved);
    }
    assert(partialCache["partial"].boolean());
    assert(partialCache["rows"].size() == 2);
    failTrending = false;
    clock += 11 * 60 * 1000;
    const size_t beforeRetry = asked.size();
    {
        HomeDiscover retry(partialDir, fetcher, [&clock] { return clock; });
        const Json stale = retry.feed("tmdb", "");
        assert(stale["rows"].size() >= 2); // Existing rows are returned during refresh.
        assert(waitForAsked(beforeRetry + 3));
    }
    {
        HomeDiscover recovered(partialDir, fetcher, [&clock] { return clock; });
        const Json feed = recovered.feed("tmdb", "");
        assert(feed["rows"][0]["key"].string() == "trending");
        assert(feed["rows"].size() == 3); // The three TMDB rows.
    }

    failAll = true;
    clock = localMilliseconds(2026, 10, 21, 6);
    const size_t beforeStaleRefresh = asked.size();
    {
        HomeDiscover staleService(directory.path(), fetcher, [&clock] { return clock; });
        const Json stale = staleService.feed("private-tmdb-key", "youtube-key");
        assert(stale["rows"].size() == 4);
        assert(waitForAsked(beforeStaleRefresh + 4));
    }
    const size_t beforeStaleRetry = asked.size();
    {
        HomeDiscover staleCache(directory.path(), fetcher, [&clock] { return clock; });
        const Json stale = staleCache.feed("private-tmdb-key", "youtube-key");
        assert(stale["rows"].size() == 4);
        assert(waitForAsked(beforeStaleRetry + 4));
    }

    const std::string youtubeDir = directory.path() + "/youtube";
    assert(mkdir(youtubeDir.c_str(), 0700) == 0);
    asked.clear();
    failAll = false;
    {
        HomeDiscover youtubeOnly(youtubeDir, fetcher, [&clock] { return clock; });
        const Json first = youtubeOnly.feed("", "youtube only");
        assert(!first["titlesEnabled"].boolean());
        assert(first["rows"].size() == 0);
        std::unique_lock<std::mutex> lock(askedMutex);
        assert(askedChanged.wait_for(lock, std::chrono::seconds(2),
                                     [&asked] { return !asked.empty(); }));
    }
    assert(asked.size() == 1 && asked[0].find("googleapis.com") != std::string::npos);
    assert(asked[0].find("youtube%20only") != std::string::npos);
    {
        HomeDiscover youtubeCache(youtubeDir, fetcher, [&clock] { return clock; });
        const Json feed = youtubeCache.feed("", "youtube only");
        assert(feed["rows"].size() == 1);
        assert(feed["rows"][0]["key"].string() == "youtube");
        assert(feed["rows"][0]["items"].size() == 1);
    }
    assert(asked.size() == 1);

    // Only titles that open somewhere (a YouTube trailer or a Netflix title) are listed, and a title in
    // two rows is asked about once.
    const std::string linkedDir = directory.path() + "/linked";
    assert(mkdir(linkedDir.c_str(), 0700) == 0);
    asked.clear();
    std::vector<std::string> checked;
    {
        HomeDiscover linked(linkedDir, fetcher, [&clock] { return clock; });
        linked.setLinkCheck([&checked](const std::string &type, const std::string &id) {
            checked.push_back(type + ":" + id);
            return id == "1" || id == "3" || id == "11";
        });
        (void)linked.feed("tmdb", "");
        assert(waitForAsked(3));
    }
    {
        HomeDiscover linkedCache(linkedDir, fetcher, [&clock] { return clock; });
        const Json feed = linkedCache.feed("tmdb", "");
        assert(feed["rows"][0]["key"].string() == "trending");
        assert(feed["rows"][0]["items"].size() == 2);
        assert(feed["rows"][0]["items"][0]["id"].number() == 1 &&
               feed["rows"][0]["items"][1]["id"].number() == 3);
        assert(feed["rows"][1]["items"].size() == 1 && feed["rows"][1]["items"][0]["id"].number() == 11);
    }
    {
        size_t askedAboutOne = 0;
        for (const std::string &entry : checked)
            if (entry == "movie:1") ++askedAboutOne;
        assert(askedAboutOne == 1);
    }
    // The Available on Netflix row lists the titles of the trending rows that have a Netflix link, once each.
    const std::string netflixDir = directory.path() + "/netflix";
    assert(mkdir(netflixDir.c_str(), 0700) == 0);
    asked.clear();
    {
        HomeDiscover onNetflix(netflixDir, fetcher, [&clock] { return clock; });
        onNetflix.setNetflixCheck([](const std::string &, const std::string &id) {
            return id == "2" || id == "12" || id == "22";
        });
        (void)onNetflix.feed("tmdb", "");
        assert(waitForAsked(3));
    }
    {
        HomeDiscover netflixCache(netflixDir, fetcher, [&clock] { return clock; });
        const Json feed = netflixCache.feed("tmdb", "");
        assert(feed["rows"].size() == 4 && feed["rows"][3]["key"].string() == "netflix");
        assert(feed["rows"][3]["items"].size() == 3 && feed["rows"][3]["items"][0]["id"].number() == 2 &&
               feed["rows"][3]["items"][1]["id"].number() == 12 && feed["rows"][3]["items"][2]["id"].number() == 22);
    }
    // A key saved after the cache was made is asked for at once, not tomorrow; a cache made with the keys is
    // left alone, and a removed key only hides its row.
    const std::string addedDir = directory.path() + "/added";
    assert(mkdir(addedDir.c_str(), 0700) == 0);
    asked.clear();
    {
        HomeDiscover titlesOnly(addedDir, fetcher, [&clock] { return clock; });
        (void)titlesOnly.feed("tmdb", "");
        assert(waitForAsked(3));
    }
    {
        HomeDiscover sameKeys(addedDir, fetcher, [&clock] { return clock; });
        assert(sameKeys.feed("tmdb", "")["rows"].size() == 3);
    }
    assert(asked.size() == 3);
    {
        HomeDiscover withYoutube(addedDir, fetcher, [&clock] { return clock; });
        assert(withYoutube.feed("tmdb", "youtube-key")["rows"].size() == 3); // no YouTube row in the cache yet
        assert(waitForAsked(7));
    }
    {
        HomeDiscover refreshed(addedDir, fetcher, [&clock] { return clock; });
        assert(refreshed.feed("tmdb", "youtube-key")["rows"].size() == 4);
        assert(refreshed.feed("tmdb", "")["rows"].size() == 3);
    }
    assert(asked.size() == 7);
    // With the TV's country, each streaming service has a row of what is popular on it there: its
    // films and series in turn, each marked with the service, whether or not it has a trailer; and
    // YouTube's trending is that country's.
    const std::string regionDir = directory.path() + "/region";
    assert(mkdir(regionDir.c_str(), 0700) == 0);
    asked.clear();
    {
        HomeDiscover regional(regionDir, fetcher, [&clock] { return clock; });
        regional.setLinkCheck([](const std::string &, const std::string &id) { return id == "1"; });
        (void)regional.feed("tmdb", "youtube-key", "IT");
        assert(waitForAsked(10)); // three trending rows, two lists for each of three services, YouTube
    }
    {
        bool askedPrime = false;
        for (const std::string &url : asked) {
            if (url.find("with_watch_providers=9%7C119") != std::string::npos) {
                askedPrime = true;
                assert(url.find("watch_region=IT") != std::string::npos);
                assert(url.find("with_watch_monetization_types=flatrate") != std::string::npos);
            }
            if (url.find("googleapis.com") != std::string::npos)
                assert(url.find("&regionCode=IT") != std::string::npos);
        }
        assert(askedPrime);
    }
    {
        HomeDiscover regionalCache(regionDir, fetcher, [&clock] { return clock; });
        const Json feed = regionalCache.feed("tmdb", "youtube-key", "IT");
        assert(feed["rows"].size() == 5); // Top picks, the three services, YouTube
        assert(feed["rows"][0]["key"].string() == "trending" && feed["rows"][0]["items"].size() == 1);
        assert(feed["rows"][1]["key"].string() == "on:netflix");
        const Json prime = feed["rows"][2];
        assert(prime["key"].string() == "on:prime" && prime["items"].size() == 6);
        const int order[] = {41, 51, 42, 52, 43, 53};
        for (size_t index = 0; index < 6; ++index) {
            assert(prime["items"][index]["id"].number() == order[index]);
            assert(prime["items"][index]["service"].string() == "prime");
        }
        assert(prime["items"][1]["type"].string() == "tv");
        assert(feed["rows"][3]["key"].string() == "on:disney");
    }
    assert(asked.size() == 10);
    {
        // Another country's rows are not this TV's: they are asked for again at once.
        HomeDiscover moved(regionDir, fetcher, [&clock] { return clock; });
        (void)moved.feed("tmdb", "youtube-key", "FR");
        assert(waitForAsked(20));
    }
    {
        // Without a country there are no service rows, and the cache made with one is replaced.
        HomeDiscover nowhere(regionDir, fetcher, [&clock] { return clock; });
        (void)nowhere.feed("tmdb", "youtube-key", "Italy");
        assert(waitForAsked(24));
    }
    {
        HomeDiscover nowhereCache(regionDir, fetcher, [&clock] { return clock; });
        const Json feed = nowhereCache.feed("tmdb", "youtube-key", "");
        for (const Json &row : feed["rows"].items())
            assert(row["key"].string().compare(0, 3, "on:") != 0);
    }
    assert(asked.size() == 24 && asked[23].find("regionCode") == std::string::npos);
    (void)unlink((regionDir + "/home-discover.json").c_str());
    (void)unlink((regionDir + "/home-discover.json.tmp").c_str());
    (void)rmdir(regionDir.c_str());

    // Each title's art with its name on it, as Google TV's cards have: TMDB's backdrop in English,
    // else in the country's language, never a plain one. A cache from before the lookups is
    // refreshed at once; then each title is asked about once a week, and a failed lookup again at
    // the next refresh.
    const std::string artDir = directory.path() + "/art";
    assert(mkdir(artDir.c_str(), 0700) == 0);
    std::vector<std::string> artUrls;
    bool artOffline = true;
    auto artFetcher = [&](const std::string &url, Json &document, std::string &error) {
        std::lock_guard<std::mutex> lock(askedMutex);
        artUrls.push_back(url);
        askedChanged.notify_all();
        if (url.find("/images?") == std::string::npos) {
            document = url.find("trending/all/week") != std::string::npos
                           ? titleReply(1) : Json::parse("{\"results\":[]}");
            return true;
        }
        if (artOffline && url.find("/movie/3/") != std::string::npos) {
            error = "offline";
            return false;
        }
        const char *plain = R"({"iso_639_1":null,"file_path":"/plain-abcdefghij.jpg"})";
        const char *italian = R"({"iso_639_1":"it","file_path":"/italian-abcdefg.jpg"})";
        const char *english = R"({"iso_639_1":"en","file_path":"/english-abcdefg.jpg"})";
        const std::string backdrops =
            url.find("/movie/1/") != std::string::npos   ? std::string(plain) + "," + italian + "," + english
            : url.find("/movie/2/") != std::string::npos ? std::string(plain) + "," + italian
                                                         : std::string(plain);
        document = Json::parse("{\"backdrops\":[" + backdrops + "]}");
        return true;
    };
    const auto artLookups = [&]() {
        size_t count = 0;
        for (const std::string &url : artUrls)
            if (url.find("/images?") != std::string::npos) ++count;
        return count;
    };
    const auto waitForArt = [&](size_t urls, size_t lookups) {
        std::unique_lock<std::mutex> lock(askedMutex);
        return askedChanged.wait_for(lock, std::chrono::seconds(2), [&] {
            return artUrls.size() >= urls && artLookups() >= lookups;
        });
    };
    {
        HomeDiscover plain(artDir, artFetcher, [&clock] { return clock; });
        (void)plain.feed("tmdb", "", "IT");
        assert(waitForArt(9, 0)); // three trending rows, two lists for each of three services
    }
    assert(artLookups() == 0);
    {
        HomeDiscover titled(artDir, artFetcher, [&clock] { return clock; });
        titled.setTitledArt(true);
        (void)titled.feed("tmdb", "", "IT");
        assert(waitForArt(21, 3));
    }
    assert(artLookups() == 3);
    for (const std::string &url : artUrls)
        if (url.find("/images?") != std::string::npos)
            assert(url.find("&include_image_language=en,it") != std::string::npos);
    {
        HomeDiscover titled(artDir, artFetcher, [&clock] { return clock; });
        titled.setTitledArt(true);
        const Json items = titled.feed("tmdb", "", "IT")["rows"][0]["items"];
        assert(items.size() == 3);
        assert(items[0]["art"].string() == "/english-abcdefg.jpg");
        assert(items[1]["art"].string() == "/italian-abcdefg.jpg");
        assert(!items[2].has("art"));
    }
    assert(artUrls.size() == 21);
    artOffline = false;
    clock += 24LL * 60 * 60 * 1000;
    {
        HomeDiscover nextDay(artDir, artFetcher, [&clock] { return clock; });
        nextDay.setTitledArt(true);
        (void)nextDay.feed("tmdb", "", "IT");
        assert(waitForArt(31, 4));
    }
    assert(artLookups() == 4);
    for (size_t index = 21; index < artUrls.size(); ++index)
        if (artUrls[index].find("/images?") != std::string::npos)
            assert(artUrls[index].find("/movie/3/images?") != std::string::npos);
    clock += 8LL * 24 * 60 * 60 * 1000;
    {
        HomeDiscover nextWeek(artDir, artFetcher, [&clock] { return clock; });
        nextWeek.setTitledArt(true);
        (void)nextWeek.feed("tmdb", "", "IT");
        assert(waitForArt(43, 7));
    }
    assert(artLookups() == 7);
    (void)unlink((artDir + "/home-discover.json").c_str());
    (void)unlink((artDir + "/home-discover.json.tmp").c_str());
    (void)rmdir(artDir.c_str());

    (void)unlink((addedDir + "/home-discover.json").c_str());
    (void)unlink((addedDir + "/home-discover.json.tmp").c_str());
    (void)rmdir(addedDir.c_str());
    (void)unlink((netflixDir + "/home-discover.json").c_str());
    (void)unlink((netflixDir + "/home-discover.json.tmp").c_str());
    (void)rmdir(netflixDir.c_str());
    (void)unlink((linkedDir + "/home-discover.json").c_str());
    (void)unlink((linkedDir + "/home-discover.json.tmp").c_str());
    (void)rmdir(linkedDir.c_str());

    (void)unlink((partialDir + "/home-discover.json").c_str());
    (void)unlink((partialDir + "/home-discover.json.tmp").c_str());
    (void)rmdir(partialDir.c_str());
    (void)unlink((youtubeDir + "/home-discover.json").c_str());
    (void)unlink((youtubeDir + "/home-discover.json.tmp").c_str());
    (void)rmdir(youtubeDir.c_str());
    return 0;
}
