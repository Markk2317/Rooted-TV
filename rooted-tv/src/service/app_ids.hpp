#pragma once

#include <string>

namespace rootedtv {
namespace service {

bool appIdIsValid(const std::string &appId);
bool appIdIsProtected(const std::string &appId);
bool appPackageIdIsSafe(const std::string &appId);

} // namespace service
} // namespace rootedtv
