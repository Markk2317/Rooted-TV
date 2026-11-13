#include "service/screensaver_photos.hpp"
#include "service/weather.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace rootedtv;
using namespace rootedtv::service;

namespace {
// The smallest JPEG the wallpaper check takes, as tests/home_wallpapers.cpp makes it.
std::string validJpeg() {
    std::string bytes("\xff\xd8", 2);
    bytes.append("\xff\xdb\x00\x43", 4);
    bytes.append(65, '\0');
    bytes.append("\xff\xc4\x00\x14", 4);
    bytes.append(18, '\0');
    bytes.append("\xff\xc0\x00\x0b\x08\x00\x01\x00\x01\x01\x01\x11\x00", 13);
    bytes.append("\xff\xda\x00\x08\x01\x01\x00\x00\x3f\x00", 10);
    bytes.push_back('\x01');
    bytes.append("\xff\xd9", 2);
    return bytes;
}
} // namespace

int main() {
    char pattern[] = "/tmp/rootedtv-saver-photos-XXXXXX";
    const std::string root = mkdtemp(pattern);
    std::string error;

    assert(screensaverPhotoStatus(root)["count"].number() == 0);
    assert(!saveScreensaverPhoto(root, "not a jpeg", error) && !error.empty());
    assert(saveScreensaverPhoto(root, validJpeg(), error));
    assert(saveScreensaverPhoto(root, validJpeg(), error));
    Json status = screensaverPhotoStatus(root);
    assert(status["count"].number() == 2 && status["ids"][0].number() == 1 &&
           status["ids"][1].number() == 2 && status["bytes"].number() == 2 * validJpeg().size());
    const ScreensaverPhoto photo = readScreensaverPhoto(root, 2);
    assert(photo.found && photo.bytes == validJpeg() && !photo.etag.empty());
    assert(!readScreensaverPhoto(root, 7).found && !readScreensaverPhoto(root, 0).found);

    // Numbers keep growing after a removal, so a photo is never mistaken for another.
    assert(removeScreensaverPhotos(root, 1));
    assert(saveScreensaverPhoto(root, validJpeg(), error));
    status = screensaverPhotoStatus(root);
    assert(status["count"].number() == 2 && status["ids"][0].number() == 2 &&
           status["ids"][1].number() == 3);
    assert(removeScreensaverPhotos(root, 0) && screensaverPhotoStatus(root)["count"].number() == 0);
    for (unsigned id = 1; id <= 3; ++id)
        (void)unlink((root + "/screensaver-photos/photo-" + std::to_string(id) + ".jpg").c_str());
    (void)rmdir((root + "/screensaver-photos").c_str());
    (void)rmdir(root.c_str());

    // The weather: a town looked up once, the weather there read at most every half hour.
    std::vector<std::string> asked;
    int64_t clock = 1000;
    Weather weather(
        [&asked](const std::string &url, Json &document, std::string &) {
            asked.push_back(url);
            if (url.find("geocoding-api.open-meteo.com") != std::string::npos)
                document = url.find("name=Nowhere") != std::string::npos
                               ? Json::parse(R"({"generationtime_ms":0.1})")
                               : Json::parse(R"({"results":[{"name":"Milano","latitude":45.46427,"longitude":9.18951,"country_code":"IT"}]})");
            else
                document = Json::parse(R"({"current":{"temperature_2m":17.6,"weather_code":61,"is_day":1}})");
            return true;
        },
        [&clock]() { return clock; });
    const Json found = weather.find("  Milano ");
    assert(found["ok"].boolean() && found["city"].string() == "Milano" &&
           found["latitude"].number() == 45.46427 && asked.back().find("name=Milano") != std::string::npos);
    assert(!weather.find("Nowhere")["ok"].boolean());
    assert(!weather.find("x")["ok"].boolean());
    const size_t before = asked.size();
    Json now = weather.current(45.46427, 9.18951);
    assert(now["ok"].boolean() && now["temperature"].number() == 18 && now["words"].string() == "Rain" &&
           now["day"].boolean() && now["icon"].string() == "rain");
    assert(asked.back().find("latitude=45.464&longitude=9.190") != std::string::npos);
    (void)weather.current(45.46427, 9.18951);
    assert(asked.size() == before + 1);
    clock += 31 * 60 * 1000;
    (void)weather.current(45.46427, 9.18951);
    assert(asked.size() == before + 2);
    assert(weatherWords(0) == "Clear" && weatherWords(3) == "Cloudy" && weatherWords(73) == "Snow" &&
           weatherWords(96) == "Thunderstorm" && weatherWords(4).empty());
    assert(weatherIcon(0, true) == "clear-day" && weatherIcon(0, false) == "clear-night" &&
           weatherIcon(2, false) == "partly-night" && weatherIcon(81, true) == "rain" &&
           weatherIcon(53, true) == "drizzle" && weatherIcon(86, true) == "snow" &&
           weatherIcon(99, true) == "storm" && weatherIcon(4, true).empty());
    std::printf("Screensaver photos passed.\n");
    return 0;
}
