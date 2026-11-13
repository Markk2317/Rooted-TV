#include "service/weather.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <curl/curl.h>

namespace rootedtv {
namespace service {
namespace {
const int64_t kLifetimeMs = 30LL * 60 * 1000;
const size_t kMaximumBytes = 256 * 1024;

size_t appendBody(char *data, size_t size, size_t count, void *context) {
    if (size && count > std::numeric_limits<size_t>::max() / size) return 0;
    const size_t bytes = size * count;
    std::string *body = static_cast<std::string *>(context);
    if (body->size() + bytes > kMaximumBytes) return 0;
    body->append(data, bytes);
    return bytes;
}

std::string urlEncode(const std::string &text) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;
    for (unsigned char value : text) {
        if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '-' || value == '_' || value == '.' ||
            value == '~')
            encoded << static_cast<char>(value);
        else
            encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<unsigned>(value);
    }
    return encoded.str();
}

std::string coordinate(double value) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(3) << value;
    return text.str();
}

int64_t systemNow() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
} // namespace

std::string weatherWords(int code) {
    if (code == 0) return "Clear";
    if (code == 1 || code == 2) return "Partly cloudy";
    if (code == 3) return "Cloudy";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 57) return "Drizzle";
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "Rain";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "Snow";
    if (code >= 95) return "Thunderstorm";
    return "";
}

std::string weatherIcon(int code, bool day) {
    if (code == 0) return day ? "clear-day" : "clear-night";
    if (code == 1 || code == 2) return day ? "partly-day" : "partly-night";
    if (code == 3) return "cloudy";
    if (code == 45 || code == 48) return "fog";
    if (code >= 51 && code <= 57) return "drizzle";
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "rain";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "snow";
    if (code >= 95) return "storm";
    return "";
}

Weather::Weather(Fetcher fetcher, Clock clock)
    : fetcher_(std::move(fetcher)), clock_(std::move(clock)) {
    if (!clock_) clock_ = systemNow;
}

bool Weather::fetch(const std::string &url, Json &document, std::string &error) {
    if (fetcher_) return fetcher_(url, document, error);
    static std::once_flag once;
    std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
    CURL *curl = curl_easy_init();
    if (!curl) {
        error = "Could not create a connection";
        return false;
    }
    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#pragma GCC diagnostic pop
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "tvweb");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBody);
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
        error = "Open-Meteo answered HTTP " + std::to_string(status);
        return false;
    }
    document = Json::parse(body);
    if (!document.isObject()) {
        error = "Open-Meteo did not answer in JSON";
        return false;
    }
    return true;
}

Json Weather::find(const std::string &name) {
    Json result = Json::object();
    std::string trimmed = name;
    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
    trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
    if (trimmed.size() < 2 || trimmed.size() > 80) {
        result.set("ok", Json::flag(false));
        result.set("error", Json::text("Type the name of a town or city."));
        return result;
    }
    Json document;
    std::string error;
    if (!fetch("https://geocoding-api.open-meteo.com/v1/search?count=1&format=json&name=" +
                   urlEncode(trimmed),
               document, error)) {
        result.set("ok", Json::flag(false));
        result.set("error", Json::text(error));
        return result;
    }
    const Json place = document["results"][0];
    if (!place["latitude"].isNumber() || !place["longitude"].isNumber()) {
        result.set("ok", Json::flag(false));
        result.set("error", Json::text("No town or city of that name was found."));
        return result;
    }
    result.set("ok", Json::flag(true));
    result.set("city", Json::text(place["name"].string(trimmed).substr(0, 80)));
    result.set("country", Json::text(place["country_code"].string().substr(0, 2)));
    result.set("latitude", Json::numeric(place["latitude"].number()));
    result.set("longitude", Json::numeric(place["longitude"].number()));
    return result;
}

Json Weather::current(double latitude, double longitude) {
    const std::string place = coordinate(latitude) + "," + coordinate(longitude);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (place == cachedPlace_ && cached_.isObject() && clock_() - cachedAt_ < kLifetimeMs)
            return Json::parse(cached_.dump());
    }
    Json document;
    std::string error;
    Json result = Json::object();
    const bool fetched = fetch("https://api.open-meteo.com/v1/forecast?latitude=" +
                                   coordinate(latitude) + "&longitude=" + coordinate(longitude) +
                                   "&current=temperature_2m,weather_code,is_day&timezone=auto",
                               document, error);
    const Json now = document["current"];
    if (!fetched || !now["temperature_2m"].isNumber()) {
        result.set("ok", Json::flag(false));
        return result;
    }
    result.set("ok", Json::flag(true));
    result.set("temperature", Json::numeric(std::round(now["temperature_2m"].number())));
    const int code = static_cast<int>(now["weather_code"].number(-1));
    const bool day = now["is_day"].number(1) != 0;
    result.set("words", Json::text(weatherWords(code)));
    result.set("day", Json::flag(day));
    result.set("icon", Json::text(weatherIcon(code, day)));
    std::lock_guard<std::mutex> lock(mutex_);
    cachedPlace_ = place;
    cachedAt_ = clock_();
    cached_ = Json::parse(result.dump());
    return result;
}

} // namespace service
} // namespace rootedtv
