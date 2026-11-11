#include "service/screen_title.hpp"

#include <algorithm>
#include <cctype>

namespace nativetv {
namespace service {
namespace screen_title {
namespace {
// Lower case letters and digits only, so "Stranger Things" matches "stranger things:".
std::string folded(const std::string &text) {
    std::string result;
    for (unsigned char c : text)
        if (std::isalnum(c)) result += static_cast<char>(std::tolower(c));
        else if (c >= 0x80) result += static_cast<char>(c);
    return result;
}

std::string yearOf(const Json &value) {
    if (value.isNumber()) return std::to_string(static_cast<long>(value.number()));
    return value.string().substr(0, 4);
}
} // namespace

std::string prompt(const std::string &serviceName) {
    return "This is a screenshot of a TV showing the " + serviceName +
           " app just after its playback was paused or stopped. If it shows which film or series "
           "was being watched (a pause screen, the title's own page, a player with its name), "
           "answer only with JSON {\"title\": <its name as shown, without season or episode>, "
           "\"type\": \"movie\" or \"tv\", \"year\": <year if shown, else null>}. If no single "
           "title can be read (a menu, a list of many titles, a black or loading screen), answer "
           "only with {}.";
}

Json parse(const std::string &answer) {
    const size_t start = answer.find('{'), end = answer.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end < start) return Json();
    const Json parsed = Json::parse(answer.substr(start, end - start + 1));
    const std::string title = parsed["title"].string();
    const std::string type = parsed["type"].string();
    if (title.empty() || title.size() > 120 || (type != "movie" && type != "tv")) return Json();
    Json seen = Json::object();
    seen.set("title", Json::text(title));
    seen.set("type", Json::text(type));
    const std::string year = yearOf(parsed["year"]);
    seen.set("year", year.size() == 4 ? Json::text(year) : Json());
    return seen;
}

Json match(const Json &results, const Json &seen) {
    const std::string wanted = folded(seen["title"].string());
    if (wanted.empty()) return Json();
    Json sameName, sameKind;
    for (const Json &item : results.items()) {
        if (folded(item["title"].string()) != wanted) continue;
        if (!sameName.valid()) sameName = item;
        if (item["type"].string() != seen["type"].string()) continue;
        if (!sameKind.valid()) sameKind = item;
        if (seen["year"].isString() && yearOf(item["year"]) == seen["year"].string()) return item;
    }
    return sameKind.valid() ? sameKind : sameName;
}

std::string base64(const std::string &bytes) {
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const uint32_t n = (static_cast<uint8_t>(bytes[i]) << 16) |
                           (static_cast<uint8_t>(bytes[i + 1]) << 8) | static_cast<uint8_t>(bytes[i + 2]);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += kAlphabet[n & 63];
    }
    if (i < bytes.size()) {
        uint32_t n = static_cast<uint8_t>(bytes[i]) << 16;
        if (i + 1 < bytes.size()) n |= static_cast<uint8_t>(bytes[i + 1]) << 8;
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? kAlphabet[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

} // namespace screen_title
} // namespace service
} // namespace nativetv
