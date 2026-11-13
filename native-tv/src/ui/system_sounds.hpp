#pragma once

namespace nativetv {
// Google TV's system sounds (Settings > Display & Sound > System sounds). Android plays one file,
// AOSP's Effect_Tick, for a focus move and a click alike (core/res/res/xml/audio_assets.xml, the
// same on a Chromecast with Google TV), and Google TV's launcher plays it too where it has sounds
// of its own (app launch, select, Home's return to its start) while Google keeps those off, as
// its enable_gtv_sound_effects flag does by default.
class SystemSounds {
  public:
    virtual ~SystemSounds() = default;
    virtual void tick() = 0;
};
} // namespace nativetv
