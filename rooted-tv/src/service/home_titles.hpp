#pragma once

#include "core/json.hpp"
#include <mutex>
#include <string>

namespace rootedtv {
namespace service {

// Each Home profile's own titles, as Google TV keeps them for an account: its watchlist, the titles
// it liked (each with the titles TMDB finds similar, kept from when it was liked), the ones it
// said were not for it, which Home then leaves out, and the ones it has watched. Kept in
// home-titles.json beside the server.
class HomeTitles {
  public:
    explicit HomeTitles(std::string dataRoot);

    // {"watchlist": [items], "liked": [items with "similar"], "disliked": ["movie:42", ...],
    //  "watched": ["movie:42", ...]}, each newest first.
    Json state(const std::string &profile);
    // `op` is watch, unwatch, like, dislike, unrate, seen (watched) or unseen; `item` a title as the
    // feed lists it ({id, type, title, year, rating, overview, poster, backdrop}), and for like and
    // watch its `similar`.
    // False with `error` set when either is not one.
    bool apply(const std::string &profile, const std::string &op, const Json &item,
               std::string &error);
    // The feed's rows for a profile's `state`: without the titles it said were not for it, the top
    // picks led by titles like the ones it liked and put on its watchlist, then its watchlist
    // ("watchlist") and the titles like the one it liked last ("liked", with its title). The top
    // picks and the liked title's row leave out what it has watched.
    static Json personalRows(const Json &rows, const Json &state);
    // The keys of each list, for the TV app to mark a title's buttons: {"watchlist": ["movie:42"],
    // "liked": [...], "disliked": [...], "watched": [...]}.
    static Json summary(const Json &state);
    // A title cut to the fields Home shows, or null when it is not a film or series of TMDB's.
    static Json trimmed(const Json &item);
    // "movie:42"
    static std::string keyOf(const Json &item);

  private:
    void loadLocked();
    void saveLocked();

    std::string path_;
    std::mutex mutex_;
    bool loaded_ = false;
    Json profiles_; // {profile: {"watchlist", "liked", "disliked", "watched"}}
};

} // namespace service
} // namespace rootedtv
