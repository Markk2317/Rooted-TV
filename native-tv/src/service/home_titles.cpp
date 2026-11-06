#include "service/home_titles.hpp"
#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace nativetv {
namespace service {
namespace {
const size_t kWatchlistMax = 60;
const size_t kLikedMax = 30;
const size_t kDislikedMax = 300;
const size_t kSimilarMax = 12;

// At most `maximum` characters, never cutting one in two.
std::string utf8Prefix(const std::string &text, size_t maximum) {
    size_t offset = 0, characters = 0;
    while (offset < text.size() && characters < maximum) {
        const unsigned char first = static_cast<unsigned char>(text[offset]);
        size_t width = (first & 0xe0) == 0xc0 ? 2 : (first & 0xf0) == 0xe0 ? 3
                     : (first & 0xf8) == 0xf0 ? 4 : 1;
        if (offset + width > text.size()) break;
        offset += width;
        ++characters;
    }
    return text.substr(0, offset);
}

std::string idOf(const Json &value) {
    std::string id = value.isNumber() ? compactNumber(value.number()) : value.string();
    if (id.empty() || id.size() > 10 || id[0] < '1' || id[0] > '9' ||
        !std::all_of(id.begin(), id.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
        return "";
    return id;
}

// A TMDB image path as the feed gives it: "/abc123.jpg".
std::string imagePath(const Json &value) {
    const std::string path = value.string();
    const bool valid = path.size() > 1 && path.size() <= 64 && path[0] == '/' &&
                       std::all_of(path.begin() + 1, path.end(), [](unsigned char c) {
                           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                  (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
                       });
    return valid ? path : "";
}

Json copyOf(const Json &value) { return value.valid() ? Json::parse(value.dump()) : Json(); }

// The list without the title `key`, newest first, then `item` in front when it is given.
Json withFront(const Json &list, const std::string &key, const Json &item, size_t maximum) {
    Json result = Json::array();
    if (item.valid()) result.add(item);
    for (const Json &entry : list.items()) {
        if (result.size() >= maximum) break;
        const std::string entryKey = entry.isString() ? entry.string() : HomeTitles::keyOf(entry);
        if (entryKey != key) result.add(copyOf(entry));
    }
    return result;
}

Json emptyProfile() {
    Json profile = Json::object();
    profile.set("watchlist", Json::array());
    profile.set("liked", Json::array());
    profile.set("disliked", Json::array());
    return profile;
}
} // namespace

HomeTitles::HomeTitles(std::string dataRoot)
    : path_((dataRoot.empty() ? "/var/lib/tvweb" : std::move(dataRoot)) + "/home-titles.json") {}

Json HomeTitles::trimmed(const Json &item) {
    if (!item.isObject()) return Json();
    const std::string id = idOf(item["id"]);
    const std::string type = item["type"].string();
    const std::string title = utf8Prefix(item["title"].string(), 200);
    if (id.empty() || (type != "movie" && type != "tv") || title.empty()) return Json();
    Json result = Json::object();
    result.set("id", Json::numeric(std::stod(id)));
    result.set("type", Json::text(type));
    result.set("title", Json::text(title));
    const std::string year = item["year"].string();
    result.set("year", Json::text(year.size() == 4 && std::all_of(year.begin(), year.end(),
        [](unsigned char c) { return c >= '0' && c <= '9'; }) ? year : ""));
    const Json rating = item["rating"];
    result.set("rating", rating.isNumber() && rating.number() >= 0 && rating.number() <= 10
                             ? Json::numeric(std::round(rating.number() * 10) / 10) : Json());
    result.set("overview", Json::text(utf8Prefix(item["overview"].string(), 280)));
    result.set("poster", Json::text(imagePath(item["poster"])));
    result.set("backdrop", Json::text(imagePath(item["backdrop"])));
    return result;
}

std::string HomeTitles::keyOf(const Json &item) {
    const std::string id = idOf(item["id"]);
    const std::string type = item["type"].string();
    return id.empty() || (type != "movie" && type != "tv") ? "" : type + ":" + id;
}

void HomeTitles::loadLocked() {
    if (loaded_) return;
    loaded_ = true;
    profiles_ = Json::object();
    std::ifstream input(path_.c_str(), std::ios::binary);
    if (!input) return;
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const Json parsed = Json::parse(contents);
    if (parsed.isObject()) profiles_ = parsed;
}

void HomeTitles::saveLocked() {
    const std::string temporary = path_ + ".tmp";
    const std::string contents = profiles_.dump();
    const int fd = open(temporary.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) return;
    size_t written = 0;
    bool ok = true;
    while (written < contents.size()) {
        const ssize_t count = write(fd, contents.data() + written, contents.size() - written);
        if (count <= 0) { ok = false; break; }
        written += static_cast<size_t>(count);
    }
    if (close(fd) != 0) ok = false;
    if (!ok || rename(temporary.c_str(), path_.c_str()) != 0) (void)unlink(temporary.c_str());
}

Json HomeTitles::personalRows(const Json &rows, const Json &state) {
    std::vector<std::string> disliked;
    for (const Json &key : state["disliked"].items()) disliked.push_back(key.string());
    const auto wanted = [&disliked](const Json &item) {
        const std::string key = keyOf(item);
        return key.empty() || std::find(disliked.begin(), disliked.end(), key) == disliked.end();
    };
    const auto filtered = [&wanted](const Json &items) {
        Json kept = Json::array();
        for (const Json &item : items.items())
            if (wanted(item)) kept.add(item);
        return kept;
    };
    Json result = Json::array();
    for (const Json &row : rows.items()) {
        Json copy = Json::object();
        for (const std::string &name : row.keys())
            if (name != "items") copy.set(name, row[name]);
        copy.set("items", filtered(row["items"]));
        if (copy["items"].size()) result.add(copy);
    }
    const Json watchlist = filtered(state["watchlist"]);
    if (watchlist.size()) {
        Json row = Json::object();
        row.set("key", Json::text("watchlist"));
        row.set("items", watchlist);
        result.add(row);
    }
    for (const Json &liked : state["liked"].items()) {
        const Json similar = filtered(liked["similar"]);
        if (!similar.size()) continue;
        Json row = Json::object();
        row.set("key", Json::text("liked"));
        row.set("title", Json::text("Because you liked " + liked["title"].string()));
        row.set("items", similar);
        result.add(row);
        break;
    }
    return result;
}

Json HomeTitles::summary(const Json &state) {
    Json result = Json::object();
    for (const char *list : {"watchlist", "liked", "disliked"}) {
        Json keys = Json::array();
        for (const Json &entry : state[list].items())
            keys.add(Json::text(entry.isString() ? entry.string() : keyOf(entry)));
        result.set(list, keys);
    }
    return result;
}

Json HomeTitles::state(const std::string &profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    loadLocked();
    Json result = emptyProfile();
    const Json stored = profiles_[profile];
    for (const char *list : {"watchlist", "liked", "disliked"})
        if (stored[list].isArray()) result.set(list, copyOf(stored[list]));
    return result;
}

bool HomeTitles::apply(const std::string &profile, const std::string &op, const Json &item,
                       std::string &error) {
    const Json title = trimmed(item);
    const std::string key = keyOf(item);
    if (!title.valid() || key.empty()) {
        error = "that is not a film or series";
        return false;
    }
    if (op != "watch" && op != "unwatch" && op != "like" && op != "dislike" && op != "unrate") {
        error = "op must be watch, unwatch, like, dislike or unrate";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    loadLocked();
    Json current = emptyProfile();
    for (const char *list : {"watchlist", "liked", "disliked"})
        if (profiles_[profile][list].isArray()) current.set(list, copyOf(profiles_[profile][list]));
    if (op == "watch" || op == "unwatch")
        current.set("watchlist", withFront(current["watchlist"], key,
                                           op == "watch" ? title : Json(), kWatchlistMax));
    if (op == "like" || op == "dislike" || op == "unrate") {
        Json liked;
        if (op == "like") {
            liked = copyOf(title);
            Json similar = Json::array();
            for (const Json &entry : item["similar"].items()) {
                const Json other = trimmed(entry);
                if (other.valid() && keyOf(other) != key) similar.add(other);
                if (similar.size() == kSimilarMax) break;
            }
            liked.set("similar", similar);
        }
        current.set("liked", withFront(current["liked"], key, liked, kLikedMax));
        current.set("disliked", withFront(current["disliked"], key,
                                          op == "dislike" ? Json::text(key) : Json(),
                                          kDislikedMax));
    }
    profiles_.set(profile, current);
    saveLocked();
    return true;
}

} // namespace service
} // namespace nativetv
