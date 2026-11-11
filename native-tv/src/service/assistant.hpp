#pragma once

#include "core/json.hpp"
#include <functional>
#include <string>

namespace nativetv {
namespace service {

// Google TV's Gemini button, with the owner's own Gemini API key (free, from Google AI Studio): a
// question goes to Gemini with the TV's tools, which can search films and series, choose the
// ones to show, open one in its streaming app and control the TV; their answers go back to Gemini
// until it replies in words.
class Assistant {
  public:
    struct Reply {
        long status = 0;
        Json body;
    };
    // POSTs `body` to `url` with the key; false when no HTTP answer came back.
    using Transport = std::function<bool(const std::string &url, const std::string &key,
                                         const std::string &body, Reply &reply,
                                         std::string &error)>;
    // Runs one of the tools Gemini asked for, by name, and answers what Gemini is told back.
    using Tool = std::function<Json(const std::string &name, const Json &args)>;

    explicit Assistant(Transport transport = Transport());

    // {"ok", "answer"} or {"ok": false, "error"}. `context` says where the TV is (its country, the
    // services it has, the profile), for the instructions.
    Json ask(const std::string &key, const std::string &model, const std::string &question,
             const std::string &context, const Tool &tool);

    // The function declarations Gemini is given.
    static Json tools();
    // The HTTPS POST every request to Gemini goes through.
    static Transport https();

  private:
    Transport transport_;
};

} // namespace service
} // namespace nativetv
