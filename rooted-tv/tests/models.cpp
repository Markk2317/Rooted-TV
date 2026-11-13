#include "domain/models.hpp"
#include "ui/keyboard.hpp"
#include <cstdio>
#include <stdexcept>

using namespace rootedtv;
namespace {
int checks = 0;
void check(bool condition, const char *message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
const MenuRow &find(const std::vector<MenuRow> &rows, const std::string &label) {
    for (const auto &row : rows)
        if (row.label == label)
            return row;
    throw std::runtime_error("Missing row: " + label);
}
} // namespace
int main() {
    try {
        Json parsed = Json::parse("{\"title\":\"Café 🌿\",\"items\":[1,true,null]}");
        check(parsed["title"].string() == "Café 🌿", "JSON preserves Unicode");
        check(parsed["items"].size() == 3, "JSON arrays remain bounded and indexable");
        check(!Json::parse("{broken").valid(), "Malformed JSON is rejected");
        check(!Json::parse(std::string(8 * 1024 * 1024 + 1, 'x')).valid(),
              "Oversized JSON is rejected");
        Json copy = parsed;
        parsed = Json();
        check(copy["title"].valid(), "JSON ownership survives independent copies");
        check(encode("a b&/é") == "a%20b%26%2F%C3%A9",
              "Query encoding handles UTF-8 and delimiters");
        Keyboard keyboard;
        keyboard.reset("é🌿", 3);
        keyboard.press("left");
        check(keyboard.cursor() == 2, "Cursor moves across a complete UTF-8 code point");
        check(keyboard.erase() && keyboard.value() == "🌿",
              "Backspace removes a complete UTF-8 code point");
        check(keyboard.insert("a") && keyboard.value() == "a🌿", "Insertion respects the cursor");
        check(keyboard.insert("b") && !keyboard.insert("c"),
              "Character limits count Unicode characters");
        check(!keyboard.insert("\n"), "Input excludes control characters");
        keyboard.reset("", 20);
        keyboard.press("shift");
        keyboard.press("a");
        check(keyboard.value() == "A", "Shift changes letter input");
        keyboard.press("symbols");
        check(keyboard.rows()[1][0] == "!", "Symbol layer is available");
        Json catalogue = Json::parse(
            R"({"apps":[{"id":"a","title":"Alpha"},{"id":"b","title":"Beta"}],"devices":[{"id":"hdmi1","name":"Console","port":1}],"channels":[],"order":{"app":["b","a","lg-home"],"dev":["hdmi1"],"ch":[]},"layout":{"rows":["app","dev"],"hidden":{"app":["b"]},"showInputs":true,"showLive":false,"showLgHome":false},"sources":{"channels":0}})");
        auto tabs = homeTabs(catalogue);
        check(tabs.size() == 3 && tabs[2].key == "inputs",
              "Home tabs honor tuner availability and layout");
        auto rows = homeRows("home", catalogue, Json(), Json());
        check(rows.size() == 1 && rows[0].cards.size() == 1 && rows[0].cards[0].id == "a",
              "Hidden apps and LG Home remain hidden");
        auto inputs = homeRows("inputs", catalogue, Json(), Json());
        check(inputs[0].cards[0].command.body["kind"].string() == "dev",
              "Input launch uses the existing API contract");
        Json byKind = Json::parse(
            R"({"apps":[{"id":"a","title":"Alpha","kind":"video"},{"id":"b","title":"Beta","kind":"tools"},{"id":"c","title":"Gamma","kind":"video"}],"devices":[],"channels":[],"order":{"app":["a","b","c"],"dev":[],"ch":[]},"kinds":[{"key":"tools","label":"Tools"},{"key":"video","label":"Movies & TV"}],"layout":{"rows":["app"],"hidden":{"app":[]},"sort":{"app":"kind"},"showLgHome":false},"sources":{"channels":0}})");
        // Google TV's Apps tab: banners of the apps of films, music and games, Your apps, the
        // search bar, a button per kind opening its page, then a row per kind in the owner's order.
        auto kinds = homeRows("apps", byKind, Json(), Json());
        check(kinds.size() == 6 && kinds[0].key == "appfeature" && kinds[0].cards.size() == 2 &&
                  kinds[0].cards[0].id == "a" && kinds[0].cards[1].id == "c",
              "The Apps tab's banners are its apps of films, music and games");
        check(kinds[1].key == "app" && kinds[1].title == "Your apps" && kinds[1].cards.size() == 3 &&
                  kinds[1].cards[0].id == "b" && kinds[2].key == "search" &&
                  kinds[2].cards[0].command.route == "search",
              "Your apps, sorted by kind, and the search bar follow");
        check(kinds[3].key == "chips" && kinds[3].cards.size() == 2 && kinds[3].cards[0].title == "Tools" &&
                  kinds[3].cards[1].command.route == "apps:video",
              "A button per kind opens the kind's page");
        check(kinds[4].key == "appkind" && kinds[4].group == "tools" && kinds[4].title == "Tools" &&
                  kinds[4].cards.size() == 1 && kinds[5].cards.size() == 2 && kinds[5].cards[1].id == "c",
              "Apps sorted by kind come as one row per kind, in the owner's order");
        // The stores to get more apps from, after the buttons: LG Content Store first, opened
        // like an app even while it is hidden, and the Homebrew Channel's catalogue.
        Json withStores = byKind;
        Json storeApps = byKind["apps"];
        storeApps.add(Json::parse(R"({"id":"org.webosbrew.hbchannel","title":"Homebrew Channel","kind":"tools"})"));
        storeApps.add(Json::parse(R"({"id":"com.webos.app.discovery","title":"LG Content Store"})"));
        withStores.set("apps", storeApps);
        auto stored = homeRows("apps", withStores, Json(), Json());
        check(stored.size() == 7 && stored[4].key == "appstore" && stored[4].title == "Get more apps" &&
                  stored[4].cards.size() == 2 && stored[4].cards[0].id == "com.webos.app.discovery" &&
                  stored[4].cards[0].image == "app:com.webos.app.discovery" &&
                  stored[4].cards[0].command.body["kind"].string() == "app" &&
                  stored[4].cards[0].command.body["id"].string() == "com.webos.app.discovery" &&
                  stored[4].cards[1].title == "Homebrew Channel" && stored[4].cards[1].command.route == "catalog" &&
                  stored[5].key == "appkind",
              "Get more apps follows the buttons: LG Content Store, then the Homebrew Channel's catalogue");
        auto flat = homeRows("home", byKind, Json(), Json());
        check(flat.size() == 1 && flat[0].cards.size() == 3 && flat[0].cards[0].id == "b",
              "Home's apps row follows the kinds' order");
        // The Apps page and Home's row have their own hidden apps (`hidden.appsTab` and `hidden.app`):
        // a kind whose apps are all hidden on the page loses its row there, and nothing carries over.
        Json hiddenByKind = Json::parse(
            R"({"apps":[{"id":"a","title":"Alpha","kind":"video"},{"id":"b","title":"Beta","kind":"tools"},{"id":"c","title":"Gamma","kind":"video"}],"devices":[],"channels":[],"order":{"app":["a","b","c"],"dev":[],"ch":[]},"kinds":[{"key":"tools","label":"Tools"},{"key":"video","label":"Movies & TV"}],"layout":{"rows":["app"],"hidden":{"app":[],"appsTab":["b"]},"sort":{"app":"kind"},"showLgHome":false},"sources":{"channels":0}})");
        auto kindRows = homeRows("apps", hiddenByKind, Json(), Json());
        check(kindRows.size() == 5 && kindRows[3].cards.size() == 1 && kindRows[3].cards[0].id == "kind:video" &&
                  kindRows[4].group == "video" && kindRows[4].cards.size() == 2,
              "A kind whose apps are all hidden on the Apps page has no row there, nor a button");
        auto homeApps = homeRows("home", hiddenByKind, Json(), Json());
        check(homeApps.size() == 1 && homeApps[0].cards.size() == 3,
              "An app hidden on the Apps page is still on Home's row");
        Json hiddenApps = hiddenByKind;
        hiddenApps.set("layout", Json::parse(
            R"({"rows":["app"],"hidden":{"app":["c"],"appsTab":["a"]},"sort":{"app":"custom"},"showLgHome":false})"));
        auto tabRows = homeRows("apps", hiddenApps, Json(), Json());
        check(tabRows.size() == 6 && tabRows[1].cards.size() == 2 && tabRows[1].cards[0].id == "b" &&
                  tabRows[1].cards[1].id == "c" && tabRows[0].cards.size() == 1 && tabRows[0].cards[0].id == "c",
              "The Apps tab leaves out what it hides, and keeps what only Home hides");
        homeApps = homeRows("home", hiddenApps, Json(), Json());
        check(homeApps[0].cards.size() == 2 && homeApps[0].cards[0].id == "a" &&
                  homeApps[0].cards[1].id == "b",
              "Home's row leaves out what Home hides, and keeps what only the Apps page hides");
        CardRow pageShown = appsRow(hiddenApps, Json(), false, "appsTab"),
                pageListed = appsRow(hiddenApps, Json(), true, "appsTab"),
                homeListed = appsRow(hiddenApps, Json(), true);
        check(pageShown.cards.size() == 2 && pageListed.cards.size() == 3 &&
                  pageListed.cards[2].id == "a" && pageListed.cards[2].hidden &&
                  !pageListed.cards[0].hidden && homeListed.cards.size() == 3 &&
                  homeListed.cards[2].id == "c" && homeListed.cards[2].hidden,
              "Editing lists the apps hidden from that list last, flagged; otherwise they are left out");
        // A catalogue from before the Apps page had a list hides there what Home hides.
        Json oldCatalogue = hiddenByKind;
        oldCatalogue.set("layout", Json::parse(
            R"({"rows":["app"],"hidden":{"app":["b"]},"sort":{"app":"custom"},"showLgHome":false})"));
        check(homeRows("apps", oldCatalogue, Json(), Json())[0].cards.size() == 2,
              "Without an Apps page list the page hides what Home hides");
        Json movedOrder = Json::parse(R"(["a","h","b","c"])");
        std::vector<std::string> moved = orderAfterMove(movedOrder, {"c", "b", "a"});
        check(moved == std::vector<std::string>({"c", "h", "b", "a"}),
              "A moved order keeps the hidden app where it was");
        moved = orderAfterMove(movedOrder, {"a", "b", "c", "n"});
        check(moved.size() == 5 && moved[4] == "n", "An app the order did not list follows it");
        Json feed = Json::parse(
            R"({"rows":[{"key":"netflix","items":[{"id":42,"type":"tv","title":"A title","year":"2026","rating":8.5,"backdrop":"/abc.jpg"}]}]})");
        rows = homeRows("home", catalogue, feed, Json());
        check(rows.size() == 2 && rows[1].key == "netflix" && rows[1].title == "Available on Netflix",
              "The Netflix row is consumed without a new data source");
        check(rows[1].cards[0].kind == "netflix" && rows[1].cards[0].command.body["kind"].string() == "netflix" &&
                  rows[1].cards[0].command.body["type"].string() == "tv",
              "OK on a Netflix card opens it in Netflix");
        check(rows[1].cards[0].image == "tmdb:w500:/abc.jpg",
              "Artwork remains proxied through the server");
        check(rows[1].cards[0].subtitle == "Series  ·  2026  ·  ★ 8.5",
              "A title's facts lead with its kind and star the rating");
        Json titlesFeed = Json::parse(
            R"({"rows":[{"key":"movies","items":[{"id":7,"type":"movie","title":"Seven","year":"2025","rating":7.4,"backdrop":"/b7.jpg","overview":"A plot."},{"id":8,"type":"movie","title":"Eight"}]},)"
            R"({"key":"on:netflix","items":[{"id":7,"type":"movie","title":"Seven"}]},)"
            R"({"key":"films:popular","items":[{"id":7,"type":"movie","title":"Seven","year":"2025","rating":7.4},{"id":9,"type":"movie","title":"Nine","year":"2024"}]},)"
            R"({"key":"shows:top","items":[{"id":3,"type":"tv","title":"Three"}]}]})");
        tabs = homeTabs(catalogue, titlesFeed);
        check(tabs.size() == 5 && tabs[1].key == "films" && tabs[1].title == "Films" &&
                  tabs[2].key == "shows" && tabs[2].title == "Shows" && tabs[3].key == "apps" &&
                  titlesTab("films") && titlesTab("shows") && !titlesTab("home"),
              "A feed with the tabs' rows adds Films and Shows after Home, as on Google TV");
        rows = homeRows("films", catalogue, titlesFeed, Json());
        check(rows.size() == 2 && rows[0].key == "feature" && rows[0].cards.size() == 1 &&
                  rows[0].cards[0].image == "tmdb:w1280:/b7.jpg" &&
                  rows[0].cards[0].subtitle == "A plot." &&
                  rows[0].cards[0].data["service"].string() == "netflix" &&
                  rows[1].key == "films:popular" && rows[1].title == "Popular films" &&
                  rows[1].cards.size() == 2,
              "Films leads with the trending films that have a backdrop, then its own rows");
        check(titleCaption(rows[1].cards[0]) == "Netflix  ·  2025  ·  ★ 7.4" &&
                  titleCaption(rows[1].cards[1]) == "2024",
              "A tab card's caption names the service a Popular on row lists it on, then its facts");
        rows = homeRows("shows", catalogue, titlesFeed, Json());
        check(rows.size() == 1 && rows[0].title == "Top rated shows",
              "Shows lists its own rows, without featured cards where no trending series has a backdrop");
        Json topPicks = Json::parse(
            R"({"rows":[{"key":"trending","items":[{"id":1,"type":"movie","title":"One","poster":"/p.jpg"}]}]})");
        rows = homeRows("home", catalogue, topPicks, Json());
        check(rows.size() == 2 && rows[0].key == "trending" && rows[1].key == "app" &&
                  rows[0].title == "Top picks for you",
              "The top picks lead Home and the apps follow them, as on Google TV since 2025");
        check(rows[0].cards[0].image == "tmdb:w342:/p.jpg",
              "A title with no backdrop shows its poster on the landscape card");
        // A streaming service's row follows the top picks in the order of its app in Your apps,
        // only for an app there (not one hidden from the row), and its Netflix row stands in for
        // Available on Netflix.
        Json streamingCatalogue = Json::parse(
            R"({"apps":[{"id":"netflix","title":"Netflix"},{"id":"amazon.global","title":"Prime Video"},{"id":"com.disney.disneyplus-prod","title":"Disney+"}],"devices":[],"channels":[],"order":{"app":["amazon.global","netflix","com.disney.disneyplus-prod"]},"layout":{"rows":["app"],"hidden":{"app":["com.disney.disneyplus-prod"]}},"sources":{"channels":0}})");
        Json streamingFeed = Json::parse(
            R"({"rows":[{"key":"trending","items":[{"id":1,"type":"movie","title":"One","poster":"/p.jpg"}]},{"key":"series","items":[{"id":3,"type":"tv","title":"Three","poster":"/s.jpg"}]},{"key":"on:netflix","items":[{"id":4,"type":"tv","title":"Four","poster":"/n.jpg","service":"netflix"}]},{"key":"on:prime","items":[{"id":5,"type":"movie","title":"Five","poster":"/a.jpg","service":"prime"}]},{"key":"on:disney","items":[{"id":6,"type":"movie","title":"Six","poster":"/d.jpg","service":"disney"}]},{"key":"netflix","items":[{"id":7,"type":"tv","title":"Seven","backdrop":"/x.jpg"}]}]})");
        rows = homeRows("home", streamingCatalogue, streamingFeed, Json());
        check(rows.size() == 5 && rows[0].key == "trending" && rows[1].key == "app" &&
                  rows[2].key == "on:prime" && rows[3].key == "on:netflix" && rows[4].key == "series",
              "The services' rows follow the top picks in the apps' order, without a hidden app's");
        check(rows[2].title == "Popular on Prime Video" && rows[3].title == "Popular on Netflix",
              "A service's row is named after it");
        streamingFeed.set("watchedServices", Json::parse(R"(["netflix","disney"])"));
        auto watchedRows = homeRows("home", streamingCatalogue, streamingFeed, Json());
        check(watchedRows.size() == 5 && watchedRows[2].key == "on:netflix" &&
                  watchedRows[3].key == "on:prime",
              "The service the profile watches most leads the services' rows");
        Json personalFeed = Json::parse(
            R"({"rows":[{"key":"trending","items":[{"id":1,"type":"movie","title":"One","poster":"/p.jpg"}]},{"key":"movies","items":[{"id":2,"type":"movie","title":"Two","poster":"/m.jpg"}]},{"key":"liked","title":"Because you liked One","items":[{"id":3,"type":"tv","title":"Three","poster":"/l.jpg"}]},{"key":"watchlist","items":[{"id":4,"type":"movie","title":"Four","poster":"/w.jpg"}]}]})");
        auto personal = homeRows("home", catalogue, personalFeed, Json());
        check(personal.size() == 5 && personal[0].key == "trending" && personal[1].key == "app" &&
                  personal[2].key == "watchlist" && personal[2].title == "Your watchlist" &&
                  personal[3].key == "liked" && personal[3].title == "Because you liked One" &&
                  personal[4].key == "movies",
              "The profile's watchlist and the titles like its last like follow the apps");
        check(personal[2].cards[0].kind == "title" && personal[2].cards[0].command.route == "details",
              "OK on a watchlist title opens its details");
        Json continueFeed = Json::parse(
            R"({"rows":[{"key":"trending","items":[{"id":1,"type":"movie","title":"One","poster":"/p.jpg"}]},{"key":"watchlist","items":[{"id":4,"type":"movie","title":"Four","poster":"/w.jpg"}]},{"key":"continue","items":[{"id":5,"type":"movie","title":"Five","poster":"/c.jpg","service":"prime","progress":0.4}]}]})");
        auto continuing = homeRows("home", catalogue, continueFeed, Json());
        check(continuing.size() == 4 && continuing[2].key == "continue" &&
                  continuing[2].title == "Continue watching" && continuing[3].key == "watchlist",
              "Continue watching follows the top picks and the apps");
        const Card &resume = continuing[2].cards[0];
        check(resume.kind == "continue" && resume.command.body["kind"].string() == "service" &&
                  resume.command.body["service"].string() == "prime" &&
                  resume.data["progress"].number() == 0.4,
              "OK on a title being watched goes back into its app, and the card knows how far");
        Json playFeed = Json::parse(
            R"({"rows":[{"key":"continue","items":[{"kind":"app","id":"netflix","appId":"netflix","service":"netflix","title":"Netflix","playedMs":3780000,"endedAt":5,"frame":"netflix@5"},{"kind":"app","id":"amazon.global","appId":"amazon.global","service":"prime","title":"Prime Video","playedMs":2820000,"endedAt":4}]}]})");
        auto plays = homeRows("home", catalogue, playFeed, Json());
        const Card &played = plays[1].cards[0];
        check(plays[1].key == "continue" && played.kind == "continue" &&
                  played.title == "Netflix" && played.subtitle == "Watched for 1 h 3 min" &&
                  played.image == "frame:netflix@5" && played.backdrop == played.image &&
                  played.command.body["kind"].string() == "app" &&
                  played.command.body["id"].string() == "netflix",
              "A play without a title is its app's card: its last frame, and OK opens the app");
        check(plays[1].cards[1].image.empty() &&
                  plays[1].cards[1].subtitle == "Watched for 47 min",
              "A play with no frame has no picture");
        Json withSubscriptions = Json::parse(
            R"({"apps":[{"id":"youtube.leanback.v4","title":"YouTube"},{"id":"netflix","title":"Netflix"}],"devices":[],"channels":[],"order":{"app":["youtube.leanback.v4","netflix"]},"layout":{"rows":["app"],"hidden":{}},"sources":{"channels":0}})");
        Json subscriptionsFeed = Json::parse(
            R"({"rows":[{"key":"on:netflix","items":[{"id":4,"type":"tv","title":"Four","poster":"/n.jpg","service":"netflix"}]},{"key":"youtube:subscriptions","items":[{"id":"bbbbbbbbbb1","type":"youtube","title":"Newest","overview":"B","image":"https://i.ytimg.com/vi/bbbbbbbbbb1/mqdefault.jpg"}]}]})");
        auto subscriptionRows = homeRows("home", withSubscriptions, subscriptionsFeed, Json());
        check(subscriptionRows.size() == 3 && subscriptionRows[1].key == "youtube:subscriptions" &&
                  subscriptionRows[1].title == "From your YouTube subscriptions" &&
                  subscriptionRows[2].key == "on:netflix",
              "The YouTube subscriptions take YouTube's place among the services");
        check(subscriptionRows[1].cards[0].kind == "youtube" &&
                  subscriptionRows[1].cards[0].command.body["kind"].string() == "youtube" &&
                  !subscriptionRows[1].posters,
              "OK on a subscription's video plays it in YouTube");
        check(rows[2].cards[0].kind == "title" && rows[2].cards[0].command.route == "details" &&
                  rows[2].cards[0].data["service"].string() == "prime",
              "OK on a service's title opens its details, which know the service");
        Json oled = Json::parse(
            R"({"ok":true,"isOled":true,"writable":true,"serviceControls":true,"gsr":true,"panelHours":123})");
        auto menu = dashboardRows("oled", oled, Json());
        check(find(menu, "Global sticky reduction").command.confirm,
              "Disabling OLED protection requires confirmation");
        check(
            find(menu, "Global sticky reduction").command.body["value"]["enabled"].boolean(true) ==
                false,
            "Protection commands retain typed booleans");
        Json apps = Json::parse(
            R"({"ok":true,"writable":true,"installed":[{"id":"org.test","title":"Test"}],"tileHidingEnabled":false,"tileHidingHeld":true,"systemTiles":[{"id":"stock"}]})");
        menu = dashboardRows("apps", apps, Json());
        check(find(menu, "Test").command.confirm, "Uninstall requires confirmation");
        check(find(menu, "System App Hiding").command.empty(),
              "Firmware-held controls remain unavailable");
        Json caps = fields({{"allowPower", Json::flag(false)}});
        menu = dashboardRows("options", Json::parse("{\"ok\":true}"), caps);
        bool power = false;
        for (const auto &item : menu)
            if (item.label == "Power off")
                power = true;
        check(!power, "Power actions honor server capabilities");
        caps.set("allowPower", Json::flag(true));
        menu = dashboardRows("options", Json::parse("{\"ok\":true}"), caps);
        check(find(menu, "Power off").command.confirm, "Power actions retain confirmation");
        for (const auto &tab : dashboardTabs())
            check(!dashboardRows(tab.key, Json::parse("{\"ok\":true}"), caps).empty(),
                  "Every dashboard tab has a defined empty state");
        auto setup = setupRows("network", Json());
        check(setup[0].command.body["open"].boolean(),
              "Network setup sends the server's open field");
        setup = setupRows("standby", Json());
        check(setup[0].command.body["on"].boolean(), "Standby setup sends the server's on field");
        setup = setupRows("summary", Json());
        check(setup.back().command.body["action"].string() == "done",
              "Setup completion uses the existing action");
        std::printf("%d native model checks passed\n", checks);
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
