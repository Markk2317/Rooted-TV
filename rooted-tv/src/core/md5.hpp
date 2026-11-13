#pragma once

#include <string>

namespace rootedtv {

// Stable lowercase MD5 encoding used for cache filenames shared with Node.
std::string md5Hex(const std::string &text);

} // namespace rootedtv
