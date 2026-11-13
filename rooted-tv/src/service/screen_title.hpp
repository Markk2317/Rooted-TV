#pragma once

#include "core/json.hpp"

#include <string>

namespace rootedtv {
namespace service {

// Continue watching for what was started inside a streaming app (Netflix, Prime Video, Disney+),
// whose apps tell no other app what they play: when playback in one of them pauses or stops, the
// server captures the screen, where the app shows the title (its pause screen, or its page once
// playback stops), and Gemini, with the owner's key, reads the title off it. These are the parts
// that need no TV: the question, reading its answer and finding the title among TMDB's results.
namespace screen_title {

// The question sent with the screenshot.
std::string prompt(const std::string &serviceName);
// {"title", "type": "movie" | "tv", "year"} from Gemini's answer, or null when it saw no title.
Json parse(const std::string &answer);
// The TMDB search result that is the title seen: its kind first, then its year, else the first
// of its kind; null when none has its name.
Json match(const Json &results, const Json &seen);
// Standard base64, for the screenshot inside the request.
std::string base64(const std::string &bytes);

} // namespace screen_title
} // namespace service
} // namespace rootedtv
