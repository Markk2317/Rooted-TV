// Home's choreography: what moves when focus, tab, hero or data change. The view (home_view.cpp)
// only reads the values set here.
#include "domain/names.hpp"
#include "domain/say.hpp"
#include "ui/application.hpp"
#include "ui/controller_helpers.hpp"
#include "ui/home_layout.hpp"
#include "ui/kit/tuning.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace rootedtv {
namespace {
using kit::easing;
using kit::springParams;
namespace ms = kit::motion::ms;
namespace ease = kit::motion::ease;
namespace stagger = kit::motion::stagger;

constexpr uint32_t kBeatMs = 5000;
constexpr uint32_t kHeroSettleMs = 400; // a held key moves on without changing the hero each step
constexpr uint32_t kPageSwapMs = 110;   // the old page has all but left
constexpr uint32_t kHeroWaitMs = 1200;  // longest wait for the incoming art
constexpr uint32_t kIconWaitMs = 300;   // longest wait for the apps' icons before their row enters
// The hero's art is asked for at the screen's size (application.cpp asks a full-width picture so).
constexpr int kHeroWidth = 1920, kHeroHeight = 1080;
// The rows' offset past which they cover the banner: its text has faded out (heroParallax).
constexpr double kBannerCoveredY = -150;

bool due(uint32_t now, uint32_t at) {
    return static_cast<int32_t>(now - at) >= 0;
}
bool isTitle(const Card &card) {
    return card.kind == "title" || card.kind == "youtube" || card.kind == "netflix" ||
           card.kind == "continue";
}
std::string artOf(const Card &card) {
    return isTitle(card) ? (card.backdrop.empty() ? card.image : card.backdrop) : "";
}
// A deep, quiet colour per card: the prototype tints the wall with the artwork's darkest tone,
// and the server sends no palette, so the tone comes from the app's colour or the title. An app
// has no picture of its own behind its lines, so its tint is stronger.
Color tintOf(const Card &card) {
    float strength = isTitle(card) ? .22f : .42f;
    std::string color = card.data["color"].string();
    if (color.size() == 7 && color[0] == '#') {
        Color base = kit::rgb(static_cast<unsigned>(std::strtoul(color.c_str() + 1, nullptr, 16)));
        return {base.r * strength + .02f, base.g * strength + .02f, base.b * strength + .03f, 1};
    }
    return home::hsl(static_cast<float>(home::hashOf(card.id + card.title) % 360), .45f,
                     isTitle(card) ? .115f : .2f);
}
// The wash of pages without art (Apps, the screens over Home) in the banner art's own colour, very
// dark: the left third's mean (graphics/image_loader.hpp), its hue and saturation kept, its
// lightness brought down.
Color scrimOf(int tone) {
    const float r = ((tone >> 16) & 0xff) / 255.0f, g = ((tone >> 8) & 0xff) / 255.0f,
                b = (tone & 0xff) / 255.0f;
    const float high = std::max(r, std::max(g, b)), low = std::min(r, std::min(g, b));
    const float lightness = (high + low) / 2, chroma = high - low;
    float hue = 0, saturation = 0;
    if (chroma > 0) {
        saturation = chroma / (1 - std::fabs(2 * lightness - 1));
        hue = high == r ? 60 * std::fmod((g - b) / chroma + 6, 6.0f)
              : high == g ? 60 * ((b - r) / chroma + 2)
                          : 60 * ((r - g) / chroma + 4);
    }
    return home::hsl(hue, std::min(saturation, .8f), std::min(std::max(lightness * .35f, .06f), .15f));
}
// The pick on show behind a focused app, as a wallpaper.
constexpr float kWallpaperAlpha = .22f;
size_t rowIndex(const std::vector<CardRow> &rows, const char *key) {
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].key == key)
            return i;
    return rows.size();
}
// Where focus lands when Home opens: the top picks, so the hero opens on artwork; the apps row
// above them is one press up.
size_t defaultRow(const HomeState &home) {
    if (home.tabs.empty() || home.tabs[home.tab].key != "home")
        return 0;
    size_t trending = rowIndex(home.rows, "trending");
    return trending < home.rows.size() ? trending : 0;
}
// The row that names the card in the hero: the focused one when it holds the card (a title can
// sit in two rows), otherwise the first that does.
std::string rowTitleOf(const ViewState &state, const Card &card) {
    auto holds = [&](const CardRow &row) {
        for (const auto &candidate : row.cards)
            if (candidate.id == card.id && candidate.kind == card.kind)
                return true;
        return false;
    };
    const auto &rows = state.home.rows;
    if (state.home.row < rows.size() && holds(rows[state.home.row]))
        return rows[state.home.row].title;
    for (const auto &row : rows)
        if (holds(row))
            return row.title;
    return "";
}
// The first sentence of an overview, Google TV's one-line logline; the whole text when the first
// sentence is very short or runs long (the line ends in an ellipsis anyway).
std::string loglineOf(const std::string &overview) {
    for (size_t at = overview.find_first_of(".!?", 24); at != std::string::npos && at < 220;
         at = overview.find_first_of(".!?", at + 1))
        if (at + 1 == overview.size() || overview[at + 1] == ' ')
            return overview.substr(0, at + 1);
    return overview;
}
bool featured(const ViewState &state, const Card &card) {
    for (const Card &candidate : state.home.spotlight)
        if (candidate.id == card.id && candidate.kind == card.kind)
            return true;
    return false;
}
HeroText linesOf(const ViewState &state, const Card &card) {
    HeroText text;
    text.title = card.title;
    if (isTitle(card) && featured(state, card) && card.data["overview"].isString()) {
        // Google TV's banner: the service that has the title over it, then why it is shown; with
        // the focus, its logline and a button under that.
        const std::string service = card.data["service"].string();
        for (const auto &candidate : names::streamingServices())
            if (candidate.key == service)
                text.source = candidate.name;
        // Google TV shows the service's own wordmark there, in white.
        if (!text.source.empty())
            text.wordmark = "wordmark-" + service;
        text.tag = say::msg("tv.banner.trending", "Trending");
        text.facts = card.subtitle;
        text.description = loglineOf(card.data["overview"].string());
        text.button = text.source.empty() ? say::msg("tv.banner.moreInfo", "More info")
                                          : say::msg("tv.banner.watchNow", "Watch now");
    } else if (isTitle(card) && state.home.spotlightOwn) {
        // A focused title of a row on Google TV's banner: its logo or title, the service that has
        // it, two lines of its overview and its facts.
        text.preview = true;
        const std::string service = card.data["service"].string();
        for (const auto &candidate : names::streamingServices())
            if (candidate.key == service)
                text.source = candidate.name;
        if (text.source.empty() && card.kind == "youtube")
            text.source = "YouTube";
        text.facts = card.subtitle.empty() && card.kind == "youtube" ? "" : card.subtitle;
        text.more = card.data["overview"].string();
        // A pick Gemini chose says why after its service: "Netflix · Because you watched Heat".
        text.tag = card.data["because"].string();
    } else if (isTitle(card)) {
        text.source = card.data["because"].string().empty() ? rowTitleOf(state, card)
                                                            : card.data["because"].string();
        if (text.source.empty())
            text.source = card.kind == "youtube" ? "YouTube" : say::msg("tv.home.topPicks", "Top picks for you");
        text.facts = card.subtitle.empty() && card.kind == "youtube" ? "YouTube" : card.subtitle;
        text.description = card.data["overview"].string();
    } else if (card.kind == "dev" || card.kind == "ch") {
        text.source = card.kind == "dev" ? say::msg("tv.banner.input", "Input")
                                         : say::msg("tv.banner.channel", "Channel");
        text.facts = card.subtitle;
        text.hint = card.kind == "dev" ? say::msg("tv.banner.switch", "Switch")
                                       : say::msg("tv.banner.watch", "Watch");
    } else {
        text.source = card.id == "lg-home" ? say::msg("tv.home.lgHome", "LG Home")
                                           : say::msg("tv.home.yourApps", "Your apps");
        text.facts = card.data["description"].string();
        text.image = card.image;
        text.hint = say::msg("tv.banner.open", "Open");
    }
    return text;
}
// How many of a row's items start on the screen; the ones past it have no entrance, they are
// scrolled to.
size_t shownOf(const CardRow &row) {
    float pitch = home::itemWidth(row) + home::itemGap(row);
    size_t fit = static_cast<size_t>((kit::canvas::width - kit::canvas::safeX) / pitch) + 1;
    return std::min(row.cards.size(), fit);
}
// Whether the icons of the apps on the first screen of a page of `rows` have arrived (or can
// not). Asking is also what starts their fetch, at the size the circles draw them at.
bool iconsReady(Pictures *pictures, const std::vector<CardRow> &rows, double now) {
    if (!pictures)
        return true;
    bool ready = true;
    for (size_t r = 0; r < rows.size() && r < 3; ++r) {
        const CardRow &row = rows[r];
        if (!home::isApps(row))
            continue;
        size_t shown = shownOf(row);
        int texels = pictureTexels(kit::layout::appD);
        for (size_t c = 0; c < shown; ++c) {
            if (row.cards[c].image.empty())
                continue;
            ImageView view = pictures->view(row.cards[c].image, texels, texels, ImageFit::Cover, now);
            ready = ready && (view.texture != 0 || view.failed);
        }
    }
    return ready;
}
} // namespace

// ---- Data and layout -------------------------------------------------------------------------

// The page is shown from the start: nothing of the last visit's animation survives.
void Application::homeStart() {
    auto &home = state_.home;
    home.arranging = false;
    home.editing = home.editHide = false;
    flow_.entered = false;
    flow_.rowsEntered = 0;
    flow_.pendingTab = -1;
    flow_.tabPending = flow_.textPending = flow_.heroWaiting = flow_.waiting = false;
    flow_.shown = home.tab;
    flow_.lastKeyAt = now_;
    flow_.idle = false;
    flow_.focusId.clear();
    flow_.wantId.clear();
    if (!flow_.beatAt)
        flow_.beatAt = now_ + kBeatMs;
    for (const char *prefix :
         {"f:", "rx", "rd", "rg", "rn", "rs", "ru", "bd", "he", "hb", "ht", "hn", "hu", "ba", "E", "ha", "hx", "dot",
          "pgx", "pgs", "dim", "sy"})
        motion_.erasePrefix(prefix);
    home.art = HeroArt();
    home.hero = Card();
    home.spotlight.clear();
    home.spotIndex = 0;
    home.heroFocus = false;
    bool hero = !home.tabs.empty() && home.tabs[home.tab].key == "home";
    flow_.heroUntouched = hero;
    motion_.set("wall", hero ? 1 : 0);
    motion_.set("dots", 1);
    for (const char *name : {"hk:0", "hk:1", "hp"}) {
        motion_.set(name, 0);
        motion_.ambient(name);
    }
    for (int i = 0; i < 3; ++i)
        motion_.set(home::id("hx", i), 1);
}
// Called whenever the rows, tabs or catalogue changed under the page.
void Application::homeData() {
    auto &home = state_.home;
    if (home.tabs.empty())
        return;
    // The spotlight: the banner's own titles (the feed's featured row), as Google TV's carousel is
    // apart from Top picks for you. A server without them gives the first top picks.
    home.spotlight.clear();
    home.spotlightOwn = false;
    if (home.tabs[home.tab].key == "home") {
        for (const Card &card : featuredCards(state_.discover))
            if (home.spotlight.size() < home::kSpotlightMax && isTitle(card))
                home.spotlight.push_back(card);
        home.spotlightOwn = !home.spotlight.empty();
        size_t trending = rowIndex(home.rows, "trending");
        if (home.spotlight.empty() && trending < home.rows.size())
            for (const Card &card : home.rows[trending].cards)
                if (home.spotlight.size() < home::kSpotlightMax && isTitle(card))
                    home.spotlight.push_back(card);
    }
    home.spotIndex = home.spotlight.empty() ? 0 : std::min(home.spotIndex, home.spotlight.size() - 1);
    // The focus starts on the banner once it has titles, unless a key has moved it since.
    const bool heroFocus = homeHeroOffered() && (home.heroFocus || flow_.heroUntouched) &&
                           !home.navigation && !home.editing && !home.arranging;
    if (heroFocus != home.heroFocus) {
        home.heroFocus = heroFocus;
        homeFocus(true);
    }
    std::vector<std::string> titles;
    for (const auto &tab : home.tabs)
        titles.push_back(tab.title);
    if (titles != flow_.tabTitles) {
        flow_.tabTitles = titles;
        homeRetargetPill(true);
    }
    if (!flow_.entered && !home.rows.empty() && flow_.pendingTab < 0) {
        // The page enters once the top picks are in, so the rows do not reorder under their own
        // entrance (a feed that stays silent costs 600 ms), and once the apps' icons are in, so
        // a circle does not enter as a colour and change under the eye (300 ms at most). Until
        // then nothing of it is drawn.
        if (!flow_.waiting) {
            flow_.waiting = true;
            flow_.waitFrom = now_;
            flow_.rowsEntered = 0;
            motion_.set("pgx", 1);
        }
        const bool homeTab = home.tabs[home.tab].key == "home";
        const bool feed = !homeTab || state_.discover.valid() || due(now_, flow_.waitFrom + 600);
        const bool icons = flow_.rowsEntered ||
                           iconsReady(pictures_, home.rows, now_) ||
                           due(now_, flow_.waitFrom + kIconWaitMs);
        if (feed && icons) {
            flow_.entered = true;
            flow_.waiting = false;
            home.row = defaultRow(home);
            homePlayEntrance(flow_.rowsEntered);
            homeFocus(true);
        } else if (icons && homeTab && !flow_.rowsEntered && home::isApps(home.rows[0])) {
            // The apps lead Home and the feed's rows follow them: they enter without the feed,
            // and the focus will not be on them, so their name is already dim.
            homePlayEntrance(0, 1);
            motion_.set(home::id("rd", 0), 1);
        }
    } else if (flow_.entered && flow_.pendingTab < 0) {
        // Rows that came late (a feed past its 600 ms) enter like the others.
        if (home.rows.size() > flow_.rowsEntered)
            homePlayEntrance(flow_.rowsEntered);
        flow_.rowsEntered = std::min(flow_.rowsEntered, home.rows.size());
        homeFocus(); // rows changed under the focus: its targets follow the card it kept
    }
}
// Plays the entrance of rows [firstRow, endRow); the apps lead Home, so they can enter before the
// rows that follow them.
void Application::homePlayEntrance(size_t firstRow, size_t endRow) {
    auto &home = state_.home;
    Timeline timeline;
    Tween rise(1, 0, ms::base, easing(ease::out));
    motion_.set("pgx", 0); // the page was held hidden until now
    endRow = std::min(endRow, home.rows.size());
    for (size_t r = firstRow; r < endRow && r < 3; ++r) {
        double rowDelay = stagger::homeBase + stagger::homeRow * static_cast<double>(r);
        timeline.add(home::id("ER", r), rise.delayedBy(rowDelay));
        for (size_t c = 0; c < shownOf(home.rows[r]); ++c)
            timeline.add(home::id("EC", r, c),
                         rise.delayedBy(rowDelay +
                                        stagger::homeCell * std::min(static_cast<int>(c), stagger::homeCellMax)));
    }
    flow_.rowsEntered = endRow;
    timeline.play(motion_);
}

// ---- Focus ----------------------------------------------------------------------------------

void Application::homeRetargetPill(bool jump) {
    auto &home = state_.home;
    const bool search = state_.discover["titlesEnabled"].boolean();
    std::vector<Rect> boxes = kit::tabRects(*glyphs_, flow_.tabTitles, search);
    const bool onSearch = home.navigation && home.nav == home.tabs.size() + 2;
    bool onTab = home.navigation && home.nav < home.tabs.size();
    size_t target = onTab ? home.nav : home.tab;
    // A registered screen keeps the pill under its own tab, and search's page under search.
    if (!onTab && screen_ && screen_->barTab() >= 0)
        target = static_cast<size_t>(screen_->barTab());
    if (search && (onSearch || (!onTab && screen_ && screen_->barSearch()))) {
        boxes = {kit::geometry::searchButton()};
        target = 0;
    }
    onTab = onTab || onSearch;
    if (target < boxes.size()) {
        float left = boxes[target].x, right = boxes[target].x + boxes[target].w;
        if (jump || motion_.value("pill:1") < 1) {
            motion_.set("pill:0", left);
            motion_.set("pill:1", right);
        } else {
            // The edge that leads the move is the stiffer spring: the pill stretches, then settles.
            bool rightward = right > motion_.value("pill:1") + .5;
            motion_.spring("pill:1", right,
                           springParams(rightward ? kit::motion::spring::pillLead
                                                  : kit::motion::spring::pillTrail));
            motion_.spring("pill:0", left,
                           springParams(rightward ? kit::motion::spring::pillTrail
                                                  : kit::motion::spring::pillLead));
        }
    }
    motion_.tween("pill:hot", onTab ? 1 : 0, ms::fast, easing(ease::out));
}
// Retargets every value that follows the focus: ring and lift, row scroll, stack, dim, the pill
// and the bar's buttons. A held key only moves targets, so each value glides on from where it is.
void Application::homeFocus(bool jump) {
    auto &home = state_.home;
    if (home.tabs.empty())
        return;
    auto glide = [&](const std::string &name, double to) {
        if (jump)
            motion_.set(name, to);
        else if (name.compare(0, 2, "f:") == 0)
            // Google TV's card focus (its launcher's state animator): in over 300 ms, out over 500.
            motion_.tween(name, to, to > 0 ? ms::cardFocus : ms::cardUnfocus,
                          easing(to > 0 ? ease::cardFocus : ease::cardUnfocus));
        else
            motion_.tween(name, to, ms::focus, easing(ease::decelerate));
    };
    std::string now;
    // The bar, editing or moving take the focus from the banner.
    if (home.navigation || home.editing || home.arranging)
        home.heroFocus = false;
    if (home.navigation) {
        if (home.nav == home.tabs.size())
            now = "f:av";
        else if (home.nav == home.tabs.size() + 1)
            now = "f:gr";
        else if (home.nav == home.tabs.size() + 2)
            now = "f:sr";
        else if (home.nav == home.tabs.size() + 4)
            now = "f:sv";
    } else if (home.heroFocus)
        now = "f:hero";
    else if (home.editing && home.editHide)
        now = "f:eh"; // the Hide from Home button above the page
    else if (home.row < home.rows.size() && flow_.pendingTab < 0)
        now = home::id("f", home.row, home.column);
    if (now != flow_.focusId) {
        if (!flow_.focusId.empty())
            glide(flow_.focusId, 0);
        if (!now.empty())
            glide(now, 1);
        flow_.focusId = now;
    }
    for (size_t r = 0; r < home.rows.size(); ++r)
        glide(home::id("rd", r), !home.navigation && !home.heroFocus && r != home.row ? 1 : 0);
    if (!home.navigation && !home.heroFocus && home.row < home.rows.size()) {
        const CardRow &row = home.rows[home.row];
        std::string name = home::id("rx", home.row);
        double target = kit::geometry::rowScrollTarget(static_cast<int>(home.column),
                                                       static_cast<int>(row.cards.size()),
                                                       home::itemWidth(row), home::itemGap(row));
        if (jump)
            motion_.set(name, target);
        else
            motion_.spring(name, target, springParams(kit::motion::spring::row));
    }
    // Google TV's Home, as measured on a Chromecast with Google TV (Android 14, 2025): with the bar
    // focused the banner is compact and the top picks and Your apps sit under it; the banner focused
    // moves its lines down, adds its description and button, and the rows drop to the screen's
    // foot; the top picks focused lose the bar and the banner shows the focused title; from Your
    // apps down the rows rise to the top, the focused one's title large. The rows' titles grow from
    // 24 to 36 px as soon as anything under the bar has the focus. Without a banner of its own (the
    // Live tab, an older server) the first rows stay under the hero and past them the focused row
    // takes the first row's place.
    const bool overBanner = homeHeroOffered();
    enum class Place { Bar, Banner, Picks, Lower, Other };
    Place place = Place::Other;
    if (overBanner) {
        if (home.navigation)
            place = Place::Bar;
        else if (home.heroFocus)
            place = Place::Banner;
        else if (home.row < home.rows.size())
            place = home.rows[home.row].key == "trending" ? Place::Picks : Place::Lower;
    }
    // Films, Shows and Apps: the featured cards stay where they are; a row under them rises to the
    // top, its title large, as Home's lower rows do. On Apps the rows' titles grow to 36 px as soon
    // as its banners have the focus, as on Google TV.
    const bool titles = titlesTab(home.tabs[home.tab].key);
    const bool apps = home.tabs[home.tab].key == "apps";
    // While apps are edited the rows keep their place, under the button that hides an app.
    if ((titles || apps) && !home.navigation && !home.editing && home.row > 0 && home.row < home.rows.size())
        place = Place::Lower;
    const bool risen = place == Place::Lower;
    const bool titlesUp = place == Place::Banner || place == Place::Picks || risen ||
                          (apps && !home.navigation && !home.editing && !home.rows.empty());
    auto tween = [&](const std::string &name, double to) {
        if (jump)
            motion_.set(name, to);
        else
            motion_.tween(name, to, ms::page, easing(ease::decelerate));
    };
    tween("rs", risen ? 1 : 0);
    tween("ru", titlesUp ? 1 : 0);
    tween("bd", home.navigation && !home.editing ? 1 : 0);
    tween("he", place == Place::Banner || place == Place::Picks ? 1 : 0); // the banner's description
    // and its button, which goes at once, before the lines move up over it
    if (jump)
        motion_.set("hb", place == Place::Banner ? 1 : 0);
    else
        motion_.tween("hb", place == Place::Banner ? 1 : 0,
                      place == Place::Banner ? ms::page : ms::pageOut, easing(ease::decelerate));
    tween("ht", place == Place::Lower ? 0 : 1); // the banner's text
    tween("hu", place == Place::Picks ? 1 : 0); // a focused pick's lines, higher up, without dots
    tween("hn", place == Place::Banner ? 1 : 0); // the banner's lines, lower down
    tween("ba", place == Place::Picks || place == Place::Lower ? 1 : 0);
    std::vector<home::RowLook> looks;
    for (size_t r = 0; r < home.rows.size(); ++r) {
        home::RowLook look;
        look.up = titlesUp ? 1 : 0;
        look.risen = risen ? 1 : 0;
        look.grow = risen && r == home.row ? 1 : 0;
        look.names = home::isApps(home.rows[r]) && risen && r == home.row ? 1 : 0;
        look.captions = titles || home::isAppCards(home.rows[r]) ? look.grow : 0;
        tween(home::id("rg", r), look.grow);
        tween(home::id("rn", r), look.names);
        looks.push_back(look);
    }
    const std::vector<float> tops = home::rowTops(home.rows, looks);
    const int firstScreenRows = home.tabs[home.tab].key == "home" ? 1 : 0;
    const size_t first = defaultRow(home);
    const int contentRow = static_cast<int>(home.row) - static_cast<int>(first);
    double stack = 0;
    if (place == Place::Banner)
        stack = kit::layout::bannerDrop;
    else if (place == Place::Picks)
        stack = kit::layout::picksDrop;
    else if (place == Place::Lower)
        // The Apps tab's search bar has no title: it rises to where a risen row's items stand.
        stack = (home::isSearch(home.rows[home.row]) ? home::risenItemsTop() : kit::layout::rowsRisenTop) -
                tops[home.row] - (titles || apps ? kit::layout::featureTop - kit::layout::rowsTop : 0);
    else if (home.row < home.rows.size() && !home.navigation && !home.heroFocus && contentRow > firstScreenRows &&
             first < tops.size())
        stack = tops[first] - tops[home.row];
    // A row of the first screen that reaches past the bottom (Your apps, with the focused app's
    // name under it) rises just enough to show it whole.
    if (place == Place::Other && !home.navigation && !home.heroFocus && !titles && !apps &&
        contentRow >= 0 && contentRow <= firstScreenRows && home.row < home.rows.size()) {
        const CardRow &row = home.rows[home.row];
        const float bottom = tops[home.row] + home::labelArea(looks[home.row]) +
                             (home::isApps(row) ? kit::layout::appNameTop + kit::type::labelM.line
                                                : home::itemHeight(row)) +
                             kit::layout::rowLabelGap;
        if (bottom > kit::canvas::height)
            stack = std::min(stack, static_cast<double>(kit::canvas::height - bottom));
    }
    if (jump)
        motion_.set("sy", stack);
    else
        motion_.spring("sy", stack, springParams(kit::motion::spring::page));
    homeRetargetPill(jump);
}

void Application::homeKeyMoved(Key key) {
    auto &home = state_.home;
    flow_.heroUntouched = false;
    flow_.barPagePending = false;
    if (home.navigation && home.nav < home.tabs.size() && home.nav != flow_.shown &&
        (key == Key::Left || key == Key::Right)) {
        flow_.tabPending = true;
        flow_.commitAt = now_ + kTabSettleMs;
    } else if (home.navigation && home.nav == home.tabs.size() + 2 &&
               (key == Key::Left || key == Key::Right)) {
        // Search is a tab of Google TV's bar: resting on it opens its page.
        flow_.tabPending = false;
        flow_.barPagePending = true;
        flow_.commitAt = now_ + kTabSettleMs;
    } else if (key == Key::Left || key == Key::Right || key == Key::Up)
        flow_.tabPending = false;
    homeFocus();
}

// ---- Arranging the kinds ----------------------------------------------------------------------

// While a row is lifted, Up and Down move it past its neighbours (the focus and the row's scroll
// travel with it), OK saves the kinds' order, Left and Right do nothing.
void Application::homeArrange(Key key) {
    auto &home = state_.home;
    if (key == Key::Up || key == Key::Down) {
        if ((key == Key::Up && home.row == 0) || (key == Key::Down && home.row + 1 >= home.rows.size()))
            return;
        size_t to = key == Key::Up ? home.row - 1 : home.row + 1;
        if (home.rows[to].key != home.rows[home.row].key) // the rows of apps move among themselves
            return;
        std::swap(home.rows[home.row], home.rows[to]);
        double fromScroll = motion_.value(home::id("rx", home.row)),
               toScroll = motion_.value(home::id("rx", to));
        motion_.set(home::id("rx", home.row), toScroll);
        motion_.set(home::id("rx", to), fromScroll);
        home.row = to;
        // The ring stays on the same card at its new index; the stack slides one pitch.
        double stack = motion_.value("sy");
        homeFocus(true);
        motion_.set("sy", stack);
        homeFocus();
        return;
    }
    if (key == Key::Ok) {
        // The server keeps every kind: one with no app has no row here and holds its place, the
        // rows on screen take the order they were left in.
        std::vector<std::string> shownKinds;
        for (const CardRow &row : home.rows)
            if (row.key == "appkind")
                shownKinds.push_back(row.group);
        Json kinds = Json::array();
        size_t next = 0;
        for (const Json &kind : state_.catalogue["kinds"].items()) {
            const std::string key = kind["key"].string();
            const bool shown = std::find(shownKinds.begin(), shownKinds.end(), key) != shownKinds.end();
            kinds.add(Json::text(shown && next < shownKinds.size() ? shownKinds[next++] : key));
        }
        if (!kinds.size())
            for (const std::string &key : shownKinds)
                kinds.add(Json::text(key));
        home.arranging = false;
        execute(post(Operation::SetHomeLayout, fields({{"kinds", kinds}})));
        homeFocus();
    }
}
// Back while arranging: the rows return to the saved order, the focus with the lifted row.
void Application::homeArrangeCancel() {
    auto &home = state_.home;
    std::string lifted = home.row < home.rows.size() ? home.rows[home.row].group : "";
    const size_t column = home.column; // the rebuild clamps it against the row now at this index
    std::map<std::string, double> scrolls;
    for (size_t i = 0; i < home.rows.size(); ++i)
        scrolls[home.rows[i].group] = motion_.value(home::id("rx", i));
    home.arranging = false;
    rebuildHome();
    for (size_t i = 0; i < home.rows.size(); ++i) {
        if (home.rows[i].group == lifted) {
            home.row = i;
            home.column = clampIndex(column, home.rows[i].cards.size());
        }
        if (scrolls.count(home.rows[i].group))
            motion_.set(home::id("rx", i), scrolls[home.rows[i].group]);
    }
    homeFocus();
}

// ---- Tabs and pages ---------------------------------------------------------------------------

void Application::openBarPage() {
    auto &home = state_.home;
    flow_.barPagePending = false;
    const size_t stop = home.nav;
    if (stop == home.tabs.size() + 2) {
        route("search");
    } else if (stop < home.tabs.size()) {
        route("home");
        home.tab = stop;
        rebuildHome();
        homeStart();
        homeData();
    } else
        return;
    barFocus_ = screen_ != nullptr;
    home.navigation = true;
    home.nav = stop;
    homeFocus();
}

// Starts the old page leaving; its rows are swapped for the new tab's once it has.
void Application::homeSelectTab(size_t tab) {
    auto &home = state_.home;
    flow_.tabPending = false;
    if (tab >= home.tabs.size() || (tab == flow_.shown && flow_.pendingTab < 0))
        return;
    flow_.pendingTab = static_cast<int>(tab);
    flow_.swapAt = now_ + kPageSwapMs;
    // The Apps page's icons (a larger size than Home's) are asked for while the old page leaves,
    // so its entrance does not wait for them.
    if (home.tabs[tab].key == "apps")
        iconsReady(pictures_, homeRows("apps", state_.catalogue, state_.discover, state_.guide), now_);
    motion_.tween("pgx", 1, ms::pageOut, easing(ease::out));
    // Google TV's tab change: the page slides away from the tab chosen as it fades.
    flow_.pageDirection = tab > flow_.shown ? 1 : -1;
    motion_.tween("pgs", -flow_.pageDirection * kit::layout::pageSlide, ms::pageOut, easing(ease::out));
    motion_.tween("wall", home.tabs[tab].key == "home" ? 1 : 0, ms::page, easing(ease::out));
    dirty_ = true;
}

void Application::homeUpdate() {
    auto &home = state_.home;
    if (home.tabs.empty())
        return;
    if (!flow_.entered)
        homeData();
    if (flow_.tabPending && due(now_, flow_.commitAt))
        homeSelectTab(home.nav);
    if (flow_.barPagePending && due(now_, flow_.commitAt))
        openBarPage();
    if (flow_.pendingTab >= 0 && due(now_, flow_.swapAt)) {
        home.tab = static_cast<size_t>(flow_.pendingTab);
        flow_.pendingTab = -1;
        flow_.shown = home.tab;
        home.row = home.column = 0;
        home.heroFocus = false;
        flow_.heroUntouched = home.tabs[home.tab].key == "home";
        home.hero = Card();
        home.art.shownId.clear();
        // The lines go with the page: a tab with nothing to focus (Live without channels) would
        // otherwise keep the banner's.
        home.art.text = HeroText();
        flow_.wantId.clear();
        flow_.heroWaiting = false;
        for (const char *prefix : {"f:", "rx", "rd", "E"})
            motion_.erasePrefix(prefix);
        for (const char *name : {"sy", "pgx"})
            motion_.set(name, 0);
        // and the new one comes in from the other side.
        motion_.set("pgs", flow_.pageDirection * kit::layout::pageSlide);
        motion_.tween("pgs", 0, ms::page, easing(ease::decelerate));
        flow_.entered = flow_.waiting = false;
        flow_.focusId.clear();
        rebuildHome();
        home.row = defaultRow(home);
        homeFocus(true);
    }
    // A refresh, a mutation while editing or a jump between rows can leave the focus on a card the row
    // no longer has; it comes back to the nearest one rather than resting on nothing.
    if (!home.rows.empty() && !home.navigation && !home.editing && !home.arranging) {
        const size_t row = clampIndex(home.row, home.rows.size());
        const size_t column = clampIndex(home.column, home.rows[row].cards.size());
        if (row != home.row || column != home.column) {
            home.row = row;
            home.column = column;
            homeFocus();
        }
    }
    // Hero: what the focused card, or the spotlight, asks for; the change waits for the art.
    bool heroTab = home.tabs[home.tab].key != "apps" && flow_.pendingTab < 0;
    for (const char *name : {"hp", "hk:0", "hk:1"})
        if (!heroTab && motion_.moving(name))
            motion_.set(name, motion_.value(name));
    if (heroTab && flow_.entered && !home.rows.empty()) {
        const Card *focus = nullptr;
        if (!home.navigation && !home.heroFocus && home.row < home.rows.size() &&
            home.column < home.rows[home.row].cards.size())
            focus = &home.rows[home.row].cards[home.column];
        // Google TV's banner keeps its own titles while the focus is on it, the bar, the top picks
        // or Your apps, and moves on by itself while it is in view; further down its art, dimmed
        // under the rows, follows the focused title.
        // Without titles of its own (an older server) the banner shows the top picks and follows
        // the focused title, as it did.
        const bool picks = !home.spotlight.empty();
        const size_t firstScreenEnd = defaultRow(home) + 2;
        const bool appFocus = focus && focus->kind == "app" && picks;
        bool onSpotlight = home.spotlightOwn
                               ? home.navigation || home.heroFocus || !focus
                               : home.navigation || home.row == rowIndex(home.rows, "trending") ||
                                     appFocus;
        (void)firstScreenEnd;
        Card want;
        if (home.spotlightOwn && appFocus && !flow_.wantId.empty())
            want = flow_.want; // Google TV keeps the last art, dimmed, behind Your apps
        else if (focus && !(home.spotlightOwn && onSpotlight) && !appFocus)
            want = *focus;
        else if (picks)
            want = home.spotlight[home.spotIndex];
        else if (!home.hero.title.empty())
            want = home.hero;
        else if (!home.rows[0].cards.empty())
            want = home.rows[0].cards[0];
        std::string wantId = want.kind + ":" + want.id, shownId = home.art.shownId;
        if (wantId != flow_.wantId) {
            flow_.wantId = wantId;
            flow_.want = want;
            flow_.wantSince = now_;
            flow_.heroWaiting = true;
        }
        if (wantId == shownId)
            flow_.heroWaiting = false;
        else if (flow_.heroWaiting && (shownId.empty() || due(now_, flow_.wantSince + kHeroSettleMs))) {
            std::string art = artOf(flow_.want);
            // The title's logo is waited for with its picture, so the name does not change to it
            // once on screen.
            const bool logo = logoSettled(flow_.want);
            bool ready =
                art.empty() || !pictures_ ||
                (logo && pictures_->view(art, kHeroWidth, kHeroHeight, ImageFit::Cover, now_).texture != 0) ||
                due(now_, flow_.wantSince + kHeroSettleMs + kHeroWaitMs);
            if (ready) {
                flow_.heroWaiting = false;
                homeRetargetHero(flow_.want, shownId.empty());
            }
        }
        // The slideshow: with nobody pressing keys the hero moves on when its progress is full, dimmed
        // or not, as Google TV's does until its ambient mode.
        if (home.spotlight.size() > 1 && onSpotlight && !motion_.moving("hp") &&
            !shownId.empty() && motion_.value("sy") > kBannerCoveredY) {
            if (due(now_, flow_.lastKeyAt + static_cast<uint32_t>(ms::heroAdvance)))
                home.spotIndex = (home.spotIndex + 1) % home.spotlight.size();
            motion_.play("hp", Tween(0, 1, ms::heroAdvance, Easing::linear()));
        }
    }
    if (flow_.textPending && due(now_, flow_.textSwapAt))
        homeSwapText();
    // The Ken Burns of a layer that has been replaced stops once it is invisible.
    for (int layer = 0; layer < 2; ++layer)
        if (motion_.value(home::id("ha", layer)) <= 0 && motion_.moving(home::id("hk", layer)))
            motion_.set(home::id("hk", layer), 0);
}

// ---- Hero -------------------------------------------------------------------------------------

// Whether the page has a banner to focus: Home's, once it has titles of its own.
bool Application::homeHeroOffered() const {
    const auto &home = state_.home;
    return home.spotlightOwn && !home.spotlight.empty() && !home.tabs.empty() &&
           home.tabs[home.tab].key == "home" && flow_.pendingTab < 0;
}
// The focused banner: Left and Right show the title before or after it, OK opens it, Up reaches the
// bar and Down the first row.
void Application::homeHeroKey(Key key) {
    auto &home = state_.home;
    const size_t count = home.spotlight.size();
    if (key == Key::Up) {
        home.heroFocus = false;
        home.navigation = true;
        home.nav = home.tab;
        return;
    }
    if (key == Key::Down || !count) {
        home.heroFocus = home.rows.empty() && count;
        homeEnterRow(defaultRow(home));
        return;
    }
    const size_t index = std::min(home.spotIndex, count - 1);
    if (key == Key::Left || key == Key::Right)
        home.spotIndex = (index + (key == Key::Right ? 1 : count - 1)) % count;
    if (key == Key::Ok) {
        const Card &card = home.spotlight[index];
        if (card.kind == "title")
            openDetails(card.data);
        else
            execute(card.command);
    }
}

void Application::homeEnterRow(size_t row) {
    auto &home = state_.home;
    auto identity = [](const CardRow &of) { return of.key + "|" + of.group + "|" + of.title; };
    if (home.row < home.rows.size() && home.column < home.rows[home.row].cards.size())
        home.rowFocus[identity(home.rows[home.row])] = home.rows[home.row].cards[home.column].id;
    home.row = std::min(row, home.rows.empty() ? 0 : home.rows.size() - 1);
    home.column = 0;
    if (home.row >= home.rows.size())
        return;
    auto remembered = home.rowFocus.find(identity(home.rows[home.row]));
    const auto &cards = home.rows[home.row].cards;
    if (remembered != home.rowFocus.end())
        for (size_t i = 0; i < cards.size(); ++i)
            if (cards[i].id == remembered->second)
                home.column = i;
}

void Application::homeRetargetHero(const Card &card, bool instant) {
    auto &home = state_.home;
    auto &art = home.art;
    std::string path = artOf(card);
    float artAlpha = 1;
    // An app or input has no picture: the pick on show stays behind it, dimmed to a wallpaper.
    if (path.empty() && !home.spotlight.empty()) {
        path = artOf(home.spotlight[std::min(home.spotIndex, home.spotlight.size() - 1)]);
        artAlpha = kWallpaperAlpha;
    }
    art.shownId = card.kind + ":" + card.id;
    home.hero = card;
    // The slideshow resumes from whichever card the hero now shows.
    for (size_t i = 0; i < home.spotlight.size(); ++i)
        if (home.spotlight[i].id == card.id && home.spotlight[i].kind == card.kind)
            home.spotIndex = i;
    int outgoing = art.newest, incoming = 1 - outgoing;
    bool hadArt = !art.image[outgoing].empty();
    bool samePicture = !path.empty() && path == art.image[outgoing];
    if (!path.empty() || hadArt) {
        art.newest = incoming;
        art.image[incoming] = path;
        art.artAlpha[incoming] = artAlpha;
        const int tone = pictures_ && !path.empty()
                             ? pictures_->view(path, kHeroWidth, kHeroHeight, ImageFit::Cover, now_).tone
                             : -1;
        art.wallTint = tone >= 0 && isTitle(card) ? scrimOf(tone) : tintOf(card);
        art.tint[incoming] = art.wallTint;
        std::string in = home::id("ha", incoming), out = home::id("ha", outgoing);
        if (instant || !hadArt) {
            motion_.set(in, 1);
            motion_.set(out, 0);
            art.image[outgoing].clear();
        } else {
            motion_.play(in, Tween(0, 1, ms::hero, easing(ease::inOut)));
            // The old layer stays opaque underneath until the new one covers it. The same
            // picture dimming to a wallpaper, or back, fades across instead, so its brightness
            // never jumps.
            motion_.play(out, samePicture ? Tween(1, 0, ms::hero, easing(ease::inOut))
                                          : Tween(1, 0, 60, Easing::linear(), ms::hero));
        }
        std::string push = home::id("hk", incoming);
        if (samePicture && !instant) {
            // The push carries on from where the picture was.
            double at = motion_.value(home::id("hk", outgoing));
            motion_.set(push, at);
            motion_.play(push, Tween(static_cast<float>(at), 1,
                                     static_cast<float>(ms::kenburns * (1 - at)), Easing::linear()));
        } else
            motion_.play(push, Tween(0, 1, ms::kenburns, Easing::linear()));
    } else {
        art.wallTint = tintOf(card);
    }
    motion_.play("hp", Tween(0, 1, ms::heroAdvance, Easing::linear()));
    // The dots describe the top picks: they fade while the hero shows an app or an input.
    if (instant)
        motion_.set("dots", isTitle(card) ? 1 : 0);
    else
        motion_.tween("dots", isTitle(card) ? 1 : 0, ms::fast, easing(ease::out));
    for (size_t i = 0; i < home::kSpotlightMax; ++i) {
        bool current = i < home.spotlight.size() && home.spotlight[i].id == card.id &&
                       home.spotlight[i].kind == card.kind;
        if (instant)
            motion_.set(home::id("dot", i), current ? 1 : 0);
        else
            motion_.tween(home::id("dot", i), current ? 1 : 0, ms::fast, easing(ease::out));
    }
    HeroText next = heroLinesOf(card);
    if (instant || art.text == HeroText() || art.text == next) {
        art.text = next;
        if (instant)
            for (int i = 0; i < 3; ++i)
                motion_.play(home::id("hx", i),
                             Tween(1, 0, ms::heroText, easing(ease::out),
                                   stagger::homeBase + stagger::heroText[i]));
        flow_.textPending = false;
        return;
    }
    // The old lines leave first (up and out), then the new ones rise in one after another.
    for (int i = 0; i < 3; ++i)
        motion_.play(home::id("hx", i), Tween(0, -1, ms::heroTextOut, easing(ease::out)));
    flow_.textSwapAt = now_ + 170;
    flow_.textPending = true;
}
bool Application::logoSettled(const Card &card) {
    if (card.logo.empty() || !pictures_)
        return true;
    const ImageView view = pictures_->view(card.logo, pictureTexels(kit::layout::heroLogoW),
                                           pictureTexels(kit::layout::previewLogoH),
                                           ImageFit::Contain, now_);
    return view.texture != 0 || view.failed;
}
HeroText Application::heroLinesOf(const Card &card) {
    HeroText text = linesOf(state_, card);
    // Google TV draws a title's logo in its preview; the banner's own titles are lettered.
    if (card.logo.empty() || !pictures_ || !isTitle(card) || !text.preview)
        return text;
    const ImageView view = pictures_->view(card.logo, pictureTexels(kit::layout::heroLogoW),
                                           pictureTexels(kit::layout::previewLogoH),
                                           ImageFit::Contain, now_);
    // A dark logo (black lettering) would not read over the dimmed art: the name stays.
    const int luma = view.tone < 0 ? 0
                                   : (((view.tone >> 16) & 0xff) * 54 + ((view.tone >> 8) & 0xff) * 183 +
                                      (view.tone & 0xff) * 19) >> 8;
    if (view.texture && luma >= 90)
        text.logo = card.logo;
    return text;
}
void Application::homeSwapText() {
    auto &home = state_.home;
    flow_.textPending = false;
    home.art.text = heroLinesOf(home.hero);
    for (int i = 0; i < 3; ++i)
        motion_.play(home::id("hx", i),
                     Tween(1, 0, ms::heroText, easing(ease::out), stagger::heroText[i]));
}

// ---- Idle, beat -------------------------------------------------------------------------------

// The server's heartbeat, as the old Home page sent it: the reply says whether the build, the
// layout or the wallpapers changed, and three silent beats raise the "not answering" banner.
void Application::homeBeat(const Response *reply) {
    if (!reply) {
        if (!active_ || state_.screen == Screen::Splash || !state_.catalogue.valid() ||
            !due(now_, flow_.beatAt))
            return;
        flow_.beatAt = now_ + kBeatMs;
        services_.write("beat", Operation::HomeBeat, Json::object());
        return;
    }
    Json data = reply->json();
    if (!reply->ok() || !data["ok"].boolean()) {
        if (++flow_.beatFails >= 3 && !state_.serverSilent) {
            state_.serverSilent = true;
            motion_.tween("fault", 1, ms::fast, easing(ease::out));
        }
        return;
    }
    flow_.beatFails = 0;
    bool changed = false;
    if (state_.serverSilent) {
        state_.serverSilent = false;
        motion_.tween("fault", 0, ms::fast, easing(ease::out));
        changed = true;
    }
    // A new release of the server or a saved layout: the data is read again.
    std::string build = data["build"].string(), layout = data["layoutVersion"].string();
    if (!build.empty()) {
        changed = changed || (!flow_.build.empty() && build != flow_.build);
        flow_.build = build;
    }
    if (!layout.empty()) {
        changed = changed || (!flow_.layoutVersion.empty() && layout != flow_.layoutVersion);
        flow_.layoutVersion = layout;
    }
    Json wallpapers = data["wallpapers"];
    if (wallpapers.isObject() && state_.catalogue.isObject() &&
        wallpapers.dump() != state_.catalogue["wallpapers"].dump()) {
        state_.catalogue.set("wallpapers", wallpapers);
        syncWallpaper();
    }
    if (changed)
        refresh();
    dirty_ = true;
}
} // namespace rootedtv
