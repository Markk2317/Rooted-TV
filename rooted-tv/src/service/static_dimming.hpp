#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rootedtv {
namespace service {

// Logo dimming where the panel has none (the OLEDs of webOS 3.4, whose picture settings have no
// logoLuminanceAdjust): from a small capture of the whole screen every few seconds it finds a bright
// element that stays put while the rest of the picture moves (a channel's logo, a score, a game's
// HUD, a news ticker's frame) and lowers the OLED light while one is there, the way LG's Logo
// Luminance and GSR dim a stationary element on later panels. A whole picture that stands still
// (a pause, a menu) is left to the panel's own ASBL, which every LG OLED has.
class StaticDimming {
  public:
    enum class Level { Off, Light, Strong };
    static Level levelOf(const std::string &name); // "light", "strong", anything else Off
    static const char *nameOf(Level level);

    // A pixel whose luma moves by more than this between two captures has changed.
    static constexpr int kChangeLuma = 10;
    // Below this share of changed pixels the whole picture is taken as still.
    static constexpr double kMovingShare = 0.10;
    // Captures in a row the picture must move before a still element counts, so a picture that
    // stood still a while (every pixel long unchanged) is not taken for one as it starts again.
    static constexpr int kMovingCaptures = 3;
    // Captures a pixel must stay unchanged to belong to an element: two minutes at 10 s.
    static constexpr int kStillCaptures = 12;
    // Darker pixels do not wear the panel enough to dim for (black bars, a dark logo).
    static constexpr int kBrightLuma = 90;
    // Pixels of a 64 x 36 capture (each about 30 x 30 on the panel) an element needs: a channel
    // logo of about 150 x 80 covers 15.
    static constexpr int kElementPixels = 6;
    // The OLED light is lowered by this share of the owner's own value, a step per capture.
    static constexpr double kLightShare = 0.15, kStrongShare = 0.30;
    // Points of OLED light per capture: down slowly, as LG's own dimming, back up faster.
    static constexpr int kStep = 4, kRestoreStep = 10;

    // One capture: `luma` holds width * height values, row by row. Returns true while a still
    // element is on a moving picture.
    bool observe(const std::vector<uint8_t> &luma, int width, int height);
    // The OLED light to set now, or -1 to leave it: `observed` is the light the TV reports (-1 when
    // unknown). A light the owner changed while it was lowered becomes their own value, and the
    // dimming starts again from there once the element has gone. Off brings the owner's value
    // back and then does nothing.
    int light(Level level, int observed);
    bool lowered() const { return applied_ >= 0; }
    // The owner's value while lowered, -1 otherwise: kept by the caller so a restart can put it back.
    int ownLight() const { return applied_ >= 0 ? own_ : -1; }
    int appliedLight() const { return applied_; }
    void restore(int own, int applied) {
        own_ = own;
        applied_ = applied;
    }
    // A new start (the TV app in front, the screen off): the history of the pixels is dropped.
    void forget();

  private:
    std::vector<uint8_t> previous_;
    std::vector<uint16_t> still_;
    int width_ = 0, height_ = 0;
    int moving_ = 0;
    bool present_ = false;
    bool heldByOwner_ = false; // the owner set the light while an element was there
    int own_ = -1, applied_ = -1;
};

// The luma of an uncompressed 24- or 32-bit BMP (what com.webos.service.tv.capture writes for
// "format":"BMP"), top row first; false for anything else.
bool bmpLuma(const std::string &bytes, std::vector<uint8_t> &luma, int &width, int &height);

} // namespace service
} // namespace rootedtv
