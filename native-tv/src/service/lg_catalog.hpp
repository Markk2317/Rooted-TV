#pragma once

#include "core/json.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace nativetv {
namespace service {

// LG's own catalogue, the one LG's Search and LG Content Store answer from: the films and series
// of the streaming apps it knows in the TV's country, each with the content ID its app takes, and
// the apps of LG Content Store. The server asks through the TV's sdx service
// (com.webos.service.sdx/send) as LG's Search app does; sdx signs the request with the TV's own
// credentials, which never reach the server.

// sdx/send's payload for a catalogue search; `domains` is a comma-separated list of LG's domains
// ("movie", "tvshow", "app", "premium").
Json lgCatalogSearchRequest(const std::string &query, const std::string &domains, int maxResults);
// sdx/send's payload for an LG Content Store app's page.
Json lgStoreAppRequest(const std::string &appId);

// The documents of a search reply: a film or series as {"domain", "title", "appId", "contentId"},
// the app that has it and the ID that app takes; an app as lgStoreApps lists it. Null when sdx or
// LG's server failed.
Json lgCatalogDocuments(const Json &reply);

// The content ID each app has a film ("movie") or series ("tv") under, {"<appId>": "<contentId>"},
// from the documents whose title is one of `titles`, compared by their letters and digits without
// case. The first document of an app wins, as LG's Search ranks them.
Json lgCatalogTitleIds(const Json &documents, const std::string &type,
                       const std::vector<std::string> &titles);

// The contentTarget that puts the title on screen in `service`'s app from LG's content ID, or ""
// when the ID is not one that app takes. Prime Video plays a film from "watch?gti=" and opens a
// series' page from its GTI; Disney+ plays a film from LG's "page=play" ID and takes a series' as
// "page=browse"; Netflix opens the title's page, with Play in focus, whatever it is given.
std::string lgCatalogTarget(const std::string &service, const std::string &type,
                            const std::string &contentId);

// LG Content Store's apps among a search's documents, at most `limit`: [{"id", "title", "icon",
// "color", "price", "free", "rating"}], the rating out of 10.
Json lgStoreApps(const Json &documents, size_t limit);
// An app's page from sdx's reply to lgStoreAppRequest: {"id", "title", "icon", "color", "seller",
// "category", "description", "version", "price", "free", "rating", "ratings", "size",
// "installedSize", "pictures": [url], "premium"}. Null when it failed.
Json lgStoreApp(const Json &reply);

// LG Content Store's pictures (an app's icon and its screenshots) by a short reference the TV app
// can ask the server's image proxy for: "lgstore:icon/20230315/37810399.png" for
// https://ngfts.lge.com/...&file_path=/appstore/app/icon/20230315/37810399.png, "" for any other
// address. lgStoreImageUrl gives the address back, "" for anything lgStoreImageRef would not make.
std::string lgStoreImageRef(const std::string &url);
std::string lgStoreImageUrl(const std::string &ref);

// One message of com.webos.appInstallService/install's subscription as {"state", "progress"}:
// state "downloading" (progress 0 to 100), "installing", "installed" or "failed" (with "error",
// the install service's own words).
Json lgInstallProgress(const Json &message);

// The catalogue with a day's memory of each title's IDs and each app's page, so that opening a
// title or an app again asks LG once. `call` runs sdx/send and gives its reply.
class LgCatalog {
  public:
    using Caller = std::function<bool(const Json &payload, Json &reply)>;
    using Clock = std::function<int64_t()>;

    explicit LgCatalog(Caller call, Clock clock = Clock());

    // {"<appId>": "<contentId>"} for the film or series `key` ("movie:42"), looked up under the
    // first of `titles` (the title in the TV's language first) and matched against all of them.
    Json titleIds(const std::string &key, const std::string &type,
                  const std::vector<std::string> &titles);
    // LG Content Store's apps whose name has `query`, as lgStoreApps lists them; null on failure.
    Json searchApps(const std::string &query, size_t limit);
    // An app's page, as lgStoreApp gives it; null on failure.
    Json app(const std::string &appId);

  private:
    struct Entry {
        int64_t at = 0;
        Json value;
    };
    bool cached(const std::string &key, Json &value);
    void remember(const std::string &key, const Json &value);

    Caller call_;
    Clock clock_;
    std::mutex mutex_;
    std::map<std::string, Entry> cache_;
};

} // namespace service
} // namespace nativetv
