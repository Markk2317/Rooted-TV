#pragma once

#include <string>

namespace rootedtv {
namespace daemon {

// Persist the legacy media_seen latch without changing its plain-text format.
// Repeated calls leave an already-correct marker untouched.
bool markMediaSeen(const std::string &path, std::string *error = nullptr);

} // namespace daemon
} // namespace rootedtv
