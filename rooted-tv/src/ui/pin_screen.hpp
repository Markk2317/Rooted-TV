#pragma once
#include "ui/screen.hpp"
#include <memory>
#include <string>

namespace rootedtv {
// The admin PIN prompt ("pin"). The application opens it over whatever was showing when the server
// refuses an action with 403 and `pin: true`; it answers null for any other route.
std::unique_ptr<ScreenView> makePinScreen(const std::string &route);
} // namespace rootedtv
