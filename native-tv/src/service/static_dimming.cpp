#include "service/static_dimming.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace nativetv {
namespace service {

StaticDimming::Level StaticDimming::levelOf(const std::string &name) {
    if (name == "light") return Level::Light;
    if (name == "strong") return Level::Strong;
    return Level::Off;
}

const char *StaticDimming::nameOf(Level level) {
    return level == Level::Light ? "light" : level == Level::Strong ? "strong" : "off";
}

bool StaticDimming::observe(const std::vector<uint8_t> &luma, int width, int height) {
    const size_t count = static_cast<size_t>(width > 0 && height > 0 ? width * height : 0);
    if (!count || luma.size() != count) return present_;
    if (width != width_ || height != height_ || previous_.size() != count) {
        width_ = width;
        height_ = height;
        previous_ = luma;
        still_.assign(count, 0);
        moving_ = 0;
        present_ = false;
        return false;
    }
    size_t changed = 0;
    for (size_t i = 0; i < count; ++i) {
        if (std::abs(static_cast<int>(luma[i]) - static_cast<int>(previous_[i])) > kChangeLuma) {
            ++changed;
            still_[i] = 0;
        } else if (still_[i] < 0xffff) {
            ++still_[i];
        }
    }
    previous_ = luma;
    // A picture that stands still is the panel's own ASBL's to dim; an element only counts on a
    // picture that has been moving for a while.
    if (static_cast<double>(changed) < kMovingShare * static_cast<double>(count)) {
        moving_ = 0;
        present_ = false;
        return false;
    }
    if (moving_ < kMovingCaptures) ++moving_;
    if (moving_ < kMovingCaptures) {
        present_ = false;
        return false;
    }
    int element = 0;
    for (size_t i = 0; i < count; ++i)
        if (still_[i] >= kStillCaptures && luma[i] >= kBrightLuma) ++element;
    present_ = element >= kElementPixels;
    return present_;
}

int StaticDimming::light(Level level, int observed) {
    // The owner moved the light while it was lowered: theirs from now on, and nothing more is done
    // until the element has gone.
    if (applied_ >= 0 && observed >= 0 && observed != applied_) {
        own_ = observed;
        applied_ = -1;
        heldByOwner_ = present_;
        return -1;
    }
    if (!present_) heldByOwner_ = false;
    const bool dim = level != Level::Off && present_ && !heldByOwner_;
    if (applied_ < 0) {
        if (!dim || observed <= 0) return -1;
        own_ = observed;
        applied_ = observed;
    }
    const double share = level == Level::Strong ? kStrongShare : kLightShare;
    const int target = dim ? static_cast<int>(std::lround(own_ * (1.0 - share))) : own_;
    int next = applied_;
    if (next > target) next = std::max(target, next - kStep);
    else if (next < target) next = std::min(target, next + kRestoreStep);
    if (next == own_ && !dim) {
        // Back at the owner's own value: nothing lowered any more.
        const bool moved = applied_ != own_;
        applied_ = -1;
        return moved ? own_ : -1;
    }
    if (next == applied_) return -1;
    applied_ = next;
    return next;
}

void StaticDimming::forget() {
    previous_.clear();
    still_.clear();
    width_ = height_ = 0;
    moving_ = 0;
    present_ = false;
}

namespace {
uint32_t little(const std::string &bytes, size_t at, int size) {
    uint32_t value = 0;
    for (int i = size - 1; i >= 0; --i)
        value = (value << 8) | static_cast<uint8_t>(bytes[at + static_cast<size_t>(i)]);
    return value;
}
} // namespace

bool bmpLuma(const std::string &bytes, std::vector<uint8_t> &luma, int &width, int &height) {
    if (bytes.size() < 54 || bytes[0] != 'B' || bytes[1] != 'M') return false;
    const uint32_t offset = little(bytes, 10, 4);
    const int32_t w = static_cast<int32_t>(little(bytes, 18, 4));
    const int32_t h = static_cast<int32_t>(little(bytes, 22, 4));
    const uint32_t bits = little(bytes, 28, 2);
    const uint32_t compression = little(bytes, 30, 4);
    if (w <= 0 || w > 4096 || h == 0 || std::abs(h) > 4096 || (bits != 24 && bits != 32) ||
        (compression != 0 && !(compression == 3 && bits == 32)))
        return false;
    const int rows = std::abs(h);
    const size_t pixelBytes = bits / 8;
    const size_t stride = (static_cast<size_t>(w) * pixelBytes + 3) & ~static_cast<size_t>(3);
    if (offset > bytes.size() || bytes.size() - offset < stride * static_cast<size_t>(rows))
        return false;
    luma.resize(static_cast<size_t>(w) * static_cast<size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        // Bottom-up unless the height is negative.
        const int source = h > 0 ? rows - 1 - row : row;
        const size_t base = offset + stride * static_cast<size_t>(source);
        for (int x = 0; x < w; ++x) {
            const size_t at = base + static_cast<size_t>(x) * pixelBytes;
            const int blue = static_cast<uint8_t>(bytes[at]);
            const int green = static_cast<uint8_t>(bytes[at + 1]);
            const int red = static_cast<uint8_t>(bytes[at + 2]);
            luma[static_cast<size_t>(row) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                static_cast<uint8_t>((red * 54 + green * 183 + blue * 19) >> 8);
        }
    }
    width = w;
    height = rows;
    return true;
}

} // namespace service
} // namespace nativetv
