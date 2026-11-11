#pragma once
#include <cstdint>

namespace nativetv {
// The colour of Google TV's focus glow for a picture, as its launcher picks it: the picture's
// palette (androidx Palette: at most 16 colours by median cut over about 12,500 pixels, near-black,
// near-white and skin tones left out), the colour nearest saturation 0.67 and value 1 for its
// population, set to that saturation and value at its own hue. 0xRRGGBB, or -1 when that colour
// is greyer than saturation 0.25 (or nothing is drawn), where the launcher's glow stays white.
int glowColour(const uint8_t *rgba, int width, int height);
} // namespace nativetv
