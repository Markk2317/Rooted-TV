// Profiles, title details and the search and profile editors, driven through the Application as
// the remote drives them: keys in, requests and replies at the Services seam, the scene and the
// snapshot read back. (The Quick drawer's own tests are in quick_drawer.cpp.)
#include "overlay_harness.hpp"
#include "ui/profile_names.hpp"

using namespace overlay_test;

// ---- Kit parts --------------------------------------------------------------------------------

Rect hitOf(const Scene &scene, const std::string &id) {
    for (const auto &hit : scene.hits)
        if (hit.id == id)
            return hit.rect;
    return {};
}
void kitChecks() {
    kit::LegacyGlyphs glyphs;
    {
        Scene scene;
        kit::PillButton button;
        button.rest = {100, 100, 300, 64};
        button.id = "pill";
        button.label = "Open";
        kit::pillButton({scene, glyphs}, button);
        check(hitOf(scene, "pill").w == 300, "A button at rest is its own size");
        Scene focused;
        button.focus = 1;
        kit::pillButton({focused, glyphs}, button);
        Rect lifted = hitOf(focused, "pill");
        check(lifted.w > 310 && lifted.w < 325 && lifted.x < 100, "A focused button grows about its centre");
        bool ring = false;
        for (const auto &node : focused.nodes)
            ring = ring || node.ring.outer > 0;
        check(ring, "A focused button has its ring");
        Scene hidden;
        button.enter = {0, 28};
        kit::pillButton({hidden, glyphs}, button);
        check(hidden.nodes.empty() && hidden.hits.empty(), "A button that has not entered draws nothing");
    }
    {
        Scene scene;
        kit::veil({scene, glyphs}, 0);
        check(scene.nodes.empty(), "A veil at zero draws nothing");
        kit::veil({scene, glyphs}, .5f);
        check(scene.nodes.size() == 1 && scene.nodes[0].rect.w == 1920 && scene.nodes[0].color.a == .5f,
              "A veil is one full-screen quad");
    }
    {
        Scene off, on;
        kit::radio({off, glyphs}, {0, 0, 28, 28}, 0);
        kit::radio({on, glyphs}, {0, 0, 28, 28}, 1);
        check(off.nodes.size() == 1 && on.nodes.size() == 2, "A radio mark has a dot only when it is on");
    }
    {
        Scene plain, loading;
        kit::StatusPanel panel;
        panel.rest = {1000, 200, 800, 380};
        panel.title = "Searching…";
        kit::statusPanel({plain, glyphs}, panel);
        panel.loadingMs = 300;
        kit::statusPanel({loading, glyphs}, panel);
        check(loading.nodes.size() == plain.nodes.size() + 12, "A loading panel turns the circular spinner");
    }
    {
        Scene early, later;
        kit::spinner({early, glyphs}, {100, 100, 40, 40}, 0, {1, 1, 1, 1});
        kit::spinner({later, glyphs}, {100, 100, 40, 40}, 400, {1, 1, 1, 1});
        check(early.nodes.size() == 12 && later.nodes.size() == 12,
              "The circular progress indicator is twelve dots");
        float headEarly = 0, headLater = 0;
        size_t at = 0, moved = 0;
        for (size_t i = 0; i < 12; ++i) {
            if (early.nodes[i].color.a > headEarly) {
                headEarly = early.nodes[i].color.a;
                at = i;
            }
            if (later.nodes[i].color.a > headLater) {
                headLater = later.nodes[i].color.a;
                moved = i;
            }
        }
        check(headEarly > .95f && headLater > .8f && at != moved,
              "One dot is the bright head, and it has moved on a moment later");
        Scene none;
        kit::spinner({none, glyphs}, {0, 0, 40, 40}, 0, {1, 1, 1, 1}, 0);
        check(none.nodes.empty(), "A spinner at no opacity draws nothing");
    }
    {
        Scene scene;
        float width = kit::keyHint({scene, glyphs}, 80, 1000, "Back", "Return", "hint");
        Rect box = hitOf(scene, "hint");
        check(width > 60 && std::fabs(box.w - width) < .5f && box.x == 80, "A hint is as wide as it says and clickable");
    }
}

// ---- Details ----------------------------------------------------------------------------------

void detailsChecks() {
    World world;
    world.press(Key::Ok);
    check(world.screen() == "details", "OK on a title opens its details");
    check(world.transport.count("action") == 0 && world.transport.count("title:details") == 1,
          "Opening details reads them and launches nothing");
    check(world.transport.last("title:details").operation == Operation::TitleDetails &&
              world.transport.last("title:details").body["id"].string() == "7" &&
              world.transport.last("title:details").body["type"].string() == "movie",
          "The details are read by the title's type and id");
    check(world.app.shows("Loading details…") && world.app.shows("Coast") && world.app.shows("A journey."),
          "The card's own lines show while the details load");
    check(world.snapshot()["contentState"].string() == "loading", "Details start in their loading state");

    world.reply("title:details", detailsJson);
    world.settle(600);
    check(world.snapshot()["contentState"].string() == "ready" && world.snapshot()["focus"].string() == "Trailer" &&
              world.app.shows("A journey across the water.") && world.app.shows("Cast & crew"),
          "A reply fills the page, with Trailer focused (the only deep links are Netflix and YouTube) "
          "and the cast and crew below");
    check(!world.app.shows("Open"), "There is no Open button: a title opens on Netflix or as a trailer");
    world.reply("title:netflix", R"({"ok":true,"netflixId":"80057281"})");
    check(world.snapshot()["focus"].string() == "Open on Netflix",
          "A late Netflix link is the first action, and has the focus while nothing has moved it");
    world.press(Key::Ok);
    check(world.on("w:details.open").size() == 1 && world.on("w:details.open").back().body["kind"].string() == "netflix" &&
              !world.on("w:details.open").back().body.has("netflixId") &&
              world.on("w:details.open").back().body["id"].string() == "7",
          "Open on Netflix posts the title and leaves the mapping to the server");
    world.press(Key::Ok);
    check(world.on("w:details.open").size() == 1, "A second OK while it is opening is ignored");
    world.answer(world.on("w:details.open").back(), false, "Refused");
    check(world.toasted("That did not open: Refused"), "A refused open says so, with the daemon's reason");
    world.press(Key::Right);
    check(world.snapshot()["focus"].string() == "Trailer", "Trailer is reachable");
    world.press(Key::Ok);
    check(world.on("w:details.open").size() == 2 && world.on("w:details.open").back().body["kind"].string() == "youtube" &&
              world.on("w:details.open").back().body["id"].string() == "dQw4w9WgXcQ",
          "Trailer opens the video");

    // As on Google TV the cast and crew come first under the actions, then the similar titles,
    // which open on top of this one; Back peels them off.
    world.press(Key::Down);
    world.settle(600);
    check(world.snapshot()["focus"].string() == "Ada" && world.app.shows("Ada") &&
              world.app.shows("Ben") && world.app.shows("If you liked Coast"),
          "Down raises the cast and crew to the top, the similar titles under them");
    world.press(Key::Ok);
    check(world.snapshot()["depth"].number() == 1, "OK on a person opens nothing");
    world.press(Key::Down);
    check(world.snapshot()["focus"].string() == "Horizon", "Down again moves to the similar titles");
    world.press(Key::Right);
    world.press(Key::Ok);
    check(world.snapshot()["depth"].number() == 2 && world.transport.last("title:details").body["id"].string() == "21",
          "A similar title opens as a second level and is read");
    world.press(Key::Back);
    check(world.screen() == "details" && world.snapshot()["depth"].number() == 1 &&
              world.snapshot()["titleId"].string() == "7",
          "Back returns to the title underneath");
    world.press(Key::Back);
    check(world.screen() == "home" && world.snapshot()["column"].number() == 0,
          "Back from the first title returns to Home as it was");

    // A reply for a title that was left behind does not fill the one on screen.
    World stale;
    stale.press(Key::Ok);
    unsigned first = stale.transport.revisions["title:details"];
    stale.reply("title:details", detailsJson, 200, static_cast<int>(first) - 1);
    check(stale.snapshot()["contentState"].string() == "loading", "A reply to an older request is ignored");
}

void detailsFailureChecks() {
    World world;
    world.press(Key::Ok);
    world.reply("title:details", R"({"ok":false,"error":"Metadata is warming up"})", 503);
    world.settle(300);
    check(world.screen() == "details" && world.snapshot()["contentState"].string() == "error" &&
              world.app.shows("Metadata is warming up") && world.app.shows("Retry"),
          "A failed read keeps the page, says why and offers Retry");
    for (int i = 0; i < 4; ++i)
        world.press(Key::Right);
    check(world.snapshot()["focus"].string() == "Retry", "Retry is the last action");
    world.press(Key::Ok);
    check(world.transport.count("title:details") == 2 && world.snapshot()["contentState"].string() == "loading",
          "Retry reads the details again without leaving");
    world.reply("title:details", R"({"ok":true,"enabled":true,"netflixEnabled":false,"item":{"id":7,"type":"movie","title":"Coast","overview":""}})");
    world.settle(300);
    check(world.snapshot()["contentState"].string() == "ready" && world.app.shows("No overview available"),
          "A title without an overview says so");
    World stale;
    stale.press(Key::Ok);
    stale.reply("title:details", R"({"ok":true,"enabled":true,"stale":true,"item":{"id":7,"type":"movie","title":"Coast","overview":"Kept."}})");
    check(stale.snapshot()["contentStatus"].string() == "Saved details · offline",
          "Details served from the saved copy say they are offline");
    World busy;
    busy.transport.refuse = true;
    busy.press(Key::Ok);
    check(busy.snapshot()["contentState"].string() == "error",
          "A request the transport cannot take is an error, not a spinner");
}

void detailsMarksChecks() {
    World world;
    world.openDetails();
    world.press(Key::Right); // past the trailer
    check(world.snapshot()["focus"].string() == "Watchlist", "The watchlist follows the trailer, as on Google TV");
    world.press(Key::Ok);
    world.settle(50);
    check(world.snapshot()["focus"].string() == "On watchlist", "The watchlist button says so at once");
    auto sent = world.on("w:details.wl:movie:7");
    check(sent.size() == 1 && sent.back().operation == Operation::Control &&
              sent.back().body["action"].string() == "setHomeTitle" &&
              sent.back().body["value"]["op"].string() == "watch" &&
              sent.back().body["value"]["item"]["title"].string() == "Coast" &&
              !sent.back().body["value"]["item"].has("similar"),
          "The title goes to the profile's watchlist through the server");
    world.answer(sent.back(), false, "Refused");
    world.settle(300);
    const bool said = world.toasted("That was not saved: Refused");
    check(said && world.snapshot()["focus"].string() == "Watchlist",
          "A refusal puts the button back and says why");
    world.press(Key::Right);
    check(world.snapshot()["focus"].string() == "Like", "Thumbs up follows, a round button");
    world.press(Key::Ok);
    world.settle(50);
    auto rated = world.on("w:details.rt:movie:7");
    check(rated.size() == 1 && rated.back().body["value"]["op"].string() == "like" &&
              world.snapshot()["focus"].string() == "Liked",
          "Thumbs up likes the title");
    world.answer(rated.back());
    world.settle(300);
    world.press(Key::Right);
    world.press(Key::Ok);
    world.settle(50);
    rated = world.on("w:details.rt:movie:7");
    check(rated.back().body["value"]["op"].string() == "dislike" &&
              world.snapshot()["focus"].string() == "Not for me (marked)",
          "Thumbs down replaces the like");
}

void detailsWatchOptionsChecks() {
    // The services that have the title, as Google TV's watch options, where their app is installed.
    std::string catalogue = catalogueJson;
    catalogue.replace(catalogue.find(R"([{"id":"a","title":"Alpha"}])"),
                      std::string(R"([{"id":"a","title":"Alpha"}])").size(),
                      R"([{"id":"a","title":"Alpha"},{"id":"amazon.global","title":"Prime Video"}])");
    World world(catalogue.c_str());
    world.openDetails(R"({"ok":true,"enabled":true,"item":{"id":7,"type":"movie","title":"Coast","overview":"A journey.","services":["disney","prime"]}})");
    check(world.snapshot()["focus"].string() == "Open on Prime Video" && !world.app.shows("Open on Disney+"),
          "Open on Prime Video leads; Disney+ has no app here");
    world.press(Key::Ok);
    check(world.on("w:details.open").size() == 1 &&
              world.on("w:details.open").back().body["kind"].string() == "service" &&
              world.on("w:details.open").back().body["service"].string() == "prime" &&
              world.on("w:details.open").back().body["id"].string() == "7",
          "It opens the title through the server, which finds the app and the link");
}

void detailsOverviewChecks() {
    World world;
    world.openDetails();
    // After the opens and the trailer: the watchlist and the two thumbs, then the overview.
    for (int i = 0; i < 5; ++i)
        world.press(Key::Right);
    check(world.snapshot()["focus"].string() == "Full overview", "Full overview is an action");
    world.press(Key::Ok);
    world.settle(300);
    check(world.snapshot()["overview"].boolean() && world.app.shows("A journey across the water."),
          "The full overview opens as a sheet over the page");
    world.press(Key::Down);
    check(world.snapshot()["overviewPage"].number() == 0, "A short overview has one page");
    world.press(Key::Back);
    check(world.screen() == "details" && !world.snapshot()["overview"].boolean(),
          "Back closes the sheet and stays on the title");
    world.press(Key::Back);
    check(world.screen() == "home", "Back again leaves the title");
    // A long overview pages by words, never in the middle of one.
    std::string long_text;
    for (int i = 0; i < 400; ++i)
        long_text += "wordy ";
    World paged;
    paged.press(Key::Ok);
    paged.reply("title:details", std::string(R"({"ok":true,"enabled":true,"item":{"id":7,"type":"movie","title":"Coast","overview":")") +
                                     long_text + R"("}})");
    paged.press({Key::Right, Key::Right, Key::Right}); // past the watchlist and the thumbs
    paged.press(Key::Ok);
    paged.press(Key::PageDown);
    check(paged.snapshot()["overviewPage"].number() == 1, "A long overview has more than one page");
    paged.press(Key::PageUp);
    check(paged.snapshot()["overviewPage"].number() == 0, "Page up goes back");
}

void detailsDepthChecks() {
    World world;
    world.openDetails();
    for (int i = 0; i < 7; ++i) {
        world.press({Key::Down, Key::Down}); // past the cast and crew to the similar titles
        world.press(Key::Ok);
        world.reply("title:details", detailsJson);
    }
    check(world.snapshot()["depth"].number() == 8, "Titles open up to eight deep");
    world.press({Key::Down, Key::Down});
    world.press(Key::Ok);
    check(world.snapshot()["depth"].number() == 8 && world.toasted("Go back before opening another title"),
          "A ninth title is refused with a message");
}

// Google TV's rows of a title's genres, after the titles like it: one per genre the details name
// (two at most), each a mix of films and shows without the title itself.
void detailsGenreChecks() {
    std::string answer = detailsJson;
    answer.replace(answer.find(R"("cast":)"), 7, R"("genreKeys":["adventure","scifi","drama"],"cast":)");
    World world;
    world.openDetails(answer.c_str());
    check(world.on("details:genre:1").size() == 1 && world.on("details:genre:2").size() == 1 &&
              world.on("details:genre:3").empty() &&
              world.on("details:genre:1")[0].operation == Operation::SearchTitles &&
              world.on("details:genre:1")[0].body["genre"].string() == "adventure" &&
              world.on("details:genre:2")[0].body["genre"].string() == "scifi",
          "The details ask for the titles of the title's first two genres");
    check(world.snapshot()["rows"].size() == 2, "Until they come, the rows are the cast and the titles like it");
    // The second genre's answer first: its row comes in under the titles like it.
    world.reply("details:genre:2",
                R"({"ok":true,"items":[{"id":7,"type":"movie","title":"Coast"},{"id":30,"type":"movie","title":"Orbit"},)"
                R"({"id":31,"type":"movie","title":"Nova"},{"id":40,"type":"tv","title":"Station"}]})");
    world.settle(100);
    check(world.snapshot()["rows"].size() == 3 && world.snapshot()["rows"][2].string() == "genre:scifi",
          "A genre's row comes in under the titles like it");
    world.press({Key::Down, Key::Down, Key::Down});
    world.settle(600);
    check(world.app.shows("Science fiction films and shows"), "named after its genre");
    check(world.snapshot()["focus"].string() == "Orbit", "Its titles leave the title itself out");
    world.press(Key::Right);
    check(world.snapshot()["focus"].string() == "Station", "and take a film and a show in turn");
    world.reply("details:genre:1",
                R"({"ok":true,"items":[{"id":50,"type":"movie","title":"Summit"},{"id":21,"type":"tv","title":"Tide"},)"
                R"({"id":31,"type":"movie","title":"Nova"}]})");
    world.settle(100);
    check(world.snapshot()["rows"].size() == 4 && world.snapshot()["rows"][2].string() == "genre:adventure" &&
              world.snapshot()["focus"].string() == "Station",
          "A row that comes in above keeps the focus where it was");
    world.press(Key::Up);
    check(world.snapshot()["focus"].string() == "Summit", "and the row above has the genre's titles");
    world.press(Key::Right);
    check(world.snapshot()["focus"].string() == "Nova",
          "less those of the rows above it (Tide is a title like it)");
    world.press(Key::Down);
    world.press(Key::Left);
    check(world.snapshot()["focus"].string() == "Orbit", "and a row under it gives up those it has (Nova)");
    world.press(Key::Right);
    world.press(Key::Ok);
    world.reply("title:details", detailsJson);
    world.settle(300);
    check(world.snapshot()["depth"].number() == 2 && world.snapshot()["rows"].size() == 2,
          "A genre's title opens over the page, with rows of its own");
}

void detailsFocusReturnChecks() {
    World world;
    world.press({Key::Right, Key::Right});
    world.settle(300);
    check(world.snapshot()["column"].number() == 2, "Home focus moved to the third card");
    world.press(Key::Ok);
    check(world.screen() == "details", "The third card opens");
    world.press(Key::Back);
    check(world.screen() == "home" && world.snapshot()["column"].number() == 2,
          "Back lands on the card that opened the details");
}

// ---- Profiles ---------------------------------------------------------------------------------


// ---- Editors ----------------------------------------------------------------------------------

void assistantChecks() {
    // With a Gemini key the bar has the assistant before the gear, as Google TV's Gemini button.
    std::string feed = feedJson;
    feed.replace(feed.find(R"("titlesEnabled":true)"), std::string(R"("titlesEnabled":true)").size(),
                 R"("titlesEnabled":true,"assistant":true)");
    World world;
    world.reply("discover", feed.c_str(), 0);
    world.settle(300);
    check(world.app.activate("tool:3"), "The assistant is on the bar");
    world.settle(300);
    check(world.screen() == "assistant" && world.app.shows("Ask Gemini"), "It opens the assistant's page");
    world.app.input("un film di boxe");
    world.settle(600);
    check(world.transport.count("assistant:ask") == 0, "Typing asks nothing yet");
    check(world.app.activate("editor:kb:4:4"), "Ask is the keyboard's Done key");
    check(world.transport.count("assistant:ask") == 1 &&
              world.transport.last("assistant:ask").operation == Operation::Assistant &&
              world.transport.last("assistant:ask").body["question"].string() == "un film di boxe" &&
              world.snapshot()["contentState"].string() == "loading",
          "Ask sends the question to the assistant");
    world.reply("assistant:ask", R"({"ok":true,"answer":"Rocky is on Prime Video.","titles":[{"id":1366,"type":"movie","title":"Rocky"}]})");
    world.settle(300);
    check(world.snapshot()["answer"].string() == "Rocky is on Prime Video." &&
              world.snapshot()["results"].number() == 1 && world.app.shows("Rocky is on Prime Video."),
          "The answer shows over the titles it named");
    check(world.app.activate("editor:kb:4:4") && world.snapshot()["zone"].string() == "results",
          "Done with the question answered moves to the titles");
    world.press(Key::Ok);
    check(world.screen() == "details" && world.snapshot()["origin"].string() == "assistant",
          "OK on a title opens its details over the answer");
    world.press(Key::Back);
    world.app.activate("editor:kb:4:4");
    world.press(Key::Back);
    world.app.input("?");
    world.app.activate("editor:kb:4:4");
    world.reply("assistant:ask", R"({"ok":false,"error":"Gemini: API key not valid."})", 502);
    world.settle(300);
    check(world.snapshot()["contentState"].string() == "error" && world.app.shows("Gemini: API key not valid."),
          "Gemini's refusal is said as it gave it");
}

void searchChecks() {
    World world;
    world.openSearch();
    check(world.screen() == "search" && world.snapshot()["page"].string() == "tab" &&
              world.snapshot()["focus"].string() == "field" &&
              world.app.shows("Search films, series and more") && world.app.shows("Search by genre") &&
              world.app.shows("Science fiction"),
          "Search opens on Google TV's Search tab: the field over the genres");
    world.press(Key::Ok);
    check(world.snapshot()["page"].string() == "typing" && world.snapshot()["focusedKey"].string() == "q",
          "OK on the field opens the typing page on the keyboard's first key");
    world.app.input("s");
    world.settle(500);
    check(world.transport.count("title:suggest") == 0, "A single character asks for no suggestions");
    world.app.input("t");
    world.settle(300);
    check(world.transport.count("title:suggest") == 0, "Suggestions wait for the debounce");
    world.settle(200);
    check(world.transport.count("title:suggest") == 1 &&
              world.transport.last("title:suggest").body["query"].string() == "st",
          "One request for suggestions goes after the last key");
    world.reply("title:suggest",
                R"({"ok":true,"enabled":true,"items":[{"id":7,"type":"movie","title":"Coast"},{"id":8,"type":"tv","title":"Harbor"},{"id":9,"type":"movie","title":"Coast"}]})");
    world.settle(300);
    check(world.snapshot()["suggestions"].number() == 2 && world.app.shows("Coast") && world.app.shows("Harbor"),
          "The keyboard's strip suggests the titles found, each once");
    // The search key ends the last row, two keys wide.
    check(world.app.activate("search:kb:3:6"), "The search key is on the keyboard");
    check(world.snapshot()["page"].string() == "results" && world.snapshot()["focus"].string() == "edit" &&
              world.transport.last("title:search").body["query"].string() == "st" &&
              world.snapshot()["contentState"].string() == "loading",
          "The search key opens the results page on Edit search and asks for the results");
    world.reply("title:search",
                R"({"ok":true,"enabled":true,"items":[{"id":7,"type":"movie","title":"Coast"},{"id":8,"type":"tv","title":"Harbor"},{"id":10,"type":"movie","title":"Cove"}]})");
    world.settle(300);
    check(world.snapshot()["results"].number() == 3 && world.app.shows("Films") && world.app.shows("Series") &&
              world.app.shows("Edit search"),
          "The results come in a row of films and one of series");
    world.press(Key::Down);
    world.press(Key::Right);
    check(world.snapshot()["focus"].string() == "rows" && world.snapshot()["row"].number() == 0 &&
              world.snapshot()["column"].number() == 1,
          "Down reaches the films and Right moves along them");
    world.press(Key::Ok);
    check(world.screen() == "details" && world.snapshot()["origin"].string() == "search",
          "OK on a result opens its details");
    world.press(Key::Back);
    check(world.screen() == "search" && world.snapshot()["page"].string() == "results" &&
              world.snapshot()["column"].number() == 1,
          "Back from the details restores the results as they were");
    world.press(Key::Back);
    check(world.snapshot()["focus"].string() == "edit", "Back from the rows returns to Edit search");
    world.press(Key::Back);
    check(world.snapshot()["page"].string() == "typing" && world.snapshot()["query"].string() == "st",
          "Back from Edit search returns to the keyboard with the words searched");
    world.press(Key::Back);
    check(world.snapshot()["page"].string() == "tab" && world.snapshot()["query"].string().empty(),
          "Back from the keyboard returns to the Search tab, which forgets them, as Google TV's does");
    world.press(Key::Back);
    check(world.screen() == "home", "Back from the field returns Home");
}

// The TV's apps whose name has the words searched lead the results, and open as apps.
void searchAppsChecks() {
    World world;
    world.openSearch();
    world.press(Key::Ok);
    world.app.input("al");
    world.settle(600);
    check(world.app.activate("search:kb:3:6"), "The search key is on the keyboard");
    world.reply("title:search", R"({"ok":true,"enabled":true,"items":[{"id":7,"type":"movie","title":"Alien"}]})");
    world.settle(300);
    check(world.snapshot()["results"].number() == 2 && world.app.shows("Apps") && world.app.shows("Films"),
          "An app whose name has the words is a row of its own, over the films");
    world.press(Key::Down);
    world.settle(600);
    check(world.snapshot()["row"].number() == 0 && world.app.shows("Alpha"),
          "The apps lead the results, the focused one's name under it");
    world.press(Key::Ok);
    check(world.screen() == "search" && world.transport.requests.back().operation == Operation::OpenHomeItem &&
              world.transport.requests.back().body["id"].string() == "a",
          "OK on an app opens it rather than a title's details");
}

void searchGenreChecks() {
    World world;
    world.openSearch();
    world.press(Key::Down);
    check(world.snapshot()["focus"].string() == "grid" && world.snapshot()["genreIndex"].number() == 0,
          "Down from the field reaches the genres");
    world.settle(800);
    check(world.motion("search:rise") > .99, "The grid rises under its title, the bar leaving");
    world.press(Key::Right);
    world.press(Key::Down);
    check(world.snapshot()["genreIndex"].number() == 7, "The genres stand six to a row");
    world.press(Key::Ok);
    check(world.snapshot()["page"].string() == "results" &&
              world.transport.last("title:search").body["genre"].string() == "family" &&
              world.snapshot()["query"].string() == "Family",
          "A genre opens its results under its name");
    world.reply("title:search", R"({"ok":true,"enabled":true,"items":[{"id":1,"type":"movie","title":"Kin"}]})");
    world.settle(300);
    world.press(Key::Back);
    check(world.snapshot()["page"].string() == "tab" && world.snapshot()["focus"].string() == "grid",
          "Back from a genre's results returns to the grid");
    world.press(Key::Back);
    check(world.snapshot()["focus"].string() == "field", "Back from the grid returns to the field");
}

void searchStateChecks() {
    World world;
    world.openSearch();
    world.app.input("Wa");
    world.settle(500);
    world.reply("title:suggest", R"({"ok":false,"error":"Search service is starting"})", 503);
    world.settle(300);
    check(world.snapshot()["page"].string() == "typing" && world.snapshot()["suggestions"].number() == 0 &&
              world.app.shows("Type a title, then press search"),
          "Typing opens the keyboard; without suggestions its strip says what to do");
    world.app.activate("search:kb:3:6");
    unsigned first = world.transport.revisions["title:search"];
    world.reply("title:search", R"({"ok":false,"error":"Search service is starting"})", 503);
    world.settle(300);
    check(world.snapshot()["contentState"].string() == "error" && world.app.shows("Search is unavailable. Try again."),
          "A failed search says so");
    world.press(Key::Ok);
    check(world.snapshot()["page"].string() == "typing" && world.snapshot()["query"].string() == "Wa",
          "Edit search returns to the keyboard with the words");
    world.app.activate("search:kb:3:6");
    world.reply("title:search", R"({"ok":true,"enabled":true,"items":[]})");
    world.settle(300);
    check(world.snapshot()["contentState"].string() == "empty" && world.app.shows("No films or series found"),
          "An empty answer has its own state");
    world.reply("title:search", R"({"ok":true,"enabled":true,"items":[{"id":1,"type":"movie","title":"Late"}]})", 200,
                static_cast<int>(first));
    check(world.snapshot()["results"].number() == 0, "A reply to an older search is dropped");
    World disabled;
    disabled.openSearch();
    disabled.app.input("ab");
    disabled.app.activate("search:kb:3:6");
    disabled.reply("title:search", R"({"ok":true,"enabled":false,"items":[]})");
    check(disabled.snapshot()["contentState"].string() == "error", "Search turned off on the server is reported");
    World busy;
    busy.openSearch();
    busy.transport.refuse = true;
    busy.app.input("ab");
    busy.app.activate("search:kb:3:6");
    check(busy.snapshot()["contentState"].string() == "error", "A request the transport cannot take is an error");
}

void searchRowsChecks() {
    World world;
    world.openSearch();
    world.app.input("ab");
    world.app.activate("search:kb:3:6");
    world.reply("title:search",
                R"({"ok":true,"enabled":true,"items":[{"id":1,"type":"movie","title":"A"},{"id":2,"type":"movie","title":"B"},{"id":3,"type":"movie","title":"C"},{"id":4,"type":"movie","title":"D"},{"id":5,"type":"movie","title":"E"},{"id":6,"type":"tv","title":"F"}]})");
    world.settle(300);
    world.press(Key::Down);
    for (int i = 0; i < 3; ++i)
        world.press(Key::Right);
    world.settle(900);
    check(std::fabs(world.motion("search:rx:0") - kit::geometry::rowScrollTarget(3, 5, 391, 41)) < .5 &&
              world.motion("search:rs") > .99 && world.motion("search:rg:0") > .99,
          "The focused row rises and scrolls to keep its card first, as Home's rows do");
    world.press(Key::Down);
    check(world.snapshot()["row"].number() == 1 && world.snapshot()["column"].number() == 0,
          "Down moves to the series, at the card it was left on");
    world.press(Key::Up);
    check(world.snapshot()["column"].number() == 3, "Each row keeps its own place");
    world.press(Key::Up);
    world.settle(900);
    check(world.snapshot()["focus"].string() == "edit" && world.motion("search:rs") < .01,
          "Up from the first row returns to Edit search and the rows settle back");
}

void keyboardChecks() {
    World world;
    world.openSearch();
    world.press(Key::Ok);
    // Google TV's keyboard: letters with the digits in the top row's corners.
    check(world.app.activate("search:kb:0:4"), "The keys are targets");
    check(world.snapshot()["query"].string() == "t", "A key is typed");
    world.press(Key::Down);
    world.press(Key::Right);
    world.press(Key::Ok);
    check(world.snapshot()["query"].string() == "th", "Arrow keys move over the keyboard and OK types");
    world.press(Key::Delete);
    check(world.snapshot()["query"].string() == "t", "The Delete key erases a character");
    world.app.input("héllo");
    check(world.snapshot()["query"].string() == "théllo", "Typed text is inserted whole");
    world.app.activate("search:kb:2:0");
    world.app.activate("search:kb:1:0");
    check(world.snapshot()["query"].string() == "théllo" + std::string("A"), "Shift turns the next letter to capitals");
    check(world.app.shows("?123"), "The symbols key is Gboard's ?123");
    const Scene keysScene = world.app.scene();
    auto keyRect = [&](const std::string &id) {
        for (const Hit &hit : keysScene.hits)
            if (hit.id == id)
                return hit.rect;
        return Rect{0, 0, 0, 0};
    };
    // The focused key (a, after shift) is grown; the one beside it is at rest.
    check(keyRect("search:kb:1:1").x == 570 + 79 && keyRect("search:kb:1:1").y == 718 + 86 &&
              keyRect("search:kb:1:1").w == 68 && keyRect("search:kb:3:3").w == 3 * 68 + 2 * 11 &&
              keyRect("search:kb:3:6").x == 570 + 8 * 79 && keyRect("search:kb:3:6").w == 2 * 68 + 11,
          "The keys stand on Gboard's 79x86 pitch, space three keys wide and search two");
    world.app.activate("search:kb:3:0");
    check(world.app.activate("search:kb:0:0") && world.snapshot()["query"].string().back() == '1',
          "?123 switches to the digits and symbols");
}











// ---- Profiles ---------------------------------------------------------------------------------

void profileNameChecks() {
    check(tvShortName("OLED65B3PUA.DUSQLJR") == "B3" && tvShortName("OLED55C4PUA") == "C4" &&
              tvShortName("OLED55CXPUA") == "CX" && tvShortName("OLED77G3PUA") == "G3",
          "An OLED model gives its series");
    check(tvShortName("55UQ7570PUB") == "LGTV" && tvShortName("") == "LGTV" && tvShortName("OLED") == "LGTV",
          "Anything else is LGTV");
    Json main = Json::parse(R"({"id":"main","name":"Main"})"), renamed = Json::parse(R"({"id":"main","name":"Dad"})"),
         other = Json::parse(R"({"id":"guest","name":"Main"})");
    check(profileName(main, "OLED65B3PUA.DUSQLJR") == "B3" && profileName(renamed, "OLED65B3PUA") == "Dad" &&
              profileName(other, "OLED65B3PUA") == "Main",
          "Only the untouched first profile takes the TV's name");
}

void profilesChecks() {
    World world;
    world.openProfiles();
    check(world.screen() == "profiles" && world.snapshot()["selected"].number() == 0 &&
              world.snapshot()["active"].string() == "main",
          "The avatar opens the profile page on the profile in use");
    check(world.app.shows("LGTV") && !world.app.shows("Main"),
          "The first profile is named after the TV (LGTV until the model is known)");
    world.reply("profiles:stats", R"({"ok":true,"device":{"model":"OLED65B3PUA.DUSQLJR"}})");
    world.settle(100);
    check(world.app.shows("B3"), "and after the model B3");

    // Switching: the chosen profile is the one in use at once, before any reply.
    world.press(Key::Right);
    world.press(Key::Ok);
    check(world.snapshot()["active"].string() == "guest" && world.snapshot()["busy"].boolean(),
          "OK on a profile makes it the one in use at once, pending");
    check(world.on("w:profiles.active").size() == 1 &&
              world.on("w:profiles.active")[0].operation == Operation::SetProfile &&
              world.on("w:profiles.active")[0].body["action"].string() == "switch" &&
              world.on("w:profiles.active")[0].body["id"].string() == "guest",
          "The write is a switch to it");
    world.settle(300);
    check(world.snapshot()["active"].string() == "guest" && world.screen() == "profiles",
          "With no reply the page waits and keeps it");
    // A change of mind: the newest wins, the older reply says nothing.
    world.press(Key::Left);
    world.press(Key::Ok);
    check(world.snapshot()["active"].string() == "main", "A second choice replaces it at once");
    world.settle(200);
    check(world.on("w:profiles.active").size() == 2 &&
              world.on("w:profiles.active")[1].body["id"].string() == "main",
          "Only the newest is sent after the interval");
    world.answer(world.on("w:profiles.active")[0]);
    check(world.screen() == "profiles" && world.snapshot()["active"].string() == "main",
          "The reply to the older switch changes nothing");
    world.answer(world.on("w:profiles.active")[1]);
    world.settle(300);
    check(world.screen() == "home", "The newest reply starts Home over");
}

void profilesFailureChecks() {
    World world;
    world.openProfiles();
    world.press(Key::Right);
    world.press(Key::Ok);
    world.answer(world.on("w:profiles.active")[0], false, "Not allowed");
    world.settle(40);
    check(world.screen() == "profiles" && world.snapshot()["active"].string() == "main" &&
              !world.snapshot()["busy"].boolean(),
          "A refused switch gives the profile back");
    check(world.toasted("That did not work: Not allowed"), "and says why");
    // Silence is a failure too.
    World silent;
    silent.openProfiles();
    silent.press(Key::Right);
    silent.press(Key::Ok);
    silent.settle(8300);
    check(silent.snapshot()["active"].string() == "main" && silent.screen() == "profiles",
          "Eight seconds without an answer give it back");
    // Leaving before the reply: the reply no longer opens Home.
    World leaving;
    leaving.openProfiles();
    leaving.press(Key::Right);
    leaving.press(Key::Ok);
    leaving.press(Key::Back);
    check(leaving.screen() == "home", "Back leaves at once");
    leaving.answer(leaving.on("w:profiles.active")[0]);
    leaving.settle(200);
    check(leaving.screen() == "home", "and a late reply stays out of the way");
    // Add profile.
    World adding;
    adding.openProfiles();
    adding.press({Key::Right, Key::Right});
    adding.press(Key::Ok);
    check(adding.on("w:profiles.add").size() == 1 &&
              adding.on("w:profiles.add")[0].body["action"].string() == "add" &&
              adding.on("w:profiles.add")[0].body["switch"].boolean() && adding.snapshot()["busy"].boolean(),
          "Add profile posts an add that switches to it, pending");
    adding.press(Key::Ok);
    check(adding.on("w:profiles.add").size() == 1, "A second press while it is on its way is dropped");
    adding.answer(adding.on("w:profiles.add")[0]);
    check(adding.toasted("Profile added") && adding.screen() == "home", "A new profile is announced and Home starts over");
    World same;
    same.openProfiles();
    same.press(Key::Ok);
    check(same.screen() == "home" && same.on("w:profiles.active").empty(),
          "OK on the profile in use only closes the page");
    World back;
    back.openProfiles();
    back.press(Key::Back);
    check(back.screen() == "home", "Back returns Home");
    // The profile in use's settings: Down from the avatars reaches Manage profile, whose panel holds
    // Rename and the colours.
    World colour;
    colour.openProfiles();
    check(colour.app.shows("Choose a profile") && colour.app.shows("Manage profile"),
          "The page is Google TV's account chooser, with Manage profile under the avatars");
    colour.press(Key::Down);
    check(colour.snapshot()["onButton"].boolean() && !colour.snapshot()["manage"].boolean(),
          "Down from the avatars reaches Manage profile");
    colour.press(Key::Ok);
    colour.settle(600);
    check(colour.snapshot()["manage"].boolean() && colour.snapshot()["row"].number() == 0 &&
              colour.app.shows("Rename") && colour.app.shows("Purple"),
          "OK opens the profile's settings panel on Rename, over the colours");
    colour.press(Key::Down);
    check(colour.snapshot()["colour"].string() == "#a8c7fa", "Down reaches the first colour");
    colour.press(Key::Down);
    check(colour.snapshot()["colour"].string() == "#f2b8b5", "and the next");
    colour.press(Key::Ok);
    check(colour.on("w:profile.colour.main").size() == 1 &&
              colour.on("w:profile.colour.main")[0].operation == Operation::SetProfile &&
              colour.on("w:profile.colour.main")[0].body["action"].string() == "rename" &&
              colour.on("w:profile.colour.main")[0].body["id"].string() == "main" &&
              colour.on("w:profile.colour.main")[0].body["color"].string() == "#f2b8b5" &&
              colour.screen() == "profiles" && colour.snapshot()["manage"].boolean(),
          "OK saves it on the profile route and the panel stays");
    colour.answer(colour.on("w:profile.colour.main")[0], false, "Busy");
    check(colour.toasted("The colour could not be saved: Busy"), "A refusal says why");
    colour.press(Key::Back);
    check(!colour.snapshot()["manage"].boolean() && colour.snapshot()["onButton"].boolean() &&
              colour.screen() == "profiles",
          "Back closes the panel onto Manage profile");
    colour.press(Key::Up);
    check(!colour.snapshot()["onButton"].boolean(), "Up goes back to the avatars");
    World rename;
    rename.openProfiles();
    rename.press(Key::Down);
    rename.press(Key::Ok);
    rename.press(Key::Ok);
    check(rename.screen() == "rename", "Rename in the panel opens the editor");
    rename.press(Key::Back);
    check(rename.screen() == "profiles" && rename.snapshot()["manage"].boolean() &&
              rename.snapshot()["row"].number() == 0,
          "Back from the editor returns to the profile's settings, on Rename");
    rename.press({Key::Back, Key::Back});
    check(rename.screen() == "home", "Back closes the panel, then the page");
}

void renameChecks() {
    World world;
    world.openProfiles();
    world.press(Key::Down);
    world.press(Key::Ok);
    world.press(Key::Ok);
    check(world.screen() == "rename" && world.snapshot()["query"].string() == "LGTV",
          "Rename starts with the name on screen");
    world.app.activate("editor:clear");
    check(world.snapshot()["contentState"].string() == "empty" && world.app.shows("Enter a name"),
          "An empty name is explained");
    world.app.activate("editor:kb:4:4");
    check(world.on("w:profile.name.main").empty() && world.screen() == "rename", "An empty name is not sent");
    world.app.input("Sasha");
    world.app.activate("editor:kb:4:4");
    check(world.screen() == "profiles", "Save returns to the profiles at once");
    check(world.app.shows("Sasha") && !world.app.shows("LGTV"), "with the new name on them before any reply");
    check(world.on("w:profile.name.main").size() == 1 &&
              world.on("w:profile.name.main")[0].operation == Operation::SetProfile &&
              world.on("w:profile.name.main")[0].body["action"].string() == "rename" &&
              world.on("w:profile.name.main")[0].body["id"].string() == "main" &&
              world.on("w:profile.name.main")[0].body["name"].string() == "Sasha",
          "The write is a rename on the profile route");
    world.settle(300);
    check(world.app.shows("Sasha"), "The name stays while the server has not answered");
    world.answer(world.on("w:profile.name.main")[0], false, "Profile service is starting");
    check(world.app.shows("LGTV") && !world.app.shows("Sasha"), "A refusal gives the name back");
    check(world.toasted("The name could not be saved. Try again.: Profile service is starting"),
          "and says why");
    // An accepted rename is held until the catalogue says it too.
    World ok;
    ok.openProfiles();
    ok.press(Key::Down);
    ok.press(Key::Ok);
    ok.press(Key::Ok);
    ok.app.activate("editor:clear");
    ok.app.input("Ana");
    ok.app.activate("editor:kb:4:4");
    ok.answer(ok.on("w:profile.name.main")[0]);
    ok.settle(100);
    check(ok.app.shows("Ana"), "An accepted name stays");
    size_t catalogues = ok.transport.count("catalogue");
    ok.settle(800);
    check(ok.transport.count("catalogue") == catalogues + 1, "The catalogue is read once to settle it");
    ok.reply("catalogue", std::string(catalogueJson).replace(std::string(catalogueJson).find("\"name\":\"Main\""), 13, "\"name\":\"Ana\""));
    ok.settle(100);
    check(ok.app.shows("Ana"), "and the server's name is the one that shows");
    World limit;
    limit.openProfiles();
    limit.press(Key::Down);
    limit.press(Key::Ok);
    limit.press(Key::Ok);
    limit.app.input("aaaaaaaaaaaaaaaaaaaaaaaaa");
    check(limit.snapshot()["query"].string() == "LGTV", "A name over twenty characters is not taken whole");
    limit.app.input("aaaaaaaaaaaaaaaa");
    limit.app.input("b");
    check(limit.snapshot()["query"].string() == "LGTVaaaaaaaaaaaaaaaa", "A name is limited to twenty characters");
}

int main() {
    try {
        kitChecks();
        detailsChecks();
        detailsFailureChecks();
        detailsMarksChecks();
        detailsWatchOptionsChecks();
        detailsOverviewChecks();
        detailsDepthChecks();
        detailsGenreChecks();
        detailsFocusReturnChecks();
        profileNameChecks();
        profilesChecks();
        profilesFailureChecks();
        searchChecks();
        searchAppsChecks();
        assistantChecks();
        searchStateChecks();
        searchRowsChecks();
        searchGenreChecks();
        keyboardChecks();
        renameChecks();
        std::printf("%d native overlay-screen checks passed\n", checkCount());
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
