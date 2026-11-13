#include "domain/models.hpp"
#include "domain/names.hpp"
#include "domain/say.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
#include <map>

namespace nativetv {
using say::msg;
Json fields(std::initializer_list<std::pair<std::string, Json>> values) {
    Json result = Json::object();
    for (const auto &pair : values)
        result.set(pair.first, pair.second);
    return result;
}
Command post(Operation operation, const Json &body, bool confirm) {
    Command c;
    c.operation = operation;
    c.body = body;
    c.confirm = confirm;
    return c;
}
Command control(const std::string &action, const Json &value, bool confirm) {
    return post(Operation::Control, fields({{"action", Json::text(action)}, {"value", value}}),
                confirm);
}
Command navigate(const std::string &route) {
    Command c;
    c.route = route;
    return c;
}
std::string artwork(const Json &item, bool hero) {
    std::string path = item[hero ? "backdrop" : "poster"].string();
    if (!hero && !item["backdrop"].string().empty())
        path = item["backdrop"].string();
    if (path.empty())
        return "";
    // The hero's art at the screen's size: TMDB answers this size with a 1920x1080 picture (w1280,
    // the largest of its listed sizes short of the 4K original, is drawn half again as large).
    std::string size = hero ? kHeroArtSize : item["backdrop"].string().empty() ? "w342" : "w500";
    return "tmdb:" + size + ":" + path;
}
// "Film  ·  2026  ·  ★ 6.4": the kit draws the star as an icon (the face has none). The details
// page names the kind on its source line, so it asks for the facts without it.
std::string titleFacts(const Json &item, bool withKind) {
    std::vector<std::string> parts;
    if (withKind && item["type"].string() == "movie")
        parts.push_back(msg("tv.title.film", "Film"));
    else if (withKind && item["type"].string() == "tv")
        parts.push_back(msg("tv.title.series", "Series"));
    if (!item["year"].string().empty())
        parts.push_back(item["year"].string());
    if (item["rating"].number() > 0)
        parts.push_back("★ " + compactNumber(item["rating"].number(), 1));
    if (item["runtime"].number() > 0)
        parts.push_back(msg("tv.title.minutes", "{minutes} min",
                            fields({{"minutes", Json::text(compactNumber(item["runtime"].number()))}})));
    if (item["seasons"].number() == 1)
        parts.push_back(msg("tv.title.season", "1 season"));
    else if (item["seasons"].number() > 1)
        parts.push_back(msg("tv.title.seasons", "{seasons} seasons",
                            fields({{"seasons", Json::text(compactNumber(item["seasons"].number()))}})));
    for (const auto &genre : item["genres"].items())
        parts.push_back(genre.string());
    std::string text;
    for (const auto &part : parts) {
        if (!text.empty())
            text += "  ·  ";
        text += part;
    }
    return text;
}
std::string pictureLabel(const Json &picture) {
    if (!picture["ok"].boolean())
        return "";
    std::vector<std::string> parts;
    Json signal = picture["signal"];
    if (signal["width"].number() > 0 && signal["height"].number() > 0) {
        std::string dimensions = signal["width"].string() + " × " + signal["height"].string();
        if (signal["hz"].number() > 0) {
            std::string hz = compactNumber(signal["hz"].number(), 2);
            while (!hz.empty() && hz.back() == '0')
                hz.pop_back();
            if (!hz.empty() && hz.back() == '.')
                hz.pop_back();
            dimensions = msg("tv.picture.rate", "{size} at {hz} Hz",
                             fields({{"size", Json::text(dimensions)}, {"hz", Json::text(hz)}}));
        }
        parts.push_back(dimensions);
        std::string hdr = signal["hdr"].string();
        if (hdr == "hdr10")
            parts.push_back("HDR10");
        if (hdr == "hlg")
            parts.push_back("HLG");
        if (hdr == "dolby_vision")
            parts.push_back("Dolby Vision");
    }
    if (!picture["mode"]["display"].string().empty())
        parts.push_back(picture["mode"]["display"].string());
    std::string text;
    for (const auto &part : parts) {
        if (!text.empty())
            text += "  ·  ";
        text += part;
    }
    return text;
}
bool titlesTab(const std::string &tab) {
    return tab == "films" || tab == "shows";
}
std::vector<Tab> homeTabs(const Json &catalogue, const Json &discover) {
    std::vector<Tab> tabs = {{"home", msg("tv.tab.home", "Home"), Operation::None}};
    bool films = false, shows = false;
    for (const auto &row : discover["rows"].items()) {
        const std::string key = row["key"].string();
        films = films || key.compare(0, 6, "films:") == 0;
        shows = shows || key.compare(0, 6, "shows:") == 0;
    }
    if (films)
        tabs.push_back({"films", msg("tv.tab.films", "Films"), Operation::None});
    if (shows)
        tabs.push_back({"shows", msg("tv.tab.shows", "Shows"), Operation::None});
    if (catalogue["layout"]["showLive"].boolean() &&
        catalogue["sources"]["channels"].number(-1) != 0)
        tabs.push_back({"live", msg("tv.tab.live", "Live"), Operation::None});
    tabs.push_back({"apps", msg("tv.tab.apps", "Apps"), Operation::None});
    if (catalogue["layout"]["showInputs"].boolean(true) && catalogue["devices"].size())
        tabs.push_back({"inputs", msg("tv.tab.inputs", "Inputs"), Operation::None});
    return tabs;
}
// The ids hidden from `list`: "app", "dev" or "ch" are Home's rows, "appsTab" is the Apps tab's own
// apps. A catalogue from before the Apps tab had a list hides there what Home hides.
Json hiddenIds(const Json &catalogue, const std::string &list) {
    Json hidden = catalogue["layout"]["hidden"];
    return list == "appsTab" && !hidden["appsTab"].isArray() ? hidden["app"] : hidden[list];
}
namespace {
const char *const kClientApp = "com.tvweb.dashboard";
bool contains(const Json &values, const std::string &id) {
    for (const auto &value : values.items())
        if (value.string() == id)
            return true;
    return false;
}
// `withHidden`: the hidden apps (never LG Home) follow the others, flagged. `list`: the hidden list
// that applies, the row's own unless given.
CardRow catalogueRow(const std::string &kind, const Json &catalogue, const Json &guide,
                     bool withHidden = false, const std::string &list = std::string()) {
    const Json hiddenList = hiddenIds(catalogue, list.empty() ? kind : list);
    CardRow row;
    row.key = kind;
    row.title = kind == "app"   ? msg("tv.tab.apps", "Apps")
                : kind == "dev" ? msg("tv.tab.inputs", "Inputs")
                                : msg("tv.home.channels", "Channels");
    std::map<std::string, Json> index;
    for (const auto &item : catalogue[kind == "app"   ? "apps"
                                      : kind == "dev" ? "devices"
                                                      : "channels"]
                                .items())
        index[item["id"].string()] = item;
    std::vector<Card> hiddenCards;
    for (const auto &value : catalogue["order"][kind].items()) {
        std::string id = value.string();
        const bool hidden = contains(hiddenList, id);
        if (hidden && !withHidden)
            continue;
        if (id == "lg-home" && !catalogue["layout"]["showLgHome"].boolean(true))
            continue;
        if (!index.count(id) && id != "lg-home")
            continue;
        Json item = index[id];
        Card card;
        card.id = id;
        card.kind = kind;
        card.hidden = hidden;
        card.data = item;
        card.title = id == "lg-home" ? msg("tv.home.lgHome", "LG Home")
                                     : item["title"].string(item["name"].string(id));
        if (kind == "app" && id != "lg-home")
            card.image = "app:" + id;
        if (kind == "dev")
            card.subtitle =
                item["unused"].boolean()   ? msg("tv.inputs.nothing", "Nothing connected")
                : item["port"].isNumber() ? "HDMI " + compactNumber(item["port"].number())
                                          : "HDMI " + item["port"].string();
        if (kind == "ch") {
            card.subtitle = item["number"].string();
            Json programme = guide[id][static_cast<size_t>(0)];
            if (programme.valid())
                card.subtitle += "  " + programme["title"].string();
        }
        // The Rooted TV tile is this client: it is already in front while Home shows it, so a
        // launch would change nothing. It opens the settings the tile opens from LG's launcher.
        card.command = kind == "app" && id == kClientApp
                           ? navigate("dashboard")
                           : post(Operation::OpenHomeItem,
                                  fields({{"kind", Json::text(id == "lg-home" ? "lg" : kind)},
                                          {"id", Json::text(id)}}));
        (hidden ? hiddenCards : row.cards).push_back(card);
    }
    row.cards.insert(row.cards.end(), hiddenCards.begin(), hiddenCards.end());
    return row;
}
// The apps as one row or, with the apps sorted by kind, one row per kind in the owner's order.
// `list` names the hidden apps that are left out: Home's ("app") or the Apps tab's ("appsTab").
std::vector<CardRow> appRows(const Json &catalogue, const Json &guide, const std::string &list) {
    CardRow all = catalogueRow("app", catalogue, guide, false, list);
    std::vector<CardRow> rows;
    if (catalogue["layout"]["sort"]["app"].string() == "kind")
        for (const auto &kind : catalogue["kinds"].items()) {
            CardRow row;
            row.key = "app";
            row.group = kind["key"].string();
            row.title = appKindName(row.group, kind["label"].string(row.group));
            for (const Card &card : all.cards)
                if (kindOf(card) == row.group)
                    row.cards.push_back(card);
            if (!row.cards.empty())
                rows.push_back(row);
        }
    if (rows.empty())
        rows.push_back(all);
    return rows;
}
} // namespace
std::string kindOf(const Card &card) {
    return card.id == "lg-home" ? "tv" : card.data["kind"].string("other");
}
CardRow appsRow(const Json &catalogue, const Json &guide, bool withHidden, const std::string &list) {
    CardRow apps;
    apps.key = "app";
    apps.title = list == "appsTab" ? msg("tv.tab.apps", "Apps") : msg("tv.home.yourApps", "Your apps");
    for (const CardRow &row : appRows(catalogue, guide, list))
        apps.cards.insert(apps.cards.end(), row.cards.begin(), row.cards.end());
    if (withHidden)
        for (const Card &card : catalogueRow("app", catalogue, guide, true, list).cards)
            if (card.hidden)
                apps.cards.push_back(card);
    return apps;
}
std::vector<std::string> orderAfterMove(const Json &order, const std::vector<std::string> &shown) {
    std::vector<std::string> result;
    size_t next = 0;
    for (const auto &value : order.items()) {
        const std::string id = value.string();
        const bool onScreen = std::find(shown.begin(), shown.end(), id) != shown.end();
        // A card on screen takes the next place held by a card on screen; the rest keep theirs.
        if (onScreen && next < shown.size())
            result.push_back(shown[next++]);
        else
            result.push_back(id);
    }
    // Cards the order did not list (an app installed since) follow.
    for (; next < shown.size(); ++next)
        if (std::find(result.begin(), result.end(), shown[next]) == result.end())
            result.push_back(shown[next]);
    return result;
}
namespace {
// "47 min", "1 h 3 min".
std::string playedFor(double milliseconds) {
    const long minutes = std::lround(milliseconds / 60000);
    if (minutes < 60)
        return msg("tv.title.minutes", "{minutes} min", fields({{"minutes", Json::text(std::to_string(minutes))}}));
    if (minutes % 60 == 0)
        return msg("tv.title.hours", "{hours} h", fields({{"hours", Json::text(std::to_string(minutes / 60))}}));
    return msg("tv.title.hoursMinutes", "{hours} h {minutes} min",
               fields({{"hours", Json::text(std::to_string(minutes / 60))},
                       {"minutes", Json::text(std::to_string(minutes % 60))}}));
}
// A title card of the feed's row `rowKey`.
Card feedCard(const Json &item, const std::string &rowKey) {
    const bool videos = rowKey == "youtube" || rowKey == "youtube:subscriptions";
    Card card;
    card.id = item["id"].string();
    card.kind = videos                 ? "youtube"
                : rowKey == "netflix"  ? "netflix"
                : rowKey == "continue" ? "continue"
                                       : "title";
    card.data = item;
    card.title = item["title"].string();
    card.subtitle = titleFacts(item);
    // Every card is landscape, so the backdrop is preferred over the portrait poster, and the key art
    // with the title on it, as on Google TV's cards, over both.
    card.image = artwork(item);
    card.backdrop = artwork(item, true);
    if (!item["art"].string().empty()) {
        card.image = "tmdb:w500:" + item["art"].string();
        card.titled = true;
    }
    if (!item["logo"].string().empty())
        card.logo = "tmdb:w500:" + item["logo"].string();
    if (card.kind == "netflix") {
        // OK opens the title in Netflix itself, as a YouTube card opens YouTube.
        card.command = post(Operation::OpenHomeItem,
                            fields({{"kind", Json::text("netflix")}, {"id", item["id"]},
                                    {"type", item["type"]}}));
    } else if (card.kind == "continue" && item["kind"].string() == "app") {
        // A play in a service's own app, of a title nobody named: its last frame, and OK opens the
        // app, whose own row resumes it.
        card.image = card.backdrop =
            item["frame"].string().empty() ? "" : "frame:" + item["frame"].string();
        card.subtitle = msg("tv.home.watchedFor", "Watched for {time}",
                            fields({{"time", Json::text(playedFor(item["playedMs"].number()))}}));
        card.command = post(Operation::OpenHomeItem,
                            fields({{"kind", Json::text("app")}, {"id", item["appId"]}}));
    } else if (card.kind == "continue") {
        // Back into the service's app on the title, where its own Resume is.
        card.command = post(Operation::OpenHomeItem,
                            fields({{"kind", Json::text("service")},
                                    {"service", item["service"]},
                                    {"id", item["id"]},
                                    {"type", item["type"]}}));
    } else if (card.kind == "youtube") {
        card.image = "youtube:" + card.id;
        card.command = post(Operation::OpenHomeItem,
                            fields({{"kind", Json::text("youtube")}, {"id", item["id"]}}));
    } else
        card.command = navigate("details");
    return card;
}
} // namespace
std::string continueLine(const Card &card) {
    if (card.data["kind"].string() == "app")
        return card.subtitle;
    std::string line;
    const std::string service = card.data["service"].string();
    for (const auto &candidate : nativetv::names::streamingServices())
        if (candidate.key == service)
            line = candidate.name;
    const double runtime = card.data["runtime"].number(), progress = card.data["progress"].number();
    if (runtime > 0 && progress > 0 && progress < 1) {
        const double left = runtime * (1 - progress) * 60000;
        line += (line.empty() ? "" : " • ") +
                msg("tv.home.timeLeft", "{time} left", fields({{"time", Json::text(playedFor(left))}}));
    }
    return line;
}
namespace {
// Google TV's Films and Shows tabs show eight featured titles at most.
constexpr size_t kFeaturedMax = 8;
// Google TV's Films and Shows tabs: the week's trending films or series as large featured cards,
// with their service where a Popular on row lists them, then the tab's own rows from the feed.
std::vector<CardRow> titleTabRows(const std::string &tab, const Json &discover) {
    const bool films = tab == "films";
    const std::vector<std::pair<std::string, std::string>> rows =
        films ? std::vector<std::pair<std::string, std::string>>{
                    {"films:popular", msg("tv.films.popular", "Popular films")},
                    {"films:family", msg("tv.films.family", "Family films")},
                    {"films:top", msg("tv.films.top", "Top rated films")},
                    {"films:action", msg("tv.films.action", "Action and adventure")},
                    {"films:comedy", msg("tv.films.comedy", "Comedies")},
                    {"films:animation", msg("tv.films.animation", "Animated films")}}
              : std::vector<std::pair<std::string, std::string>>{
                    {"shows:popular", msg("tv.shows.popular", "Popular shows")},
                    {"shows:animation", msg("tv.shows.animation", "Animated shows")},
                    {"shows:top", msg("tv.shows.top", "Top rated shows")},
                    {"shows:crime", msg("tv.shows.crime", "Crime shows")},
                    {"shows:comedy", msg("tv.shows.comedy", "Comedy shows")},
                    {"shows:docs", msg("tv.shows.docs", "Documentaries")}};
    std::map<std::string, Json> feedRows;
    std::map<std::string, std::string> services;
    for (const auto &source : discover["rows"].items()) {
        const std::string key = source["key"].string();
        feedRows[key] = source;
        if (key.compare(0, 3, "on:") == 0)
            for (const auto &item : source["items"].items())
                services.emplace(item["type"].string() + ":" + item["id"].string(), key.substr(3));
    }
    std::vector<CardRow> result;
    // A title a Popular on row lists plays on that service, which its captions name.
    auto markService = [&](Card &card) {
        const auto service =
            services.find(card.data["type"].string() + ":" + card.data["id"].string());
        if (service != services.end())
            card.data.set("service", Json::text(service->second));
    };
    CardRow featured;
    featured.key = "feature";
    for (const auto &item : feedRows[films ? "movies" : "series"]["items"].items()) {
        if (item["backdrop"].string().empty() || featured.cards.size() == kFeaturedMax)
            continue;
        Card card = feedCard(item, "feature");
        // The plain backdrop at the card's size: the card letters its title itself.
        card.image = "tmdb:w1280:" + item["backdrop"].string();
        card.titled = false;
        card.subtitle = item["overview"].string();
        markService(card);
        featured.cards.push_back(card);
    }
    if (!featured.cards.empty())
        result.push_back(featured);
    for (const auto &spec : rows) {
        const auto found = feedRows.find(spec.first);
        if (found == feedRows.end())
            continue;
        CardRow row;
        row.key = spec.first;
        row.title = spec.second;
        row.posters = true;
        for (const auto &item : found->second["items"].items()) {
            Card card = feedCard(item, row.key);
            markService(card);
            row.cards.push_back(card);
        }
        if (!row.cards.empty())
            result.push_back(row);
    }
    return result;
}
} // namespace
std::string genreName(const std::string &key) {
    if (key == "action") return msg("tv.genre.action", "Action");
    if (key == "adventure") return msg("tv.genre.adventure", "Adventure");
    if (key == "animation") return msg("tv.genre.animation", "Animation");
    if (key == "comedy") return msg("tv.genre.comedy", "Comedy");
    if (key == "crime") return msg("tv.genre.crime", "Crime");
    if (key == "documentary") return msg("tv.genre.documentary", "Documentary");
    if (key == "drama") return msg("tv.genre.drama", "Drama");
    if (key == "family") return msg("tv.genre.family", "Family");
    if (key == "fantasy") return msg("tv.genre.fantasy", "Fantasy");
    if (key == "war") return msg("tv.genre.war", "War");
    if (key == "history") return msg("tv.genre.history", "History");
    if (key == "horror") return msg("tv.genre.horror", "Horror");
    if (key == "music") return msg("tv.genre.music", "Music");
    if (key == "mystery") return msg("tv.genre.mystery", "Mystery");
    if (key == "reality") return msg("tv.genre.reality", "Reality");
    if (key == "romance") return msg("tv.genre.romance", "Romance");
    if (key == "scifi") return msg("tv.genre.scifi", "Science fiction");
    if (key == "thriller") return msg("tv.genre.thriller", "Thriller");
    const names::SearchGenre *genre = names::searchGenre(key);
    return genre ? genre->name : key;
}
std::string appKindName(const std::string &key, const std::string &fallback) {
    if (key == "video") return msg("tv.apps.kind.video", "Movies & TV");
    if (key == "music") return msg("tv.apps.kind.music", "Music & audio");
    if (key == "games") return msg("tv.apps.kind.games", "Games");
    if (key == "home") return msg("tv.apps.kind.home", "Smart home");
    if (key == "tv") return msg("tv.apps.kind.tv", "LG & TV");
    if (key == "tools") return msg("tv.apps.kind.tools", "Tools");
    if (key == "other") return msg("tv.apps.kind.other", "Other");
    return fallback;
}
std::string titleCaption(const Card &card) {
    std::string line;
    if (const auto *service = nativetv::names::streamingService(card.data["service"].string()))
        line = service->name;
    const std::string facts = titleFacts(card.data, false);
    if (!facts.empty())
        line += (line.empty() ? "" : "  \xc2\xb7  ") + facts;
    return line;
}
namespace {
// Google TV's Apps tab: large banners over Your apps, a bar that searches the apps, a button for
// each category, the stores to get more apps from and a row of each category's apps. Google's
// banners and rows come from the Play Store; here they are the TV's own apps: the banners its apps
// of films, music and games in Your apps' order, the rows its apps by kind in the owner's order of
// the kinds. In the Play Store's place, LG Content Store (opened like any app, hidden or not) and
// the Homebrew Channel's catalogue, which installs its apps from Rooted TV.
constexpr size_t kAppBanners = 6;
constexpr const char *kLgStoreApp = "com.webos.app.discovery", *kHomebrewApp = "org.webosbrew.hbchannel";
CardRow storesRow(const Json &catalogue) {
    CardRow row;
    row.key = "appstore";
    row.title = msg("tv.apps.getMore", "Get more apps");
    for (const auto &app : catalogue["apps"].items()) {
        const std::string id = app["id"].string();
        if (id != kLgStoreApp && id != kHomebrewApp)
            continue;
        Card card;
        card.id = id;
        card.kind = "app";
        card.data = app;
        card.image = "app:" + id;
        card.title = id == kHomebrewApp ? "Homebrew Channel" : app["title"].string("LG Content Store");
        card.command = id == kHomebrewApp ? navigate("catalog")
                                          : post(Operation::OpenHomeItem,
                                                 fields({{"kind", Json::text("app")}, {"id", Json::text(id)}}));
        // LG's store first, as the TV's own.
        if (id == kLgStoreApp)
            row.cards.insert(row.cards.begin(), card);
        else
            row.cards.push_back(card);
    }
    return row;
}
std::vector<CardRow> appsTabRows(const Json &catalogue, const Json &guide) {
    std::vector<CardRow> result;
    CardRow apps = appsRow(catalogue, guide, false, "appsTab");
    apps.title = msg("tv.home.yourApps", "Your apps");
    if (apps.cards.empty())
        return result;
    CardRow banners;
    banners.key = "appfeature";
    for (const Card &card : apps.cards) {
        const std::string kind = kindOf(card);
        if (banners.cards.size() < kAppBanners && (kind == "video" || kind == "music" || kind == "games"))
            banners.cards.push_back(card);
    }
    if (!banners.cards.empty())
        result.push_back(banners);
    result.push_back(apps);
    CardRow search;
    search.key = "search";
    Card field;
    field.id = "search";
    field.kind = "search";
    field.title = msg("tv.apps.search", "Search apps");
    field.command = navigate("search");
    search.cards.push_back(field);
    result.push_back(search);
    CardRow chips;
    chips.key = "chips";
    chips.title = msg("tv.apps.categories", "App categories");
    std::vector<CardRow> kinds;
    for (const auto &kind : catalogue["kinds"].items()) {
        CardRow row;
        row.key = "appkind";
        row.group = kind["key"].string();
        row.title = appKindName(row.group, kind["label"].string(row.group));
        for (const Card &card : apps.cards)
            if (kindOf(card) == row.group)
                row.cards.push_back(card);
        if (row.cards.empty())
            continue;
        Card chip;
        chip.id = "kind:" + row.group;
        chip.kind = "chip";
        chip.title = row.title;
        chip.command = navigate("apps:" + row.group);
        chips.cards.push_back(chip);
        kinds.push_back(row);
    }
    if (!chips.cards.empty())
        result.push_back(chips);
    CardRow stores = storesRow(catalogue);
    if (!stores.cards.empty())
        result.push_back(stores);
    result.insert(result.end(), kinds.begin(), kinds.end());
    return result;
}
} // namespace
std::vector<CardRow> homeRows(const std::string &tab, const Json &catalogue, const Json &discover,
                              const Json &guide) {
    std::vector<CardRow> result;
    if (tab == "apps")
        return appsTabRows(catalogue, guide);
    if (tab == "inputs" || tab == "live") {
        CardRow row = catalogueRow(tab == "inputs" ? "dev" : "ch", catalogue, guide);
        if (!row.cards.empty())
            result.push_back(row);
        return result;
    }
    if (titlesTab(tab))
        return titleTabRows(tab, discover);
    const std::map<std::string, std::string> names = {
        {"trending", msg("tv.home.topPicks", "Top picks for you")},
        {"movies", msg("tv.home.trendingFilms", "Trending films this week")},
        {"series", msg("tv.home.trendingSeries", "Trending series this week")},
        {"netflix", msg("tv.home.onNetflix", "Available on Netflix")},
        {"watchlist", msg("tv.home.watchlist", "Your watchlist")},
        {"continue", msg("tv.home.continue", "Continue watching")},
        {"youtube", msg("tv.home.trendingYoutube", "Trending on YouTube")}};
    std::map<std::string, Json> feedRows;
    for (const auto &source : discover["rows"].items())
        feedRows[source["key"].string()] = source;
    // The apps, in their kinds' order when sorted by kind, and the top picks the hero is drawn
    // from; the top picks lead once there are any (below).
    CardRow apps = appsRow(catalogue, guide, false);
    if (!apps.cards.empty())
        result.push_back(apps);
    // A streaming service's row follows the top picks, as on Google TV, in the order its app has
    // in Your apps: a service whose app is not there (not installed, or hidden from the row by
    // this profile) has no row. Its Netflix row stands in for Available on Netflix.
    // Continue watching comes first after the top picks and the apps, as on Google TV.
    std::vector<std::string> keys = {"trending", "continue"};
    std::map<std::string, std::string> serviceNames;
    // The videos from the profile's YouTube subscriptions take YouTube's place among them.
    for (const Card &card : apps.cards) {
        if (card.id == "youtube.leanback.v4" && feedRows.count("youtube:subscriptions") &&
            !serviceNames.count("youtube:subscriptions")) {
            keys.push_back("youtube:subscriptions");
            serviceNames["youtube:subscriptions"] =
                msg("tv.home.subscriptions", "From your YouTube subscriptions");
        }
        for (const auto &service : nativetv::names::streamingServices()) {
            const std::string key = "on:" + service.key;
            if (feedRows.count(key) && !serviceNames.count(key) &&
                std::find(service.apps.begin(), service.apps.end(), card.id) != service.apps.end()) {
                keys.push_back(key);
                serviceNames[key] =
                    msg("tv.home.popularOn", "Popular on {service}", fields({{"service", Json::text(service.name)}}));
            }
        }
    }
    // The services the profile watches most lead the services' rows (the server's
    // "watchedServices", by time played in their apps).
    std::vector<size_t> slots;
    std::vector<std::string> serviceKeys;
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i].compare(0, 3, "on:") == 0) {
            slots.push_back(i);
            serviceKeys.push_back(keys[i]);
        }
    const auto rankOf = [&discover](const std::string &key) {
        size_t rank = 0;
        for (const auto &watched : discover["watchedServices"].items()) {
            if ("on:" + watched.string() == key)
                return rank;
            ++rank;
        }
        return rank;
    };
    std::stable_sort(serviceKeys.begin(), serviceKeys.end(),
                     [&rankOf](const std::string &a, const std::string &b) {
                         return rankOf(a) < rankOf(b);
                     });
    for (size_t i = 0; i < slots.size(); ++i)
        keys[slots[i]] = serviceKeys[i];
    // The profile's own: its watchlist, then the titles like the one it liked last.
    keys.insert(keys.end(), {"watchlist", "liked", "movies", "series"});
    if (!serviceNames.count("on:netflix"))
        keys.push_back("netflix");
    keys.push_back("youtube");
    for (const auto &key : keys) {
        if (!feedRows.count(key))
            continue;
        Json source = feedRows[key];
        CardRow row;
        row.key = source["key"].string();
        const bool videos = row.key == "youtube" || row.key == "youtube:subscriptions";
        row.posters = row.key != "trending" && !videos;
        auto name = names.find(row.key);
        auto serviceName = serviceNames.find(row.key);
        row.title = serviceName != serviceNames.end() ? serviceName->second
                    : name == names.end()            ? source["title"].string(row.key)
                                                     : name->second;
        for (const auto &item : source["items"].items())
            row.cards.push_back(feedCard(item, row.key));
        if (!row.cards.empty())
            result.push_back(row);
    }
    // Since Google TV's 2025 redesign the top picks come first and Your apps follows them.
    if (result.size() > 1 && result[0].key == "app" && result[1].key == "trending")
        std::swap(result[0], result[1]);
    return result;
}
std::vector<Card> featuredCards(const Json &discover) {
    std::vector<Card> cards;
    for (const auto &source : discover["rows"].items())
        if (source["key"].string() == "featured")
            for (const auto &item : source["items"].items())
                cards.push_back(feedCard(item, "featured"));
    return cards;
}
} // namespace nativetv
