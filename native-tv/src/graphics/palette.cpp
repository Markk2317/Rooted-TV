#include "graphics/palette.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <queue>
#include <vector>

namespace nativetv {
namespace {
// androidx Palette's defaults: the picture scaled (nearest) to at most 112 x 112 pixels of area,
// 16 colours, each channel quantised to 5 bits.
constexpr int kResizeArea = 112 * 112;
constexpr int kMaxColours = 16;
constexpr int kBits = 5;

struct Swatch {
    int rgb, population;
};
int red5(int colour) {
    return (colour >> (2 * kBits)) & 31;
}
int green5(int colour) {
    return (colour >> kBits) & 31;
}
int blue5(int colour) {
    return colour & 31;
}
// Palette widens a 5-bit channel by shifting, without filling the low bits.
int rgbOf5(int red, int green, int blue) {
    return (red << 19) | (green << 11) | (blue << 3);
}
// ColorUtils.RGBToHSL, enough of it for the filter.
void hsl(int rgb, float out[3]) {
    const float r = ((rgb >> 16) & 0xff) / 255.0f, g = ((rgb >> 8) & 0xff) / 255.0f,
                b = (rgb & 0xff) / 255.0f;
    const float high = std::max(r, std::max(g, b)), low = std::min(r, std::min(g, b)),
                delta = high - low;
    float h = 0, s = 0;
    const float l = (high + low) / 2;
    if (delta > 0) {
        if (high == r)
            h = std::fmod((g - b) / delta, 6.0f);
        else if (high == g)
            h = (b - r) / delta + 2;
        else
            h = (r - g) / delta + 4;
        s = delta / (1 - std::fabs(2 * l - 1));
    }
    h = std::fmod(h * 60, 360.0f);
    if (h < 0)
        h += 360;
    out[0] = h;
    out[1] = s;
    out[2] = l;
}
// Palette's default filter: near-black, near-white and the skin tones near the red I line.
bool ignored(int rgb) {
    float c[3];
    hsl(rgb, c);
    return c[2] <= .05f || c[2] >= .95f || (c[0] >= 10 && c[0] <= 37 && c[1] <= .82f);
}
void hsv(int rgb, float out[3]) {
    const float r = ((rgb >> 16) & 0xff) / 255.0f, g = ((rgb >> 8) & 0xff) / 255.0f,
                b = (rgb & 0xff) / 255.0f;
    const float high = std::max(r, std::max(g, b)), low = std::min(r, std::min(g, b)),
                delta = high - low;
    float h = 0;
    if (delta > 0) {
        if (high == r)
            h = (g - b) / delta;
        else if (high == g)
            h = 2 + (b - r) / delta;
        else
            h = 4 + (r - g) / delta;
        h *= 60;
        if (h < 0)
            h += 360;
    }
    out[0] = h;
    out[1] = high > 0 ? delta / high : 0;
    out[2] = high;
}
// Skia's SkHSVToColor.
int rgbOfHsv(float h, float s, float v) {
    const int value = static_cast<int>(std::lround(v * 255));
    if (s <= 0)
        return (value << 16) | (value << 8) | value;
    const float sector = (h < 0 || h >= 360) ? 0 : h / 60;
    const int whole = static_cast<int>(std::floor(sector));
    const float fraction = sector - whole;
    const int p = static_cast<int>(std::lround((1 - s) * v * 255)),
              q = static_cast<int>(std::lround((1 - s * fraction) * v * 255)),
              t = static_cast<int>(std::lround((1 - s * (1 - fraction)) * v * 255));
    int r, g, b;
    switch (whole) {
    case 0: r = value, g = t, b = p; break;
    case 1: r = q, g = value, b = p; break;
    case 2: r = p, g = value, b = t; break;
    case 3: r = p, g = q, b = value; break;
    case 4: r = t, g = p, b = value; break;
    default: r = value, g = p, b = q; break;
    }
    return (r << 16) | (g << 8) | b;
}

// ColorCutQuantizer's box over a range of the distinct colours.
struct Box {
    int lower, upper, population = 0;
    int minR = 0, maxR = 0, minG = 0, maxG = 0, minB = 0, maxB = 0;
    int volume() const {
        return (maxR - minR + 1) * (maxG - minG + 1) * (maxB - minB + 1);
    }
};
struct Quantiser {
    std::vector<int> colours, histogram;
    void fit(Box &box) const {
        box.minR = box.minG = box.minB = 255;
        box.maxR = box.maxG = box.maxB = 0;
        box.population = 0;
        for (int i = box.lower; i <= box.upper; ++i) {
            const int colour = colours[i];
            box.population += histogram[colour];
            box.minR = std::min(box.minR, red5(colour)), box.maxR = std::max(box.maxR, red5(colour));
            box.minG = std::min(box.minG, green5(colour)), box.maxG = std::max(box.maxG, green5(colour));
            box.minB = std::min(box.minB, blue5(colour)), box.maxB = std::max(box.maxB, blue5(colour));
        }
    }
    // Sorts the box along its longest side (the others break ties in Palette's order) and splits
    // it where half its population is reached.
    Box split(Box &box) {
        const int spanR = box.maxR - box.minR, spanG = box.maxG - box.minG, spanB = box.maxB - box.minB;
        auto key = [&](int colour) {
            if (spanR >= spanG && spanR >= spanB)
                return colour;
            if (spanG >= spanR && spanG >= spanB)
                return (green5(colour) << (2 * kBits)) | (red5(colour) << kBits) | blue5(colour);
            return (blue5(colour) << (2 * kBits)) | (green5(colour) << kBits) | red5(colour);
        };
        std::sort(colours.begin() + box.lower, colours.begin() + box.upper + 1,
                  [&](int a, int b) { return key(a) < key(b); });
        int at = box.lower, count = 0;
        for (int i = box.lower; i <= box.upper; ++i) {
            count += histogram[colours[i]];
            if (count >= box.population / 2) {
                at = std::min(box.upper - 1, i);
                break;
            }
        }
        Box upper{at + 1, box.upper};
        box.upper = at;
        fit(box);
        fit(upper);
        return upper;
    }
    Swatch average(const Box &box) const {
        long r = 0, g = 0, b = 0, population = 0;
        for (int i = box.lower; i <= box.upper; ++i) {
            const int colour = colours[i], count = histogram[colour];
            population += count;
            r += static_cast<long>(red5(colour)) * count;
            g += static_cast<long>(green5(colour)) * count;
            b += static_cast<long>(blue5(colour)) * count;
        }
        const float n = static_cast<float>(std::max(1L, population));
        return {rgbOf5(static_cast<int>(std::lround(r / n)), static_cast<int>(std::lround(g / n)),
                       static_cast<int>(std::lround(b / n))),
                static_cast<int>(population)};
    }
};

std::vector<Swatch> palette(const uint8_t *rgba, int width, int height) {
    int sampleWidth = width, sampleHeight = height;
    if (static_cast<long>(width) * height > kResizeArea) {
        const double ratio = std::sqrt(static_cast<double>(kResizeArea) / (static_cast<double>(width) * height));
        sampleWidth = static_cast<int>(std::ceil(width * ratio));
        sampleHeight = static_cast<int>(std::ceil(height * ratio));
    }
    Quantiser q;
    q.histogram.assign(1 << (3 * kBits), 0);
    for (int y = 0; y < sampleHeight; ++y)
        for (int x = 0; x < sampleWidth; ++x) {
            // Android reads colour without alpha: what is see-through reads as its (black) colour.
            const uint8_t *pixel =
                rgba + (static_cast<size_t>(y * height / sampleHeight) * width + x * width / sampleWidth) * 4;
            ++q.histogram[((pixel[0] >> 3) << (2 * kBits)) | ((pixel[1] >> 3) << kBits) | (pixel[2] >> 3)];
        }
    for (int colour = 0; colour < static_cast<int>(q.histogram.size()); ++colour) {
        if (q.histogram[colour] && ignored(rgbOf5(red5(colour), green5(colour), blue5(colour))))
            q.histogram[colour] = 0;
        if (q.histogram[colour])
            q.colours.push_back(colour);
    }
    std::vector<Swatch> swatches;
    if (q.colours.size() <= static_cast<size_t>(kMaxColours)) {
        for (int colour : q.colours)
            swatches.push_back({rgbOf5(red5(colour), green5(colour), blue5(colour)), q.histogram[colour]});
        return swatches;
    }
    // Largest volume first; Java's PriorityQueue breaks ties in no fixed order, nor does this.
    auto smaller = [](const Box &a, const Box &b) { return a.volume() < b.volume(); };
    std::priority_queue<Box, std::vector<Box>, decltype(smaller)> boxes(smaller);
    Box all{0, static_cast<int>(q.colours.size()) - 1};
    q.fit(all);
    boxes.push(all);
    while (boxes.size() < static_cast<size_t>(kMaxColours)) {
        Box box = boxes.top();
        if (box.upper <= box.lower)
            break;
        boxes.pop();
        Box other = q.split(box);
        boxes.push(other);
        boxes.push(box);
    }
    for (; !boxes.empty(); boxes.pop()) {
        const Swatch swatch = q.average(boxes.top());
        if (!ignored(swatch.rgb))
            swatches.push_back(swatch);
    }
    return swatches;
}
} // namespace

int glowColour(const uint8_t *rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0)
        return -1;
    // The launcher's choice (its Ludb.u): the least distance from (0.67, 1) in saturation and
    // value per pixel of population.
    int chosen = -1;
    float best = FLT_MAX, c[3];
    for (const Swatch &swatch : palette(rgba, width, height)) {
        hsv(swatch.rgb, c);
        const float score = static_cast<float>(std::hypot(.67 - c[1], 1.0 - c[2])) / swatch.population;
        if (score < best) {
            best = score;
            chosen = swatch.rgb;
        }
    }
    if (chosen < 0)
        return -1;
    hsv(chosen, c);
    if (c[1] < .25f)
        return -1;
    return rgbOfHsv(static_cast<float>(std::rint(c[0] * 100) / 100), .67f, 1);
}
} // namespace nativetv
