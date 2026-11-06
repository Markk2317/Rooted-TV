#pragma once

#include "core/json.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace nativetv {
namespace service {

// The owner's photos for the Photos screen saver, Google TV's ambient mode with the owner's own
// pictures: JPEGs of at most 1920 x 1080 the dashboard prepares, kept in screensaver-photos/ in
// the data directory as photo-<n>.jpg, at most kScreensaverPhotosMax of them and
// kScreensaverPhotosBytes in all.
constexpr size_t kScreensaverPhotosMax = 60;
constexpr uint64_t kScreensaverPhotosBytes = 60ULL * 1024 * 1024;

struct ScreensaverPhoto {
    bool found = false;
    std::string bytes, etag;
};

// The photos' numbers, oldest first.
std::vector<unsigned> screensaverPhotoIds(const std::string &dataRoot);
// {"count", "bytes", "ids": [...]}.
Json screensaverPhotoStatus(const std::string &dataRoot);
// Saves a validated JPEG as the next photo; false with `error` (for the dashboard) otherwise.
bool saveScreensaverPhoto(const std::string &dataRoot, const std::string &bytes,
                          std::string &error);
ScreensaverPhoto readScreensaverPhoto(const std::string &dataRoot, unsigned id);
// One photo, or every one when `id` is 0.
bool removeScreensaverPhotos(const std::string &dataRoot, unsigned id);

} // namespace service
} // namespace nativetv
