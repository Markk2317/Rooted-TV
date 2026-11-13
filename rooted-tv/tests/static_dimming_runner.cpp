#include "daemon/static_dimming_runner.hpp"
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
using rootedtv::Json;
using rootedtv::daemon::StaticDimmingRunner;

// A 64 x 36 24-bit BMP of a moving picture with a bright logo in a corner.
std::string capture(int frame) {
    const int width = 64, height = 36, stride = width * 3;
    std::string bytes(54 + stride * height, '\0');
    auto put = [&bytes](size_t at, uint32_t value, int size) {
        for (int i = 0; i < size; ++i)
            bytes[at + i] = static_cast<char>((value >> (8 * i)) & 0xff);
    };
    bytes[0] = 'B';
    bytes[1] = 'M';
    put(10, 54, 4);
    put(14, 40, 4);
    put(18, width, 4);
    put(22, height, 4);
    put(26, 1, 2);
    put(28, 24, 2);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const bool logo = x >= 56 && x < 60 && y >= 31 && y < 34; // bottom-up: near the top
            const int value = logo ? 230 : (x * 7 + y * 3 + frame * 40) % 160 + 20;
            for (int c = 0; c < 3; ++c)
                bytes[54 + y * stride + x * 3 + c] = static_cast<char>(value);
        }
    return bytes;
}

Json dimensionOf(const std::string &mode) {
    Json dimension = Json::object();
    dimension.set("input", Json::text("default"));
    dimension.set("pictureMode", Json::text(mode));
    return dimension;
}

struct Tv {
    std::map<std::string, int> light{{"game", 70}, {"cinema", 50}};
    std::string mode = "game";
    std::string level = "light";
    int frame = 0;
    StaticDimmingRunner::Deps deps(const std::string &statePath) {
        StaticDimmingRunner::Deps deps;
        deps.level = [this]() { return level; };
        deps.watching = []() { return true; };
        deps.capture = [this](std::string &bytes) {
            bytes = capture(frame++);
            return true;
        };
        deps.readLight = [this]() {
            Json result = Json::object();
            result.set("value", Json::numeric(light[mode]));
            result.set("dimension", dimensionOf(mode));
            return result;
        };
        deps.readLightOf = [this](const Json &dimension) {
            return light[dimension["pictureMode"].string()];
        };
        deps.setLightOf = [this](const Json &dimension, int value) {
            light[dimension["pictureMode"].string()] = value;
            return true;
        };
        deps.statePath = statePath;
        return deps;
    }
};
} // namespace

int main(int, char **argv) {
    const std::string statePath = std::string(argv[0]) + ".state.json";
    std::remove(statePath.c_str());
    try {
        Tv tv;
        {
            StaticDimmingRunner runner(tv.deps(statePath));
            for (int i = 0; i < 20; ++i)
                runner.step();
            check(tv.light["game"] == 60, "A still logo on a moving picture lowers the light to 85 %");
            std::FILE *state = std::fopen(statePath.c_str(), "r");
            check(state != nullptr, "and the owner's value is kept on disk meanwhile");
            std::fclose(state);
            // The owner switches to another picture mode: the lowered one comes back.
            tv.mode = "cinema";
            runner.step();
            check(tv.light["game"] == 70 && tv.light["cinema"] == 50,
                  "Another mode on screen puts the lowered one back and leaves its own alone");
            for (int i = 0; i < 20; ++i)
                runner.step();
            check(tv.light["cinema"] == 43, "The new mode is lowered in turn");
        }
        {
            // A restart while lowered, with the dimming switched off meanwhile.
            tv.level = "off";
            StaticDimmingRunner runner(tv.deps(statePath));
            runner.step();
            check(tv.light["cinema"] == 50, "A restart puts the light back");
            std::FILE *state = std::fopen(statePath.c_str(), "r");
            check(state == nullptr, "and forgets it");
            if (state) std::fclose(state);
        }
    } catch (const std::exception &error) {
        std::printf("FAIL: %s\n", error.what());
        std::remove(statePath.c_str());
        return 1;
    }
    std::puts("static dimming runner checks passed");
    return 0;
}
