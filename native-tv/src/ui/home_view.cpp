// Home and Apps drawn on the kit from the controller's state and the motion engine's values.
#include "domain/say.hpp"
#include "ui/application.hpp"
#include "ui/home_layout.hpp"
#include "ui/kit/entry.hpp"
#include "ui/kit/overlay.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace nativetv {
namespace {
namespace layout = kit::layout;

// How far up the bar goes as it leaves, and over how much of the rows' scroll.
constexpr float kBarRise = 48, kBarLeaveSpan = 160;
// A row's items enter one after another; a page leaving takes them all with it.
kit::Enter enterOf(const Motion &motion, const std::string &id) {
    double hidden = std::max(motion.value(id), motion.value("pgx"));
    return {static_cast<float>(1 - hidden), static_cast<float>(28 * hidden)};
}
// Hero text: positive is rising in from below (18 px), negative is leaving upward (8 px).
kit::Enter textEnter(const Motion &motion, int index) {
    double value = motion.value(home::id("hx", index)), leaving = motion.value("pgx");
    double alpha = (value >= 0 ? 1 - value : 1 + value) * (1 - leaving);
    return {static_cast<float>(std::max(0.0, alpha)),
            static_cast<float>(value >= 0 ? 18 * value : 8 * value)};
}
std::string clockText(const ViewState &state) {
    char text[16];
    if (state.catalogue["layout"]["clockFormat"].string() == "12h")
        std::snprintf(text, sizeof(text), "%d:%02d", state.hour % 12 ? state.hour % 12 : 12,
                      state.minute);
    else
        std::snprintf(text, sizeof(text), "%02d:%02d", state.hour, state.minute);
    return text;
}
std::string initialOf(const ViewState &state) {
    std::string name;
    for (const auto &person : state.catalogue["profiles"].items())
        if (person["active"].boolean())
            name = person["name"].string();
    if (name.empty())
        return "M";
    size_t end = 1;
    while (end < name.size() && (static_cast<unsigned char>(name[end]) & 0xc0) == 0x80)
        ++end;
    return name.substr(0, end);
}

void wallpaper(Scene &scene, const HomeState &home) {
    const Rect stage = {0, 0, kit::canvas::width, kit::canvas::height};
    if (home.wallpaperTransition) {
        if (!home.wallpaperFrom.empty())
            scene.image(stage, home.wallpaperFrom, 0, false,
                        {1, 1, 1, home.wallpaperFromOpacity * (1 - home.wallpaperProgress)});
        if (!home.wallpaperTarget.empty())
            scene.image(stage, home.wallpaperTarget, 0, false,
                        {1, 1, 1, home.wallpaperTargetOpacity * home.wallpaperProgress});
    } else if (!home.wallpaperShown.empty()) {
        scene.image(stage, home.wallpaperShown, 0, false, {1, 1, 1, home.wallpaperShownOpacity});
        if (!home.wallpaperTarget.empty() && home.wallpaperTarget != home.wallpaperShown)
            scene.image(stage, home.wallpaperTarget, 0, false, {1, 1, 1, 0});
    } else if (!home.wallpaperTarget.empty()) {
        // A zero-alpha node keeps the picture requested without showing an empty frame.
        scene.image(stage, home.wallpaperTarget, 0, false, {1, 1, 1, 0});
    }
}

kit::Hero heroOf(const Frame &frame) {
    const HomeState &home = frame.state.home;
    const Motion &motion = frame.motion;
    kit::Hero hero;
    for (int i = 0; i < 2; ++i) {
        // The newest layer is last, so it covers the one it replaces.
        int layer = i == 0 ? 1 - home.art.newest : home.art.newest;
        hero.slot[i].image = home.art.image[layer];
        hero.slot[i].opacity = static_cast<float>(motion.value(home::id("ha", layer)));
        hero.slot[i].kenBurns = static_cast<float>(motion.value(home::id("hk", layer)));
        hero.slot[i].artAlpha = home.art.artAlpha[layer];
        hero.slot[i].tint = home.art.tint[layer];
    }
    hero.wallAlpha = static_cast<float>(motion.value("wall"));
    hero.stackY = static_cast<float>(motion.value("sy"));
    hero.source = home.art.text.source;
    hero.title = home.art.text.title;
    hero.facts = home.art.text.facts;
    hero.description = home.art.text.description;
    hero.image = home.art.text.image;
    hero.hint = home.art.text.hint;
    hero.tag = home.art.text.tag;
    hero.wordmark = home.art.text.wordmark;
    hero.logo = home.art.text.logo;
    hero.more = home.art.text.more;
    hero.button = home.art.text.button;
    hero.expand = static_cast<float>(motion.value("he"));
    hero.buttonAlpha = static_cast<float>(motion.value("hb"));
    hero.textAlpha = static_cast<float>(motion.value("ht"));
    hero.rowsFocus = static_cast<float>(motion.value("ba"));
    hero.drop = kit::layout::bannerTextDrop * static_cast<float>(motion.value("hn"));
    hero.preview = home.art.text.preview;
    for (int i = 0; i < 3; ++i)
        hero.text[i] = textEnter(motion, i);
    return hero;
}

} // namespace

// pageTab: the bar's tab a registered screen belongs to; -1 keeps the pill off every tab. Home
// passes its own selected tab.
void drawTopBar(kit::Paint paint, const Frame &frame, int pageTab, float away) {
    const ViewState &state = frame.state;
    const HomeState &home = state.home;
    const Motion &motion = frame.motion;
    kit::TopBar bar;
    bar.avatarInitial = initialOf(state);
    for (const auto &tab : home.tabs)
        bar.tabs.push_back(tab.title);
    bar.selected = home.navigation && home.nav < home.tabs.size() ? static_cast<int>(home.nav)
                                                                  : pageTab;
    const bool searchPage = state.screen == Screen::Kit && state.kitComponent == "search";
    // The pill sits on search while it has the focus or its page shows, as on a tab.
    const bool onTab = home.navigation && home.nav < home.tabs.size();
    bar.searchSelected = (home.navigation && home.nav == home.tabs.size() + 2) || (searchPage && !onTab);
    bar.pillAlpha = bar.selected < 0 && !bar.searchSelected ? 0.0f : 1.0f;
    bar.settingsSelected = pageTab < 0 && !home.navigation && !searchPage;
    bar.pillLeft = static_cast<float>(motion.value("pill:0"));
    bar.pillRight = static_cast<float>(motion.value("pill:1"));
    bar.pillHot = static_cast<float>(motion.value("pill:hot"));
    bar.avatarFocus = static_cast<float>(motion.value("f:av"));
    bar.settingsFocus = static_cast<float>(motion.value("f:gr"));
    bar.search = state.discover["titlesEnabled"].boolean();
    bar.saverFocus = static_cast<float>(motion.value("f:sv"));
    bar.clock = clockText(state);
    bar.enter = {1 - away, -kBarRise * away};
    kit::topBar(paint, bar);
    if (state.catalogue["layout"]["picture"].boolean() && !home.editing)
        paint.glyphs.text(paint.scene, {800, 136, 1040, kit::type::labelM.line},
                          pictureLabel(home.picture), kit::type::labelM, kit::color::onSurface3,
                          Node::Align::Right);
}

namespace {
// The rows of cards; `top` moves them all, so the Apps page can start its rows under its head.
void rows(kit::Paint paint, const Frame &frame, float top = 0) {
    const ViewState &state = frame.state;
    const HomeState &home = state.home;
    const Motion &motion = frame.motion;
    const float stack = static_cast<float>(motion.value("sy")) + top;
    const bool appNames = state.catalogue["layout"]["appNames"].boolean(true);
    const float risen = static_cast<float>(motion.value("rs")), up = static_cast<float>(motion.value("ru"));
    const bool titles = titlesTab(home.tabs[home.tab].key);
    std::vector<home::RowLook> looks;
    for (size_t ri = 0; ri < home.rows.size(); ++ri) {
        home::RowLook look;
        look.up = up;
        look.risen = risen;
        look.grow = static_cast<float>(motion.value(home::id("rg", ri)));
        look.names = static_cast<float>(motion.value(home::id("rn", ri)));
        look.captions = titles || home::isAppCards(home.rows[ri]) ? look.grow : 0;
        looks.push_back(look);
    }
    const std::vector<float> tops = home::rowTops(home.rows, looks);
    for (size_t ri = 0; ri < home.rows.size(); ++ri) {
        const CardRow &row = home.rows[ri];
        const home::RowLook &look = looks[ri];
        const float labelY = tops[ri] + stack, itemsY = labelY + home::itemsOffset(row, look);
        // Rows above the screen or below it are not drawn at all; the one just above the focused
        // row peeks in at the top, as on Google TV.
        if (labelY > kit::canvas::height + 40 || itemsY + home::itemHeight(row) + 40 < 0)
            continue;
        if (home.arranging && ri == home.row) {
            // The lifted row sits on a raised slab while it is being moved. The slab is as wide as
            // the row, with room all round for the focused name, which is wider than its circle.
            constexpr float kPad = 40;
            const float left = kit::canvas::safeX - kPad;
            float right = kit::canvas::width - kit::canvas::safeX + kPad / 2;
            if (!row.cards.empty()) {
                const Rect last = home::item(row, itemsY, row.cards.size() - 1);
                right = std::min(right, last.x + last.w + kPad +
                                            static_cast<float>(motion.value(home::id("rx", ri))));
                right = std::max(right, left + 480);
            }
            const float height = home::labelArea(look) - 12 + home::itemHeight(row) + 12;
            Node &slab = paint.scene.shape({left, labelY - 12, right - left, height + 36},
                                           kit::color::surface2, kit::radius::lg);
            slab.ring = {1, 0, kit::alpha::outline};
            slab.shadow = {0, kit::elevation::raised.dy, kit::elevation::raised.blur, 0,
                           {0, 0, 0, .5f}};
        }
        kit::Row label;
        label.index = static_cast<int>(ri);
        label.placed = true;
        label.y = labelY;
        label.px = home::labelPx(look);
        label.grow = look.grow;
        label.label = home.editing && ri == home.row && home::isApps(row) ? say::msg("tv.home.movingApps", "Moving apps") : row.title;
        label.dim = static_cast<float>(motion.value(home::id("rd", ri)));
        label.enter = enterOf(motion, home::id("ER", ri));
        // The next row's name is not shown cut by the screen's edge: it fades in as it rises.
        label.enter.alpha *=
            kit::focus::clamp01((kit::canvas::height - 40 - labelY) / 48);
        if (!row.title.empty()) // the featured cards and the Apps tab's search bar have none
            kit::row(paint, label);
        const float scroll = static_cast<float>(motion.value(home::id("rx", ri)));
        for (size_t ci = 0; ci < row.cards.size(); ++ci) {
            Rect rest = home::item(row, itemsY, ci);
            rest.x += scroll;
            if (rest.x + rest.w < -140 || rest.x > kit::canvas::width + 140)
                continue;
            const Card &card = row.cards[ci];
            std::string id = home::hit(ri, ci);
            float focus = static_cast<float>(motion.value(home::id("f", ri, ci)));
            kit::Enter enter = enterOf(motion, home::id("EC", ri, ci));
            if (row.key == "appfeature" || home::isAppCards(row)) {
                // Google TV's Apps tab: its banners, and the wide cards of its rows of apps with the
                // focused one's name under it.
                const bool banner = row.key == "appfeature";
                kit::AppCard app;
                app.rest = rest;
                app.id = id;
                app.name = card.title;
                app.image = card.image;
                app.icon = card.id == "lg-home" ? "home" : "apps";
                app.color = home::appColor(card);
                app.radius = banner ? layout::featureR : layout::cardR;
                app.zoom = banner ? kit::motion::scale::card : kit::motion::scale::appCard;
                app.focus = focus;
                app.caption = banner ? 0 : std::min(look.captions, focus);
                app.enter = enter;
                kit::appCard(paint, app);
            } else if (home::isSearch(row)) {
                kit::SearchPill pill;
                pill.rest = rest;
                pill.id = id;
                pill.label = card.title;
                pill.focus = focus;
                pill.enter = enter;
                kit::searchPill(paint, pill);
            } else if (home::isChips(row)) {
                kit::CategoryChip chip;
                chip.rest = rest;
                chip.id = id;
                chip.label = card.title;
                chip.focus = focus;
                chip.enter = enter;
                kit::categoryChip(paint, chip);
            } else if (home::isFeature(row)) {
                kit::FeatureCard feature;
                feature.rest = rest;
                feature.id = id;
                feature.image = card.image;
                feature.title = card.title;
                feature.line = card.subtitle;
                const std::string service = card.data["service"].string();
                if (service == "netflix" || service == "prime" || service == "disney")
                    feature.wordmark = "wordmark-" + service;
                feature.focus = focus;
                feature.enter = enter;
                kit::featureCard(paint, feature);
            } else if (row.key == "app") {
                kit::AppCircle circle;
                circle.rest = rest;
                circle.id = id;
                circle.name = appNames ? card.title : "";
                circle.icon = card.id == "lg-home" ? "home" : "apps";
                circle.image = card.image;
                circle.color = home::appColor(card);
                circle.names = look.names;
                circle.focus = focus;
                circle.enter = enter;
                if (card.hidden) // hidden from Home: listed only while the apps are edited
                    circle.enter.alpha *= .4f;
                if (home.arranging && ri != home.row) // the rows behind the lifted one
                    circle.enter.alpha *= .55f;
                kit::appCircle(paint, circle);
            } else if (row.key == "dev" || row.key == "ch") {
                kit::InputTile tile;
                tile.rest = rest;
                tile.id = id;
                bool device = row.key == "dev";
                bool unused = device && card.data["unused"].boolean();
                // The server's port can arrive as 1.0; the tile says "HDMI 1".
                const Json port = card.data["port"];
                const std::string portText =
                    port.isNumber() ? compactNumber(port.number()) : port.string("");
                tile.name = device ? "HDMI " + portText : card.title;
                if (device && portText.empty())
                    tile.name = card.title;
                tile.sub = device ? (unused ? say::msg("tv.inputs.notConnected", "Not connected") : card.title)
                                  : card.subtitle;
                if (tile.sub == tile.name)
                    tile.sub.clear();
                tile.icon = device ? "hdmi" : "tv";
                tile.unused = unused;
                tile.focus = focus;
                tile.enter = enter;
                kit::inputTile(paint, tile);
            } else {
                kit::PosterCard poster;
                poster.rest = rest;
                poster.id = id;
                poster.image = card.image;
                // Key art carries the title, so the card prints none, as on Google TV.
                if (!card.titled) {
                    poster.title = card.title;
                    poster.subtitle = card.subtitle;
                }
                if (card.data["progress"].isNumber())
                    poster.progress = static_cast<float>(card.data["progress"].number());
                if (row.key == "continue") {
                    // Google TV's Continue watching: what OK does over the art, the title and
                    // its service under the card.
                    poster.action = say::msg("tv.home.resume", "RESUME");
                    poster.caption = card.title;
                    poster.captionLine = continueLine(card);
                    poster.title.clear();
                } else if (look.captions > 0) {
                    // Google TV's focused row on its Films and Shows tabs: where each title plays
                    // under its card, and the focused one's title over that line.
                    if (focus > .5f)
                        poster.caption = card.title;
                    poster.captionLine = titleCaption(card);
                    poster.captionAlpha = look.captions;
                }
                poster.focus = focus;
                poster.enter = enter;
                kit::card(paint, poster);
            }
        }
    }
}

// The hint keys on a band of ink, so a row that reaches the bottom passes under them.
void hintBand(kit::Paint paint, const std::vector<kit::Hint> &hints) {
    Node &band = paint.scene.shape({0, 900, kit::canvas::width, 180}, kit::color::ink);
    band.gradient.angle = 0;
    band.gradient.stops = 3;
    band.gradient.color[0] = kit::color::ink;
    band.gradient.at[0] = 0;
    band.gradient.color[1] = kit::color::ink;
    band.gradient.at[1] = .5f;
    band.gradient.color[2] = kit::focus::withAlpha(kit::color::ink, 0);
    band.gradient.at[2] = 1;
    kit::hintBar(paint, {kit::canvas::safeX, 998, 0, 30}, hints);
}

// The hint band while the Apps tab's rows of apps are moved among themselves.
void arrangeHints(kit::Paint paint, const Frame &frame) {
    if (frame.state.home.arranging)
        hintBand(paint, {{"", say::msg("tv.hint.move", "Move")}, {"OK", say::msg("tv.hint.save", "Save")},
                         {say::msg("tv.hint.back", "Back"), say::msg("tv.hint.cancel", "Cancel")}});
}

void banner(kit::Paint paint, float amount) {
    Scene &scene = paint.scene;
    scene.shape({0, 0, kit::canvas::width, kit::canvas::height},
                kit::focus::withAlpha(kit::alpha::scrimStrong, amount));
    scene.pushOpacity(amount);
    paint.glyphs.text(scene, {400, 430, 1120, kit::type::headline.line},
                      say::msg("tv.home.silent", "Rooted TV is not answering"),
                      kit::type::headline, kit::color::onSurface, Node::Align::Center);
    paint.glyphs.text(scene, {560, 500, 800, kit::type::body.line * 2},
                      say::msg("tv.home.silentDesc", "Home comes back when Rooted TV answers again."),
                      kit::type::body,
                      kit::color::onSurface2, Node::Align::Center, 2);
    scene.popOpacity();
}
} // namespace

void drawHome(kit::Paint paint, const Frame &frame) {
    const ViewState &state = frame.state;
    const HomeState &home = state.home;
    const Motion &motion = frame.motion;
    Scene &scene = paint.scene;
    scene.background = kit::color::ink;
    if (home.tabs.empty())
        return;
    const bool apps = home.tabs[home.tab].key == "apps", titles = titlesTab(home.tabs[home.tab].key);
    wallpaper(scene, home);
    float wall = static_cast<float>(motion.value("wall"));
    kit::ambientWall(paint, 1 - wall, home.art.wallTint);
    // A tab change slides the page's content sideways; the art behind it only fades.
    const float slide = static_cast<float>(motion.value("pgs"));
    if (apps || titles) {
        kit::Hero none = heroOf(frame);
        none.wallAlpha = wall;
        kit::heroWall(paint, none);
        scene.pushTransform(Transform::translate(slide, 0));
        // Films, Shows and Apps: Google TV's featured cards from y 192, the rows under them.
        rows(paint, frame, layout::featureTop - layout::rowsTop);
        scene.popTransform();
        arrangeHints(paint, frame);
    } else {
        kit::Hero hero = heroOf(frame);
        kit::heroWall(paint, hero);
        // Google TV darkens only the banner's art while the bar has the focus; its lines and the
        // rows keep their light.
        const float artDim = static_cast<float>(motion.value("bd"));
        if (artDim > .002f)
            scene.shape({0, 0, kit::canvas::width, kit::canvas::height},
                        kit::focus::withAlpha(kit::alpha::artDim, artDim));
        scene.pushTransform(Transform::translate(slide, 0));
        kit::heroText(paint, hero);
        if (home.spotlight.size() > 1) {
            kit::Pagination dots;
            dots.count = static_cast<int>(home.spotlight.size());
            for (int i = 0; i < dots.count; ++i)
                dots.current.push_back(static_cast<float>(motion.value(home::id("dot", i))));
            dots.progress = static_cast<float>(motion.value("hp"));
            kit::geometry::HeroParallax parallax = kit::geometry::heroParallax(hero.stackY);
            dots.alpha = hero.text[2].alpha * parallax.textAlpha *
                         static_cast<float>(motion.value("dots")) * hero.textAlpha *
                         (1 - static_cast<float>(motion.value("hu")));
            // Google TV's dots go down with the rows while the banner has the focus.
            dots.y = parallax.textY + std::max(hero.stackY, 0.0f);
            kit::pagination(paint, dots);
        }
        rows(paint, frame);
        scene.popTransform();
        if (home.rows.empty())
            paint.glyphs.text(scene, {kit::canvas::safeX, 800, 1600, kit::type::body.line},
                              state.catalogue.valid()
                                  ? say::msg("tv.home.empty", "No items to show. Open Settings to arrange Home.")
                                  : say::msg("tv.home.loading", "Loading Home…"),
                              kit::type::body, kit::color::onSurface2);
    }
    if (home.editing && home.row < home.rows.size() && home.column < home.rows[home.row].cards.size()) {
        // Editing the apps: the button above the page hides the focused app, or shows it again when
        // it was one of the hidden ones; Red does the same.
        const bool hidden = home.rows[home.row].cards[home.column].hidden;
        // Home's row and the Apps page each have their own hidden apps.
        const bool onApps = home.tabs[home.tab].key == "apps";
        const std::string label = hidden ? (onApps ? say::msg("tv.home.showOnApps", "Show on Apps")
                                                   : say::msg("tv.home.showOnHome", "Show on Home"))
                                         : (onApps ? say::msg("tv.home.hideFromApps", "Hide from Apps")
                                                   : say::msg("tv.home.hideFromHome", "Hide from Home"));
        hintBand(paint, {{"", say::msg("tv.hint.move", "Move")}, {"OK", say::msg("tv.hint.done", "Done")},
                         {say::msg("tv.hint.red", "Red"), label}});
        kit::PillButton hide;
        hide.rest = {kit::canvas::width - kit::canvas::safeX - 380, 136, 380, 56};
        hide.id = "edit:hide";
        hide.label = label;
        hide.icon = hidden ? "eye" : "eyeoff";
        hide.focus = static_cast<float>(motion.value("f:eh"));
        kit::pillButton(paint, hide);
    }
    // On the Apps, Films and Shows pages Google TV darkens all of it under the bar while the bar has
    // the focus.
    const float barDim = apps || titles ? static_cast<float>(motion.value("bd")) : 0;
    if (barDim > .002f)
        scene.shape({0, 0, kit::canvas::width, kit::canvas::height}, kit::focus::withAlpha(kit::alpha::barDim, barDim));
    // The bar leaves with the page once the rows are scrolled up under it (on Home past its first
    // two rows, on Films, Shows and Apps past the featured cards), and comes back with the focus.
    float away = 0;
    if (!home.navigation && !home.editing)
        away = std::max(static_cast<float>(motion.value("ba")),
                        kit::focus::clamp01(-static_cast<float>(motion.value("sy")) / kBarLeaveSpan));
    drawTopBar(paint, frame, static_cast<int>(home.tab), away);
    float fault = static_cast<float>(motion.value("fault"));
    if (fault > .002f)
        banner(paint, fault);
}
} // namespace nativetv
