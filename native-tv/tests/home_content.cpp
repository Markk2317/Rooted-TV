#include "service/home_content.hpp"
#include <cassert>
#include <cstdio>
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
        char pattern[] = "/tmp/nativetv-home-content-XXXXXX";
        char *created = mkdtemp(pattern);
        assert(created);
        path_ = created;
    }
    ~TemporaryDirectory() {
        (void)unlink((path_ + "/home-content.json").c_str());
        (void)unlink((path_ + "/home-content.json.tmp").c_str());
        (void)rmdir(path_.c_str());
    }
    const std::string &path() const { return path_; }

  private:
    std::string path_;
};

nativetv::Json rawTitle(int id, const std::string &type = "movie") {
    using nativetv::Json;
    Json raw = Json::object();
    raw.set("id", Json::numeric(id));
    raw.set("media_type", Json::text(type));
    raw.set("title", Json::text("Film " + std::to_string(id)));
    raw.set("name", Json::text("Series " + std::to_string(id)));
    raw.set("poster_path", Json::text("/abcdefghijklmnop.jpg"));
    raw.set("backdrop_path", Json::text("/ponmlkjihgfedcba.jpg"));
    raw.set("release_date", Json::text("2026-01-01"));
    raw.set("vote_average", Json::numeric(8.04));
    raw.set("vote_count", Json::numeric(100));
    raw.set("overview", Json::text("A story."));
    return raw;
}

bool lookupHasPreviews = true;
bool appleTvHasTrailer = false;

nativetv::Json responseFor(const std::string &url) {
    using nativetv::Json;
    // Apple's chart: the film by its title and a year off by one, a namesake from another decade,
    // and an entry with no preview.
    if (url.find("/rss/topmovies/") != std::string::npos) {
        const std::string preview = url.find("itunes.apple.com/it/") != std::string::npos
                                        ? "https://video-ssl.itunes.apple.com/it.m4v"
                                        : "https://video-ssl.itunes.apple.com/us.m4v";
        return Json::parse(
            "{\"feed\":{\"entry\":["
            "{\"im:name\":{\"label\":\"Film 52\"},\"im:releaseDate\":{\"label\":\"1990-01-01\"},"
            "\"link\":[{\"attributes\":{\"im:assetType\":\"preview\",\"href\":\"https://video-ssl.itunes.apple.com/old.m4v\"}}]},"
            "{\"im:name\":{\"label\":\"Film 51\"},\"im:releaseDate\":{\"label\":\"2026-02-01\"},"
            "\"link\":{\"attributes\":{\"rel\":\"alternate\",\"href\":\"https://itunes.apple.com/x\"}}},"
            "{\"im:name\":{\"label\":\"FILM 52 (2027)\"},\"im:releaseDate\":{\"label\":\"2027-03-01\"},"
            "\"id\":{\"attributes\":{\"im:id\":\"6793590242\"}},"
            "\"link\":[{\"attributes\":{\"rel\":\"alternate\"}},{\"attributes\":{\"im:assetType\":"
            "\"preview\",\"href\":\"" + preview + "\"}}]}]}}");
    }
    if (url.find("wikidata.org") != std::string::npos) {
        Json claims = Json::array();
        claims.add(Json::parse("null"));
        Json deprecated = Json::object();
        deprecated.set("rank", Json::text("deprecated"));
        deprecated.set("mainsnak", Json::parse(
            "{\"datavalue\":{\"value\":\"999999\"}}"));
        claims.add(deprecated);
        Json accepted = Json::object();
        accepted.set("mainsnak", Json::parse(
            "{\"datavalue\":{\"value\":\"80057281\"}}"));
        claims.add(accepted);
        Json entity = Json::object();
        entity.set("claims", Json::object());
        entity["claims"].set("P1874", claims);
        Json gtis = Json::array();
        gtis.add(Json::parse("{\"mainsnak\":{\"datavalue\":{\"value\":\"B00ABCDEFG\"}}}"));
        gtis.add(Json::parse("{\"mainsnak\":{\"datavalue\":{\"value\":"
                             "\"amzn1.dv.gti.afd5bf09-38ee-44f6-b4c0-78d991a104fd\"}}}"));
        entity["claims"].set("P14462", gtis);
        entity["claims"].set("P6398", Json::parse(
            "[{\"mainsnak\":{\"datavalue\":{\"value\":\"219214592\"}}}]"));
        Json entities = Json::object();
        entities.set("Q42", entity);
        Json result = Json::object();
        result.set("entities", entities);
        return result;
    }
    // Apple TV's page of a film, HTML that names its HLS trailer where it has one.
    if (url.compare(0, 21, "https://tv.apple.com/") == 0) {
        Json page = Json::object();
        page.set("page", Json::text(appleTvHasTrailer
                                        ? "<a href=\"https://play-edge.itunes.apple.com/WebObjects/MZPlayLocal.woa/hls/"
                                          "playlist.m3u8?cc=IT&amp;a=219214592&amp;id=7&amp;l=it-IT&amp;aec=HD\">"
                                        : "<html></html>"));
        return page;
    }
    // Apple's store: the Italian one has no preview of the film, the US one has, and a page
    // elsewhere is not taken.
    if (url.find("itunes.apple.com/lookup") != std::string::npos) {
        if (!lookupHasPreviews) return Json::parse("{\"resultCount\":0,\"results\":[]}");
        if (url.find("country=it") != std::string::npos)
            return Json::parse("{\"resultCount\":1,\"results\":[{\"previewUrl\":"
                               "\"https://example.com/preview.m4v\"}]}");
        return Json::parse("{\"resultCount\":1,\"results\":[{\"kind\":\"feature-movie\","
                           "\"previewUrl\":\"https://video-ssl.itunes.apple.com/itunes-assets/"
                           "Video128/clip.640x458.h264lc.U.p.m4v\"}]}");
    }
    // A genre: its two most popular films or series, which TMDB's discover lists without a kind.
    if (url.find("/discover/") != std::string::npos) {
        const bool series = url.find("/discover/tv") != std::string::npos;
        Json results = Json::array();
        for (int id : {series ? 31 : 21, series ? 32 : 22}) {
            Json raw = rawTitle(id, series ? "tv" : "movie");
            raw.set("media_type", Json::text(""));
            results.add(raw);
        }
        Json document = Json::object();
        document.set("results", results);
        return document;
    }
    if (url.find("search/multi") != std::string::npos) {
        Json results = Json::array();
        results.add(rawTitle(5));
        results.add(rawTitle(5));
        results.add(rawTitle(8, "tv"));
        Json person = rawTitle(9);
        person.set("media_type", Json::text("person"));
        results.add(person);
        Json adult = rawTitle(10);
        adult.set("adult", Json::flag(true));
        results.add(adult);
        Json document = Json::object();
        document.set("results", results);
        return document;
    }
    const size_t slash = url.find_last_of('/', url.find('?'));
    const size_t question = url.find('?', slash);
    const int id = std::atoi(url.substr(slash + 1, question - slash - 1).c_str());
    const std::string type = url.find("/tv/") != std::string::npos ? "tv" : "movie";
    Json document = rawTitle(id, type);
    if (type == "movie") document.set("runtime", Json::numeric(121));
    else document.set("episode_run_time", Json::parse("[44]"));
    document.set("genres", Json::parse(type == "movie"
        ? "[{\"id\":18,\"name\":\"Drama\"},{\"id\":12,\"name\":\"Adventure\"},{\"id\":4242,\"name\":\"Odd\"}]"
        : "[{\"id\":18,\"name\":\"Drama\"},{\"id\":10759,\"name\":\"Action & Adventure\"}]"));
    document.set("credits", Json::parse(
        "{\"cast\":[{\"name\":\"<Actor>\",\"character\":\"A role\",\"profile_path\":\"/actor.jpg\"}],"
        "\"crew\":[{\"name\":\"Writer\",\"job\":\"Writer\"},{\"name\":\"Maker\",\"job\":\"Director\"}]}"));
    document.set("external_ids", Json::parse("{\"wikidata_id\":\"Q42\"}"));
    document.set("videos", Json::parse(
        "{\"results\":[{\"site\":\"YouTube\",\"type\":\"Trailer\",\"key\":\"bad\"},"
        "{\"site\":\"Other\",\"type\":\"Trailer\",\"key\":\"dQw4w9WgXcQ\"},"
        "{\"site\":\"YouTube\",\"type\":\"Trailer\",\"official\":true,\"key\":\"dQw4w9WgXcQ\"}]}"));
    document.set("watch/providers", Json::parse(
        "{\"results\":{\"IT\":{\"flatrate\":[{\"provider_id\":119},{\"provider_id\":999},"
        "{\"provider_id\":8},{\"provider_id\":9}]},\"FR\":{\"flatrate\":[{\"provider_id\":337}]}}}"));
    document.set("similar", Json::parse("{\"results\":["
        "{\"id\":7,\"media_type\":\"movie\",\"poster_path\":\"/abcdefghijklmnop.jpg\"},"
        "{\"id\":20,\"media_type\":\"movie\",\"poster_path\":\"/abcdefghijklmnop.jpg\"},"
        "{\"id\":21,\"media_type\":\"movie\",\"poster_path\":\"/abcdefghijklmnop.jpg\"}]}"));
    return document;
}
} // namespace

int main() {
    using namespace nativetv;
    using namespace nativetv::service;
    TemporaryDirectory directory;
    int64_t clock = 1792930000000LL;
    std::mutex askedMutex;
    std::vector<std::string> asked;
    bool fail = false;
    auto fetcher = [&](const std::string &url, Json &document, std::string &error) {
        std::lock_guard<std::mutex> lock(askedMutex);
        asked.push_back(url);
        if (fail) {
            error = "offline";
            return false;
        }
        document = responseFor(url);
        return true;
    };

    HomeContent content(directory.path(), fetcher, [&clock] { return clock; });
    assert(!content.details("", false, "movie", "7")["enabled"].boolean());
    assert(!content.search("", "film")["enabled"].boolean());
    assert(!content.netflix("", true, "movie", "7")["enabled"].boolean());
    assert(asked.empty());

    assert(!content.details("key", false, "person", "7")["ok"].boolean());
    assert(!content.details("key", false, "movie", "../7")["ok"].boolean());
    assert(!content.details("key", false, "movie", "2147483648")["ok"].boolean());
    assert(!content.search("key", "a\nfilm")["ok"].boolean());
    assert(!content.search("key", std::string(81, 'x'))["ok"].boolean());
    assert(content.search("key", " a ")["items"].size() == 0);
    assert(asked.empty());

    const Json details = content.details("private-test-key", false, "movie", "7");
    assert(details["ok"].boolean() && details["enabled"].boolean());
    assert(!details["netflixEnabled"].boolean());
    const Json item = details["item"];
    assert(item["id"].number() == 7);
    assert(item["runtime"].number() == 121);
    assert(item["rating"].number() == 8.0);
    assert(item["genres"][0].string() == "Drama");
    // The Search page's genres it is in, for the details' rows: TMDB's Odd is none of them.
    assert(item["genreKeys"].size() == 2 && item["genreKeys"][0].string() == "drama" &&
           item["genreKeys"][1].string() == "adventure");
    // The director first, as Google TV's Cast & crew lists them, then the cast with their photos.
    assert(item["cast"][0]["name"].string() == "Maker" &&
           item["cast"][0]["character"].string() == "Director" &&
           item["cast"][0]["photo"].string().empty());
    assert(item["cast"][1]["name"].string() == "<Actor>" &&
           item["cast"][1]["photo"].string() == "/actor.jpg");
    assert(item["trailer"].string() == "dQw4w9WgXcQ");
    assert(item["wikidataId"].string() == "Q42");
    assert(item["similar"].size() == 2 && item["similar"][0]["id"].number() == 20);
    const size_t afterDetails = asked.size();
    assert(content.details("private-test-key", true, "movie", "7")["netflixEnabled"].boolean());
    assert(asked.size() == afterDetails);
    // A series' Action & Adventure is the grid's Action, its first genre with that series genre.
    const Json series = content.details("private-test-key", false, "tv", "8")["item"];
    assert(series["genreKeys"].size() == 2 && series["genreKeys"][0].string() == "drama" &&
           series["genreKeys"][1].string() == "action");

    const Json search = content.search("private-test-key", "secret phrase & /?");
    assert(search["items"].size() == 2);
    assert(search["items"][0]["id"].number() == 5);
    assert(search["items"][1]["id"].number() == 8);
    assert(asked.back().find("query=secret%20phrase%20%26%20%2F%3F") != std::string::npos);

    // A genre of Search's grid: its films, then its series, from TMDB's discover by genre ID.
    const Json scifi = content.genre("private-test-key", "scifi");
    assert(scifi["ok"].boolean() && scifi["items"].size() == 4);
    assert(scifi["items"][0]["type"].string() == "movie" && scifi["items"][0]["id"].number() == 21);
    assert(scifi["items"][2]["type"].string() == "tv" && scifi["items"][2]["id"].number() == 31);
    assert(asked[asked.size() - 2].find("discover/movie") != std::string::npos &&
           asked[asked.size() - 2].find("with_genres=878") != std::string::npos);
    assert(asked.back().find("discover/tv") != std::string::npos &&
           asked.back().find("with_genres=10765") != std::string::npos);
    const size_t afterGenre = asked.size();
    // History has no series genre on TMDB: only its films are asked for.
    assert(content.genre("private-test-key", "history")["items"].size() == 2);
    assert(asked.size() == afterGenre + 1 && asked.back().find("with_genres=36") != std::string::npos);
    assert(!content.genre("private-test-key", "nonsense")["ok"].boolean());
    assert(!content.genre("", "scifi")["enabled"].boolean());
    struct stat cacheInfo = {};
    assert(stat((directory.path() + "/home-content.json").c_str(), &cacheInfo) == 0);
    assert((cacheInfo.st_mode & 0777) == 0600);
    {
        std::ifstream input((directory.path() + "/home-content.json").c_str());
        std::string saved((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());
        assert(saved.find("secret phrase") == std::string::npos);
        assert(saved.find("private-test-key") == std::string::npos);
        const Json cache = Json::parse(saved);
        bool matchingSearchKey = false;
        for (const Json &entry : cache["entries"].items())
            matchingSearchKey = matchingSearchKey ||
                entry["key"].string() == "267247212e9822f0fe8a7eef55bb694a";
        assert(matchingSearchKey);
    }

    const size_t beforeNetflix = asked.size();
    const Json netflix = content.netflix("private-test-key", true, "movie", "7");
    assert(netflix["ok"].boolean() && netflix["netflixId"].string() == "80057281");
    assert(asked.size() == beforeNetflix + 1);
    assert(asked.back() == "https://www.wikidata.org/wiki/Special:EntityData/Q42.json");

    // Prime Video's GTI comes from the same entity, kept apart from the Netflix ID; an ID that is
    // not a GTI is passed over.
    assert(!content.prime("private-test-key", false, "movie", "7")["enabled"].boolean());
    const Json prime = content.prime("private-test-key", true, "movie", "7");
    assert(prime["ok"].boolean() &&
           prime["gti"].string() == "amzn1.dv.gti.afd5bf09-38ee-44f6-b4c0-78d991a104fd");
    assert(asked.size() == beforeNetflix + 2);
    assert(asked.back() == "https://www.wikidata.org/wiki/Special:EntityData/Q42.json");
    assert(content.netflix("private-test-key", true, "movie", "7")["netflixId"].string() == "80057281");
    assert(asked.size() == beforeNetflix + 2);

    const size_t beforeRestart = asked.size();
    HomeContent restarted(directory.path(), fetcher, [&clock] { return clock; });
    assert(restarted.details("private-test-key", false, "movie", "7")["ok"].boolean());
    assert(restarted.netflix("private-test-key", true, "movie", "7")["netflixId"].string() == "80057281");
    assert(asked.size() == beforeRestart);

    clock += 24 * 60 * 60 * 1000LL + 1;
    fail = true;
    const size_t beforeFailure = asked.size();
    const Json stale = content.details("private-test-key", false, "movie", "7");
    assert(stale["ok"].boolean() && stale["stale"].boolean());
    assert(asked.size() == beforeFailure + 1);
    (void)content.details("private-test-key", false, "movie", "7");
    assert(asked.size() == beforeFailure + 1);
    fail = false;

    for (int index = 0; index < 45; ++index)
        assert(content.search("private-test-key", "query " + std::to_string(index))["ok"].boolean());
    {
        std::ifstream input((directory.path() + "/home-content.json").c_str());
        std::string saved((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());
        const Json cache = Json::parse(saved);
        assert(cache["version"].number() == 3);
        assert(cache["entries"].size() == 40);
    }
    // With the TV's country, a title's details say which services have it there, in TMDB's order,
    // once each, and the answer is kept apart from another country's.
    {
        HomeContent regional(directory.path(), fetcher, [&clock] { return clock; });
        assert(regional.details("private-test-key", false, "movie", "31")["item"]["services"].size() == 0);
        regional.setRegion("IT");
        const Json italian = regional.details("private-test-key", false, "movie", "31");
        assert(asked.back().find("watch/providers&watch_region=IT") != std::string::npos);
        const Json services = italian["item"]["services"];
        assert(services.size() == 2 && services[0].string() == "prime" &&
               services[1].string() == "netflix");
        regional.setRegion("FR");
        assert(regional.details("private-test-key", false, "movie", "31")["item"]["services"][0].string() ==
               "disney");
        regional.setRegion("italy");
        assert(regional.details("private-test-key", false, "movie", "31")["item"]["services"].size() == 0);
    }
    // A film's trailer: the iTunes id from Wikidata, looked up in the TV's country and then in the
    // US store; Apple's servers only.
    {
        // Newer than the entries above, so the full cache drops those first.
        clock += 1000;
        HomeContent trailers(directory.path(), fetcher, [&clock] { return clock; });
        assert(!trailers.trailer("", "41")["enabled"].boolean());
        trailers.setRegion("IT");
        const Json found = trailers.trailer("private-test-key", "41");
        assert(found["ok"].boolean() &&
               found["url"].string() == "https://video-ssl.itunes.apple.com/itunes-assets/"
                                        "Video128/clip.640x458.h264lc.U.p.m4v");
        assert(asked.back() == "https://itunes.apple.com/lookup?id=219214592&country=us");
        assert(asked[asked.size() - 2] == "https://itunes.apple.com/lookup?id=219214592&country=it");
        const size_t before = asked.size();
        assert(trailers.trailer("private-test-key", "41")["url"].string() == found["url"].string());
        assert(asked.size() == before);
        assert(trailers.trailer("private-test-key", "abc")["url"].string().empty());
        // With no preview through Wikidata, the TV's country's chart has the film by title and
        // year; a film it does not list has none.
        lookupHasPreviews = false;
        clock += 4LL * 24 * 60 * 60 * 1000; // past the lookups' three days
        assert(trailers.trailer("private-test-key", "52")["url"].string() ==
               "https://video-ssl.itunes.apple.com/it.m4v");
        // The chart's store id is asked of Apple TV first, which has no trailer here.
        assert(asked.back() == "https://tv.apple.com/it/movie/id6793590242");
        assert(asked[asked.size() - 2] == "https://itunes.apple.com/it/rss/topmovies/limit=100/json");
        assert(trailers.trailer("private-test-key", "51")["url"].string().empty());
        assert(asked.back() == "https://itunes.apple.com/us/rss/topmovies/limit=100/json");
        // Apple TV's page of the film, in the TV's country, names its HLS trailer, which goes
        // before the iTunes Store's preview.
        lookupHasPreviews = true;
        appleTvHasTrailer = true;
        clock += 4LL * 24 * 60 * 60 * 1000;
        assert(trailers.trailer("private-test-key", "41")["url"].string() ==
               "https://play-edge.itunes.apple.com/WebObjects/MZPlayLocal.woa/hls/"
               "playlist.m3u8?cc=IT&a=219214592&id=7&l=it-IT&aec=HD");
        assert(asked.back() == "https://tv.apple.com/it/movie/id219214592");
    }
    return 0;
}
