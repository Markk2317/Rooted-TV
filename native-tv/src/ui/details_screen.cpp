// Title details, drawn on the kit over the title's backdrop. Reads and writes go to the routes the
// JavaScript page used (details, netflix, open); every reply is checked against the request it
// answers, so a title left behind cannot fill the one on screen.
#include "ui/details_screen.hpp"
#include "ui/application.hpp"
#include "domain/models.hpp"
#include "domain/names.hpp"
#include "domain/say.hpp"
#include "ui/home_layout.hpp"
#include "ui/kit/overlay.hpp"
#include "ui/kit/tuning.hpp"
#include "ui/optimistic.hpp"
#include "ui/overlay_routes.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace nativetv {
namespace {
namespace layout = kit::layout;
using kit::easing;
using say::msg;

constexpr size_t kMaxDepth = 8;
constexpr float kSimilarW = layout::cardLW, kSimilarH = layout::cardLH, kSimilarGap = layout::cardLGap;
// Under a row's items: a cast member's name and part, a title's two lines.
constexpr float kCastH = layout::castD + layout::castNameGap + 2 * layout::cardCaptionLine,
                kSimilarItemH = layout::cardLH + layout::cardCaptionGap + 2 * layout::cardCaptionLine;
constexpr size_t kOverviewPageBytes = 1100, kGenreRows = 2, kGenreTitles = 12;
constexpr const char *kDetails = "title:details", *kNetflix = "title:netflix", *kGenre = "details:genre:";

bool succeeded(const Response &response, const Json &data) {
    return response.ok() && data.valid() && (!data.has("ok") || data["ok"].boolean());
}
// A page of an overview ends at a space where one can, so a word is never cut.
std::vector<std::string> pagesOf(const std::string &text) {
    std::vector<std::string> pages;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = std::min(text.size(), start + kOverviewPageBytes);
        if (end < text.size()) {
            size_t space = text.rfind(' ', end);
            if (space != std::string::npos && space > start)
                end = space;
            while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
                ++end;
        }
        pages.push_back(text.substr(start, end - start));
        start = text.find_first_not_of(' ', end);
        if (start == std::string::npos)
            break;
    }
    return pages;
}
bool listed(const Json &keys, const std::string &key) {
    for (const auto &entry : keys.items())
        if (entry.string() == key)
            return true;
    return false;
}
bool sameTitle(const Json &item, const std::string &type, const std::string &id) {
    return item["id"].string() == id && (type.empty() || item["type"].string() == type);
}
// "movie:42", as Home's feed lists the profile's titles.
std::string titleKey(const Json &item) {
    return item["type"].string() + ":" +
           (item["id"].isNumber() ? compactNumber(item["id"].number()) : item["id"].string());
}
// A film's trailer, where the server offers them (Apple's previews are of films only).
std::string trailerOf(const ViewState &view, const Json &item) {
    return view.discover["trailers"].boolean() && item["type"].string() == "movie" && item["id"].valid()
               ? "trailer:" + titleKey(item)
               : "";
}
// A genre's titles for a title's row of them, as Google TV mixes films and shows: one of each in
// turn, the title's own kind first, leaving out those `shown` already (the title, the rows above).
Json genreTitles(const Json &found, const Json &item, std::vector<std::string> &shown) {
    std::vector<Json> kinds[2];
    for (const auto &entry : found.items())
        if (std::find(shown.begin(), shown.end(), titleKey(entry)) == shown.end())
            kinds[entry["type"].string() == item["type"].string() ? 0 : 1].push_back(entry);
    Json titles = Json::array();
    for (size_t i = 0; titles.size() < kGenreTitles && (i < kinds[0].size() || i < kinds[1].size()); ++i)
        for (const auto &kind : kinds)
            if (i < kind.size() && titles.size() < kGenreTitles) {
                titles.add(kind[i]);
                shown.push_back(titleKey(kind[i]));
            }
    return titles;
}
kit::Enter enterOf(const Motion &motion, int block) {
    double hidden = motion.value("details:t:" + std::to_string(block));
    return {static_cast<float>(1 - hidden), static_cast<float>(18 * hidden)};
}
} // namespace

constexpr int DetailsScreen::kCastRow; // C++14: push_back takes it by reference

DetailsScreen::DetailsScreen(const std::string &type, const std::string &id, const Json &item,
                             bool embedded)
    : type_(type), id_(id), first_(item), embedded_(embedded) {}

// ---- Data ------------------------------------------------------------------------------------

void DetailsScreen::enter(ScreenHost &host) {
    Json item = first_;
    if (!item.isObject()) {
        // The card Home showed carries what the page can draw before the details arrive.
        const ViewState &view = host.view();
        for (const auto &row : view.home.rows)
            for (const auto &card : row.cards)
                if (card.kind == "title" && sameTitle(card.data, type_, id_) && !item.isObject())
                    item = card.data;
        if (!item.isObject()) {
            item = Json::object();
            item.set("type", Json::text(type_));
            item.set("id", Json::text(id_));
        }
    }
    push(host, item);
}
void DetailsScreen::leave(ScreenHost &host) {
    host.services().cancel(kDetails);
    host.services().cancel(kNetflix);
    cancelGenres(host);
    host.motion().erasePrefix("details:");
}
void DetailsScreen::rebuildActions(ScreenHost &host, Title &title) {
    const ViewState &view = host.view();
    const Json &item = title.item;
    const bool kept = title.action < title.actions.size();
    const Action::Kind keepKind = kept ? title.actions[title.action].kind : Action::Kind::Retry;
    const std::string keepService = kept ? title.actions[title.action].service : "";
    title.actions.clear();
    if (!item["netflixId"].string().empty())
        title.actions.push_back({Action::Kind::Netflix, msg("tv.details.netflix", "Open on Netflix")});
    if (!title.actions.empty())
        title.actions.back().icon = "play";
    // As Google TV's watch options: each service that has the title in the TV's country and whose
    // app is installed, the row the title came from first. On Netflix, the Netflix link above
    // opens it already.
    std::vector<std::string> services;
    if (item["service"].isString())
        services.push_back(item["service"].string());
    for (const auto &key : item["services"].items())
        if (std::find(services.begin(), services.end(), key.string()) == services.end())
            services.push_back(key.string());
    for (const std::string &key : services) {
        const names::StreamingService *service = names::streamingService(key);
        if (!service || (key == "netflix" && !item["netflixId"].string().empty()) ||
            names::streamingApp(*service, view.catalogue["apps"]).empty())
            continue;
        Action open{Action::Kind::Service,
                    msg("tv.details.service", "Open on {service}",
                        fields({{"service", Json::text(service->name)}}))};
        open.service = key;
        open.icon = "play";
        title.actions.push_back(open);
    }
    if (!item["trailer"].string().empty()) {
        title.actions.push_back({Action::Kind::Trailer, msg("tv.details.trailer", "Trailer")});
        title.actions.back().icon = "movie";
    }
    // The profile's own marks, as on Google TV's details: the watchlist, then thumbs up and down.
    if (view.discover["titlesEnabled"].boolean() && !title.error) {
        const Marks marks = marksOf(host, item);
        Action watchlist{Action::Kind::Watchlist,
                         marks.watchlisted ? msg("tv.details.watchlisted", "On watchlist")
                                           : msg("tv.details.watchlist", "Watchlist")};
        watchlist.icon = marks.watchlisted ? "bookmarked" : "bookmarkadd";
        title.actions.push_back(watchlist);
        Action like{Action::Kind::Like, marks.rating == "like" ? msg("tv.details.liked", "Liked")
                                                               : msg("tv.details.like", "Like")};
        like.icon = "thumbup";
        like.iconOnly = true;
        title.actions.push_back(like);
        Action dislike{Action::Kind::Dislike,
                       marks.rating == "dislike" ? msg("tv.details.disliked", "Not for me (marked)")
                                                 : msg("tv.details.dislike", "Not for me")};
        dislike.icon = "thumbdown";
        dislike.iconOnly = true;
        title.actions.push_back(dislike);
    }
    if (!item["overview"].string().empty()) {
        title.actions.push_back({Action::Kind::Overview, msg("tv.details.overview", "Full overview")});
        title.actions.back().icon = "info";
    }
    if (title.error)
        title.actions.push_back({Action::Kind::Retry, msg("tv.details.retry", "Retry")});
    // Until the focus is moved it stays on the first action, which a late Netflix link or the
    // details' trailer can put in front of the overview; after that it stays on what it was on,
    // whatever its label says now.
    title.action = std::min(title.action, title.actions.size() - 1);
    if (!title.moved)
        title.action = 0;
    else
        for (size_t i = 0; i < title.actions.size(); ++i)
            if (title.actions[i].kind == keepKind && title.actions[i].service == keepService)
                title.action = i;
}
void DetailsScreen::push(ScreenHost &host, const Json &item) {
    if (stack_.size() >= kMaxDepth) {
        host.toast(msg("tv.details.goBack", "Go back before opening another title"));
        return;
    }
    Title title;
    title.item = item;
    rebuildActions(host, title);
    stack_.push_back(title);
    host.services().cancel(kNetflix);
    cancelGenres(host); // the title underneath asks again for any still on their way when it is back
    load(host);
    for (const char *prefix : {"details:a:", "details:s", "details:c", "details:rg:", "details:vy"})
        host.motion().erasePrefix(prefix);
    focusMoved(host, true);
    playEntrance(host);
    host.invalidate();
}
void DetailsScreen::load(ScreenHost &host) {
    Title &title = top();
    if (title.loading)
        return;
    title.loading = true;
    title.error = false;
    title.status = msg("tv.details.loading", "Loading details…");
    if (title.actions.size() && title.actions[title.action].kind == Action::Kind::Retry)
        title.action = 0;
    rebuildActions(host, title);
    title.detailsGeneration = host.services().read(kDetails, Operation::TitleDetails, title.item);
    if (!title.detailsGeneration) {
        title.loading = false;
        title.error = true;
        title.status = msg("tv.details.unavailable", "Details are unavailable. Retry.");
        rebuildActions(host, title);
    }
    host.invalidate();
}
void DetailsScreen::fillShelves(ScreenHost &host, Title &title) {
    title.shelves.clear();
    Shelf similar;
    similar.items = title.item["similar"].isArray() ? title.item["similar"] : Json::array();
    title.shelves.push_back(similar);
    for (const auto &key : title.item["genreKeys"].items()) {
        if (title.shelves.size() > kGenreRows || !names::searchGenre(key.string()))
            continue;
        Shelf genre;
        genre.genre = key.string();
        genre.generation = 1; // asked for below
        title.shelves.push_back(genre);
    }
    askGenres(host, title);
}
void DetailsScreen::askGenres(ScreenHost &host, Title &title) {
    for (size_t k = 0; k < title.shelves.size(); ++k)
        if (!title.shelves[k].genre.empty() && title.shelves[k].generation)
            title.shelves[k].generation = host.services().read(
                kGenre + std::to_string(k), Operation::SearchTitles,
                fields({{"genre", Json::text(title.shelves[k].genre)}}));
}
void DetailsScreen::cancelGenres(ScreenHost &host) {
    for (size_t k = 1; k <= kGenreRows; ++k)
        host.services().cancel(kGenre + std::to_string(k));
}
void DetailsScreen::response(ScreenHost &host, const Response &response) {
    if (stack_.empty())
        return;
    Title &title = top();
    Json data = response.json();
    if (response.channel == kDetails && title.detailsGeneration &&
        response.generation == title.detailsGeneration) {
        title.loading = false;
        title.detailsGeneration = 0;
        if (!succeeded(response, data) || !data["enabled"].boolean() || !data["item"].isObject()) {
            title.error = true;
            title.status = data["error"].string(
                msg("tv.details.unavailable", "Details are unavailable. Retry."));
        } else {
            std::string netflixId = title.item["netflixId"].string();
            const Json service = title.item["service"];
            title.item = data["item"];
            if (!netflixId.empty() && title.item["netflixId"].string().empty())
                title.item.set("netflixId", Json::text(netflixId));
            // The service's row the title came from, which the details do not say.
            if (service.isString())
                title.item.set("service", service);
            title.error = false;
            title.stale = data["stale"].boolean();
            title.status = title.stale ? msg("tv.details.cached", "Saved details · offline") : "";
            if (data["netflixEnabled"].boolean())
                title.netflixGeneration =
                    host.services().read(kNetflix, Operation::NetflixTitle, title.item);
            fillShelves(host, title);
        }
        rebuildActions(host, title);
        host.invalidate();
    } else if (response.channel == kNetflix && title.netflixGeneration &&
               response.generation == title.netflixGeneration) {
        title.netflixGeneration = 0;
        if (succeeded(response, data) && !data["netflixId"].string().empty()) {
            title.item.set("netflixId", data["netflixId"]);
            rebuildActions(host, title);
            focusMoved(host, false);
            host.invalidate();
        }
    } else if (response.channel.compare(0, std::strlen(kGenre), kGenre) == 0) {
        for (size_t k = 0; k < title.shelves.size(); ++k) {
            Shelf &shelf = title.shelves[k];
            if (!shelf.generation || response.generation != shelf.generation ||
                response.channel != kGenre + std::to_string(k))
                continue;
            // A row that comes in above the focused one keeps the focus on the row it was on.
            const int focused = focusedSection(title);
            shelf.generation = 0;
            if (succeeded(response, data))
                shelf.found = data["items"];
            // Each row leaves out what the rows above it have: a series' Action & Adventure is
            // both the Action and the Adventure of the grid.
            std::vector<std::string> shown = {titleKey(title.item)};
            for (Shelf &each : title.shelves) {
                if (each.genre.empty())
                    for (const auto &entry : each.items.items())
                        shown.push_back(titleKey(entry));
                else
                    each.items = genreTitles(each.found, title.item, shown);
                each.focus = std::min(each.focus, each.items.size() ? each.items.size() - 1 : 0);
            }
            const std::vector<int> sections = sectionsOf(title);
            const auto at = std::find(sections.begin(), sections.end(), focused);
            if (title.row)
                title.row = at != sections.end() ? static_cast<size_t>(at - sections.begin()) + 1
                                                 : std::min(title.row, sections.size());
            focusMoved(host, false);
            host.invalidate();
        }
    }
}

// ---- Keys ------------------------------------------------------------------------------------

std::vector<int> DetailsScreen::sectionsOf(const Title &title) {
    std::vector<int> sections;
    if (title.item["cast"].size())
        sections.push_back(kCastRow);
    for (size_t k = 0; k < title.shelves.size(); ++k)
        if (title.shelves[k].items.size())
            sections.push_back(static_cast<int>(k));
    return sections;
}
int DetailsScreen::focusedSection(const Title &title) {
    const std::vector<int> sections = sectionsOf(title);
    return title.row > 0 && title.row <= sections.size() ? sections[title.row - 1] : kCastRow - 1;
}
const DetailsScreen::Shelf *DetailsScreen::focusedShelf(const Title &title) {
    const int section = focusedSection(title);
    return section >= 0 ? &title.shelves[static_cast<size_t>(section)] : nullptr;
}
DetailsScreen::Shelf *DetailsScreen::focusedShelf(Title &title) {
    const int section = focusedSection(title);
    return section >= 0 ? &title.shelves[static_cast<size_t>(section)] : nullptr;
}
namespace {
// The rows' looks with the focus on `row` (0 the actions): their titles at 36 px, the focused
// row's grown, and each row's title top on the page.
std::vector<home::RowLook> rowLooks(size_t count, size_t row) {
    std::vector<home::RowLook> looks(count);
    for (size_t i = 0; i < count; ++i) {
        looks[i].up = 1;
        looks[i].risen = 1;
        looks[i].grow = row == i + 1 ? 1 : 0;
    }
    return looks;
}
std::vector<float> rowTopsOf(const std::vector<float> &heights, const std::vector<home::RowLook> &looks) {
    std::vector<float> tops;
    float top = layout::detailsRowsTop;
    for (size_t i = 0; i < heights.size(); ++i) {
        tops.push_back(top);
        top += home::labelArea(looks[i]) + heights[i] + layout::rowGapRisen;
    }
    return tops;
}
std::vector<float> sectionHeights(const std::vector<int> &sections) {
    std::vector<float> heights;
    for (const int section : sections)
        heights.push_back(section < 0 ? kCastH : kSimilarItemH);
    return heights;
}
} // namespace

void DetailsScreen::focusMoved(ScreenHost &host, bool jump) {
    Motion &motion = host.motion();
    const Title &title = top();
    auto glide = [&](const std::string &id, double to) {
        if (jump)
            motion.set(id, to);
        else
            motion.tween(id, to, kit::motion::ms::focus, easing(kit::motion::ease::out));
    };
    auto slide = [&](const std::string &id, double to, const kit::SpringTuning &spring) {
        if (jump)
            motion.set(id, to);
        else
            motion.spring(id, to, kit::springParams(spring));
    };
    for (size_t i = 0; i < title.actions.size(); ++i)
        glide("details:a:" + std::to_string(i), title.row == 0 && i == title.action ? 1 : 0);
    const size_t cast = title.item["cast"].size();
    const int focused = focusedSection(title);
    for (size_t i = 0; i < cast; ++i)
        glide("details:c:" + std::to_string(i), focused == kCastRow && i == title.cast ? 1 : 0);
    slide("details:cx",
          kit::geometry::rowScrollTarget(static_cast<int>(title.cast), static_cast<int>(cast),
                                         layout::castD, layout::castGap),
          kit::motion::spring::row);
    for (size_t k = 0; k < title.shelves.size(); ++k) {
        const Shelf &shelf = title.shelves[k];
        const std::string prefix = "details:s" + std::to_string(k);
        for (size_t i = 0; i < shelf.items.size(); ++i)
            glide(prefix + ":" + std::to_string(i), focused == static_cast<int>(k) && i == shelf.focus ? 1 : 0);
        slide(prefix + "x",
              kit::geometry::rowScrollTarget(static_cast<int>(shelf.focus), static_cast<int>(shelf.items.size()),
                                             kSimilarW, kSimilarGap),
              kit::motion::spring::row);
    }
    // Google TV's details rise as the focus goes down: the focused row's title at the top,
    // large, and the page's head gone above it.
    const std::vector<int> sections = sectionsOf(title);
    const std::vector<home::RowLook> looks = rowLooks(sections.size(), title.row);
    const std::vector<float> tops = rowTopsOf(sectionHeights(sections), looks);
    for (size_t i = 0; i < sections.size(); ++i)
        glide("details:rg:" + std::to_string(i), looks[i].grow);
    slide("details:vy",
          title.row == 0 || title.row > tops.size() ? 0 : layout::rowsRisenTop - tops[title.row - 1],
          kit::motion::spring::page);
}
void DetailsScreen::playEntrance(ScreenHost &host) {
    Timeline timeline;
    Tween rise(1, 0, kit::motion::ms::heroText, easing(kit::motion::ease::out));
    for (int block = 0; block < 4; ++block) {
        host.motion().set("details:t:" + std::to_string(block), 1);
        timeline.add("details:t:" + std::to_string(block),
                     rise.delayedBy(80.0 * (block + 1)));
    }
    timeline.play(host.motion());
}
void DetailsScreen::toggleOverview(ScreenHost &host, bool open) {
    overview_ = open;
    if (open) {
        overviewPages_ = pagesOf(top().item["overview"].string());
        overviewPage_ = 0;
    }
    host.motion().tween("details:ov", open ? 1 : 0, kit::motion::ms::fast,
                        easing(kit::motion::ease::out));
    host.invalidate();
}
void DetailsScreen::activate(ScreenHost &host) {
    Title &title = top();
    if (const Shelf *shelf = focusedShelf(title)) {
        push(host, shelf->items[shelf->focus]);
        return;
    }
    // A person has no page here.
    if (title.row > 0 || title.action >= title.actions.size())
        return;
    switch (title.actions[title.action].kind) {
    case Action::Kind::Retry:
        load(host);
        return;
    case Action::Kind::Overview:
        toggleOverview(host, true);
        return;
    case Action::Kind::Watchlist:
    case Action::Kind::Like:
    case Action::Kind::Dislike:
        markTitle(host, title.actions[title.action].kind);
        return;
    default:
        break;
    }
    Json body = Json::object();
    Action::Kind kind = title.actions[title.action].kind;
    if (kind == Action::Kind::Trailer) {
        body.set("kind", Json::text("youtube"));
        body.set("id", title.item["trailer"]);
    } else if (kind == Action::Kind::Service) {
        body.set("kind", Json::text("service"));
        body.set("service", Json::text(title.actions[title.action].service));
        body.set("type", title.item["type"]);
        body.set("id", title.item["id"]);
    } else {
        body.set("kind", Json::text("netflix"));
        body.set("type", title.item["type"]);
        body.set("id", title.item["id"]);
    }
    // A second press while the first is on its way is dropped; a refusal is said by the layer.
    Write write;
    write.operation = Operation::OpenHomeItem;
    write.body = [body](const Json &) { return body; };
    write.failure = msg("tv.details.openFailed", "That did not open");
    host.optimistic().run("details.open", write);
}
DetailsScreen::Marks DetailsScreen::marksOf(ScreenHost &host, const Json &item) {
    const std::string key = titleKey(item);
    const Json feed = host.view().discover["profileTitles"];
    Marks marks;
    marks.watchlisted =
        host.optimistic().flag("details.wl:" + key, listed(feed["watchlist"], key));
    marks.rating = host.optimistic().text("details.rt:" + key,
                                          listed(feed["liked"], key)      ? "like"
                                          : listed(feed["disliked"], key) ? "dislike"
                                                                          : "");
    return marks;
}
// The watchlist and the thumbs change at once on the page and are saved for the profile behind
// it; Home's feed is read back after, so its rows follow, and a refusal puts the mark back.
void DetailsScreen::markTitle(ScreenHost &host, Action::Kind kind) {
    Title &title = top();
    const std::string key = titleKey(title.item);
    const Json feed = host.view().discover["profileTitles"];
    Json item = Json::object();
    for (const char *field : {"id", "type", "title", "year", "rating", "overview", "poster", "backdrop"})
        item.set(field, title.item[field]);
    const bool watch = kind == Action::Kind::Watchlist;
    Write write;
    write.operation = Operation::Control;
    write.body = [item, watch](const Json &target) {
        std::string op = watch ? (target.boolean() ? "watch" : "unwatch") : target.string();
        if (op.empty())
            op = "unrate";
        return fields({{"action", Json::text("setHomeTitle")},
                       {"value", fields({{"op", Json::text(op)}, {"item", item}})}});
    };
    write.readChannel = "discover";
    write.readOperation = Operation::HomeDiscover;
    write.failure = msg("tv.details.markFailed", "That was not saved");
    const Marks shown = marksOf(host, title.item);
    if (watch) {
        const Json server = Json::flag(listed(feed["watchlist"], key));
        host.optimistic().set("details.wl:" + key, Json::flag(!shown.watchlisted), server, write);
        host.optimistic().commit("details.wl:" + key);
    } else {
        const std::string wanted = kind == Action::Kind::Like ? "like" : "dislike";
        const Json server = Json::text(listed(feed["liked"], key)      ? "like"
                                       : listed(feed["disliked"], key) ? "dislike"
                                                                       : "");
        host.optimistic().set("details.rt:" + key,
                              Json::text(shown.rating == wanted ? "" : wanted), server, write);
        host.optimistic().commit("details.rt:" + key);
    }
    rebuildActions(host, title);
    host.invalidate();
}
bool DetailsScreen::update(ScreenHost &host) {
    if (stack_.empty())
        return false;
    Motion &motion = host.motion();
    const double now = host.nowMs();
    const std::string want = trailerOf(host.view(), top().item);
    if (want != trailer_) {
        // Another title on top: its own clip, from the start.
        trailer_ = want;
        trailerFrom_ = keyAt_ = now;
        shownAt_ = -1;
        trailerOver_ = lowered_ = false;
        motion.set("details:tv", 0);
        motion.set("details:low", 0);
    }
    if (trailer_.empty() || trailerOver_ || now - trailerFrom_ < kit::motion::ms::trailerDelay)
        return !trailer_.empty() && !trailerOver_;
    auto end = [&] {
        trailerOver_ = true;
        motion.tween("details:tv", 0, kit::motion::ms::hero, easing(kit::motion::ease::inOut));
        lower(host, false);
        return true;
    };
    // The rows or the full overview have the page: the clip gives way to them.
    if (top().row != 0 || overview_)
        return shownAt_ < 0 ? true : end();
    const ImageView clip = host.picture(trailer_, kit::canvas::width, kit::canvas::height);
    if (clip.failed) // none, or it has played to its end
        return end();
    if (shownAt_ < 0) {
        if (clip.texture) {
            shownAt_ = now;
            motion.tween("details:tv", 1, kit::motion::ms::hero, easing(kit::motion::ease::inOut));
        }
        return true;
    }
    if (!lowered_ && now - std::max(shownAt_, keyAt_) >= kit::motion::ms::trailerLower)
        lower(host, true);
    return true;
}
void DetailsScreen::lower(ScreenHost &host, bool down) {
    if (lowered_ == down)
        return;
    lowered_ = down;
    host.motion().tween("details:low", down ? 1 : 0, kit::motion::ms::detailsLower,
                        easing(kit::motion::ease::inOut));
    host.invalidate();
}
bool DetailsScreen::key(ScreenHost &host, Key key) {
    if (stack_.empty())
        return false;
    keyAt_ = host.nowMs();
    // A key on the lowered page brings it back up and does nothing else, as on Google TV.
    if (lowered_) {
        lower(host, false);
        return true;
    }
    if (overview_) {
        if ((key == Key::Down || key == Key::Right || key == Key::PageDown) &&
            overviewPage_ + 1 < overviewPages_.size())
            ++overviewPage_;
        if ((key == Key::Up || key == Key::Left || key == Key::PageUp) && overviewPage_)
            --overviewPage_;
        if (key == Key::Ok)
            toggleOverview(host, false);
        host.invalidate();
        return true;
    }
    Title &title = top();
    const size_t rows = sectionsOf(title).size();
    Shelf *shelf = focusedShelf(title);
    const bool onCast = focusedSection(title) == kCastRow;
    size_t &index = onCast ? title.cast : shelf ? shelf->focus : title.action;
    const size_t count = onCast ? title.item["cast"].size() : shelf ? shelf->items.size() : title.actions.size();
    bool moved = true;
    switch (key) {
    case Key::Up:
        if (title.row)
            --title.row;
        break;
    case Key::Down:
        if (title.row < rows)
            ++title.row;
        break;
    case Key::Left:
        if (index)
            --index;
        title.moved = title.moved || title.row == 0;
        break;
    case Key::Right:
        if (index + 1 < count)
            ++index;
        title.moved = title.moved || title.row == 0;
        break;
    case Key::Ok:
        activate(host);
        return true;
    default:
        moved = false;
    }
    if (moved)
        focusMoved(host, false);
    return moved;
}
void DetailsScreen::close(ScreenHost &host) {
    finished_ = true;
    if (embedded_)
        return;
    host.open(returnRoute(host.cameFrom()));
}
bool DetailsScreen::back(ScreenHost &host) {
    if (overview_) {
        toggleOverview(host, false);
        return true;
    }
    host.services().cancel(kDetails);
    host.services().cancel(kNetflix);
    cancelGenres(host);
    if (!stack_.empty())
        stack_.pop_back();
    if (stack_.empty()) {
        close(host);
        return true;
    }
    // The title underneath may have been left mid-load; its reply was dropped with the channel.
    Title &title = top();
    if (title.loading) {
        title.loading = false;
        load(host);
    }
    askGenres(host, title);
    for (const char *prefix : {"details:a:", "details:s", "details:c", "details:rg:", "details:vy"})
        host.motion().erasePrefix(prefix);
    focusMoved(host, true);
    playEntrance(host);
    host.invalidate();
    return true;
}
void DetailsScreen::click(ScreenHost &host, const std::string &id) {
    if (stack_.empty())
        return;
    if (id == "details:back" || id == "details:overview-close") {
        back(host);
        return;
    }
    Title &title = top();
    const std::vector<int> sections = sectionsOf(title);
    const auto rowOf = [&sections](int section) {
        return static_cast<size_t>(std::find(sections.begin(), sections.end(), section) -
                                   sections.begin()) + 1;
    };
    if (id.compare(0, 15, "details:action:") == 0) {
        title.row = 0;
        title.action = std::min(static_cast<size_t>(std::atoi(id.c_str() + 15)),
                                title.actions.size() - 1);
    } else if (id.compare(0, 14, "details:title:") == 0) {
        // "details:title:<shelf>:<index>"
        const size_t k = static_cast<size_t>(std::atoi(id.c_str() + 14)), colon = id.find(':', 14);
        if (k >= title.shelves.size() || colon == std::string::npos)
            return;
        const size_t index = static_cast<size_t>(std::atoi(id.c_str() + colon + 1));
        if (index >= title.shelves[k].items.size())
            return;
        title.row = rowOf(static_cast<int>(k));
        title.shelves[k].focus = index;
    } else if (id.compare(0, 13, "details:cast:") == 0) {
        size_t index = static_cast<size_t>(std::atoi(id.c_str() + 13));
        if (index >= title.item["cast"].size())
            return;
        title.row = rowOf(kCastRow);
        title.cast = index;
    } else
        return;
    focusMoved(host, false);
    activate(host);
}
Json DetailsScreen::snapshot() const {
    Json value = Json::object();
    if (stack_.empty())
        return value;
    const Title &title = top();
    if (const Shelf *shelf = focusedShelf(title))
        value.set("focus", Json::text(shelf->items[shelf->focus]["title"].string()));
    else if (focusedSection(title) == kCastRow)
        value.set("focus", Json::text(title.item["cast"][title.cast]["name"].string()));
    else if (title.action < title.actions.size())
        value.set("focus", Json::text(title.actions[title.action].label));
    value.set("titleId", title.item["id"]);
    value.set("depth", Json::numeric(static_cast<double>(stack_.size())));
    value.set("contentStatus", Json::text(title.status));
    value.set("contentState", Json::text(title.loading ? "loading" : title.error ? "error" : "ready"));
    value.set("overview", Json::flag(overview_));
    value.set("trailer", Json::text(shownAt_ >= 0 && !trailerOver_ ? trailer_ : ""));
    value.set("lowered", Json::flag(lowered_));
    value.set("overviewPage", Json::numeric(static_cast<double>(overviewPage_)));
    Json rows = Json::array();
    for (const int section : sectionsOf(title))
        rows.add(Json::text(section == kCastRow ? "cast"
                            : title.shelves[static_cast<size_t>(section)].genre.empty()
                                ? "similar"
                                : "genre:" + title.shelves[static_cast<size_t>(section)].genre));
    value.set("rows", rows);
    return value;
}

// ---- Drawing ---------------------------------------------------------------------------------

namespace {
// The backdrop, with one diagonal fade that keeps the lines readable at the upper left and the
// row of similar titles at the bottom. As the rows take the page (`rows` 0 to 1) the art turns
// into a dark blur of itself, as Google TV's does: TMDB's smallest cut the server serves, drawn at
// the screen's size four times a few pixels apart. The film's trailer plays over the art at
// `clip`; with the page lowered (`low`) the fade gives way to a shade at the foot under the title.
void backdrop(kit::Paint paint, const Json &item, float rows, const std::string &trailer, float clip,
              float low) {
    Scene &scene = paint.scene;
    Rect screen = {0, 0, kit::canvas::width, kit::canvas::height};
    std::string art = artwork(item, true);
    if (art.empty())
        kit::ambientWall(paint, 1, home::hsl(static_cast<float>(home::hashOf(item["id"].string()) % 360),
                                             .45f, .115f));
    else {
        if (rows < 1)
            scene.picture(screen, 0).value = art;
        if (rows < 1 && !trailer.empty() && clip > .002f) {
            Node &film = scene.picture(screen, 0);
            film.value = trailer;
            film.opacity = clip * (1 - rows);
        }
        if (rows > 0) {
            const std::string blur = "tmdb:w185:" + item["backdrop"].string();
            const float spread = kit::layout::detailsBlurSpread;
            int layer = 0;
            for (Rect at : {Rect{-spread, -spread, screen.w + 2 * spread, screen.h + 2 * spread},
                            Rect{-2 * spread, 0, screen.w + 2 * spread, screen.h + 2 * spread},
                            Rect{0, -2 * spread, screen.w + 2 * spread, screen.h + 2 * spread},
                            Rect{-2 * spread, -2 * spread, screen.w + 3 * spread, screen.h + 3 * spread}}) {
                Node &soft = scene.picture(at, 0);
                soft.value = blur;
                soft.opacity = rows / static_cast<float>(++layer);
            }
            scene.shape(screen, kit::focus::withAlpha(kit::color::ink, kit::alpha::detailsBlurDim.a * rows));
        }
    }
    if (rows >= 1)
        return;
    // The blur is dimmed evenly, so the fade goes as it comes.
    Color shade = kit::color::ink;
    if (low > .002f) {
        Node &foot = scene.shape(screen, {0, 0, 0, 0});
        foot.opacity = low;
        foot.gradient = Gradient::between(180, kit::focus::withAlpha(shade, 0), kit::focus::withAlpha(shade, .8f));
        foot.gradient.at[0] = .6f;
    }
    Node &fade = scene.shape(screen, {0, 0, 0, 0});
    fade.opacity = (1 - rows) * (1 - low);
    fade.gradient.stops = 4;
    fade.gradient.angle = 45;
    fade.gradient.color[0] = kit::focus::withAlpha(shade, .97f);
    fade.gradient.color[1] = kit::focus::withAlpha(shade, .90f);
    fade.gradient.color[2] = kit::focus::withAlpha(shade, .50f);
    fade.gradient.color[3] = kit::focus::withAlpha(shade, 0);
    fade.gradient.at[0] = 0;
    fade.gradient.at[1] = .38f;
    fade.gradient.at[2] = .60f;
    fade.gradient.at[3] = .92f;
}
void overviewSheet(kit::Paint paint, const std::string &title, const std::string &body,
                   size_t page, size_t pages, float open) {
    Scene &scene = paint.scene;
    kit::veil(paint, .82f * open);
    // What is under the sheet cannot be clicked through it.
    scene.hits.push_back({{0, 0, kit::canvas::width, kit::canvas::height}, "details:sheet"});
    scene.pushOpacity(open);
    scene.pushTransform(Transform::translate(0, 24 * (1 - open)));
    Rect panel = {260, 150, 1400, 780};
    scene.shape(panel, kit::color::surface1, kit::radius::xl).ring = {1, 0, kit::alpha::outline};
    paint.glyphs.text(scene, {panel.x + 56, panel.y + 48, panel.w - 112, kit::type::headline.line},
                      title, kit::type::headline, kit::color::onSurface);
    paint.glyphs.text(scene, {panel.x + 56, panel.y + 48 + kit::type::headline.line + 24,
                              panel.w - 112, kit::type::bodyL.line * 12},
                      body, kit::type::bodyL, kit::color::onSurface2, Node::Align::Left, 12);
    if (pages > 1)
        paint.glyphs.text(scene, {panel.x + 56, panel.y + panel.h - 88, 400, kit::type::labelM.line},
                          msg("tv.details.page", "{page} of {pages}",
                              fields({{"page", Json::text(std::to_string(page + 1))},
                                      {"pages", Json::text(std::to_string(pages))}})),
                          kit::type::labelM, kit::color::onSurface3);
    kit::PillButton close;
    close.rest = {panel.x + panel.w - 56 - 200, panel.y + panel.h - 56 - 56, 200, 56};
    close.id = "details:overview-close";
    close.label = msg("tv.details.close", "Close");
    close.focus = 1;
    kit::pillButton(paint, close);
    scene.popTransform();
    scene.popOpacity();
}
} // namespace

namespace {
// "2 h 25 min", "98 min".
std::string durationOf(double minutes) {
    const long total = std::lround(minutes);
    if (total < 60)
        return std::to_string(total) + " min";
    return std::to_string(total / 60) + " h" +
           (total % 60 ? " " + std::to_string(total % 60) + " min" : std::string());
}
// Google TV's line of facts under the title: its rating, genre, year and length, each apart.
std::vector<std::string> factsOf(const Json &item) {
    std::vector<std::string> facts;
    if (item["rating"].number() > 0)
        facts.push_back("★ " + compactNumber(item["rating"].number(), 1));
    if (item["genres"].size())
        facts.push_back(item["genres"][0].string());
    if (!item["year"].string().empty())
        facts.push_back(item["year"].string());
    if (item["runtime"].number() > 0)
        facts.push_back(durationOf(item["runtime"].number()));
    else if (item["seasons"].number() > 0)
        facts.push_back(msg("tv.details.seasons", "{count} seasons",
                            fields({{"count", Json::text(compactNumber(item["seasons"].number()))}})));
    return facts;
}
// A cast member: their photo in a circle that grows with the focus inside a white ring, their
// name and part under it.
void castMember(kit::Paint paint, Rect rest, const Json &person, float focus, const std::string &id) {
    Scene &scene = paint.scene;
    const float amount = kit::focus::clamp01(focus);
    const Rect art = kit::focus::scaledAbout(rest, 1 + .06f * amount);
    if (amount > 0) {
        Node &ring = scene.shape({art.x - 3, art.y - 3, art.w + 6, art.h + 6}, {0, 0, 0, 0},
                                 art.w / 2 + 3);
        ring.ring = {0, 4, kit::focus::withAlpha({1, 1, 1, 1}, amount)};
    }
    scene.shape(art, kit::color::surface3, art.w / 2);
    const std::string photo = person["photo"].string();
    if (!photo.empty())
        scene.picture(art, 0, art.w / 2).value = "tmdb:w185:" + photo;
    else
        paint.glyphs.icon(scene, {art.x + art.w / 2 - 36, art.y + art.h / 2 - 36, 72, 72}, "profile",
                          kit::color::onSurface3);
    const float y = rest.y + rest.h + layout::castNameGap;
    paint.glyphs.text(scene, {rest.x - 20, y, rest.w + 40, layout::cardCaptionLine},
                      person["name"].string(), kit::type::cardCaption, kit::color::onSurface,
                      Node::Align::Center, 1);
    paint.glyphs.text(scene, {rest.x - 20, y + layout::cardCaptionLine, rest.w + 40,
                              layout::cardCaptionLine},
                      person["character"].string() == "Director"
                          ? msg("tv.details.director", "Director")
                          : person["character"].string(),
                      kit::type::cardCaption, kit::color::onSurface2, Node::Align::Center, 1);
    scene.hits.push_back({art, id});
}
} // namespace

void DetailsScreen::build(ScreenHost &host, kit::Paint paint) {
    if (stack_.empty())
        return;
    // A mark being saved, or put back, changes its button's label as it is drawn.
    rebuildActions(host, top());
    const Title &title = top();
    const Motion &motion = host.motion();
    Scene &scene = paint.scene;
    const Json &item = title.item;
    scene.background = kit::color::ink;
    // The page rises as the focus goes down its rows; its head fades as it leaves.
    const float rise = static_cast<float>(motion.value("details:vy"));
    const float head = kit::focus::clamp01(1 + rise / 500);
    // Lowered under the trailer: the title at half its size and its facts at the foot, the rest
    // gone below the screen.
    const float low = static_cast<float>(motion.value("details:low"));
    const float shift = low * (layout::detailsLowFactsTop - layout::detailsFactsTop);
    backdrop(paint, item, 1 - head, trailer_, static_cast<float>(motion.value("details:tv")), low);

    scene.pushTransform(Transform::translate(0, rise));
    auto group = [&](int block, float alpha = 1) {
        kit::Enter enter = enterOf(motion, block);
        scene.pushOpacity(enter.alpha * alpha);
        scene.pushTransform(Transform::translate(0, enter.dy));
    };
    auto endGroup = [&] {
        scene.popTransform();
        scene.popOpacity();
    };
    // Block 0: the title, as large as Google TV sets it, and its facts.
    group(0, head * (1 - .3f * low));
    const std::string name = item["title"].string();
    const kit::TypeStyle &titleStyle = kit::type::detailsTitle;
    const bool twoLines = paint.glyphs.measure(name, titleStyle) > layout::detailsTitleW;
    const float titleBox = 2 * titleStyle.line;
    const float titleY = layout::detailsTitleTop + (twoLines ? 0 : (titleBox - titleStyle.line) / 2);
    const float titleScale = 1 + (layout::detailsLowTitleScale - 1) * low;
    // Lowered, the title's last line keeps its place over the facts.
    const float lowTitleY =
        layout::detailsLowTitleTop - (twoLines ? titleStyle.line * layout::detailsLowTitleScale : 0);
    scene.pushTransform(Transform::scaleAbout(layout::homeX, titleY, titleScale, titleScale, 0,
                                              low * (lowTitleY - titleY)));
    paint.glyphs.text(scene, {layout::homeX, titleY, layout::detailsTitleW, twoLines ? titleBox : titleStyle.line},
                      name, titleStyle, kit::color::onSurface, Node::Align::Left, twoLines ? 2 : 1);
    scene.popTransform();
    scene.pushTransform(Transform::translate(0, shift));
    float x = layout::homeX;
    const kit::TypeStyle &factStyle = kit::type::detailsFacts;
    for (const std::string &fact : factsOf(item)) {
        // The rating's star is an icon (kit::factsLine draws it so), as wide as the type is tall.
        const bool rated = fact.compare(0, 4, "\xe2\x98\x85 ") == 0;
        const float width = rated ? factStyle.px * 1.18f + paint.glyphs.measure(fact.substr(4), factStyle)
                                  : paint.glyphs.measure(fact, factStyle);
        kit::factsLine(paint, {x, layout::detailsFactsTop, width + 4, factStyle.line}, fact, factStyle,
                       kit::color::onSurface);
        x += width + layout::detailsFactsGap;
    }
    scene.popTransform();
    endGroup();
    scene.pushTransform(Transform::translate(0, shift));
    scene.pushOpacity(1 - low);
    // Block 1: the story, two lines.
    group(1, head);
    std::string overview = item["overview"].string();
    paint.glyphs.text(scene, {layout::homeX, layout::detailsPlotTop, layout::detailsPlotW,
                              kit::type::body.line * 2},
                      overview.empty() ? msg("tv.details.noOverview", "No overview available")
                                       : overview,
                      kit::type::body, overview.empty() ? kit::color::onSurface3 : kit::color::onSurface2,
                      Node::Align::Left, 2);
    endGroup();
    // Block 2: what can be done, Google TV's pills: dark at rest, light and a tenth larger with the
    // focus.
    group(2, head);
    x = layout::homeX;
    const float actionsY = layout::detailsActionsTop, actionH = layout::detailsActionH;
    for (size_t i = 0; i < title.actions.size(); ++i) {
        const Action &action = title.actions[i];
        const float focus = kit::focus::clamp01(
            static_cast<float>(motion.value("details:a:" + std::to_string(i))));
        const std::string label = action.iconOnly ? "" : action.label;
        const float textWidth = label.empty() ? 0 : paint.glyphs.measure(label, kit::type::bodyL);
        const float iconWidth = action.icon.empty()
                                    ? 0
                                    : layout::detailsActionIcon + (label.empty() ? 0 : layout::detailsActionIconGap);
        const float width = action.iconOnly ? layout::detailsIconActionW
                                            : textWidth + iconWidth + 2 * layout::detailsActionPad;
        const Rect rest = {x, actionsY, width, actionH};
        const Color ink = kit::focus::mix(kit::color::onSurface2, kit::color::onFocus, focus);
        scene.pushTransform(Transform::scaleAbout(rest, .5f, .5f, 1 + .1f * focus));
        scene.shape(rest, kit::focus::mix({1, 1, 1, .1f}, kit::color::focusFill, focus), actionH / 2);
        float content = action.iconOnly ? rest.x + (width - layout::detailsActionIcon) / 2
                                        : rest.x + layout::detailsActionPad;
        if (!action.icon.empty()) {
            paint.glyphs.icon(scene, {content, rest.y + (actionH - layout::detailsActionIcon) / 2,
                                      layout::detailsActionIcon, layout::detailsActionIcon},
                              action.icon, ink);
            content += iconWidth;
        }
        if (!label.empty())
            paint.glyphs.text(scene, {content, rest.y + (actionH - kit::type::bodyL.line) / 2,
                                      textWidth + 4, kit::type::bodyL.line},
                              label, kit::type::bodyL, ink);
        scene.popTransform();
        scene.hits.push_back({rest, "details:action:" + std::to_string(i)});
        x += width + layout::detailsActionGap;
    }
    if (!title.status.empty())
        paint.glyphs.text(scene, {layout::homeX, actionsY + actionH + 12, layout::detailsTitleW,
                                  kit::type::labelM.line},
                          title.status, kit::type::labelM,
                          title.error ? kit::color::bad : kit::color::onSurface3);
    endGroup();
    scene.popOpacity();
    scene.popTransform();
    scene.popTransform();
    // Block 3: the rows under them, Google TV's: the cast and crew, the titles like it, then its
    // genres' titles. They are placed on the screen, risen, since the cards leave out what is off it.
    const std::vector<int> sections = sectionsOf(title);
    std::vector<home::RowLook> looks(sections.size());
    for (size_t i = 0; i < sections.size(); ++i) {
        looks[i].up = 1;
        looks[i].risen = 1;
        looks[i].grow = static_cast<float>(motion.value("details:rg:" + std::to_string(i)));
    }
    const std::vector<float> tops = rowTopsOf(sectionHeights(sections), looks);
    scene.pushTransform(Transform::translate(0, shift));
    scene.pushOpacity(1 - low);
    group(3);
    for (size_t r = 0; r < sections.size(); ++r) {
        const float labelY = tops[r] + rise, itemsY = labelY + home::labelArea(looks[r]);
        if (labelY > kit::canvas::height + 40 || itemsY + 400 < 0)
            continue;
        kit::Row label;
        label.index = static_cast<int>(r);
        label.placed = true;
        label.y = labelY;
        label.px = home::labelPx(looks[r]);
        label.grow = looks[r].grow;
        const Shelf *shelf = sections[r] >= 0 ? &title.shelves[static_cast<size_t>(sections[r])] : nullptr;
        const names::SearchGenre *genre = shelf ? names::searchGenre(shelf->genre) : nullptr;
        label.label = !shelf ? msg("tv.details.cast", "Cast & crew")
                      : genre ? msg("tv.details.genreRow", "{genre} films and shows",
                                    fields({{"genre", Json::text(genre->name)}}))
                              : msg("tv.details.ifYouLiked", "If you liked {title}",
                                    fields({{"title", Json::text(name)}}));
        kit::row(paint, label);
        if (!shelf) {
            const float scroll = static_cast<float>(motion.value("details:cx"));
            for (size_t i = 0; i < item["cast"].size(); ++i) {
                const Rect rest = {layout::homeX + static_cast<float>(i) * (layout::castD + layout::castGap) + scroll,
                                   itemsY, layout::castD, layout::castD};
                if (rest.x + rest.w < -140 || rest.x > kit::canvas::width + 140)
                    continue;
                castMember(paint, rest, item["cast"][i],
                           static_cast<float>(motion.value("details:c:" + std::to_string(i))),
                           "details:cast:" + std::to_string(i));
            }
            continue;
        }
        const std::string prefix = "details:s" + std::to_string(sections[r]);
        const float scroll = static_cast<float>(motion.value(prefix + "x"));
        for (size_t i = 0; i < shelf->items.size(); ++i) {
            const Rect rest = {layout::homeX + static_cast<float>(i) * (kSimilarW + kSimilarGap) + scroll,
                               itemsY, kSimilarW, kSimilarH};
            if (rest.x + rest.w < -140 || rest.x > kit::canvas::width + 140)
                continue;
            const Json entry = shelf->items[i];
            kit::PosterCard card;
            card.rest = rest;
            card.id = "details:title:" + std::to_string(sections[r]) + ":" + std::to_string(i);
            card.image = artwork(entry);
            card.title = entry["title"].string();
            card.focus = static_cast<float>(motion.value(prefix + ":" + std::to_string(i)));
            // Google TV names the focused title under its card; every card has its line of facts.
            if (card.focus > .5f)
                card.caption = card.title;
            card.captionLine = titleFacts(entry, false);
            kit::card(paint, card);
        }
    }
    endGroup();
    scene.popOpacity();
    scene.popTransform();
    // A page with nothing to press yet (its details on the way, no trailer, no marks) shows how
    // to leave, which the pointer can click.
    if (title.actions.empty())
        kit::keyHint(paint, layout::homeX, 1002, "Back", msg("tv.details.return", "Return"),
                     "details:back");
    float open = static_cast<float>(motion.value("details:ov"));
    if (open > .002f)
        overviewSheet(paint, item["title"].string(),
                      overviewPage_ < overviewPages_.size() ? overviewPages_[overviewPage_] : "",
                      overviewPage_, overviewPages_.size(), open);
}

std::unique_ptr<ScreenView> makeDetailsScreen(const std::string &route) {
    if (route.compare(0, 8, "details:") != 0)
        return nullptr;
    size_t split = route.find(':', 8);
    std::string type = split == std::string::npos ? "" : route.substr(8, split - 8),
                id = split == std::string::npos ? route.substr(8) : route.substr(split + 1);
    return std::unique_ptr<ScreenView>(new DetailsScreen(type, id, Json(), false));
}
} // namespace nativetv
