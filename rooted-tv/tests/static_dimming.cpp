#include "service/static_dimming.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
using rootedtv::service::StaticDimming;
const int kWidth = 64, kHeight = 36;

// A moving picture (every pixel's value changes with `frame`), with a bright 4 x 3 logo in the
// top right corner when `logo`.
std::vector<uint8_t> frameOf(int frame, bool logo, int bright = 220) {
    std::vector<uint8_t> luma(kWidth * kHeight);
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x)
            luma[y * kWidth + x] = static_cast<uint8_t>((x * 7 + y * 3 + frame * 40) % 160 + 20);
    if (logo)
        for (int y = 2; y < 5; ++y)
            for (int x = 56; x < 60; ++x)
                luma[y * kWidth + x] = static_cast<uint8_t>(bright);
    return luma;
}

std::string bmp(int width, int height, bool topDown, const std::vector<uint8_t> &rgbTop) {
    const int stride = (width * 3 + 3) & ~3;
    std::string bytes(54 + stride * height, '\0');
    auto put = [&bytes](size_t at, uint32_t value, int size) {
        for (int i = 0; i < size; ++i)
            bytes[at + i] = static_cast<char>((value >> (8 * i)) & 0xff);
    };
    bytes[0] = 'B';
    bytes[1] = 'M';
    put(2, static_cast<uint32_t>(bytes.size()), 4);
    put(10, 54, 4);
    put(14, 40, 4);
    put(18, static_cast<uint32_t>(width), 4);
    put(22, static_cast<uint32_t>(topDown ? -height : height), 4);
    put(26, 1, 2);
    put(28, 24, 2);
    for (int row = 0; row < height; ++row) {
        const int stored = topDown ? row : height - 1 - row;
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c) // blue, green, red
                bytes[54 + stored * stride + x * 3 + c] =
                    static_cast<char>(rgbTop[(row * width + x) * 3 + (2 - c)]);
    }
    return bytes;
}
} // namespace

int main() {
    try {
        {
            StaticDimming dimming;
            // The first capture is the reference: a pixel's count starts at the second.
            int frame = 0;
            for (; frame < StaticDimming::kStillCaptures; ++frame)
                check(!dimming.observe(frameOf(frame, true), kWidth, kHeight),
                      "A logo is not taken as one before it has stood still long enough");
            check(dimming.observe(frameOf(frame++, true), kWidth, kHeight),
                  "A bright logo that stays put on a moving picture is found");
            check(!dimming.observe(frameOf(frame++, false), kWidth, kHeight),
                  "and is gone with it");

            StaticDimming dark;
            for (int i = 0; i < 20; ++i)
                dark.observe(frameOf(i, true, 40), kWidth, kHeight);
            check(!dark.observe(frameOf(20, true, 40), kWidth, kHeight),
                  "A dark element is left alone");

            StaticDimming still;
            const std::vector<uint8_t> paused = frameOf(0, true);
            for (int i = 0; i < 20; ++i)
                still.observe(paused, kWidth, kHeight);
            check(!still.observe(paused, kWidth, kHeight),
                  "A picture that stands still whole is the panel's own to dim");
            check(!still.observe(frameOf(1, true), kWidth, kHeight) &&
                      !still.observe(frameOf(2, true), kWidth, kHeight),
                  "and as it moves again, its long-still pixels are not taken for an element at once");
        }
        {
            // The light: 70 lowered by 15 % to 60 in steps of 4, then back in steps of 10.
            StaticDimming dimming;
            int frame = 0;
            for (; frame < 30; ++frame)
                dimming.observe(frameOf(frame, true), kWidth, kHeight);
            check(dimming.light(StaticDimming::Level::Off, 70) == -1 && !dimming.lowered(),
                  "Off leaves the light alone");
            int light = 70;
            std::vector<int> steps;
            for (int i = 0; i < 6; ++i) {
                const int next = dimming.light(StaticDimming::Level::Light, light);
                if (next >= 0) {
                    light = next;
                    steps.push_back(next);
                }
            }
            check(steps == std::vector<int>({66, 62, 60}), "The light goes down step by step to 85 %");
            check(dimming.lowered() && dimming.ownLight() == 70, "and the owner's value is kept");
            dimming.observe(frameOf(frame++, false), kWidth, kHeight);
            check(dimming.light(StaticDimming::Level::Light, light) == 70 && !dimming.lowered(),
                  "Once the logo has gone the owner's light comes back");

            for (int i = 0; i < 20; ++i)
                dimming.observe(frameOf(frame++, true), kWidth, kHeight);
            light = dimming.light(StaticDimming::Level::Strong, 70);
            check(light == 66, "Strong starts down again");
            check(dimming.light(StaticDimming::Level::Strong, 80) == -1 && !dimming.lowered(),
                  "A light the owner set meanwhile is theirs");
            check(dimming.light(StaticDimming::Level::Strong, 80) == -1,
                  "and is not lowered again while the same logo stays");
            dimming.observe(frameOf(frame++, false), kWidth, kHeight);
            dimming.light(StaticDimming::Level::Strong, 80);
            for (int i = 0; i < 20; ++i)
                dimming.observe(frameOf(frame++, true), kWidth, kHeight);
            check(dimming.light(StaticDimming::Level::Strong, 80) == 76,
                  "but is after it has gone and another comes");
            check(dimming.light(StaticDimming::Level::Off, 76) == 80,
                  "Switching it off puts the light back");

            StaticDimming restarted;
            restarted.restore(70, 60);
            check(restarted.light(StaticDimming::Level::Light, 60) == 70,
                  "A restart with the light lowered puts it back");
        }
        {
            std::vector<uint8_t> rgb(3 * 2 * 3);
            rgb[0] = 255;                    // top left red
            rgb[3 * 5 + 1] = 255;            // bottom right green
            std::vector<uint8_t> luma;
            int width = 0, height = 0;
            for (bool topDown : {false, true}) {
                check(rootedtv::service::bmpLuma(bmp(3, 2, topDown, rgb), luma, width, height),
                      "A 24-bit BMP is read");
                check(width == 3 && height == 2 && luma[0] == 53 && luma[5] == 182 && luma[1] == 0,
                      "row by row from the top, as luma");
            }
            check(!rootedtv::service::bmpLuma("BM short", luma, width, height),
                  "A short file is refused");
        }
    } catch (const std::exception &error) {
        std::printf("FAIL: %s\n", error.what());
        return 1;
    }
    std::puts("static dimming checks passed");
    return 0;
}
