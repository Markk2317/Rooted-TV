#include "service/screen_title.hpp"

#include <cassert>
#include <cstdio>

using nativetv::Json;
namespace screen_title = nativetv::service::screen_title;

namespace {
Json result(int id, const char *type, const char *title, const char *year) {
    Json item = Json::object();
    item.set("id", Json::numeric(id));
    item.set("type", Json::text(type));
    item.set("title", Json::text(title));
    item.set("year", Json::text(year));
    return item;
}
} // namespace

int main() {
    assert(screen_title::prompt("Netflix").find("the Netflix app") != std::string::npos);

    const Json seen = screen_title::parse(
        "```json\n{\"title\": \"Stranger Things\", \"type\": \"tv\", \"year\": 2016}\n```");
    assert(seen["title"].string() == "Stranger Things" && seen["type"].string() == "tv" &&
           seen["year"].string() == "2016");
    assert(!screen_title::parse("{}").valid());
    assert(!screen_title::parse("{\"title\": \"Menu\", \"type\": \"app\"}").valid());
    assert(!screen_title::parse("no title here").valid());
    assert(!screen_title::parse("{\"title\":\"Heat\",\"type\":\"movie\",\"year\":null}")["year"].isString());

    Json results = Json::array();
    results.add(result(1, "movie", "Stranger Things", "2010"));
    results.add(result(2, "tv", "Stranger Things", "2016"));
    results.add(result(3, "tv", "Stranger Things: Tales", "2024"));
    assert(screen_title::match(results, seen)["id"].number() == 2);
    Json noYear = Json::parse(seen.dump());
    noYear.set("year", Json());
    assert(screen_title::match(results, noYear)["id"].number() == 2); // its kind first
    Json film = screen_title::parse("{\"title\":\"stranger things:\",\"type\":\"movie\"}");
    assert(screen_title::match(results, film)["id"].number() == 1); // case and marks aside
    Json other = screen_title::parse("{\"title\":\"Dark\",\"type\":\"tv\"}");
    assert(!screen_title::match(results, other).valid());

    assert(screen_title::base64("") == "");
    assert(screen_title::base64("f") == "Zg==");
    assert(screen_title::base64("fo") == "Zm8=");
    assert(screen_title::base64("foo") == "Zm9v");
    assert(screen_title::base64(std::string("\xff\x00\x10", 3)) == "/wAQ");
    std::puts("screen title checks passed");
    return 0;
}
