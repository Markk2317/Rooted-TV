#pragma once

#include "core/json.hpp"
#include "service/static_dimming.hpp"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace rootedtv {
namespace daemon {
class Logger;

// Runs service::StaticDimming on the TV: a 64 x 36 capture of the whole screen every 10 s while
// another app's picture is in front, the OLED light read only while an element is there or the
// light is lowered, and the owner's own light kept in `statePath` while it is lowered, so a
// restart of the server puts it back. LG keeps an OLED light per input and picture mode (the
// settings' "dimension"): the one lowered is written by its dimension, and comes back as soon as
// the picture on screen is of another.
class StaticDimmingRunner {
  public:
    struct Deps {
        std::function<std::string()> level;        // "off", "light" or "strong"
        std::function<bool()> watching;            // a picture worth judging is in front
        std::function<bool(std::string &)> capture; // a BMP of the screen
        // The picture on screen's OLED light and dimension: {"value", "dimension"}, value -1 when
        // unknown.
        std::function<Json()> readLight;
        std::function<int(const Json &dimension)> readLightOf; // -1 when unknown
        std::function<bool(const Json &dimension, int light)> setLightOf;
        std::string statePath;
        Logger *logger = nullptr;
        unsigned intervalMilliseconds = 10000;
    };

    explicit StaticDimmingRunner(Deps deps);
    ~StaticDimmingRunner();
    StaticDimmingRunner(const StaticDimmingRunner &) = delete;
    StaticDimmingRunner &operator=(const StaticDimmingRunner &) = delete;

    void start();
    void stop();
    // One pass, as the thread runs it every interval.
    void step();

  private:
    void run();
    void remember();

    Deps deps_;
    service::StaticDimming dimming_;
    Json dimension_; // the lowered light's, while lowered
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
    bool remembered_ = false;
    int rememberedOwn_ = -1, rememberedApplied_ = -1; // what statePath holds, written on a change only
};

} // namespace daemon
} // namespace rootedtv
