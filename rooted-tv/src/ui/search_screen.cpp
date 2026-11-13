// Google TV's search, as its launcher has it (launcherx 1.0.1009811201 on a Chromecast with Google
// TV, measured from its screens). Search is a tab of the bar: its page is a wide field over the
// "Search by genre" grid, which rises under a large title when it takes the focus, the bar leaving.
// OK on the field opens the typing page: black, the field at the top left and the TV keyboard at
// the foot, whose strip suggests titles as one types. Its search key, a suggestion or a genre opens
// the results page: the query, Edit search, the TV's apps whose name has the words, the apps of LG
// Content Store and of the Homebrew Channel that have them (as Google TV's results list the Play
// Store's), and a row of films and one of series, which rise as Home's rows do and caption the
// focused one. A title opens its details over the search, and an app of LG Content Store its page
// there, which keep everything for Back; a Homebrew app opens in the Homebrew catalogue.
//
// Suggestions are asked for 350 ms after the last key (two characters at least); a reply is taken
// only if it answers the latest request.
#include "domain/models.hpp"
#include "domain/names.hpp"
#include "domain/say.hpp"
#include "ui/application.hpp"
#include "ui/details_screen.hpp"
#include "ui/home_layout.hpp"
#include "ui/keyboard_pad.hpp"
#include "ui/kit/overlay.hpp"
#include "ui/kit/tuning.hpp"
#include "ui/optimistic.hpp"
#include "ui/overlay_routes.hpp"
#include "ui/store_app_screen.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace rootedtv {
namespace {
namespace layout = kit::layout;
using kit::easing;
using say::msg;

constexpr const char *kResults = "title:search", *kSuggest = "title:suggest", *kPad = "search:kb",
                      *kStore = "store:search";
constexpr double kDebounceMs = 350;
constexpr size_t kQueryMax = 80, kMinQuery = 2, kSuggestionsMax = 8, kGenreColumns = 6;

// The tiles' colours, in names::searchGenres' order.
Color genreColor(size_t index) {
    static const Color colors[] = {
        kit::color::genreAction,  kit::color::genreAdventure, kit::color::genreAnimation,
        kit::color::genreComedy,  kit::color::genreCrime,     kit::color::genreDocumentary,
        kit::color::genreDrama,   kit::color::genreFamily,    kit::color::genreFantasy,
        kit::color::genreWar,     kit::color::genreHistory,   kit::color::genreHorror,
        kit::color::genreMusic,   kit::color::genreMystery,   kit::color::genreReality,
        kit::color::genreRomance, kit::color::genreScifi,     kit::color::genreThriller};
    return colors[index % (sizeof colors / sizeof colors[0])];
}
std::string trim(const std::string &value) {
    size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
size_t characters(const std::string &value) {
    size_t count = 0;
    for (unsigned char byte : value)
        if ((byte & 0xc0) != 0x80)
            ++count;
    return count;
}
Card resultCard(const Json &item) {
    Card card;
    card.id = item["id"].string();
    card.kind = "title";
    card.title = item["title"].string();
    card.subtitle = titleFacts(item);
    card.image = artwork(item);
    // Key art with the title on it, where the server found some, as Home's cards have.
    if (!item["art"].string().empty()) {
        card.image = "tmdb:w500:" + item["art"].string();
        card.titled = true;
    }
    card.data = item;
    return card;
}

class SearchScreen final : public ScreenView {
    enum class Page { Tab, Typing, Results };

  public:
    std::string name() const override {
        return details_ ? "details" : storeApp_ ? "storeapp" : "search";
    }
    // Only the Search tab's own page has the bar; the typing and results pages fill the screen.
    bool hasBar() const override {
        return !details_ && !storeApp_ && page_ == Page::Tab;
    }
    bool barSearch() const override {
        return true;
    }
    float barAway() const override {
        return barAway_;
    }
    bool paintsWall() const override {
        return true;
    }

    void enter(ScreenHost &host) override {
        KeyboardPad::Options options;
        options.id = kPad;
        options.origin = {layout::tvKeyLeft, layout::tvKeyTop, layout::tvKeyW, layout::tvKeyH};
        options.maximum = kQueryMax;
        options.tv = true;
        options.doneLabel = "";
        options.doneIcon = "search";
        pad_.begin(host.motion(), "", options);
        showPage(host, Page::Tab);
        retarget(host, true);
    }
    void leave(ScreenHost &host) override {
        if (details_) {
            details_->leave(host);
            details_.reset();
        }
        if (storeApp_) {
            storeApp_->leave(host);
            storeApp_.reset();
        }
        host.services().cancel(kStore);
        host.services().cancel(kResults);
        host.services().cancel(kSuggest);
        host.motion().erasePrefix("search:");
    }
    bool update(ScreenHost &host) override {
        if (details_)
            return false;
        if (storeApp_)
            return storeApp_->update(host);
        const double now = host.nowMs();
        if (suggestAt_ > 0 && now >= suggestAt_)
            suggest(host);
        bool lit = pad_.caretLit(now);
        if (page_ == Page::Typing && lit != caretLit_) {
            caretLit_ = lit;
            host.invalidate();
        }
        return suggestAt_ > 0 || (page_ == Page::Results && loading_);
    }
    void focus(ScreenHost &host) override {
        if (page_ == Page::Tab)
            onGrid_ = false;
        retarget(host, false);
    }

    // ---- Keys --------------------------------------------------------------------------------

    bool key(ScreenHost &host, Key key) override {
        if (details_)
            return details_->key(host, key);
        if (storeApp_)
            return storeApp_->key(host, key);
        bool used = true;
        if (page_ == Page::Tab)
            used = tabKey(host, key);
        else if (page_ == Page::Typing)
            typingKey(host, key);
        else if (resultsKey(host, key))
            return true; // another screen has taken this one's place
        retarget(host, false);
        host.invalidate();
        return used;
    }
    bool back(ScreenHost &host) override {
        if (details_) {
            details_->back(host);
            dropFinishedDetails(host);
            return true;
        }
        if (storeApp_) {
            storeApp_->back(host);
            dropFinishedDetails(host);
            return true;
        }
        if (page_ == Page::Tab) {
            if (!onGrid_) {
                host.open(returnRoute(host.cameFrom()));
                return true;
            }
            onGrid_ = false;
        } else if (page_ == Page::Typing) {
            showTab(host, false);
        } else if (!onButton_) {
            onButton_ = true;
        } else if (!genre_.empty()) {
            showTab(host, true);
        } else {
            openTyping(host, query_);
        }
        retarget(host, false);
        return true;
    }
    bool text(ScreenHost &host, const std::string &typed) override {
        if (details_ || storeApp_)
            return true;
        if (page_ != Page::Typing)
            openTyping(host, "");
        afterPad(host, pad_.type(host.nowMs(), typed));
        return true;
    }
    void click(ScreenHost &host, const std::string &id) override {
        if (details_) {
            details_->click(host, id);
            dropFinishedDetails(host);
            return;
        }
        if (storeApp_) {
            storeApp_->click(host, id);
            return;
        }
        const std::string pad = std::string(kPad) + ":";
        if (id == "search:field") {
            openTyping(host, "");
        } else if (id.compare(0, 13, "search:genre:") == 0) {
            genreIndex_ = std::min(static_cast<size_t>(std::atoi(id.c_str() + 13)),
                                   names::searchGenres().size() - 1);
            openGenre(host);
        } else if (id.compare(0, 12, "search:chip:") == 0) {
            const size_t chip = static_cast<size_t>(std::atoi(id.c_str() + 12));
            if (chip < suggestions_.size())
                openResults(host, suggestions_[chip], "");
        } else if (id.compare(0, pad.size(), pad) == 0) {
            onChips_ = false;
            afterPad(host, pad_.click(host.motion(), host.nowMs(), id));
        } else if (id == "search:edit") {
            editSearch(host);
        } else if (id.compare(0, 12, "search:card:") == 0) {
            const char *at = id.c_str() + 12;
            const size_t row = static_cast<size_t>(std::atoi(at));
            const char *colon = std::strchr(at, ':');
            if (row < rows_.size() && colon) {
                onButton_ = false;
                row_ = row;
                columns_[row_] = std::min(static_cast<size_t>(std::atoi(colon + 1)),
                                          rows_[row_].cards.size() - 1);
                if (openDetails(host))
                    return; // another screen has taken this one's place
            }
        }
        retarget(host, false);
        host.invalidate();
    }

    // ---- Replies -----------------------------------------------------------------------------

    void response(ScreenHost &host, const Response &response) override {
        if (details_) {
            details_->response(host, response);
            dropFinishedDetails(host);
        }
        if (storeApp_)
            storeApp_->response(host, response);
        const Json data = response.json();
        if (response.channel == kStore && storeGeneration_ && response.generation == storeGeneration_) {
            storeGeneration_ = 0;
            storeRows_.clear();
            if (response.ok() && data["ok"].boolean()) {
                CardRow lg, homebrew;
                lg.key = "search:store";
                lg.title = msg("tv.search.lgStore", "LG Content Store");
                homebrew.key = "search:homebrew";
                homebrew.title = msg("tv.search.homebrew", "Homebrew Channel");
                for (const auto &app : data["apps"].items()) {
                    Card card;
                    card.id = app["id"].string();
                    card.kind = "store";
                    card.title = app["title"].string();
                    card.image = app["icon"].string();
                    card.data = app;
                    lg.cards.push_back(card);
                }
                for (const auto &app : data["homebrew"].items()) {
                    Card card;
                    card.id = app["id"].string();
                    card.kind = "homebrew";
                    card.title = app["title"].string();
                    card.data = app;
                    homebrew.cards.push_back(card);
                }
                for (CardRow *row : {&lg, &homebrew})
                    if (!row->cards.empty())
                        storeRows_.push_back(*row);
            }
            composeRows(host);
        }
        const bool usable = response.ok() && data.valid() && data["ok"].boolean() && data["enabled"].boolean();
        if (response.channel == kSuggest && suggestGeneration_ &&
            response.generation == suggestGeneration_) {
            suggestGeneration_ = 0;
            suggestions_.clear();
            if (usable)
                for (const auto &item : data["items"].items()) {
                    const std::string title = item["title"].string();
                    if (!title.empty() && suggestions_.size() < kSuggestionsMax &&
                        std::find(suggestions_.begin(), suggestions_.end(), title) == suggestions_.end())
                        suggestions_.push_back(title);
                }
            chip_ = std::min(chip_, suggestions_.empty() ? 0 : suggestions_.size() - 1);
            if (suggestions_.empty() && onChips_)
                onChips_ = false;
            retarget(host, true);
        }
        if (response.channel == kResults && resultsGeneration_ &&
            response.generation == resultsGeneration_) {
            resultsGeneration_ = 0;
            loading_ = false;
            failed_ = !usable;
            titleRows_.clear();
            if (usable) {
                // Google TV's results come in rows; TMDB's are films and series, a row of each.
                CardRow films, series;
                films.key = "search:films";
                films.title = msg("tv.search.films", "Films");
                series.key = "search:series";
                series.title = msg("tv.search.series", "Series");
                for (const auto &item : data["items"].items())
                    (item["type"].string() == "tv" ? series : films).cards.push_back(resultCard(item));
                for (CardRow *row : {&films, &series})
                    if (!row->cards.empty())
                        titleRows_.push_back(*row);
            }
            composeRows(host);
        }
    }

    // ---- Drawing -----------------------------------------------------------------------------

    void build(ScreenHost &host, kit::Paint paint) override {
        barAway_ = 0;
        if (details_) {
            details_->build(host, paint);
            return;
        }
        if (storeApp_) {
            storeApp_->build(host, paint);
            return;
        }
        Scene &scene = paint.scene;
        const kit::Enter page = {static_cast<float>(host.motion().value("search:in")), 0};
        scene.shape({0, 0, kit::canvas::width, kit::canvas::height},
                    page_ == Page::Typing ? Color{0, 0, 0, 1} : kit::color::page);
        scene.pushOpacity(page.alpha);
        if (page_ == Page::Tab)
            drawTab(host, paint);
        else if (page_ == Page::Typing)
            drawTyping(host, paint);
        else
            drawResults(host, paint);
        scene.popOpacity();
    }
    Json snapshot() const override {
        if (details_ || storeApp_) {
            Json value = details_ ? details_->snapshot() : storeApp_->snapshot();
            value.set("origin", Json::text("search"));
            return value;
        }
        Json value = Json::object();
        value.set("page", Json::text(page_ == Page::Tab ? "tab" : page_ == Page::Typing ? "typing" : "results"));
        value.set("query", Json::text(page_ == Page::Typing    ? pad_.value()
                                      : page_ == Page::Results ? query_
                                                               : ""));
        value.set("genre", Json::text(genre_));
        std::string focus = page_ == Page::Tab      ? (onGrid_ ? "grid" : "field")
                            : page_ == Page::Typing ? (onChips_ ? "suggestions" : "keys")
                                                    : (onButton_ ? "edit" : "rows");
        value.set("focus", Json::text(focus));
        value.set("genreIndex", Json::numeric(static_cast<double>(genreIndex_)));
        value.set("focusedKey", Json::text(pad_.focusedKey()));
        value.set("suggestions", Json::numeric(static_cast<double>(suggestions_.size())));
        size_t results = 0;
        for (const CardRow &row : rows_)
            results += row.cards.size();
        value.set("results", Json::numeric(static_cast<double>(results)));
        value.set("row", Json::numeric(static_cast<double>(row_)));
        value.set("column", Json::numeric(static_cast<double>(row_ < columns_.size() ? columns_[row_] : 0)));
        value.set("contentState", Json::text(loading_ ? "loading" : failed_ ? "error" : results ? "ready" : "empty"));
        return value;
    }

  private:
    // ---- Pages -------------------------------------------------------------------------------

    void showPage(ScreenHost &host, Page page) {
        page_ = page;
        host.motion().set("search:in", 0);
        host.motion().tween("search:in", 1, kit::motion::ms::base, easing(kit::motion::ease::out));
    }
    // The Search tab's page; Google TV forgets what was typed.
    void showTab(ScreenHost &host, bool onGrid) {
        showPage(host, Page::Tab);
        onGrid_ = onGrid;
        pad_.setValue("", host.nowMs());
        suggestions_.clear();
        suggestAt_ = 0;
        host.services().cancel(kSuggest);
        retarget(host, true);
    }
    void openTyping(ScreenHost &host, const std::string &value) {
        showPage(host, Page::Typing);
        pad_.setValue(value, host.nowMs());
        pad_.activate(host.motion(), true);
        onChips_ = false;
        suggestions_.clear();
        if (characters(trim(value)) >= kMinQuery)
            suggestAt_ = host.nowMs();
        retarget(host, true);
    }
    void openResults(ScreenHost &host, const std::string &query, const std::string &genre) {
        showPage(host, Page::Results);
        query_ = query;
        genre_ = genre;
        rows_.clear();
        titleRows_.clear();
        storeRows_.clear();
        columns_.clear();
        row_ = 0;
        onButton_ = true;
        loading_ = true;
        failed_ = false;
        suggestAt_ = 0;
        host.services().cancel(kSuggest);
        host.services().cancel(kStore);
        // A genre has no apps; words may name one to install.
        storeGeneration_ =
            genre.empty() ? host.services().read(kStore, Operation::StoreSearch, fields({{"query", Json::text(query)}}))
                          : 0;
        resultsGeneration_ = host.services().read(
            kResults, Operation::SearchTitles,
            genre.empty() ? fields({{"query", Json::text(query)}}) : fields({{"genre", Json::text(genre)}}));
        if (!resultsGeneration_) {
            loading_ = false;
            failed_ = true;
        }
        for (const char *prefix : {"search:f:", "search:rx:", "search:rg:"})
            host.motion().erasePrefix(prefix);
        retarget(host, true);
    }
    void openGenre(ScreenHost &host) {
        const names::SearchGenre &genre = names::searchGenres()[genreIndex_];
        openResults(host, genreName(genre.key), genre.key);
    }
    // Edit search: back to the keyboard with the words searched; a genre's are not typed.
    void editSearch(ScreenHost &host) {
        openTyping(host, genre_.empty() ? query_ : "");
    }

    // ---- Keys of each page ---------------------------------------------------------------------

    bool tabKey(ScreenHost &host, Key key) {
        const size_t count = names::searchGenres().size();
        if (!onGrid_) {
            if (key == Key::Up)
                return false; // to the bar
            if (key == Key::Down)
                onGrid_ = true;
            else if (key == Key::Ok)
                openTyping(host, "");
            return true;
        }
        const size_t column = genreIndex_ % kGenreColumns;
        if (key == Key::Left && column > 0)
            --genreIndex_;
        else if (key == Key::Right && column + 1 < kGenreColumns && genreIndex_ + 1 < count)
            ++genreIndex_;
        else if (key == Key::Up && genreIndex_ < kGenreColumns)
            onGrid_ = false;
        else if (key == Key::Up)
            genreIndex_ -= kGenreColumns;
        else if (key == Key::Down && genreIndex_ + kGenreColumns < count)
            genreIndex_ += kGenreColumns;
        else if (key == Key::Ok)
            openGenre(host);
        return true;
    }
    void typingKey(ScreenHost &host, Key key) {
        if (onChips_) {
            if (key == Key::Left && chip_ > 0)
                --chip_;
            else if (key == Key::Right && chip_ + 1 < suggestions_.size())
                ++chip_;
            else if (key == Key::Down) {
                onChips_ = false;
                pad_.activate(host.motion(), true);
            } else if (key == Key::Ok && chip_ < suggestions_.size())
                openResults(host, suggestions_[chip_], "");
            return;
        }
        KeyboardPad::Result result = pad_.key(host.motion(), host.nowMs(), key);
        if (result == KeyboardPad::Result::None && key == Key::Up && !suggestions_.empty()) {
            onChips_ = true;
            chip_ = 0;
            pad_.activate(host.motion(), false);
            return;
        }
        afterPad(host, result);
    }
    // True when OK opened another screen, which has taken this one's place: nothing of it may be
    // touched after.
    bool resultsKey(ScreenHost &host, Key key) {
        if (onButton_) {
            if (key == Key::Down && !rows_.empty())
                onButton_ = false;
            else if (key == Key::Ok)
                editSearch(host);
            return false;
        }
        size_t &column = columns_[row_];
        if (key == Key::Left && column > 0)
            --column;
        else if (key == Key::Right && column + 1 < rows_[row_].cards.size())
            ++column;
        else if (key == Key::Up && row_ > 0)
            --row_;
        else if (key == Key::Up)
            onButton_ = true;
        else if (key == Key::Down && row_ + 1 < rows_.size())
            ++row_;
        else if (key == Key::Ok)
            return openDetails(host);
        return false;
    }
    // What a key of the pad did: a change asks for suggestions, its search key searches.
    void afterPad(ScreenHost &host, KeyboardPad::Result result) {
        if (result == KeyboardPad::Result::Edited) {
            suggestions_.clear();
            host.services().cancel(kSuggest);
            suggestGeneration_ = 0;
            suggestAt_ = characters(trim(pad_.value())) >= kMinQuery ? host.nowMs() + kDebounceMs : 0;
        } else if (result == KeyboardPad::Result::Done) {
            const std::string query = trim(pad_.value());
            if (characters(query) >= kMinQuery)
                openResults(host, query, "");
        }
        host.invalidate();
    }
    void suggest(ScreenHost &host) {
        suggestAt_ = 0;
        const std::string query = trim(pad_.value());
        if (characters(query) < kMinQuery)
            return;
        suggestGeneration_ =
            host.services().read(kSuggest, Operation::SearchTitles, fields({{"query", Json::text(query)}}));
    }

    // ---- Details -------------------------------------------------------------------------------

    CardRow matchingApps(ScreenHost &host) const {
        CardRow row;
        row.key = "search:apps";
        row.title = msg("tv.search.apps", "Apps");
        if (!genre_.empty())
            return row;
        auto lower = [](std::string text) {
            for (char &c : text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        };
        const std::string words = lower(trim(query_));
        if (words.empty())
            return row;
        const ViewState &view = host.view();
        for (const Card &card : appsRow(view.catalogue, view.guide, false, "appsTab").cards)
            if (lower(card.title).find(words) != std::string::npos)
                row.cards.push_back(card);
        return row;
    }
    // The rows in Google TV's order, the focus kept on the row it was on as the replies come in: the
    // TV's apps, the stores' apps, then the films and series.
    void composeRows(ScreenHost &host) {
        const std::string focusedKey = row_ < rows_.size() ? rows_[row_].key : "";
        const size_t focusedColumn = row_ < columns_.size() ? columns_[row_] : 0;
        rows_.clear();
        // The TV's own apps whose name has the words lead, as Google TV's Apps results do.
        CardRow apps = matchingApps(host);
        if (!apps.cards.empty())
            rows_.push_back(apps);
        for (const CardRow &row : storeRows_)
            rows_.push_back(row);
        for (const CardRow &row : titleRows_)
            rows_.push_back(row);
        failed_ = failed_ && rows_.empty();
        columns_.assign(rows_.size(), 0);
        row_ = 0;
        for (size_t r = 0; r < rows_.size(); ++r)
            if (!onButton_ && rows_[r].key == focusedKey) {
                row_ = r;
                columns_[r] = std::min(focusedColumn, rows_[r].cards.size() - 1);
            }
        if (rows_.empty())
            onButton_ = true;
        retarget(host, true);
        host.invalidate();
    }
    // OK on a result: an app opens, a title opens its details over the search, an app of a store its
    // page. True when another screen took this one's place.
    bool openDetails(ScreenHost &host) {
        if (row_ >= rows_.size() || columns_[row_] >= rows_[row_].cards.size())
            return false;
        const Card &chosen = rows_[row_].cards[columns_[row_]];
        if (rows_[row_].key == "search:store") {
            storeApp_.reset(new StoreAppScreen(chosen.id, chosen.data, true));
            storeApp_->enter(host);
            host.invalidate();
            return false;
        }
        if (rows_[row_].key == "search:homebrew") {
            const std::string route = "catalog:" + chosen.id;
            host.open(route);
            return true;
        }
        if (rows_[row_].key == "search:apps") {
            const Command &command = rows_[row_].cards[columns_[row_]].command;
            if (!command.route.empty()) {
                const std::string route = command.route;
                host.open(route);
                return true;
            }
            const Json body = command.body;
            Write write;
            write.operation = command.operation;
            write.body = [body](const Json &) { return body; };
            write.failure = msg("tv.apps.openFailed", "That did not open");
            host.optimistic().run("search.open", write);
            return false;
        }
        const Json &item = rows_[row_].cards[columns_[row_]].data;
        details_.reset(new DetailsScreen(item["type"].string(), item["id"].string(), item, true));
        details_->enter(host);
        host.invalidate();
        return false;
    }
    void dropFinishedDetails(ScreenHost &host) {
        if (storeApp_ && storeApp_->finished()) {
            storeApp_->leave(host);
            storeApp_.reset();
            retarget(host, true);
            host.invalidate();
        }
        if (!details_ || !details_->finished())
            return;
        details_->leave(host);
        details_.reset();
        retarget(host, true);
        host.invalidate();
    }

    // ---- Focus ---------------------------------------------------------------------------------

    // Every value that follows the focus, for the page on show.
    void retarget(ScreenHost &host, bool jump) {
        Motion &motion = host.motion();
        auto tween = [&](const std::string &id, double to, double ms, kit::Bezier curve) {
            if (jump)
                motion.set(id, to);
            else
                motion.tween(id, to, ms, easing(curve));
        };
        const bool bar = host.barFocused();
        tween("search:rise", page_ == Page::Tab && onGrid_ ? 1 : 0, kit::motion::ms::page, kit::motion::ease::decelerate);
        tween("search:ff", page_ == Page::Tab && !onGrid_ && !bar ? 1 : 0, kit::motion::ms::focus, kit::motion::ease::out);
        for (size_t i = 0; i < names::searchGenres().size(); ++i) {
            const bool on = page_ == Page::Tab && onGrid_ && i == genreIndex_;
            tween("search:g:" + std::to_string(i), on ? 1 : 0, on ? kit::motion::ms::cardFocus : kit::motion::ms::cardUnfocus,
                  on ? kit::motion::ease::cardFocus : kit::motion::ease::cardUnfocus);
        }
        for (size_t i = 0; i < suggestions_.size(); ++i)
            tween("search:chip:" + std::to_string(i), page_ == Page::Typing && onChips_ && i == chip_ ? 1 : 0,
                  kit::motion::ms::focus, kit::motion::ease::out);
        const bool rows = page_ == Page::Results && !onButton_ && !rows_.empty();
        tween("search:bf", page_ == Page::Results && onButton_ ? 1 : 0, kit::motion::ms::focus, kit::motion::ease::out);
        tween("search:rs", rows ? 1 : 0, kit::motion::ms::page, kit::motion::ease::decelerate);
        std::vector<home::RowLook> looks;
        for (size_t r = 0; r < rows_.size(); ++r) {
            home::RowLook look;
            look.up = 1;
            look.risen = rows ? 1 : 0;
            look.grow = look.captions = rows && r == row_ ? 1 : 0;
            looks.push_back(look);
            tween("search:rg:" + std::to_string(r), look.grow, kit::motion::ms::page, kit::motion::ease::decelerate);
            for (size_t c = 0; c < rows_[r].cards.size(); ++c) {
                const bool on = rows && r == row_ && c == columns_[r];
                tween("search:f:" + std::to_string(r) + "." + std::to_string(c), on ? 1 : 0,
                      on ? kit::motion::ms::cardFocus : kit::motion::ms::cardUnfocus,
                      on ? kit::motion::ease::cardFocus : kit::motion::ease::cardUnfocus);
            }
            const double scroll = kit::geometry::rowScrollTarget(static_cast<int>(columns_[r]),
                                                                 static_cast<int>(rows_[r].cards.size()),
                                                                 home::itemWidth(rows_[r]), home::itemGap(rows_[r]));
            const std::string name = "search:rx:" + std::to_string(r);
            if (jump)
                motion.set(name, scroll);
            else
                motion.spring(name, scroll, kit::springParams(kit::motion::spring::row));
        }
        // As Home's rows: the focused one's title stands where Home's risen rows have theirs.
        double stack = 0;
        if (rows) {
            const std::vector<float> tops = home::rowTops(rows_, looks, layout::resultRowsTop);
            stack = layout::rowsRisenTop - tops[row_];
        }
        if (jump)
            motion.set("search:sy", stack);
        else
            motion.spring("search:sy", stack, kit::springParams(kit::motion::spring::page));
        host.invalidate();
    }

    // ---- Parts ---------------------------------------------------------------------------------

    void drawTab(ScreenHost &host, kit::Paint paint) {
        const Motion &motion = host.motion();
        const float rise = static_cast<float>(motion.value("search:rise"));
        barAway_ = rise;
        kit::SearchField field;
        field.rest = {layout::searchFieldX, kit::focus::mix(layout::searchFieldY, layout::searchFieldRisenY, rise),
                      layout::searchFieldW, layout::searchFieldH};
        field.id = "search:field";
        field.placeholder = msg("tv.search.placeholder", "Search films, series and more");
        field.focus = host.barFocused() ? 0 : static_cast<float>(motion.value("search:ff"));
        field.enter = {kit::focus::mix(1, .5f, rise), 0};
        kit::searchField(paint, field);
        kit::Row label;
        label.placed = true;
        label.y = kit::focus::mix(layout::genreTitleY, layout::genreRisenTitleY, rise);
        label.px = kit::focus::mix(36, 63, rise);
        label.grow = rise;
        label.label = msg("tv.search.byGenre", "Search by genre");
        kit::row(paint, label);
        const float top = kit::focus::mix(layout::genreTop, layout::genreRisenTop, rise);
        const auto &genres = names::searchGenres();
        for (size_t i = 0; i < genres.size(); ++i) {
            const size_t column = i % kGenreColumns, row = i / kGenreColumns;
            kit::GenreTile tile;
            tile.rest = {layout::homeX + column * (layout::genreW + layout::genreGapX),
                         top + row * (layout::genreH + layout::genreGapY), layout::genreW, layout::genreH};
            tile.id = "search:genre:" + std::to_string(i);
            tile.label = genreName(genres[i].key);
            tile.color = genreColor(i);
            tile.lit = rise;
            tile.focus = static_cast<float>(motion.value("search:g:" + std::to_string(i)));
            kit::genreTile(paint, tile);
        }
    }
    void drawTyping(ScreenHost &host, kit::Paint paint) {
        const Motion &motion = host.motion();
        Scene &scene = paint.scene;
        paint.glyphs.icon(scene, {layout::typingIconX, layout::typingIconY, layout::typingIcon, layout::typingIcon},
                          "search", kit::color::typingIcon);
        const std::string value = pad_.value();
        const Rect line = {layout::typingTextX, layout::typingTextY, kit::canvas::width - layout::typingTextX - 116,
                           kit::type::searchTyping.line};
        paint.glyphs.text(scene, line,
                          value.empty() ? msg("tv.search.placeholder", "Search films, series and more") : value,
                          kit::type::searchTyping, value.empty() ? kit::color::typingInk : kit::color::onSurface);
        if (caretLit_) {
            const float x = line.x + paint.glyphs.measure(value.substr(0, pad_.cursor()), kit::type::searchTyping);
            scene.shape({x + 1, line.y + 6, 3, line.h - 12}, kit::color::accent);
        }
        // Google TV's keyboard: a panel at the foot, its strip over the keys.
        const Rect panel = {layout::tvKeysX, layout::tvKeysY, layout::tvKeysW, layout::tvKeysH};
        Node &base = scene.shape(panel, kit::color::tvKeys);
        base.corners = {12, 12, 0, 0};
        if (suggestions_.empty()) {
            paint.glyphs.text(scene, {panel.x, layout::tvChipY + (layout::tvChipH - kit::type::tvChip.line) / 2,
                                      panel.w, kit::type::tvChip.line},
                              msg("tv.search.typeHint", "Type a title, then press search"), kit::type::tvChip,
                              kit::color::onSurface, Node::Align::Center);
        } else {
            scene.pushClip({panel.x, layout::tvChipY - 8, panel.w, layout::tvChipH + 16});
            float x = layout::tvKeyLeft;
            for (size_t i = 0; i < suggestions_.size(); ++i) {
                const float width = paint.glyphs.measure(suggestions_[i], kit::type::tvChip) + 2 * layout::tvChipPad;
                const Rect chip = {x, layout::tvChipY, width, layout::tvChipH};
                const float on = kit::focus::clamp01(static_cast<float>(motion.value("search:chip:" + std::to_string(i))));
                scene.shape(chip, kit::focus::mix(kit::color::tvKey, kit::color::focusFill, on), chip.h / 2);
                paint.glyphs.text(scene, {chip.x, chip.y + (chip.h - kit::type::tvChip.line) / 2, chip.w, kit::type::tvChip.line},
                                  suggestions_[i], kit::type::tvChip,
                                  kit::focus::inkAt(kit::color::onSurface, kit::color::onFocus, on), Node::Align::Center);
                scene.hits.push_back({chip, "search:chip:" + std::to_string(i)});
                x += width + layout::tvChipGap;
            }
            scene.popClip();
        }
        pad_.paint(paint, motion);
    }
    void drawResults(ScreenHost &host, kit::Paint paint) {
        const Motion &motion = host.motion();
        Scene &scene = paint.scene;
        const float stack = static_cast<float>(motion.value("search:sy")),
                    risen = static_cast<float>(motion.value("search:rs"));
        // The query and Edit search go up a little faster than the rows, as Google TV's do.
        const float lift = stack - layout::resultHeaderLift * risen;
        paint.glyphs.icon(scene, {layout::resultIconX, layout::resultIconY + lift, layout::resultIcon, layout::resultIcon},
                          "search", kit::color::onSurface);
        paint.glyphs.text(scene, {layout::resultQueryX, layout::resultQueryY + lift, 1500, kit::type::searchQuery.line},
                          query_, kit::type::searchQuery, kit::color::onSurface, Node::Align::Left, 1);
        paint.glyphs.text(scene, {layout::homeX, layout::resultLineY + lift, 1500, kit::type::searchLine.line},
                          genre_.empty() ? msg("tv.search.resultsLine", "Films and series that match your search")
                                         : msg("tv.search.genreLine", "Popular films and series of this genre"),
                          kit::type::searchLine, kit::color::onSurface);
        drawEditButton(paint, lift, static_cast<float>(motion.value("search:bf")));
        if (loading_ || failed_ || rows_.empty()) {
            const std::string status = loading_  ? msg("tv.search.searching", "Searching…")
                                       : failed_ ? msg("tv.search.failed", "Search is unavailable. Try again.")
                                                 : msg("tv.search.none", "No films or series found. Try another search.");
            paint.glyphs.text(scene, {layout::homeX, layout::resultRowsTop, 1500, kit::type::searchLine.line}, status,
                              kit::type::searchLine, failed_ ? kit::color::bad : kit::color::onSurface2);
            return;
        }
        std::vector<home::RowLook> looks;
        for (size_t r = 0; r < rows_.size(); ++r) {
            home::RowLook look;
            look.up = 1;
            look.risen = risen;
            look.grow = look.captions = static_cast<float>(motion.value("search:rg:" + std::to_string(r)));
            looks.push_back(look);
        }
        const std::vector<float> tops = home::rowTops(rows_, looks, layout::resultRowsTop);
        for (size_t r = 0; r < rows_.size(); ++r) {
            const CardRow &row = rows_[r];
            const home::RowLook &look = looks[r];
            const float labelY = tops[r] + stack, itemsY = labelY + home::labelArea(look);
            if (labelY > kit::canvas::height + 40 || itemsY + home::itemHeight(row) + 40 < 0)
                continue;
            kit::Row label;
            label.placed = true;
            label.y = labelY;
            label.px = home::labelPx(look);
            label.grow = look.grow;
            label.label = row.title;
            kit::row(paint, label);
            const float scroll = static_cast<float>(motion.value("search:rx:" + std::to_string(r)));
            for (size_t c = 0; c < row.cards.size(); ++c) {
                Rect rest = home::item(row, itemsY, c);
                rest.x += scroll;
                if (rest.x > kit::canvas::width + 40 || rest.x + rest.w < -40)
                    continue;
                const Card &card = row.cards[c];
                const float focus = static_cast<float>(motion.value("search:f:" + std::to_string(r) + "." + std::to_string(c)));
                if (row.key == "search:apps" || row.key == "search:store" || row.key == "search:homebrew") {
                    kit::AppCard app;
                    app.rest = rest;
                    app.id = "search:card:" + std::to_string(r) + ":" + std::to_string(c);
                    app.name = card.title;
                    app.image = card.image;
                    app.icon = card.id == "lg-home" ? "home" : card.kind == "homebrew" ? "extension" : "apps";
                    app.color = home::appColor(card);
                    app.focus = focus;
                    app.caption = std::min(look.captions, focus);
                    kit::appCard(paint, app);
                    continue;
                }
                kit::PosterCard poster;
                poster.rest = rest;
                poster.id = "search:card:" + std::to_string(r) + ":" + std::to_string(c);
                poster.image = card.image;
                if (!card.titled) {
                    poster.title = card.title;
                    poster.subtitle = card.subtitle;
                }
                poster.focus = focus;
                if (look.captions > 0) {
                    if (poster.focus > .5f)
                        poster.caption = card.title;
                    poster.captionLine = titleCaption(card);
                    poster.captionAlpha = look.captions;
                }
                kit::card(paint, poster);
            }
        }
    }
    // Google TV's Edit search: a light pill with the keyboard's icon, grown a little with the focus.
    void drawEditButton(kit::Paint paint, float lift, float focus) {
        Scene &scene = paint.scene;
        const std::string label = msg("tv.search.edit", "Edit search");
        const float on = kit::focus::clamp01(focus),
                    width = paint.glyphs.measure(label, kit::type::title) + 2 * layout::heroButtonPad + 44;
        const Rect button = {layout::homeX, layout::resultButtonY + lift, width, layout::resultButtonH};
        scene.pushTransform(Transform::scaleAbout(button, .5f, .5f, 1 + .05f * on));
        scene.shape(button, kit::focus::mix(kit::alpha::capsule, kit::color::searchButton, on), button.h / 2);
        const Color ink = kit::focus::inkAt(kit::color::onSurface, kit::color::onFocus, on);
        const float x = button.x + layout::heroButtonPad, middle = button.y + button.h / 2;
        paint.glyphs.icon(scene, {x, middle - 16, 32, 32}, "keyboard", ink);
        paint.glyphs.text(scene, {x + 44, middle - kit::type::title.line / 2, width, kit::type::title.line}, label,
                          kit::type::title, ink);
        scene.popTransform();
        scene.hits.push_back({button, "search:edit"});
    }

    KeyboardPad pad_;
    Page page_ = Page::Tab;
    bool onGrid_ = false, onChips_ = false, onButton_ = true, loading_ = false, failed_ = false, caretLit_ = true;
    size_t genreIndex_ = 0, chip_ = 0, row_ = 0;
    std::vector<size_t> columns_;
    std::vector<std::string> suggestions_;
    std::vector<CardRow> rows_;
    std::string query_, genre_;
    double suggestAt_ = 0;
    unsigned suggestGeneration_ = 0, resultsGeneration_ = 0;
    float barAway_ = 0;
    std::unique_ptr<DetailsScreen> details_;
    std::unique_ptr<StoreAppScreen> storeApp_;
    std::vector<CardRow> titleRows_, storeRows_;
    unsigned storeGeneration_ = 0;
};
} // namespace

std::unique_ptr<ScreenView> makeSearchScreen(const std::string &route) {
    return route == "search" ? std::unique_ptr<ScreenView>(new SearchScreen()) : nullptr;
}
} // namespace rootedtv
