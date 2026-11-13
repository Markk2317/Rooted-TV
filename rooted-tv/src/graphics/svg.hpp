#pragma once
#include <string>
#include <vector>

namespace rootedtv {
struct SvgPixels {
    int width = 0, height = 0;
    std::vector<unsigned char> rgba;
};
SvgPixels rasterSvg(const std::string &path, float scale = 2, bool iconMask = false);
} // namespace rootedtv
