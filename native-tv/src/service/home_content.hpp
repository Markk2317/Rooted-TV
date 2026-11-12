#pragma once

#include "core/json.hpp"
#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace nativetv {
namespace service {

class HomeContent {
  public:
    using Fetcher = std::function<bool(const std::string &, Json &, std::string &)>;
    using Clock = std::function<int64_t()>;

    explicit HomeContent(std::string dataRoot, Fetcher fetcher = Fetcher(),
                         Clock clock = Clock());
    // The TV's country ("IT"): a title's details then say which streaming services have it there,
    // as "services". Empty, or anything but two capitals, leaves them out.
    void setRegion(const std::string &region);
    Json details(const std::string &tmdbKey, bool netflixEnabled,
                 const std::string &type, const std::string &id);
    Json search(const std::string &tmdbKey, const std::string &query,
                bool queryPresent = true);
    // Search's "Search by genre": the most popular films of a genre of names::searchGenres, then
    // its series, as {"ok", "enabled", "items"} like a search.
    Json genre(const std::string &tmdbKey, const std::string &genre);
    Json netflix(const std::string &tmdbKey, bool netflixEnabled,
                 const std::string &type, const std::string &id);
    // {"ok", "enabled", "gti"}: the title's Prime Video GTI (Wikidata's P14462), "" when Wikidata
    // has none or several.
    Json prime(const std::string &tmdbKey, bool primeEnabled,
               const std::string &type, const std::string &id);
    // {"ok", "enabled", "url"}: a film's preview on Apple's store, the banner's trailer. Wikidata
    // gives its iTunes movie id (P6398), which the iTunes lookup answers in the TV's country, or
    // else in the US store; failing that, the film is looked for by title and year in Apple's
    // charts of the films bought and rented now, which list their previews. "" when none has one.
    Json trailer(const std::string &tmdbKey, const std::string &id);

  private:
    struct Entry {
        int64_t at = 0;
        int64_t failedAt = 0;
        Json value;
    };
    struct Pending {
        bool done = false;
        Json value;
        std::condition_variable changed;
    };
    using Converter = std::function<Json(const Json &)>;
    using IdCheck = std::function<bool(const std::string &)>;

    Json wikidataLookup(const std::string &tmdbKey, const std::string &type,
                        const std::string &id, const std::string &property,
                        const IdCheck &valid, const std::string &field);

    Json request(const std::string &apiKey, const std::string &resource,
                 const std::string &query, int64_t lifetimeMs,
                 const Converter &convert);
    bool fetch(const std::string &url, Json &document, std::string &error) const;
    void loadCacheLocked();
    void saveCacheLocked();
    std::string urlFor(const std::string &apiKey, const std::string &resource,
                       const std::string &query) const;

    std::string cachePath_;
    Fetcher fetcher_;
    Clock clock_;
    std::mutex mutex_;
    std::condition_variable capacityChanged_;
    std::map<std::string, Entry> cache_;
    std::map<std::string, std::shared_ptr<Pending>> pending_;
    size_t active_ = 0;
    size_t queued_ = 0;
    bool cacheLoaded_ = false;
    std::string region_;
};

} // namespace service
} // namespace nativetv
