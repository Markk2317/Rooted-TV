#pragma once

#include "core/json.hpp"
#include "service/assistant.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace nativetv {
namespace service {

// Google TV's Top picks for you, chosen by Gemini with the owner's own key: what the profile has
// watched from Home, liked and put on its watchlist, and how long it played each streaming
// service's app lately, goes to Gemini with the titles Home has on offer (the week's trending ones
// and the popular ones on the TV's services), and Gemini orders up to 12 of them for that profile,
// each with why ("Because you watched The Dark Knight"). Only titles, years, kinds and services'
// times are sent. The answer is kept in `home-picks.json`, per profile, for a
// day or until the profile's titles change; asking runs on a worker, so Home never waits for it.
class HomePicks {
  public:
    static constexpr size_t kPicksMax = 12, kCandidatesMax = 90;
    static constexpr int64_t kLifetimeMs = 24LL * 60 * 60 * 1000, kRetryMs = 60LL * 60 * 1000;

    explicit HomePicks(std::string dataRoot, Assistant::Transport transport = Assistant::Transport(),
                       std::function<int64_t()> clock = std::function<int64_t()>());
    ~HomePicks();
    HomePicks(const HomePicks &) = delete;
    HomePicks &operator=(const HomePicks &) = delete;

    // The picks kept for `profile`: [{"key": "movie:155", "because"}], best first, or an empty
    // array. When there are none yet, or they are a day old or were made from other titles, Gemini
    // is asked again in the background. `history`: the profile's titles, [{"title", "type",
    // "year", "why": "watched" | "liked" | "watchlist"}]; `services`: the time it played each
    // service's app, most first ([{"service", "playedMs"}], HomeWatching::services);
    // `candidates`: the feed's items.
    Json picks(const std::string &key, const std::string &model, const std::string &profile,
               const Json &history, const Json &services, const Json &candidates,
               const std::string &region, const std::string &language = std::string());
    // Blocks until a running request has answered (tests).
    void wait();

    // The request Gemini is sent, and its answer read back to picks that are in `candidates`.
    // `language` is the TV's ("it-IT"), which the reasons are written in; "" is English.
    static std::string prompt(const Json &history, const Json &services, const Json &candidates,
                              const std::string &region, const std::string &language = std::string());
    static Json parse(const std::string &answer, const Json &candidates);
    static std::string keyOf(const Json &item);

  private:
    void ask(std::string key, std::string model, std::string profile, std::string inputs,
             Json history, Json services, Json candidates, std::string region,
             std::string language);
    Json load() const;
    void save(const Json &document) const;

    std::string path_;
    Assistant::Transport transport_;
    std::function<int64_t()> clock_;
    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic<bool> busy_{false};
    std::string failedInputs_; // what failed last, not asked again for kRetryMs
    int64_t failedAt_ = 0;
};

} // namespace service
} // namespace nativetv
