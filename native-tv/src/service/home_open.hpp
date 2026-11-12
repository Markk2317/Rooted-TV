#pragma once

#include "core/json.hpp"
#include <cstdint>
#include <string>

namespace nativetv {
namespace service {

// What a Home open request does. Only what the catalogue lists can be opened,
// so this is not a way to launch anything at all.
struct HomeOpenPlan {
    bool handled = false; // false: Node still owns the kind; forward the request.
    bool ok = false;      // handled && ok: carry out the action; else report error.
    std::string error;    // localized, for handled && !ok
    std::string action;   // "launch" or "channel"
    std::string appId;    // launch target for app and device tiles
    Json launchParams;    // optional application launch parameters
    std::string channelId; // openChannel target for channel tiles
    std::string row;      // recent-history row stamped when it opens
    std::string rowId;    // recent-history id stamped when it opens
    std::string service;  // a streaming service's title: the service's key
};

// LG's Home escape stays on Node until the lifecycle slice.
bool homeOpenHandledKind(const std::string &kind, const std::string &id);

// Decide what an open request does from the catalogue sources. An app tile
// launches its launch point, a device opens its HDMI input, a channel tunes,
// and YouTube videos and Netflix titles create app-specific launch parameters.
// A streaming service's title is planned by planHomeServiceOpen below.
HomeOpenPlan planHomeOpen(const std::string &kind, const std::string &id,
                          const Json &apps, const Json &devices,
                          const Json &channels);

// Complete the external Wikidata lookup before launching Netflix. The native
// HomeContent service returns the same P1874 link data as the public endpoint.
void resolveHomeNetflixOpen(HomeOpenPlan &plan, const Json &lookup);

// A title from a streaming service's row opens the service's app, which must be installed.
// resolveHomeCatalogueOpen puts it on the title where LG's catalogue has it in that app
// (service::LgCatalog's IDs, {"<appId>": "<contentId>"}), playing it where the app can; failing
// that, resolveHomeServiceOpen does where a lookup gave its ID there (Netflix's numeric ID, Prime
// Video's GTI). Without either, the app opens on its own Home. `type` is "movie" or "tv".
HomeOpenPlan planHomeServiceOpen(const std::string &service, const Json &apps);
bool resolveHomeCatalogueOpen(HomeOpenPlan &plan, const Json &ids, const std::string &type);
void resolveHomeServiceOpen(HomeOpenPlan &plan, const Json &lookup, const std::string &type = "");

// An opened row is recorded only while Native Tv Home is on and the row sorts
// by Last opened.
bool homeOpenStampWanted(const Json &document, const std::string &row);

// A device tile opened its input; the input arriving in front within this
// window is that tile, not the remote, so its port stamp is skipped.
int64_t homeTileOpenWindowMilliseconds();

bool homeTileInputFresh(int64_t now, int64_t tileAt);

} // namespace service
} // namespace nativetv
