#pragma once
#include "core/json.hpp"
#include "core/operations.hpp"
#include <string>

namespace rootedtv {
std::string legacyRoute(Operation operation, const Json &arguments = Json());
std::string legacyAssetRoute(const std::string &asset);
} // namespace rootedtv
