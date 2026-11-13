#pragma once

#include "core/json.hpp"
#include <string>
#include <vector>

namespace rootedtv {
namespace say {
std::string msg(const std::string &key, const std::string &english,
                const Json &variables = Json());
std::string list(const std::vector<std::string> &items);
} // namespace say
} // namespace rootedtv
