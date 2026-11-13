#include "service/home_content.hpp"
#include "core/md5.hpp"
#include "domain/names.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <curl/curl.h>

namespace rootedtv {
namespace service {
namespace {
const int64_t kDayMs = 24LL * 60 * 60 * 1000;
const int64_t kRetryMs = 10LL * 60 * 1000;
const size_t kMaximumCacheEntries = 40;
const int kCacheVersion = 3; // home-content.json's format; see loadCacheLocked
const size_t kMaximumDocumentBytes = 8 * 1024 * 1024;
const char *const kTmdbApi = "https://api.themoviedb.org/3/";
const char *const kEnglish = "en-US";

struct CodePoint {
    uint32_t value;
    size_t width;
};

CodePoint decode(const std::string &text, size_t offset) {
    const unsigned char first = static_cast<unsigned char>(text[offset]);
    if (first < 0x80) return {first, 1};
    if ((first & 0xe0) == 0xc0 && offset + 1 < text.size()) {
        const unsigned char second = static_cast<unsigned char>(text[offset + 1]);
        if ((second & 0xc0) == 0x80)
            return {static_cast<uint32_t>(((first & 0x1f) << 6) | (second & 0x3f)), 2};
    }
    if ((first & 0xf0) == 0xe0 && offset + 2 < text.size()) {
        const unsigned char second = static_cast<unsigned char>(text[offset + 1]);
        const unsigned char third = static_cast<unsigned char>(text[offset + 2]);
        if ((second & 0xc0) == 0x80 && (third & 0xc0) == 0x80)
            return {static_cast<uint32_t>(((first & 0x0f) << 12) |
                                          ((second & 0x3f) << 6) | (third & 0x3f)), 3};
    }
    if ((first & 0xf8) == 0xf0 && offset + 3 < text.size()) {
        const unsigned char second = static_cast<unsigned char>(text[offset + 1]);
        const unsigned char third = static_cast<unsigned char>(text[offset + 2]);
        const unsigned char fourth = static_cast<unsigned char>(text[offset + 3]);
        if ((second & 0xc0) == 0x80 && (third & 0xc0) == 0x80 &&
            (fourth & 0xc0) == 0x80)
            return {static_cast<uint32_t>(((first & 7) << 18) |
                                          ((second & 0x3f) << 12) |
                                          ((third & 0x3f) << 6) | (fourth & 0x3f)), 4};
    }
    return {first, 1};
}

bool jsWhitespace(uint32_t value) {
    return (value >= 0x0009 && value <= 0x000d) || value == 0x0020 ||
           value == 0x00a0 || value == 0x1680 ||
           (value >= 0x2000 && value <= 0x200a) || value == 0x2028 ||
           value == 0x2029 || value == 0x202f || value == 0x205f ||
           value == 0x3000 || value == 0xfeff;
}

size_t utf16Length(const std::string &text) {
    size_t length = 0;
    for (size_t offset = 0; offset < text.size();) {
        const CodePoint point = decode(text, offset);
        length += point.value > 0xffff ? 2 : 1;
        offset += point.width;
    }
    return length;
}

std::string utf16Prefix(const std::string &text, size_t maximumUnits) {
    size_t end = 0;
    size_t units = 0;
    while (end < text.size()) {
        const CodePoint point = decode(text, end);
        const size_t nextUnits = point.value > 0xffff ? 2 : 1;
        if (units + nextUnits > maximumUnits) break;
        units += nextUnits;
        end += point.width;
    }
    return text.substr(0, end);
}

std::string trim(const std::string &text) {
    size_t begin = 0;
    while (begin < text.size()) {
        const CodePoint point = decode(text, begin);
        if (!jsWhitespace(point.value)) break;
        begin += point.width;
    }
    size_t last = begin;
    for (size_t offset = begin; offset < text.size();) {
        const CodePoint point = decode(text, offset);
        offset += point.width;
        if (!jsWhitespace(point.value)) last = offset;
    }
    return text.substr(begin, last - begin);
}

std::string jsString(const Json &value) {
    if (!value.valid()) return "";
    if (value.isString()) return value.string();
    if (value.isBoolean()) return value.boolean() ? "true" : "false";
    if (value.isNumber()) {
        std::ostringstream out;
        out << std::setprecision(std::numeric_limits<double>::digits10 + 1)
            << value.number();
        return out.str();
    }
    if (value.isObject()) return "[object Object]";
    std::string result;
    const std::vector<Json> items = value.items();
    for (size_t index = 0; index < items.size(); ++index) {
        if (index) result.push_back(',');
        if (items[index].valid()) result += jsString(items[index]);
    }
    return result;
}

bool truthy(const Json &value) {
    if (!value.valid()) return false;
    if (value.isBoolean()) return value.boolean();
    if (value.isNumber()) return value.number() != 0;
    if (value.isString()) return !value.string().empty();
    return true;
}

bool strictEqual(const Json &left, const Json &right) {
    if (left.isNumber() && right.isNumber()) return left.number() == right.number();
    if (left.isString() && right.isString()) return left.string() == right.string();
    if (left.isBoolean() && right.isBoolean()) return left.boolean() == right.boolean();
    return !left.valid() && !right.valid();
}

std::string urlEncode(const std::string &text) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;
    for (unsigned char byte : text) {
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
            byte == '.' || byte == '!' || byte == '~' || byte == '*' ||
            byte == '\'' || byte == '(' || byte == ')') {
            encoded << static_cast<char>(byte);
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0')
                    << static_cast<unsigned>(byte);
        }
    }
    return encoded.str();
}

bool validHexKey(const std::string &key) {
    return key.size() == 32 && std::all_of(key.begin(), key.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

Json disabled() {
    Json result = Json::object();
    result.set("ok", Json::flag(true));
    result.set("enabled", Json::flag(false));
    return result;
}

Json failed(const std::string &message) {
    Json result = Json::object();
    result.set("ok", Json::flag(false));
    result.set("enabled", Json::flag(true));
    result.set("error", Json::text(message));
    return result;
}

bool validTitleId(const std::string &id) {
    if (id.empty() || id.size() > 10 || id[0] < '1' || id[0] > '9') return false;
    if (!std::all_of(id.begin(), id.end(), [](unsigned char c) {
            return c >= '0' && c <= '9';
        })) return false;
    char *end = nullptr;
    const long long value = std::strtoll(id.c_str(), &end, 10);
    return end && *end == '\0' && value <= 2147483647LL;
}

bool wikidataId(const std::string &id) {
    return id.size() >= 2 && id.size() <= 13 && id[0] == 'Q' && id[1] >= '1' &&
        id[1] <= '9' && std::all_of(id.begin() + 2, id.end(), [](unsigned char c) {
            return c >= '0' && c <= '9';
        });
}

bool netflixId(const std::string &id) {
    return id.size() >= 5 && id.size() <= 12 && id[0] >= '1' && id[0] <= '9' &&
        std::all_of(id.begin() + 1, id.end(), [](unsigned char c) {
            return c >= '0' && c <= '9';
        });
}

// amzn1.dv.gti. and a lower-case UUID, as Prime Video's app takes it in contentTarget.
bool primeGti(const std::string &id) {
    const std::string prefix = "amzn1.dv.gti.";
    if (id.size() != prefix.size() + 36 || id.compare(0, prefix.size(), prefix) != 0)
        return false;
    for (size_t index = 0; index < 36; ++index) {
        const char character = id[prefix.size() + index];
        const bool dash = index == 8 || index == 13 || index == 18 || index == 23;
        if (dash ? character != '-'
                 : !((character >= '0' && character <= '9') ||
                     (character >= 'a' && character <= 'f')))
            return false;
    }
    return true;
}

// The digits of an Apple store id (Wikidata's iTunes movie ID).
bool itunesId(const std::string &id) {
    return !id.empty() && id.size() <= 12 && id[0] >= '1' && id[0] <= '9' &&
        std::all_of(id.begin() + 1, id.end(), [](unsigned char c) {
            return c >= '0' && c <= '9';
        });
}

// A preview on Apple's servers (video-ssl.itunes.apple.com), over https.
// Apple TV's page of a film names its trailer's HLS playlist, as the address of Apple's player:
// the first one on the page is the film's own; "" when the page has none.
std::string appleTvPlaylist(const std::string &page) {
    static const std::string kPlayer =
        "https://play-edge.itunes.apple.com/WebObjects/MZPlayLocal.woa/hls/playlist.m3u8?";
    const size_t at = page.find(kPlayer);
    if (at == std::string::npos) return "";
    size_t end = at + kPlayer.size();
    while (end < page.size() && end - at < 512 &&
           std::string("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789&;=._%-")
                   .find(page[end]) != std::string::npos)
        ++end;
    std::string url = page.substr(at, end - at);
    for (size_t entity = url.find("&amp;"); entity != std::string::npos; entity = url.find("&amp;", entity + 1))
        url.erase(entity + 1, 4);
    return url.find(';') == std::string::npos ? url : "";
}
bool applePreview(const std::string &url) {
    const std::string scheme = "https://";
    if (url.compare(0, scheme.size(), scheme) != 0 || url.size() > 512) return false;
    const size_t slash = url.find('/', scheme.size());
    if (slash == std::string::npos) return false;
    const std::string host = url.substr(scheme.size(), slash - scheme.size());
    const std::string domain = ".apple.com";
    return host.size() > domain.size() &&
        host.compare(host.size() - domain.size(), domain.size(), domain) == 0 &&
        host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-") == std::string::npos &&
        url.find_first_of(" \"\\<>\r\n") == std::string::npos;
}

// A title as Apple's charts and TMDB both write it: lower case, letters and digits, single spaces,
// "&" as "and", without a trailing "(2026)".
std::string titleKey(const std::string &title) {
    std::string text = title;
    const size_t open = text.rfind(" (");
    if (open != std::string::npos && text.size() == open + 7 && text.back() == ')' &&
        std::all_of(text.begin() + open + 2, text.end() - 1,
                    [](unsigned char c) { return c >= '0' && c <= '9'; }))
        text.erase(open);
    std::string key;
    for (size_t index = 0; index < text.size(); ++index) {
        const unsigned char c = static_cast<unsigned char>(text[index]);
        std::string piece;
        if (c >= 'A' && c <= 'Z') piece = static_cast<char>(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80) piece = static_cast<char>(c);
        else if (c == '&') piece = " and ";
        else if (c == '\'') continue;
        else piece = " ";
        for (char out : piece) {
            if (out == ' ' && (key.empty() || key.back() == ' ')) continue;
            key += out;
        }
    }
    while (!key.empty() && key.back() == ' ') key.pop_back();
    return key;
}

// Apple's chart of the films bought and rented now (the iTunes RSS feed): each one's title key,
// year and preview, where it has one.
Json chartValue(const Json &document) {
    Json entries = document["feed"]["entry"];
    if (entries.isObject()) {
        Json one = Json::array();
        one.add(entries);
        entries = one;
    }
    if (!entries.isArray()) return Json();
    Json films = Json::array();
    for (const Json &entry : entries.items()) {
        Json links = entry["link"];
        if (links.isObject()) {
            Json one = Json::array();
            one.add(links);
            links = one;
        }
        std::string url;
        for (const Json &link : links.items())
            if (link["attributes"]["im:assetType"].string() == "preview" &&
                applePreview(link["attributes"]["href"].string()))
                url = link["attributes"]["href"].string();
        const std::string name = titleKey(entry["im:name"]["label"].string());
        if (url.empty() || name.empty()) continue;
        Json film = Json::object();
        film.set("name", Json::text(name));
        const std::string store = entry["id"]["attributes"]["im:id"].string();
        if (itunesId(store)) film.set("id", Json::text(store));
        film.set("year", Json::text(entry["im:releaseDate"]["label"].string().substr(0, 4)));
        film.set("url", Json::text(url));
        films.add(film);
    }
    Json result = Json::object();
    result.set("ok", Json::flag(true));
    result.set("enabled", Json::flag(true));
    result.set("films", films);
    return result;
}

std::string titleType(const Json &raw, const std::string &fallback) {
    const Json media = raw["media_type"];
    const std::string type = truthy(media) ? jsString(media) : fallback;
    return type == "movie" || type == "tv" ? type : std::string();
}

Json trimTitle(const Json &raw, const std::string &fallbackType) {
    if (!raw.isObject() || truthy(raw["adult"])) return Json();
    const std::string type = titleType(raw, fallbackType);
    const Json id = raw["id"];
    if (type.empty() || !truthy(id) ||
        (!truthy(raw["poster_path"]) && !truthy(raw["backdrop_path"])))
        return Json();
    const std::string date = jsString(truthy(raw["release_date"])
                                      ? raw["release_date"] : raw["first_air_date"]);
    const bool hasYear = date.size() >= 4 &&
        std::all_of(date.begin(), date.begin() + 4, [](unsigned char c) {
            return c >= '0' && c <= '9';
        });
    const Json average = raw["vote_average"];
    const Json votes = raw["vote_count"];
    double voteCount = votes.number(-1);
    if (votes.isString()) {
        const std::string voteCountText = votes.string();
        char *end = nullptr;
        voteCount = std::strtod(voteCountText.c_str(), &end);
        if (!end || *end != '\0') voteCount = -1;
    }
    Json item = Json::object();
    item.set("id", id);
    item.set("type", Json::text(type));
    item.set("title", Json::text(
        jsString(truthy(raw["title"]) ? raw["title"] : raw["name"])));
    item.set("year", Json::text(hasYear ? date.substr(0, 4) : ""));
    item.set("rating", average.isNumber() && voteCount >= 20
                            ? Json::numeric(std::round(average.number() * 10) / 10)
                            : Json::parse("null"));
    item.set("overview", Json::text(utf16Prefix(jsString(raw["overview"]), 280)));
    item.set("poster", Json::text(jsString(truthy(raw["poster_path"])
                                                ? raw["poster_path"] : Json())));
    item.set("backdrop", Json::text(jsString(truthy(raw["backdrop_path"])
                                                   ? raw["backdrop_path"] : Json())));
    return item;
}

Json trimRows(const Json &results, const std::string &type) {
    Json output = Json::array();
    std::vector<std::string> seen;
    if (!results.isArray()) return output;
    for (const Json &raw : results.items()) {
        Json item = trimTitle(raw, type);
        if (!item.valid()) continue;
        const std::string key = item["type"].string() + jsString(item["id"]);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        if (output.size() >= 16) break;
        seen.push_back(key);
        output.add(item);
    }
    return output;
}

// The streaming services of names::streamingServices that TMDB's watch providers (JustWatch's)
// list for `region` with a subscription, in TMDB's order: ["netflix", "prime"].
Json servicesIn(const Json &document, const std::string &region) {
    Json services = Json::array();
    if (region.empty()) return services;
    std::vector<std::string> seen;
    for (const Json &provider : document["watch/providers"]["results"][region]["flatrate"].items()) {
        const std::string providerId = jsString(provider["provider_id"]);
        for (const names::StreamingService &service : names::streamingServices()) {
            const std::string ids = "|" + service.providers + "|";
            if (providerId.empty() || ids.find("|" + providerId + "|") == std::string::npos ||
                std::find(seen.begin(), seen.end(), service.key) != seen.end())
                continue;
            seen.push_back(service.key);
            services.add(Json::text(service.key));
        }
    }
    return services;
}

Json detailsValue(const Json &document, const std::string &type,
                  const std::string &id, const std::string &region) {
    if (!document.isObject() || truthy(document["adult"])) return Json();
    const std::string documentIdText = jsString(document["id"]);
    char *end = nullptr;
    const double documentId = std::strtod(documentIdText.c_str(), &end);
    if (!end || *end != '\0' || documentId != std::strtod(id.c_str(), nullptr))
        return Json();
    Json item = trimTitle(document, type);
    if (!item.valid()) return Json();
    item.set("overview", Json::text(utf16Prefix(jsString(document["overview"]), 2400)));
    // For matching the title in Apple's charts, whose names are the store's own.
    item.set("originalTitle", Json::text(jsString(type == "movie" ? document["original_title"]
                                                                  : document["original_name"])));
    // As it is called in the TV's country, for LG's catalogue, which names titles that way.
    std::string localTitle;
    for (const Json &translation : document["translations"]["translations"].items())
        if (!region.empty() && translation["iso_3166_1"].string() == region) {
            localTitle = utf16Prefix(jsString(translation["data"][type == "movie" ? "title" : "name"]), 200);
            if (!localTitle.empty()) break;
        }
    item.set("localTitle", Json::text(localTitle));
    Json runtime = Json::parse("null");
    if (type == "movie") {
        const Json minutes = document["runtime"];
        if (minutes.isNumber() && minutes.number() > 0 && minutes.number() < 2000)
            runtime = Json::numeric(std::round(minutes.number()));
    } else {
        Json minutes = document["episode_run_time"][0];
        if (!minutes.isNumber()) minutes = document["last_episode_to_air"]["runtime"];
        if (minutes.isNumber() && minutes.number() > 0 && minutes.number() < 2000)
            runtime = Json::numeric(std::round(minutes.number()));
    }
    item.set("runtime", runtime);
    const Json seasons = document["number_of_seasons"];
    item.set("seasons", type == "tv" && seasons.isNumber()
                             ? seasons : Json::parse("null"));

    Json genres = Json::array();
    if (document["genres"].isArray()) {
        const std::vector<Json> values = document["genres"].items();
        const size_t count = std::min<size_t>(6, values.size());
        for (size_t index = 0; index < count; ++index) {
            const Json &genre = values[index];
            const std::string name = utf16Prefix(
                jsString(truthy(genre["name"]) ? genre["name"] : Json()), 60);
            if (!name.empty()) genres.add(Json::text(name));
        }
    }
    item.set("genres", genres);
    // The Search page's genres the title is in, for its details' rows of each: one key for each of
    // TMDB's genres, in TMDB's order (a series' Action & Adventure is Action, not Adventure too).
    Json genreKeys = Json::array();
    std::vector<int> genreIds;
    for (const Json &genre : document["genres"].items()) {
        if (!genre["id"].isNumber()) continue;
        const int genreId = static_cast<int>(genre["id"].number());
        if (std::find(genreIds.begin(), genreIds.end(), genreId) != genreIds.end()) continue;
        for (const names::SearchGenre &entry : names::searchGenres())
            if ((type == "movie" ? entry.film : entry.series) == genreId) {
                genreIds.push_back(genreId);
                genreKeys.add(Json::text(entry.key));
                break;
            }
    }
    item.set("genreKeys", genreKeys);

    // Google TV's Cast & crew: the director first, then the cast, each with their TMDB photo.
    Json cast = Json::array();
    const auto addCredit = [&cast](const Json &person, const std::string &role) {
        const std::string name = utf16Prefix(
            jsString(truthy(person["name"]) ? person["name"] : Json()), 100);
        if (name.empty() || cast.size() >= 12) return;
        Json credit = Json::object();
        credit.set("name", Json::text(name));
        credit.set("character", Json::text(utf16Prefix(role, 100)));
        const std::string photo = jsString(person["profile_path"]);
        credit.set("photo", Json::text(photo.size() > 5 && photo.size() < 40 && photo[0] == '/'
                                           ? photo : ""));
        cast.add(credit);
    };
    for (const Json &person : document["credits"]["crew"].items())
        if (person["job"].string() == "Director") {
            addCredit(person, "Director");
            break;
        }
    for (const Json &person : document["credits"]["cast"].items())
        addCredit(person, jsString(truthy(person["character"]) ? person["character"] : Json()));
    item.set("cast", cast);

    std::vector<Json> videos;
    const Json rawVideos = document["videos"]["results"];
    if (rawVideos.isArray()) {
        for (const Json &video : rawVideos.items()) {
            const std::string site = video["site"].string();
            const std::string videoType = video["type"].string();
            const std::string key = video["key"].string();
            const bool validKey = key.size() == 11 &&
                std::all_of(key.begin(), key.end(), [](unsigned char c) {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '_' || c == '-';
                });
            if (site == "YouTube" && validKey &&
                (videoType == "Trailer" || videoType == "Teaser"))
                videos.push_back(video);
        }
    }
    std::stable_sort(videos.begin(), videos.end(), [](const Json &left, const Json &right) {
        const auto weight = [](const Json &video) {
            return (video["type"].string() == "Trailer" ? 2 : 0) +
                   (video["official"].boolean() ? 1 : 0);
        };
        return weight(left) > weight(right);
    });
    item.set("trailer", Json::text(videos.empty() ? "" : videos.front()["key"].string()));

    const std::string qid = jsString(document["external_ids"]["wikidata_id"]);
    item.set("wikidataId", Json::text(wikidataId(qid) ? qid : ""));
    item.set("services", servicesIn(document, region));
    Json similar = trimRows(document["similar"]["results"], type);
    Json filteredSimilar = Json::array();
    for (const Json &entry : similar.items()) {
        if (strictEqual(entry["id"], item["id"])) continue;
        if (filteredSimilar.size() >= 12) break;
        filteredSimilar.add(entry);
    }
    item.set("similar", filteredSimilar);

    Json result = Json::object();
    result.set("ok", Json::flag(true));
    result.set("enabled", Json::flag(true));
    result.set("item", item);
    return result;
}

std::string parentDirectory(const std::string &path) {
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

bool writeAll(int descriptor, const std::string &contents) {
    size_t offset = 0;
    while (offset < contents.size()) {
        const ssize_t count = write(descriptor, contents.data() + offset,
                                    contents.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        offset += static_cast<size_t>(count);
    }
    return true;
}

bool writeAtomic(const std::string &path, const std::string &contents) {
    std::string pattern = path + ".tmp.XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    const int output = mkstemp(name.data());
    if (output < 0) return false;
    const std::string temporary(name.data());
    bool ok = fchmod(output, 0600) == 0 && writeAll(output, contents) &&
              fsync(output) == 0;
    const int failure = errno;
    if (close(output) != 0) ok = false;
    if (ok && rename(temporary.c_str(), path.c_str()) != 0) ok = false;
    if (!ok) {
        unlink(temporary.c_str());
        errno = failure ? failure : EIO;
        return false;
    }
    const std::string directoryPath = parentDirectory(path);
    const int directory = open(directoryPath.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory >= 0) {
        (void)fsync(directory);
        close(directory);
    }
    return true;
}

int64_t nowMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

size_t receiveJson(char *data, size_t size, size_t count, void *context) {
    std::string *body = static_cast<std::string *>(context);
    if (size && count > std::numeric_limits<size_t>::max() / size) return 0;
    const size_t bytes = size * count;
    if (bytes > kMaximumDocumentBytes ||
        body->size() > kMaximumDocumentBytes - bytes)
        return 0;
    body->append(data, bytes);
    return bytes;
}

bool initializeCurl() {
    static std::once_flag once;
    static CURLcode result = CURLE_FAILED_INIT;
    std::call_once(once, [] { result = curl_global_init(CURL_GLOBAL_DEFAULT); });
    return result == CURLE_OK;
}

} // namespace

HomeContent::HomeContent(std::string dataRoot, Fetcher fetcher, Clock clock)
    : cachePath_((dataRoot.empty() ? "/var/lib/tvweb" : std::move(dataRoot)) +
                 "/home-content.json"),
      fetcher_(std::move(fetcher)), clock_(std::move(clock)) {
    if (!clock_) clock_ = nowMilliseconds;
}

Json HomeContent::details(const std::string &tmdbKey, bool netflixEnabled,
                          const std::string &type, const std::string &id) {
    if (tmdbKey.empty()) return disabled();
    if ((type != "movie" && type != "tv") || !validTitleId(id))
        return failed("Invalid title");
    std::string region;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        region = region_;
    }
    // TMDB ignores watch_region on a title; it keeps each country's answer apart in the cache.
    Json result = request(tmdbKey, type + "/" + id,
                         region.empty()
                             ? "&append_to_response=credits,videos,similar,external_ids"
                             : "&append_to_response=credits,videos,similar,external_ids,"
                               "watch/providers,translations&watch_region=" + region,
                         kDayMs, [type, id, region](const Json &document) {
                             return detailsValue(document, type, id, region);
                         });
    Json copy = result.valid() ? Json::parse(result.dump()) : Json::object();
    copy.set("netflixEnabled", Json::flag(
        netflixEnabled && copy["item"].valid() &&
        !copy["item"]["wikidataId"].string().empty()));
    return copy;
}

void HomeContent::setLanguage(const std::string &language) {
    const bool valid = language.size() == 5 && language[2] == '-' &&
                       std::islower(static_cast<unsigned char>(language[0])) &&
                       std::islower(static_cast<unsigned char>(language[1])) &&
                       std::isupper(static_cast<unsigned char>(language[3])) &&
                       std::isupper(static_cast<unsigned char>(language[4]));
    std::lock_guard<std::mutex> lock(mutex_);
    language_ = valid ? language : kEnglish;
}

void HomeContent::setRegion(const std::string &region) {
    const bool valid = region.size() == 2 && std::all_of(region.begin(), region.end(),
        [](unsigned char c) { return c >= 'A' && c <= 'Z'; });
    std::lock_guard<std::mutex> lock(mutex_);
    region_ = valid ? region : "";
}

Json HomeContent::search(const std::string &tmdbKey, const std::string &query,
                         bool queryPresent) {
    if (tmdbKey.empty()) return disabled();
    if (!queryPresent || std::any_of(query.begin(), query.end(), [](unsigned char c) {
            return c < 0x20 || c == 0x7f;
        }))
        return failed("Invalid search");
    const std::string cleaned = trim(query);
    const size_t units = utf16Length(cleaned);
    if (units < 2) {
        Json result = Json::object();
        result.set("ok", Json::flag(true));
        result.set("enabled", Json::flag(true));
        result.set("items", Json::array());
        return result;
    }
    if (units > 80) return failed("Search is too long");
    return request(tmdbKey, "search/multi",
                   "&include_adult=false&query=" + urlEncode(cleaned),
                   15 * 60 * 1000, [](const Json &document) {
        if (!document["results"].isArray()) return Json();
        Json result = Json::object();
        result.set("ok", Json::flag(true));
        result.set("enabled", Json::flag(true));
        result.set("items", trimRows(document["results"], ""));
        return result;
    });
}

Json HomeContent::genre(const std::string &tmdbKey, const std::string &key) {
    if (tmdbKey.empty()) return disabled();
    const names::SearchGenre *genre = names::searchGenre(key);
    if (!genre) return failed("Unknown genre");
    Json items = Json::array();
    for (const std::string type : {"movie", "tv"}) {
        const int id = type == "movie" ? genre->film : genre->series;
        if (!id) continue;
        const Json found = request(
            tmdbKey, "discover/" + type,
            "&include_adult=false&sort_by=popularity.desc&with_genres=" + std::to_string(id),
            kDayMs, [type](const Json &document) {
                if (!document["results"].isArray()) return Json();
                Json result = Json::object();
                result.set("ok", Json::flag(true));
                result.set("enabled", Json::flag(true));
                result.set("items", trimRows(document["results"], type));
                return result;
            });
        if (!found["ok"].boolean()) return found;
        for (const Json &item : found["items"].items()) items.add(item);
    }
    Json result = Json::object();
    result.set("ok", Json::flag(true));
    result.set("enabled", Json::flag(true));
    result.set("items", items);
    return result;
}

Json HomeContent::netflix(const std::string &tmdbKey, bool netflixEnabled,
                          const std::string &type, const std::string &id) {
    if (!netflixEnabled || tmdbKey.empty()) return disabled();
    return wikidataLookup(tmdbKey, type, id, "P1874", netflixId, "netflixId");
}

Json HomeContent::prime(const std::string &tmdbKey, bool primeEnabled,
                        const std::string &type, const std::string &id) {
    if (!primeEnabled || tmdbKey.empty()) return disabled();
    return wikidataLookup(tmdbKey, type, id, "P14462", primeGti, "gti");
}

Json HomeContent::trailer(const std::string &tmdbKey, const std::string &id) {
    if (tmdbKey.empty()) return disabled();
    Json none = Json::object();
    none.set("ok", Json::flag(true));
    none.set("enabled", Json::flag(true));
    none.set("url", Json::text(""));
    const Json film = details(tmdbKey, false, "movie", id);
    if (!film["ok"].boolean()) {
        none.set("ok", Json::flag(false));
        return none;
    }
    std::string region;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        region = region_;
    }
    std::vector<std::string> countries;
    if (!region.empty() && region != "US")
        countries.push_back({static_cast<char>(region[0] - 'A' + 'a'),
                             static_cast<char>(region[1] - 'A' + 'a')});
    countries.push_back("us");
    const std::string itunes =
        wikidataLookup(tmdbKey, "movie", id, "P6398", itunesId, "itunesId")["itunesId"].string();
    // Apple TV's page of the film names its trailer as HLS in widths up to 1912 px
    // (service/home_trailers.cpp picks the one the TV plays in time): preferred to the iTunes
    // Store's 640 px preview.
    auto appleTv = [&](const std::string &store, const std::string &country) {
        return request(tmdbKey, "appletv/" + country + "/" + store, "", 3 * kDayMs, [](const Json &document) {
            if (!document["page"].isString()) return Json();
            Json result = Json::object();
            result.set("ok", Json::flag(true));
            result.set("enabled", Json::flag(true));
            result.set("url", Json::text(appleTvPlaylist(document["page"].string())));
            return result;
        });
    };
    if (!itunes.empty())
        for (const std::string &country : countries) {
            const Json found = appleTv(itunes, country);
            if (!found["url"].string().empty()) return found;
        }
    if (!itunes.empty())
        for (const std::string &country : countries) {
            const Json found = request(tmdbKey, "itunes/" + itunes, "&country=" + country,
                                       3 * kDayMs, [](const Json &document) {
                if (!document["results"].isArray()) return Json();
                std::string url;
                for (const Json &item : document["results"].items())
                    if (applePreview(item["previewUrl"].string())) {
                        url = item["previewUrl"].string();
                        break;
                    }
                Json result = Json::object();
                result.set("ok", Json::flag(true));
                result.set("enabled", Json::flag(true));
                result.set("url", Json::text(url));
                return result;
            });
            if (!found["url"].string().empty()) return found;
        }
    // New films seldom have an iTunes id on Wikidata yet, and Apple's search finds no films:
    // the charts of what is bought and rented now carry the previews, matched by title and year.
    const std::string title = titleKey(film["item"]["title"].string());
    const std::string original = titleKey(film["item"]["originalTitle"].string());
    const int year = std::atoi(film["item"]["year"].string().c_str());
    for (const std::string &country : countries) {
        // "ids" keys the charts apart from those kept before they carried the films' store ids.
        const Json chart = request(tmdbKey, "itunes-chart/" + country, "ids", kDayMs / 4, chartValue);
        for (const Json &entry : chart["films"].items()) {
            const std::string name = entry["name"].string();
            const int released = std::atoi(entry["year"].string().c_str());
            if ((name == title || (!original.empty() && name == original)) && year &&
                std::abs(released - year) <= 1) {
                if (entry["id"].isString()) {
                    const Json found = appleTv(entry["id"].string(), country);
                    if (!found["url"].string().empty()) return found;
                }
                Json result = Json::parse(none.dump());
                result.set("url", entry["url"]);
                return result;
            }
        }
    }
    return none;
}

Json HomeContent::wikidataLookup(const std::string &tmdbKey, const std::string &type,
                                 const std::string &id, const std::string &property,
                                 const IdCheck &valid, const std::string &field) {
    const Json title = details(tmdbKey, true, type, id);
    const std::string qid = title["item"]["wikidataId"].string();
    if (!title["ok"].boolean() || qid.empty()) {
        Json result = Json::object();
        result.set("ok", title["ok"].valid() ? title["ok"] : Json::flag(false));
        result.set("enabled", Json::flag(true));
        result.set(field, Json::text(""));
        if (title["error"].valid()) result.set("error", title["error"]);
        return result;
    }
    // One entity answers every property, but each is kept apart in the cache: Netflix's lookup is
    // the entity alone, as before there were others.
    const std::string part = property == "P1874" ? "" : "&" + property;
    return request(tmdbKey, "wikidata/" + qid, part, 7 * kDayMs,
                   [qid, property, valid, field](const Json &document) {
        const Json claims = document["entities"][qid]["claims"][property];
        std::vector<std::string> ids;
        if (claims.isArray()) {
            for (const Json &claim : claims.items()) {
                if (claim["rank"].string() == "deprecated") continue;
                const std::string value = jsString(
                    claim["mainsnak"]["datavalue"]["value"]);
                if (!valid(value) ||
                    std::find(ids.begin(), ids.end(), value) != ids.end())
                    continue;
                ids.push_back(value);
            }
        }
        Json result = Json::object();
        result.set("ok", Json::flag(true));
        result.set("enabled", Json::flag(true));
        result.set(field, Json::text(ids.size() == 1 ? ids[0] : ""));
        return result;
    });
}

Json HomeContent::request(const std::string &apiKey, const std::string &resource,
                          const std::string &query, int64_t lifetimeMs,
                          const Converter &convert) {
    std::unique_lock<std::mutex> lock(mutex_);
    // Each language's answer is kept apart; English keeps the keys it had before there were others.
    const std::string language = language_;
    const std::string key =
        md5Hex(resource + query + (language == kEnglish ? std::string() : "|" + language));
    loadCacheLocked();
    const auto cached = cache_.find(key);
    const bool hasEntry = cached != cache_.end();
    const Entry entry = hasEntry ? cached->second : Entry();
    const int64_t now = clock_();
    auto fallback = [&entry, hasEntry](const std::string &message) {
        if (!hasEntry) return failed(message);
        Json result = Json::parse(entry.value.dump());
        result.set("stale", Json::flag(true));
        result.set("error", Json::text(message));
        return result;
    };
    const int64_t age = hasEntry ? now - entry.at : -1;
    if (hasEntry && age >= 0 && age < lifetimeMs) return entry.value;
    if (hasEntry && entry.failedAt && now - entry.failedAt < kRetryMs)
        return fallback("TMDB is unavailable");

    const auto existing = pending_.find(key);
    if (existing != pending_.end()) {
        const std::shared_ptr<Pending> pending = existing->second;
        pending->changed.wait(lock, [&pending] { return pending->done; });
        return pending->value;
    }
    if (active_ >= 2 && queued_ >= 16)
        return fallback("Please try again shortly");
    const std::shared_ptr<Pending> pending = std::make_shared<Pending>();
    pending_[key] = pending;
    if (active_ >= 2) {
        ++queued_;
        capacityChanged_.wait(lock, [this] { return active_ < 2; });
        --queued_;
    }
    ++active_;
    lock.unlock();

    Json document;
    std::string error;
    bool fetched = false;
    Json converted;
    try {
        fetched = fetch(urlFor(apiKey, resource, query, language), document, error);
        if (fetched) converted = convert(document);
    } catch (...) {
        fetched = false;
        converted = Json();
    }
    lock.lock();
    --active_;
    Json answer;
    auto current = cache_.find(key);
    if (converted.valid()) {
        Entry saved;
        saved.at = clock_();
        saved.value = converted;
        cache_[key] = saved;
        saveCacheLocked();
        answer = converted;
    } else {
        if (current != cache_.end()) current->second.failedAt = clock_();
        answer = fallback(current != cache_.end() ? "TMDB is unavailable"
                                                  : "TMDB is unavailable. Try again.");
    }
    pending->value = answer;
    pending->done = true;
    pending_.erase(key);
    pending->changed.notify_all();
    capacityChanged_.notify_all();
    return answer;
}

bool HomeContent::fetch(const std::string &url, Json &document,
                        std::string &error) const {
    error.clear();
    if (fetcher_) return fetcher_(url, document, error);
    if (!initializeCurl()) {
        error = "HTTP initialization failed";
        return false;
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
        error = "Could not create a connection";
        return false;
    }
    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#pragma GCC diagnostic pop
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "tvweb");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receiveJson);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    if (code != CURLE_OK) {
        error = curl_easy_strerror(code);
        return false;
    }
    if (status < 200 || status >= 300) {
        error = "remote server returned HTTP " + std::to_string(status);
        return false;
    }
    // Apple TV's pages are HTML: their text is kept for the trailer named in it.
    if (url.compare(0, 21, "https://tv.apple.com/") == 0) {
        document = Json::object();
        document.set("page", Json::text(body));
        return true;
    }
    document = Json::parse(body);
    if (!document.valid()) {
        error = "remote server did not return JSON";
        return false;
    }
    return true;
}

std::string HomeContent::urlFor(const std::string &apiKey, const std::string &resource,
                               const std::string &query, const std::string &language) const {
    if (resource.compare(0, 9, "wikidata/") == 0)
        return "https://www.wikidata.org/wiki/Special:EntityData/" +
               resource.substr(9) + ".json";
    // Apple's lookup and charts take no key; `query` names the lookup's store.
    if (resource.compare(0, 7, "itunes/") == 0)
        return "https://itunes.apple.com/lookup?id=" + resource.substr(7) + query;
    if (resource.compare(0, 13, "itunes-chart/") == 0)
        return "https://itunes.apple.com/" + resource.substr(13) + "/rss/topmovies/limit=100/json";
    // "appletv/<country>/<iTunes id>": the film's page on Apple TV, which redirects to its name.
    if (resource.compare(0, 8, "appletv/") == 0) {
        const size_t slash = resource.find('/', 8);
        return "https://tv.apple.com/" + resource.substr(8, slash - 8) + "/movie/id" + resource.substr(slash + 1);
    }
    return std::string(kTmdbApi) + resource + "?api_key=" +
           urlEncode(apiKey) + "&language=" + language + query;
}

void HomeContent::loadCacheLocked() {
    if (cacheLoaded_) return;
    cacheLoaded_ = true;
    const int input = open(cachePath_.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (input < 0) return;
    struct stat info = {};
    if (fstat(input, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        static_cast<unsigned long long>(info.st_size) > kMaximumDocumentBytes) {
        close(input);
        return;
    }
    std::string contents;
    contents.reserve(static_cast<size_t>(info.st_size));
    char buffer[8192];
    bool ok = true;
    for (;;) {
        const ssize_t count = read(input, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { ok = false; break; }
        if (count == 0) break;
        if (contents.size() > kMaximumDocumentBytes - static_cast<size_t>(count)) {
            ok = false;
            break;
        }
        contents.append(buffer, static_cast<size_t>(count));
    }
    close(input);
    if (!ok) return;
    const Json saved = Json::parse(contents);
    // Version 2: a title's cast carries its photos and director; version 3: its details name the
    // genres of Search's grid it is in. Older details are fetched again.
    if (!saved.isObject() || saved["version"].number() != kCacheVersion ||
        !saved["entries"].isArray()) return;
    for (const Json &item : saved["entries"].items()) {
        const std::string key = item["key"].string();
        const Json value = item["value"];
        const Json timestamp = item["at"];
        if (!validHexKey(key) || !timestamp.isNumber() || !value.isObject() ||
            !value["ok"].boolean()) continue;
        Entry entry;
        entry.at = static_cast<int64_t>(timestamp.number());
        entry.value = value;
        cache_[key] = entry;
    }
    if (cache_.size() > kMaximumCacheEntries) {
        std::vector<std::pair<std::string, Entry>> entries(cache_.begin(), cache_.end());
        std::sort(entries.begin(), entries.end(), [](const auto &left, const auto &right) {
            return left.second.at < right.second.at;
        });
        cache_.clear();
        for (size_t index = entries.size() - kMaximumCacheEntries;
             index < entries.size(); ++index)
            cache_[entries[index].first] = entries[index].second;
    }
}

void HomeContent::saveCacheLocked() {
    if (cache_.size() > kMaximumCacheEntries) {
        std::vector<std::pair<std::string, Entry>> entries(cache_.begin(), cache_.end());
        std::sort(entries.begin(), entries.end(), [](const auto &left, const auto &right) {
            return left.second.at < right.second.at;
        });
        cache_.clear();
        for (size_t index = entries.size() - kMaximumCacheEntries;
             index < entries.size(); ++index)
            cache_[entries[index].first] = entries[index].second;
    }
    Json document = Json::object();
    document.set("version", Json::numeric(kCacheVersion));
    Json values = Json::array();
    for (const auto &pair : cache_) {
        Json entry = Json::object();
        entry.set("key", Json::text(pair.first));
        entry.set("at", Json::numeric(static_cast<double>(pair.second.at)));
        entry.set("value", pair.second.value);
        values.add(entry);
    }
    document.set("entries", values);
    (void)writeAtomic(cachePath_, document.dump());
}

} // namespace service
} // namespace rootedtv
