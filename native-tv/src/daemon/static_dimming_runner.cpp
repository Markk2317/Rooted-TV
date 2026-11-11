#include "daemon/static_dimming_runner.hpp"

#include "core/json.hpp"
#include "daemon/logger.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace nativetv {
namespace daemon {

StaticDimmingRunner::StaticDimmingRunner(Deps deps) : deps_(std::move(deps)) {
    // A server stopped with the light lowered: the owner's value comes back on the next pass.
    std::ifstream input(deps_.statePath.c_str());
    if (!input) return;
    std::stringstream text;
    text << input.rdbuf();
    const Json state = Json::parse(text.str());
    if (state["own"].isNumber() && state["applied"].isNumber() && state["dimension"].isObject()) {
        dimension_ = state["dimension"];
        rememberedOwn_ = static_cast<int>(state["own"].number());
        rememberedApplied_ = static_cast<int>(state["applied"].number());
        dimming_.restore(rememberedOwn_, rememberedApplied_);
        remembered_ = true;
    }
}

StaticDimmingRunner::~StaticDimmingRunner() {
    stop();
}

void StaticDimmingRunner::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_.joinable()) return;
    stopping_ = false;
    thread_ = std::thread(&StaticDimmingRunner::run, this);
}

void StaticDimmingRunner::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void StaticDimmingRunner::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
        lock.unlock();
        step();
        lock.lock();
        wake_.wait_for(lock, std::chrono::milliseconds(deps_.intervalMilliseconds),
                       [this]() { return stopping_; });
    }
}

void StaticDimmingRunner::step() {
    using service::StaticDimming;
    const StaticDimming::Level level =
        StaticDimming::levelOf(deps_.level ? deps_.level() : std::string());
    if (level == StaticDimming::Level::Off && !dimming_.lowered()) {
        dimming_.forget();
        return;
    }
    bool present = false;
    std::string bytes;
    std::vector<uint8_t> luma;
    int width = 0, height = 0;
    if (level != StaticDimming::Level::Off && deps_.watching && deps_.watching() &&
        deps_.capture && deps_.capture(bytes) && service::bmpLuma(bytes, luma, width, height))
        present = dimming_.observe(luma, width, height);
    else
        dimming_.forget(); // nothing to judge: whatever was lowered comes back
    if (!present && !dimming_.lowered()) return;
    if (!deps_.readLight || !deps_.readLightOf || !deps_.setLightOf) return;
    const Json current = deps_.readLight();
    int observed = -1;
    if (!dimming_.lowered()) {
        observed = current["value"].isNumber() ? static_cast<int>(current["value"].number()) : -1;
        dimension_ = current["dimension"];
        if (!dimension_.isObject()) return;
    } else {
        // The picture is of another input or mode now: the one lowered comes back.
        if (current["dimension"].isObject() && current["dimension"].dump() != dimension_.dump())
            dimming_.forget();
        observed = deps_.readLightOf(dimension_);
    }
    const bool wasLowered = dimming_.lowered();
    const int next = dimming_.light(level, observed);
    if (next >= 0) {
        if (!deps_.setLightOf(dimension_, next) && deps_.logger)
            deps_.logger->warning("logo dimming: the TV did not take OLED light " +
                                  std::to_string(next));
        else if (deps_.logger && !wasLowered)
            deps_.logger->info("logo dimming: a still element on a moving picture; lowering the "
                               "OLED light from " + std::to_string(dimming_.ownLight()));
        else if (deps_.logger && !dimming_.lowered())
            deps_.logger->info("logo dimming: the element has gone; OLED light back at " +
                               std::to_string(next));
    }
    remember();
}

void StaticDimmingRunner::remember() {
    if (deps_.statePath.empty()) return;
    if (!dimming_.lowered()) {
        if (remembered_) std::remove(deps_.statePath.c_str());
        remembered_ = false;
        return;
    }
    if (remembered_ && rememberedOwn_ == dimming_.ownLight() &&
        rememberedApplied_ == dimming_.appliedLight())
        return;
    Json state = Json::object();
    state.set("own", Json::numeric(dimming_.ownLight()));
    state.set("applied", Json::numeric(dimming_.appliedLight()));
    state.set("dimension", dimension_);
    const std::string temporary = deps_.statePath + ".tmp";
    {
        std::ofstream output(temporary.c_str(), std::ios::trunc);
        if (!output) return;
        output << state.dump();
        if (!output) return;
    }
    if (std::rename(temporary.c_str(), deps_.statePath.c_str()) == 0) {
        remembered_ = true;
        rememberedOwn_ = dimming_.ownLight();
        rememberedApplied_ = dimming_.appliedLight();
    }
}

} // namespace daemon
} // namespace nativetv
