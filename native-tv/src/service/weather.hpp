#pragma once

#include "core/json.hpp"
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace nativetv {
namespace service {

// The weather the Photos screen saver shows, as Google TV's ambient mode does, from Open-Meteo,
// which needs no key: a city name is looked up once for its coordinates, and the current weather
// there is read at most every half hour.
class Weather {
  public:
    using Fetcher = std::function<bool(const std::string &url, Json &document, std::string &error)>;
    using Clock = std::function<int64_t()>;

    explicit Weather(Fetcher fetcher = Fetcher(), Clock clock = Clock());

    // {"ok", "city", "country", "latitude", "longitude"} for the best match of `name`, or
    // {"ok": false, "error"}.
    Json find(const std::string &name);
    // {"ok", "temperature" (°C, whole), "words", "day"} at the place, or {"ok": false}.
    Json current(double latitude, double longitude);

  private:
    bool fetch(const std::string &url, Json &document, std::string &error);

    Fetcher fetcher_;
    Clock clock_;
    std::mutex mutex_;
    std::string cachedPlace_;
    int64_t cachedAt_ = 0;
    Json cached_;
};

// Open-Meteo's weather code as a word or two, in English as the TV's screens are: "Clear",
// "Rain", "Snow" ... (WMO 4677 codes as Open-Meteo gives them).
std::string weatherWords(int code);

} // namespace service
} // namespace nativetv
