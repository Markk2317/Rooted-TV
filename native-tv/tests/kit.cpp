#include "ui/application.hpp"
#include "ui/home_layout.hpp"
#include "ui/kit/components.hpp"
#include "ui/kit/focus.hpp"
#include "ui/kit/geometry.hpp"
#include "ui/kit/run_glyphs.hpp"
#include "ui/kit/tuning.hpp"
#include "ui/kit/widgets.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>

using namespace nativetv;
namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool near(float left, float right, float tolerance = .06f) {
    return std::fabs(left - right) <= tolerance;
}
bool sameRect(Rect left, Rect right, float tolerance = .06f) {
    return near(left.x, right.x, tolerance) && near(left.y, right.y, tolerance) &&
           near(left.w, right.w, tolerance) && near(left.h, right.h, tolerance);
}
bool sameColor(Color left, Color right) {
    return near(left.r, right.r, .001f) && near(left.g, right.g, .001f) &&
           near(left.b, right.b, .001f) && near(left.a, right.a, .001f);
}
bool hasHit(const Scene &scene, const std::string &id) {
    for (const auto &hit : scene.hits)
        if (hit.id == id)
            return true;
    return false;
}
const Node *textNode(const Scene &scene, const std::string &value) {
    for (const auto &node : scene.nodes)
        if (node.kind == Node::Kind::Text && node.value == value)
            return &node;
    return nullptr;
}
const Node *textAt(const Scene &scene, const std::string &value) {
    return textNode(scene, value);
}
size_t countKind(const Scene &scene, Node::Kind kind) {
    size_t count = 0;
    for (const auto &node : scene.nodes)
        if (node.kind == kind)
            ++count;
    return count;
}
const Node *shapeAt(const Scene &scene, Rect rect, float tolerance = .06f) {
    for (const auto &node : scene.nodes)
        if (node.kind == Node::Kind::Rectangle && sameRect(node.rect, rect, tolerance))
            return &node;
    return nullptr;
}
const Node *shapeWith(const Scene &scene, Rect rect, Color color) {
    for (const auto &node : scene.nodes)
        if (node.kind == Node::Kind::Rectangle && sameRect(node.rect, rect) &&
            sameColor(node.color, color))
            return &node;
    return nullptr;
}
const Hit *hitFor(const Scene &scene, const std::string &id) {
    for (const auto &hit : scene.hits)
        if (hit.id == id)
            return &hit;
    return nullptr;
}
struct Fixture {
    Scene scene;
    kit::LegacyGlyphs glyphs;
    kit::Paint paint() {
        return {scene, glyphs};
    }
};
struct NoopServices final : Services {
    unsigned read(const std::string &, Operation, const Json &) override {
        return 0;
    }
    unsigned write(const std::string &, Operation, const Json &) override {
        return 0;
    }
    void cancel(const std::string &) override {}
};

void tokens() {
    check(sameColor(kit::color::ink, kit::rgb(0x0C0B0E)) &&
              sameColor(kit::color::focusFill, kit::rgb(0xF1EEF4)) &&
              sameColor(kit::alpha::outline, kit::rgb(0xF1EEF4, .12f)),
          "Colour tokens come from tokens.json");
    check(kit::type::display.px == 72 && kit::type::display.line == 88 &&
              kit::type::display.weight == 400 && near(kit::type::display.tracking, -.01f, 1e-6f) &&
              kit::type::key.px == 32,
          "Type tokens keep size, line, weight and tracking");
    check(kit::motion::ms::page == 420 && kit::motion::ms::focus == 260 &&
              kit::motion::spring::row.stiffness == 400 &&
              near(kit::motion::spring::row.dampingRatio, 1, 1e-6f) &&
              kit::motion::stagger::heroText[2] == 240 && kit::motion::ms::idleAfter == 30000 &&
              near(kit::motion::scale::card, 1.05f, 1e-6f) &&
              near(kit::motion::ease::out.x1, .23f, 1e-6f) && kit::motion::ease::linear.x2 == 1,
          "Motion tokens keep durations, springs, scales and curves");
    check(kit::elevation::focusRingWidth == 3 && kit::elevation::focusShadow.dy == 22 &&
              kit::elevation::focusShadow.blur == 56 &&
              near(kit::elevation::focusShadow.alpha, .62f),
          "Elevation tokens keep the ring and the focus shadow");
}

// Milliseconds a spring takes to come to rest after a 100 px move.
double settleMs(kit::SpringTuning tuning, double *overshoot = nullptr) {
    Spring spring(kit::springParams(tuning), 0);
    spring.retarget(100);
    double peak = 0, elapsed = 0;
    while (!spring.settled() && elapsed < 3000) {
        spring.advance(1000.0 / 240);
        elapsed += 1000.0 / 240;
        peak = std::max(peak, spring.position() - 100);
    }
    if (overshoot)
        *overshoot = peak;
    return elapsed;
}

void motionTokens() {
    double overshoot = 0;
    check(settleMs(kit::motion::spring::row, &overshoot) < 600 && overshoot < 1,
          "The row spring settles within 600 ms without visible overshoot");
    check(settleMs(kit::motion::spring::page, &overshoot) < 800 && overshoot < .01,
          "The page spring is critically damped and settles within 800 ms");
    check(settleMs(kit::motion::spring::pillLead) < 400 &&
              settleMs(kit::motion::spring::pillTrail) < 550,
          "Both pill edges settle within about half a second");
    Easing out = kit::easing(kit::motion::ease::out),
           linear = kit::easing(kit::motion::ease::linear);
    check(out(.5) > .85 && out(0) == 0 && near(static_cast<float>(out(1)), 1, 1e-4f) &&
              linear(.3) == .3 && kit::easing(kit::motion::ease::inOut)(.1) < .05 &&
              kit::easing(kit::motion::ease::inOut)(.9) > .95,
          "Easing tokens become the engine's curves: strong ease-out, slow-at-both-ends in-out, "
          "linear");
}

void focusMath() {
    namespace focus = kit::focus;
    check(focus::scaleAt(1.1f, 0) == 1 && near(focus::scaleAt(1.1f, 1), 1.1f, 1e-6f) &&
              near(focus::scaleAt(1.1f, .5f), 1.05f, 1e-6f) &&
              near(focus::scaleAt(1.1f, 3), 1.1f, 1e-6f) && focus::scaleAt(1.1f, -1) == 1,
          "Focus scale interpolates between rest and the token and clamps");

    Rect card = kit::geometry::card(0, 0);
    Rect lifted = focus::scaledAbout(card, kit::motion::scale::card);
    check(near(lifted.w, 306 * 1.05f) && near(lifted.h, 172 * 1.05f) &&
              near(lifted.x + lifted.w / 2, card.x + card.w / 2) &&
              near(lifted.y + lifted.h / 2, card.y + card.h / 2),
          "A focused card grows 5% about its centre");
    Rect row = kit::geometry::settingsRow(0);
    Rect pushed = focus::scaledAbout(row, 1.02f, focus::leftCentre);
    check(near(pushed.x, row.x) && near(pushed.w, 840 * 1.02f) &&
              near(pushed.y + pushed.h / 2, row.y + row.h / 2),
          "A settings row grows 2% about its left-centre");

    focus::Look rest = focus::lookAt(card, kit::motion::scale::card, 0);
    check(sameRect(rest.rect, card) && rest.ringAlpha == 0 && rest.shadowAlpha == 0 &&
              rest.ringWidth == 3,
          "An unfocused card has no ring and no shadow");
    focus::Look half = focus::lookAt(card, kit::motion::scale::card, .5f);
    check(near(half.ringAlpha, .5f) && near(half.shadowAlpha, .31f) && half.shadowDy == 22 &&
              half.shadowBlur == 56 && near(half.scale, 1.025f, 1e-6f),
          "Ring and shadow fade with focus; their shape does not change");
    focus::Look full = focus::lookAt(card, kit::motion::scale::card, 1);
    check(near(full.shadowAlpha, .62f) && sameRect(full.rect, lifted),
          "Full focus is the 5% lift with the .62 shadow");

    check(sameColor(focus::inkAt(kit::color::onSurface2, kit::color::onFocus, 0),
                    kit::color::onSurface2) &&
              sameColor(focus::inkAt(kit::color::onSurface2, kit::color::onFocus, 1),
                        kit::color::onFocus),
          "Ink cross-fades from rest to focused");
    check(focus::labelRise(0) == -4 && focus::labelRise(1) == 0 &&
              near(focus::dimmedRowAlpha(1), .5f, 1e-6f) && focus::dimmedRowAlpha(0) == 1,
          "The app name rises 4 px; other rows dim to 50%");
    check(near(focus::keyScale(1, 0), 1.1f, 1e-6f) && near(focus::keyScale(1, 1), .94f, 1e-6f) &&
              near(focus::keyScale(0, 1), .94f, 1e-6f) && focus::keyScale(0, 0) == 1,
          "A key press dips to .94 from wherever focus left it");
}

void geometryAgainstMetrics() {
    namespace geometry = kit::geometry;
    namespace home = kit::metrics::home;
    std::vector<Rect> tabs =
        geometry::tabRects({home::tabHome.w - 68, home::tabApps.w - 68, home::tabLive.w - 68});
    const float tabsRight = tabs[2].x + tabs[2].w;
    check(sameRect(geometry::avatar(), home::avatar) &&
              sameRect(geometry::iconButton(tabsRight, 0), home::screenSaver, .15f) &&
              sameRect(geometry::iconButton(tabsRight, 1), home::gear, .15f),
          "The top bar's avatar, screen saver and settings buttons sit where the record puts them");
    check(sameRect(geometry::iconButton(tabsRight, 1),
                   {tabsRight + 8 + 24 + 12 + 80, 76, 64, 64}, .01f),
          "The icon capsule's 64 px buttons start 44 px after the tabs, at an 80 px pitch");
    check(sameRect(geometry::clock(140), {1920 - 116 - 140 - 2 * 20 - 2 - 130, 84, 130, 48}),
          "The clock ends before the divider and the wordmark at the right margin");
    check(sameRect(geometry::capsule(220, 600), {220, 64, 380, 88}) &&
              sameRect(geometry::searchButton(), {228, 72, 96, 72}) &&
              geometry::tabsLeft(true) == 324 && geometry::tabsLeft() == 228,
          "Search leads the tabs' capsule as a 96x72 pill slot, as tall as the tabs");
    // The prototype's widths are rounded to 0.1 each, so positions drift by up to 0.1.
    check(sameRect(tabs[0], home::tabHome, .15f) && sameRect(tabs[1], home::tabApps, .15f) &&
              sameRect(tabs[2], home::tabLive, .15f),
          "Tabs touch, each its text plus 34 px on both sides");
    check(sameRect(geometry::pill(tabs[1].x, tabs[1].x + tabs[1].w), home::tabApps, .15f) &&
              sameRect(geometry::pill(tabs[0].x, tabs[2].x + tabs[2].w),
                       {home::tabHome.x, 72, home::tabHome.w + home::tabApps.w + home::tabLive.w, 72},
                       .15f),
          "The pill spans its two edges at the bar height");
    check(sameRect(geometry::heroSource(), home::heroMeta) &&
              sameRect(geometry::heroTitle(), home::heroTitle) &&
              sameRect(geometry::heroSubtitle(), home::heroSubtitle),
          "The hero text block stacks source, title and subtitle");
    check(sameRect(geometry::dotSlot(0), home::dotSlot1) &&
              near(geometry::dotSlot(5).x + geometry::dotSlot(5).w, 1804) &&
              sameRect({geometry::dotSlot(0).x, 519, 6 * 32, 16}, home::dots6Slots),
          "Six pagination slots end at Home's right margin");
    check(sameRect(geometry::rowLabel(0), home::row1Label) &&
              sameRect(geometry::rowLabel(1), home::row2Label) &&
              sameRect(geometry::rowLabel(2), home::row3Label) &&
              sameRect({0, geometry::rowLabelY(1), 1920, kit::layout::rowPitch}, home::row2Box),
          "Row labels sit 251 px apart");
    check(sameRect(geometry::card(0, 0), home::card1) &&
              sameRect(geometry::card(0, 1), home::card2) &&
              near(geometry::card(0, 1).x - geometry::card(0, 0).x, 346) &&
              near(geometry::card(0, 0).y - geometry::rowLabelY(0), 55),
          "Poster cards are 306x172 at a 346 px pitch, 55 px under their label, as Google TV's top "
          "picks");
    check(sameRect(geometry::appCircle(1, 0), home::app1) &&
              sameRect(geometry::appCircle(1, 1), home::app2),
          "App circles are 150 px at a 192 px pitch, as Google TV's Your apps");
    check(sameRect(geometry::inputTile(2, 0), home::input1), "Input tiles sit in the third row");

    namespace apps = kit::metrics::apps;
    check(sameRect(geometry::appTile(0), apps::tile1) &&
              sameRect(geometry::appTile(1), apps::tile2) &&
              sameRect(geometry::appTile(6), apps::tile7Row2) &&
              near(geometry::appTile(0).y, kit::layout::appsTop),
          "The Apps grid has no page head: six 156 px circles per row at a 293 x 240 pitch from y 160");

    namespace live = kit::metrics::live;
    check(sameRect(geometry::statCard(0), live::stat1) &&
              sameRect(geometry::statCard(1), live::stat2) &&
              sameRect(geometry::statCard(4), live::stat5Row2),
          "Live stat cards are 422x292 in four columns");

    namespace settings = kit::metrics::settings;
    check(sameRect(geometry::settingsViewport(), settings::listViewport) &&
              sameRect(geometry::settingsRow(0), settings::row1) &&
              sameRect(geometry::settingsRow(1), settings::row2) &&
              sameRect(geometry::detailPanel(), settings::detailPanel),
          "Settings rows are 840x84 at a 92 px pitch beside the detail panel");

    namespace setup = kit::metrics::setup;
    std::vector<std::string> digits(10, "1"), shiftRow = {"shift", "z", "delete"},
                                              lastRow = {"space", "done"};
    check(sameRect(geometry::keyRow(944, 340, 0, digits)[0], setup::key1) &&
              sameRect(geometry::keyRow(944, 340, 3, shiftRow)[0], setup::shiftKey) &&
              sameRect(geometry::keyRow(944, 340, 4, lastRow)[0], setup::spaceKey) &&
              sameRect(geometry::keyRow(944, 340, 4, lastRow)[1], setup::doneKey),
          "Keys are 80x74 with a 10 px gap; shift, space and done are wider");
}

void scrolling() {
    namespace geometry = kit::geometry;
    check(geometry::rowScrollTarget(0, 20, 224, 24) == 0 &&
              near(geometry::rowScrollTarget(1, 20, 224, 24), -248) &&
              near(geometry::rowScrollTarget(3, 20, 224, 24), -3 * 248) &&
              near(geometry::rowScrollTarget(10, 20, 224, 24), -10 * 248),
          "A row scrolls with every move, keeping the focused item in the first slot");
    float lastEnd = 20 * 248 - 24 - (1920 - 2 * 116);
    check(near(geometry::rowScrollTarget(19, 20, 224, 24), -lastEnd) &&
              geometry::rowScrollTarget(5, 6, 224, 24) == 0,
          "A row stops at its end and a short row never scrolls");
    check(geometry::stackTarget(0) == 0 && geometry::stackTarget(-1) == 0 &&
              geometry::stackTarget(2) == -502,
          "The stack snaps one row pitch per content row");
    kit::geometry::HeroParallax none = geometry::heroParallax(0),
                                gone = geometry::heroParallax(-251),
                                mid = geometry::heroParallax(-126);
    check(none.textAlpha == 1 && none.scrimOpacity == 0 && near(gone.textY, -125.5f) &&
              gone.textAlpha == 0 && near(gone.artY, -30.12f) && near(gone.scrimOpacity, .95f) &&
              near(mid.textAlpha, 1 - 126.0f / 150) && near(mid.scrimOpacity, 126.0f / 251 * .95f),
          "Hero parallax follows the stack offset");
    kit::geometry::HeroParallax dropped = geometry::heroParallax(kit::layout::bannerDrop);
    check(dropped.textY == 0 && dropped.artY == 0 && dropped.textAlpha == 1 &&
              dropped.scrimOpacity == 0,
          "Rows that drop under the banner leave its text and art in place, unveiled");
    check(geometry::settingsScrollTarget(0, 12) == 0 &&
              geometry::settingsScrollTarget(3, 12) == 0 &&
              near(geometry::settingsScrollTarget(5, 12), -184) &&
              near(geometry::settingsScrollTarget(11, 12), -(12 * 92 + 16 - 756)) &&
              geometry::settingsScrollTarget(7, 4) == 0,
          "The settings list follows focus and stops at its end");
}

void homeLayout() {
    // Google TV's rows as Home lays them out: the top picks' small cards, Your apps' circles and
    // the larger cards of the rows of titles under them.
    CardRow picks, apps, more;
    picks.key = "trending";
    apps.key = "app";
    more.key = "continue";
    check(home::itemWidth(picks) == 306 && home::itemHeight(picks) == 172 &&
              home::itemGap(picks) == 40 && home::itemWidth(more) == 391 &&
              home::itemHeight(more) == 220 && home::itemGap(more) == 41 &&
              home::itemWidth(apps) == 150 && home::itemHeight(apps) == 150,
          "The top picks keep 306x172 cards; the rows of titles under Your apps take 391x220 ones");
    home::RowLook rest, risen, focused;
    risen.risen = 1;
    focused.risen = 1;
    focused.grow = 1;
    check(home::labelPx(rest) == 24 && home::labelPx(risen) == 29 && home::labelPx(focused) == 52 &&
              near(home::labelArea(rest), 55) && near(home::labelArea(focused), 121, .2f),
          "Row titles are 24 px over the banner, 29 risen and 52 for the focused row; items stand "
          "55 px under a resting title, 121 under the focused one");
    std::vector<float> tops = home::rowTops({picks, apps, more}, {});
    check(tops.size() == 3 && tops[0] == kit::layout::rowsTop &&
              near(tops[1], kit::geometry::rowLabelY(1)) &&
              near(tops[2], tops[1] + 55 + 150 + kit::layout::rowGap),
          "The first screen's rows stand where the geometry puts them, each row as tall as its "
          "items");
    check(sameRect(home::item(more, 700, 1), {116 + 391 + 41, 700, 391, 220}),
          "A row's items start at Home's 116 px margin");
}

void cards() {
    Fixture rest;
    kit::PosterCard poster;
    poster.rest = kit::geometry::card(0, 0);
    poster.id = "card:0";
    poster.title = "Glass Season";
    poster.image = "image:one";
    kit::card(rest.paint(), poster);
    const Hit *hit = hitFor(rest.scene, "card:0");
    check(hit && sameRect(hit->rect, {116, 655, 306, 172}), "A card at rest is 306x172 at 1080p");
    const Node &base = rest.scene.nodes[0];
    check(base.kind == Node::Kind::Rectangle && base.radius == 24 &&
              sameColor(base.color, kit::color::surface2) && base.ring.outer == 0 &&
              base.shadow.color.a == 0 && base.group.identity(),
          "An unfocused card has no ring, shadow or scale, and Google TV's 24 px corners");
    check(textAt(rest.scene, "Glass Season") &&
              sameRect(textAt(rest.scene, "Glass Season")->rect, {132, 777, 274, 40}),
          "The title sits 16 px in and 10 px above the bottom edge");

    Fixture lifted;
    poster.focus = 1;
    kit::card(lifted.paint(), poster);
    // The ring stands 4 px off the card, the page showing in the gap; the card follows it.
    const Node &ring = lifted.scene.nodes[0], &focused = lifted.scene.nodes[1];
    const Rect grown = {116 - 306 * .025f, 655 - 172 * .025f, 306 * 1.05f, 172 * 1.05f};
    bool shadowed = false;
    for (const auto &node : lifted.scene.nodes)
        shadowed = shadowed || node.shadow.color.a > 0;
    check(near(focused.group.sx, 1.05f, 1e-5f) && near(focused.group.sy, 1.05f, 1e-5f) &&
              sameRect(focused.group.apply(focused.rect), grown) && focused.ring.outer == 0 &&
              sameRect(ring.rect, {112, 651, 314, 180}) && ring.radius == 28 &&
              ring.color.a == 0 && near(ring.ring.outer, 3) &&
              sameColor(ring.ring.color, kit::alpha::cardRing) &&
              sameRect(ring.group.apply(ring.rect),
                       kit::focus::scaledAbout({112, 651, 314, 180}, 1.05f)) &&
              !shadowed && sameRect(hitFor(lifted.scene, "card:0")->rect, grown),
          "A focused card scales 1.05 about its centre with a 3 px ring standing 4 px off it, and "
          "no shadow");

    Fixture after;
    kit::card(after.paint(), poster);
    after.scene.shape({0, 0, 1, 1}, kit::color::ink);
    check(after.scene.nodes.back().group.identity() && after.scene.nodes.back().groupOpacity == 1,
          "A component leaves no transform or opacity pushed");

    Fixture entering;
    poster.focus = 0;
    poster.enter = {.5f, 28};
    kit::card(entering.paint(), poster);
    check(near(entering.scene.nodes[0].groupOpacity, .5f) && entering.scene.nodes[0].group.ty == 28,
          "An entering card carries its fade and rise");
    Fixture hidden, away;
    poster.enter = {0, 28};
    kit::card(hidden.paint(), poster);
    poster.enter = {};
    poster.rest.x = 4000;
    kit::card(away.paint(), poster);
    check(hidden.scene.nodes.empty() && hidden.scene.hits.empty() && away.scene.nodes.empty(),
          "A card that has not entered or is off the canvas draws nothing");

    // A row of 40 cards only draws the ones on the canvas.
    Fixture longRow;
    for (int i = 0; i < 40; ++i) {
        kit::PosterCard item;
        item.rest = kit::geometry::card(0, i);
        item.id = "card:" + std::to_string(i);
        kit::card(longRow.paint(), item);
    }
    check(longRow.scene.hits.size() == 6,
          "Six 306 px cards fit the 1920 px canvas; the rest are culled");
}

void circlesAndTiles() {
    Fixture circle;
    kit::AppCircle app;
    app.rest = kit::geometry::appCircle(1, 0);
    app.id = "app:0";
    app.name = "Netflix";
    app.icon = "play";
    app.color = kit::rgb(0xE50914);
    kit::appCircle(circle.paint(), app);
    const Node &disc = circle.scene.nodes[0];
    check(sameRect(disc.rect, {116, 906, 150, 150}) && disc.radius == 75 &&
              disc.gradient.stops == 3 && disc.gradient.angle == 145 && disc.gradient.at[2] == 1 &&
              !textAt(circle.scene, "Netflix") && circle.scene.nodes.size() == 2,
          "An app circle is a 150 px disc with a 145 degree gradient and no name at rest");
    Fixture named;
    app.focus = 1;
    kit::appCircle(named.paint(), app);
    // Google TV's focused app: the disc grows 6% inside a 4 px white ring 3 px off it, no shadow.
    const Rect grownDisc = kit::focus::scaledAbout({116, 906, 150, 150}, 1.06f);
    const Node &appRing = named.scene.nodes[0], &grownFill = named.scene.nodes[1];
    bool appShadow = false;
    for (const auto &node : named.scene.nodes)
        appShadow = appShadow || node.shadow.color.a > 0;
    check(sameRect(grownFill.rect, grownDisc) && grownFill.group.identity() &&
              sameRect(appRing.rect, {grownDisc.x - 3, grownDisc.y - 3, grownDisc.w + 6,
                                      grownDisc.h + 6}) &&
              near(appRing.ring.outer, 4) && sameColor(appRing.ring.color, kit::rgb(0xFFFFFF)) &&
              appRing.color.a == 0 && !appShadow,
          "A focused app circle grows 6% inside a 4 px white ring standing 3 px off it, without a "
          "shadow");
    check(textAt(named.scene, "Netflix") &&
              sameRect(textAt(named.scene, "Netflix")->rect, {64, 1080, 254, 32}) &&
              sameColor(textAt(named.scene, "Netflix")->color, kit::color::appNameFocused),
          "A focused app circle shows its name 174 px below its top, in the focused name colour");
    Fixture half;
    app.focus = .5f;
    kit::appCircle(half.paint(), app);
    check(near(textAt(half.scene, "Netflix")->color.a, .5f) &&
              near(textAt(half.scene, "Netflix")->rect.y, 1080 - 2),
          "The name fades in while rising 4 px");
    Fixture every;
    app.focus = 0;
    app.names = 1;
    kit::appCircle(every.paint(), app);
    check(textAt(every.scene, "Netflix") && near(textAt(every.scene, "Netflix")->rect.y, 1080) &&
              sameColor(textAt(every.scene, "Netflix")->color, kit::color::onSurface3) &&
              every.scene.nodes[0].ring.outer == 0,
          "In a row that shows every name an unfocused circle keeps its name, in the tertiary ink");
    app.names = 0;
    Fixture always;
    app.named = true;
    kit::appCircle(always.paint(), app);
    check(textAt(always.scene, "Netflix") && near(textAt(always.scene, "Netflix")->rect.y, 1080) &&
              sameColor(textAt(always.scene, "Netflix")->color, kit::color::onSurface3),
          "A named circle keeps its name at rest, in the tertiary ink");
    // With art, the fill is a backing for the picture that follows: pulled in by 1.5 px so the
    // picture's anti-aliased rim is not tinted by the app's colour, and hidden once an opaque
    // picture is on show. The focus ring is a shape of its own, round the grown circle.
    Fixture art;
    app.image = "app:netflix";
    kit::appCircle(art.paint(), app);
    check(art.scene.nodes[0].backing &&
              sameRect(art.scene.nodes[0].rect, {117.5f, 907.5f, 147, 147}) &&
              art.scene.nodes[0].radius == 73.5f && art.scene.nodes[1].kind == Node::Kind::Image &&
              art.scene.nodes[1].value == "app:netflix" &&
              sameRect(art.scene.nodes[1].rect, {116, 906, 150, 150}),
          "An app circle's fill is a backing that stops short of its picture's edge");
    Fixture artFocused;
    app.focus = 1;
    kit::appCircle(artFocused.paint(), app);
    check(!artFocused.scene.nodes[0].backing && artFocused.scene.nodes[0].ring.outer > 0 &&
              sameRect(artFocused.scene.nodes[0].rect, {108.5f, 898.5f, 165, 165}) &&
              artFocused.scene.nodes[1].backing &&
              sameRect(artFocused.scene.nodes[2].rect, grownDisc),
          "The focus ring stands off the grown circle, under the backing and the picture");
    app.focus = 0;
    app.image.clear();

    Fixture input;
    kit::InputTile tile;
    tile.rest = kit::geometry::inputTile(2, 0);
    tile.id = "input:1";
    tile.name = "HDMI 1";
    tile.sub = "Game Console";
    tile.live = true;
    tile.unused = true;
    kit::inputTile(input.paint(), tile);
    check(sameRect(input.scene.nodes[0].rect, {116, 1157, 306, 172}) &&
              sameColor(input.scene.nodes[0].color, kit::color::surface2) &&
              near(input.scene.nodes[0].ring.inner, 1) &&
              near(input.scene.nodes[0].groupOpacity, .62f) && textAt(input.scene, "HDMI 1") &&
              textAt(input.scene, "Game Console") &&
              shapeAt(input.scene, {116 + 306 - 26, 1157 + 16, 10, 10}),
          "An unused input tile is 62% opaque with its outline, names and a live dot 16 px from "
          "the corner");

    Fixture appTile;
    kit::AppTile gridTile;
    gridTile.rest = kit::geometry::banner(7);
    gridTile.id = "tile:7";
    gridTile.name = "Plex";
    gridTile.icon = "play";
    kit::appTile(appTile.paint(), gridTile);
    check(sameRect(appTile.scene.nodes[0].rect, {412, 495, 272, 153}) &&
              appTile.scene.nodes[0].gradient.angle == 150 && textAt(appTile.scene, "Plex") &&
              sameRect(textAt(appTile.scene, "Plex")->rect, {432, 495 + 153 - 20 - 40, 232, 40}),
          "An app tile is 272x153 with a 150 degree gradient and its name at the bottom left");

    // A banner tile (the Apps tab) is one flat colour with no name on it and the glyph in the middle;
    // a light colour takes dark ink.
    Fixture banner;
    kit::AppTile bannerTile = gridTile;
    bannerTile.centred = true;
    bannerTile.color = kit::rgb(0xFFFFFF);
    kit::appTile(banner.paint(), bannerTile);
    check(banner.scene.nodes[0].gradient.stops == 0 && !textAt(banner.scene, "Plex") &&
              sameRect(banner.scene.nodes[0].rect, {412, 495, 272, 153}) &&
              hitFor(banner.scene, "tile:7"),
          "A banner tile is one flat colour with no name on it");
}

void barAndHero() {
    Fixture bar;
    kit::TopBar top;
    top.avatarInitial = "I";
    top.tabs = {"Home", "Apps", "Live"};
    top.selected = 0;
    top.clock = "10:11";
    std::vector<Rect> tabs = kit::tabRects(bar.glyphs, top.tabs);
    top.pillLeft = tabs[0].x;
    top.pillRight = tabs[0].x + tabs[0].w;
    kit::topBar(bar.paint(), top);
    const float tabsRight = tabs[2].x + tabs[2].w;
    check(hitFor(bar.scene, "nav:avatar") && hitFor(bar.scene, "nav:tab:0") &&
              hitFor(bar.scene, "nav:tab:2") && hitFor(bar.scene, "nav:settings") &&
              hitFor(bar.scene, "tool:4") && !hitFor(bar.scene, "tool:2") &&
              !hitFor(bar.scene, "tool:3") &&
              sameRect(hitFor(bar.scene, "tool:4")->rect,
                       kit::geometry::iconButton(tabsRight, 0)) &&
              sameRect(hitFor(bar.scene, "nav:settings")->rect,
                       kit::geometry::iconButton(tabsRight, 1)) &&
              sameRect(hitFor(bar.scene, "nav:avatar")->rect, kit::metrics::home::avatar),
          "The top bar offers the avatar, each tab, the screen saver and the settings button");
    // The tabs' capsule, then the icon capsule (screen saver and gear), then the pill.
    const Node &tabCapsule = bar.scene.nodes[0], &iconCapsule = bar.scene.nodes[1];
    check(sameRect(tabCapsule.rect, {220, 64, tabsRight + 8 - 220, 88}) &&
              tabCapsule.radius == 44 && sameColor(tabCapsule.color, kit::alpha::capsule) &&
              sameRect(iconCapsule.rect, {tabsRight + 32, 64, 12 + 64 + 16 + 64 + 12, 88}) &&
              iconCapsule.radius == 44 && sameColor(iconCapsule.color, kit::alpha::capsule),
          "The tabs, and the screen saver and gear, each sit in a white 12% capsule, as on Google "
          "TV");
    const Node *pill = &bar.scene.nodes[2];
    check(sameRect(pill->rect, {tabs[0].x, 72, tabs[0].w, 72}) && pill->radius == 36 &&
              sameColor(pill->color, kit::color::tabSelected),
          "The pill rests on the selected tab, light as Google TV's");
    const Node *mark = textAt(bar.scene, "Native Tv");
    check(mark && near(mark->rect.x + mark->rect.w, 1804, .01f) && textAt(bar.scene, "10:11") &&
              sameRect(textAt(bar.scene, "10:11")->rect, kit::geometry::clock(mark->rect.w)) &&
              textAt(bar.scene, "10:11")->align == Node::Align::Right &&
              sameColor(textAt(bar.scene, "10:11")->color, kit::alpha::clockInk) &&
              sameColor(mark->color, kit::alpha::clockInk) &&
              sameColor(textAt(bar.scene, "Home")->color, kit::color::onFocus) &&
              sameColor(textAt(bar.scene, "Apps")->color, kit::alpha::clockInk),
          "The wordmark ends at 1804 after the clock, both 60% white; the selected tab is dark on "
          "its pill, the others 60% white");
    check(!textAt(bar.scene, "Settings") && !textAt(bar.scene, "Screensaver"),
          "No tooltip shows while no icon button has the focus");
    Fixture hot;
    top.pillHot = 1;
    top.avatarFocus = 1;
    kit::topBar(hot.paint(), top);
    check(sameColor(hot.scene.nodes[2].color, kit::color::tabFocused) &&
              sameColor(textAt(hot.scene, "Home")->color, kit::color::onFocus) &&
              sameColor(textAt(hot.scene, "Apps")->color, kit::color::onSurface),
          "With focus on the bar the pill turns to the focused tab colour and the other tabs "
          "white");
    check(near(hot.scene.nodes[3].group.sx, 1.12f, 1e-5f), "A focused round button grows 12%");
    Fixture saver;
    top.pillHot = 0;
    top.avatarFocus = 0;
    top.saverFocus = 1;
    kit::topBar(saver.paint(), top);
    const Rect saverButton = kit::geometry::iconButton(tabsRight, 0);
    const Node *tip = textAt(saver.scene, "Screensaver");
    check(tip && near(tip->rect.y, saverButton.y + saverButton.h + 24) &&
              near(tip->rect.x + tip->rect.w / 2, saverButton.x + saverButton.w / 2) &&
              tip->align == Node::Align::Center && !textAt(saver.scene, "Settings") &&
              shapeWith(saver.scene, saverButton, kit::color::tabSelected),
          "A focused icon button turns light and names itself in a tooltip under it");
    top.saverFocus = 0;
    Fixture assisted;
    top.assistant = true;
    kit::topBar(assisted.paint(), top);
    check(hitFor(assisted.scene, "tool:3") &&
              sameRect(hitFor(assisted.scene, "tool:3")->rect,
                       kit::geometry::iconButton(tabsRight, 0)) &&
              sameRect(hitFor(assisted.scene, "tool:4")->rect,
                       kit::geometry::iconButton(tabsRight, 1)) &&
              sameRect(hitFor(assisted.scene, "nav:settings")->rect,
                       kit::geometry::iconButton(tabsRight, 2)) &&
              near(assisted.scene.nodes[1].rect.w, 12 + 3 * 64 + 2 * 16 + 12),
          "The assistant leads the icon capsule, before the screen saver and the gear");
    top.assistant = false;
    Fixture searching;
    top.search = true;
    kit::topBar(searching.paint(), top);
    std::vector<Rect> shifted = kit::tabRects(bar.glyphs, top.tabs, true);
    check(hitFor(searching.scene, "tool:2") &&
              sameRect(hitFor(searching.scene, "tool:2")->rect, kit::geometry::searchButton()) &&
              near(shifted[0].x, tabs[0].x + 96, .01f) &&
              sameRect(hitFor(searching.scene, "nav:tab:0")->rect, shifted[0]),
          "Search leads the capsule as a 96 px slot and moves the tabs along");
    top.search = false;
    Fixture gone;
    top.pillAlpha = 0;
    kit::topBar(gone.paint(), top);
    check(!shapeAt(gone.scene, {tabs[0].x, 60, tabs[0].w, 72}),
          "The pill is not drawn while Settings owns the page");

    Fixture settingsOn;
    kit::TopBar onSettings;
    onSettings.tabs = {"Home"};
    onSettings.selected = -1;
    onSettings.settingsSelected = true;
    kit::topBar(settingsOn.paint(), onSettings);
    const Rect onlyGear = hitFor(settingsOn.scene, "nav:settings")->rect;
    check(shapeAt(settingsOn.scene, onlyGear) &&
              sameColor(shapeAt(settingsOn.scene, onlyGear)->color, kit::alpha::accentDim),
          "The settings button takes the accent fill when it is the page");

    Fixture wall;
    kit::Hero hero;
    hero.slot[0].image = "art:1";
    hero.slot[0].opacity = 1;
    hero.slot[0].kenBurns = 1;
    hero.source = "Harbor Originals";
    hero.title = "Harbor Lights";
    hero.facts = "2021 · 7.2 · 98 min";
    hero.description = "A ferry pilot and a lighthouse keeper";
    kit::hero(wall.paint(), hero);
    const Node *title = textAt(wall.scene, "Harbor Lights");
    check(title && sameRect(title->rect, kit::metrics::home::heroTitle) &&
              sameRect(textAt(wall.scene, "Harbor Originals")->rect, {170, 281, 1406, 40}) &&
              textAt(wall.scene, "2021 · 7.2 · 98 min") &&
              near(textAt(wall.scene, "2021 · 7.2 · 98 min")->rect.x, 116) &&
              near(textAt(wall.scene, "2021 · 7.2 · 98 min")->rect.y, 494) &&
              !textAt(wall.scene, "A ferry pilot and a lighthouse keeper") &&
              shapeAt(wall.scene, {116, 281, 40, 40}),
          "The hero text block sits where the design puts it: source, title and one line of facts");
    // The banner's own titles have Google TV's line: why it is shown, a rule and the logline.
    Fixture banner;
    kit::Hero featured = hero;
    featured.tag = "Trending";
    featured.wordmark = "wordmark-netflix";
    featured.more = "The ferry stops running; the light keeps burning.";
    featured.button = "Watch on Netflix";
    kit::heroText(banner.paint(), featured);
    const Node *tag = textAt(banner.scene, "Trending"),
               *logline = textAt(banner.scene, "A ferry pilot and a lighthouse keeper");
    const Node *serviceMark = nullptr;
    for (const auto &node : banner.scene.nodes)
        if (node.kind == Node::Kind::Image &&
            node.value.find("wordmark-netflix") != std::string::npos)
            serviceMark = &node;
    check(tag && logline && near(tag->rect.x, 116) && near(tag->rect.y, 494) &&
              near(logline->rect.y, 494) && logline->rect.x > tag->rect.x + tag->rect.w + 30 &&
              !textAt(banner.scene, "2021 · 7.2 · 98 min") && serviceMark &&
              !textAt(banner.scene, "Harbor Originals") &&
              near(serviceMark->rect.x, 116) &&
              near(serviceMark->rect.y + serviceMark->rect.h, 281 + 40) &&
              near(serviceMark->rect.h, 34) &&
              sameColor(serviceMark->color, kit::color::onSurface),
          "The banner's title has the service's white wordmark over it and \"Trending | logline\" "
          "under it");
    check(!textAt(banner.scene, "Watch on Netflix") &&
              !textAt(banner.scene, "The ferry stops running; the light keeps burning."),
          "The banner's description and button stay hidden until it has the focus");
    // With the focus Google TV's banner adds two lines of description and its button under them.
    Fixture expanded;
    featured.expand = 1;
    featured.buttonAlpha = 1;
    kit::heroText(expanded.paint(), featured);
    const Node *more = textAt(expanded.scene, "The ferry stops running; the light keeps burning."),
               *watch = textAt(expanded.scene, "Watch on Netflix");
    const Rect description = kit::geometry::heroDescription();
    check(more && watch && near(more->rect.x, 116) && near(more->rect.y, description.y) &&
              near(more->rect.h, 2 * kit::type::body.line) &&
              watch->rect.y > more->rect.y + more->rect.h &&
              shapeWith(expanded.scene,
                        {116, description.y + 2 * kit::type::body.line + 20,
                         watch->rect.w, 64},
                        kit::color::tabFocused),
          "The focused banner shows two lines of description and a light pill button under them");
    namespace home = kit::metrics::home;
    check(near(kit::geometry::heroSubtitle().y + kit::geometry::heroSubtitle().h -
                   kit::geometry::heroSource().y,
               home::heroSubtitle.y + home::heroSubtitle.h - home::heroMeta.y) &&
              near(home::heroSubtitle.y + home::heroSubtitle.h - home::heroMeta.y, 257),
          "The hero text block is 257 px tall, from the source row to the facts");
    const Node *art = nullptr;
    for (const auto &node : wall.scene.nodes)
        if (node.kind == Node::Kind::Image && node.value == "art:1")
            art = &node;
    check(art && art->value == "art:1" && sameRect(art->rect, {0, 0, 1920, 1080}) &&
              art->group.identity() && art->clipped,
          "The art covers the canvas and holds still, clipped to the screen");
    size_t gradients = 0;
    const Node *bottomScrim = nullptr;
    for (const auto &node : wall.scene.nodes)
        if (node.gradient.stops == 4) {
            ++gradients;
            bottomScrim = &node;
        }
    check(gradients == 1 && bottomScrim->gradient.angle == 0 &&
              sameColor(bottomScrim->gradient.color[0], kit::color::page) &&
              sameColor(bottomScrim->gradient.color[1], kit::color::page) &&
              bottomScrim->gradient.color[3].a == 0,
          "The bottom scrim has four stops, from the page colour to clear");

    Fixture scrolled;
    hero.stackY = -kit::layout::rowPitch;
    kit::hero(scrolled.paint(), hero);
    check(!textAt(scrolled.scene, "Harbor Lights") &&
              sameColor(scrolled.scene.nodes.back().color,
                        kit::focus::withAlpha(kit::color::page, .95f)),
          "Scrolled a full row the hero text is gone and the page-coloured veil is up at 95%");
    Fixture dimmed;
    hero.stackY = 0;
    hero.dim = 1;
    kit::heroWall(dimmed.paint(), hero);
    check(sameColor(dimmed.scene.nodes.back().color, kit::focus::withAlpha(kit::color::page, .55f)),
          "Behind Your apps the art stays, dimmed under a veil of the page colour");
    hero.dim = 0;
    Fixture lifted;
    hero.lift = kit::layout::picksLift;
    hero.textAlpha = .5f;
    kit::heroText(lifted.paint(), hero);
    check(near(textAt(lifted.scene, "Harbor Lights")->group.ty, -kit::layout::picksLift) &&
              near(textAt(lifted.scene, "Harbor Lights")->groupOpacity, .5f),
          "A focused top pick's lines move up by picksLift and fade with the banner's text");
    hero.lift = 0;
    hero.textAlpha = 1;
    Fixture mid;
    hero.stackY = -75;
    kit::heroText(mid.paint(), hero);
    check(near(textAt(mid.scene, "Harbor Lights")->group.ty, -37.5f) &&
              near(textAt(mid.scene, "Harbor Lights")->groupOpacity, .5f),
          "Halfway through the 150 px fade the title has moved by half the offset");

    Fixture dots;
    kit::Pagination pager;
    pager.count = 6;
    pager.current = {1, 0, 0, 0, 0, 0};
    pager.progress = .5f;
    kit::pagination(dots.paint(), pager);
    check(dots.scene.nodes.size() == 6 && shapeAt(dots.scene, kit::metrics::home::dot1) &&
              near(shapeAt(dots.scene, kit::metrics::home::dot1)->color.a, 1) &&
              shapeAt(dots.scene, {1652, 519, 16, 16}) &&
              near(shapeAt(dots.scene, {1652, 519, 16, 16})->color.a, .18f),
          "Pagination draws Google TV's dots, all 16 px: the title on show's white, the rest at "
          "18%");
    check(kit::heroProgress(0) == 0 && near(kit::heroProgress(6000), .5f) &&
              kit::heroProgress(20000) == 1,
          "Hero progress runs over the 12 s slide");
}

void rows() {
    Fixture dimmed;
    kit::Row row;
    row.index = 1;
    row.label = "Your apps";
    row.dim = 1;
    kit::row(dimmed.paint(), row);
    // The title is set at type::rowTitle's 52 px and scaled down to the row's size, 24 px at rest.
    const Node *label = textAt(dimmed.scene, "Your apps");
    const Rect drawn = label->group.apply(label->rect);
    const Rect record = kit::metrics::home::row2Label;
    check(near(drawn.x, record.x) && near(drawn.y, record.y) && near(drawn.w, record.w, .5f) &&
              near(drawn.h, kit::type::rowTitle.line * 24 / kit::type::rowTitle.px, .01f) &&
              near(label->group.sx, 24.0f / 52, 1e-5f) &&
              near(label->color.a, .5f),
          "A row label sits 251 px per row down at 24 px and dims to 50% when focus is elsewhere");
    Fixture risen;
    row.placed = true;
    row.y = kit::layout::rowsRisenTop;
    row.px = 52;
    row.grow = 1;
    kit::row(risen.paint(), row);
    const Node *grown = textAt(risen.scene, "Your apps");
    check(near(grown->group.apply(grown->rect).y, kit::layout::rowsRisenTop) &&
              near(grown->group.sx, 1, 1e-5f) &&
              sameColor(grown->color, kit::color::rowTitleFocused),
          "A placed row's title stands at its own top; the focused risen row's is 52 px, light and "
          "undimmed");
    row.placed = false;
    row.px = 24;
    row.grow = 0;
    row.scrollX = -324;
    Rect third = kit::rowItem(row, 3, 300, 24);
    check(near(third.x, 116 + 3 * 324 - 324) && near(third.y, 906),
          "Row items follow the row's scroll offset");
}

void statsAndSettings() {
    Fixture stat;
    kit::StatCard card;
    card.rest = kit::geometry::statCard(1);
    card.id = "stat:1";
    card.icon = "memory";
    card.label = "Memory";
    card.value = "1.7";
    card.unit = "of 2.9 GB";
    card.caption = "Swap off";
    card.bar = .5f;
    kit::statCard(stat.paint(), card);
    check(sameRect(stat.scene.nodes[0].rect, kit::metrics::live::stat2) &&
              stat.scene.nodes[0].radius == 20 && near(stat.scene.nodes[0].ring.inner, 1) &&
              sameRect(textAt(stat.scene, "1.7")->rect, {566, 436, 342, 72}) &&
              sameRect(textAt(stat.scene, "Swap off")->rect, kit::metrics::live::caption) &&
              textAt(stat.scene, "of 2.9 GB") && textAt(stat.scene, "Memory"),
          "A stat card is 398x292 with its value, caption and unit at the measured offsets");
    check(
        shapeAt(stat.scene, kit::metrics::live::bar) &&
            sameColor(shapeAt(stat.scene, kit::metrics::live::bar)->color, kit::color::surface3) &&
            shapeWith(stat.scene, {566, 600, 171, 8}, kit::color::accent),
        "The bar is an 8 px track with an accent fill to the fraction");
    check(shapeWith(stat.scene, {566, 370, 44, 44}, kit::color::surface3),
          "The icon well is 44 px at (28, 26)");
    Fixture hot;
    card.warn = true;
    card.bar = 1;
    kit::statCard(hot.paint(), card);
    check(sameColor(shapeAt(hot.scene, kit::metrics::live::bar)->color, kit::color::surface3) &&
              shapeWith(hot.scene, {566, 600, 342, 8}, kit::color::warn),
          "A warm bar turns the warn colour");
    Fixture days;
    card.bar = -1;
    card.days.assign(14, true);
    card.days[6] = false;
    kit::statCard(days.paint(), card);
    int grey = 0, up = 0;
    for (const auto &node : days.scene.nodes) {
        if (node.rect.h == 36 && node.radius == 4)
            ++(sameColor(node.color, kit::color::surface3) ? grey : up);
    }
    check(grey == 1 && up == 13, "The uptime strip has 14 days with one grey gap");
    Fixture spark;
    card.days.clear();
    for (int i = 0; i < 30; ++i)
        card.spark.push_back(.5f + .4f * std::sin(i * .5f));
    kit::statCard(spark.paint(), card);
    size_t columns = 0;
    for (const auto &node : spark.scene.nodes)
        if (node.rect.w == 6 && node.radius == 1.5f)
            ++columns;
    check(columns == 57, "The sparkline is drawn as 57 six-pixel columns");

    Fixture row;
    kit::SettingsRow setting;
    setting.rest = kit::geometry::settingsRow(1);
    setting.id = "setting:1";
    setting.icon = "display";
    setting.title = "Picture mode";
    setting.value = "Cinema";
    kit::settingsRow(row.paint(), setting);
    check(sameRect(row.scene.nodes[0].rect, kit::metrics::settings::row2) &&
              sameColor(row.scene.nodes[0].color, kit::color::surface1) &&
              shapeAt(row.scene, {98, 388 + 18, 48, 48}) &&
              sameRect(textAt(row.scene, "Picture mode")->rect, {166, 410, 420, 40}) &&
              textAt(row.scene, "Cinema")->align == Node::Align::Right &&
              near(textAt(row.scene, "Cinema")->rect.x + textAt(row.scene, "Cinema")->rect.w, 892),
          "A settings row is 840x84 with its well, title and right-aligned value");
    Fixture focused;
    setting.focus = 1;
    kit::settingsRow(focused.paint(), setting);
    check(near(focused.scene.nodes[0].group.sx, 1.02f, 1e-5f) &&
              near(focused.scene.nodes[0].group.tx, 80 - 80 * 1.02f, .01f) &&
              sameColor(textAt(focused.scene, "Picture mode")->color, kit::color::onFocus) &&
              sameColor(focused.scene.nodes[1].color, kit::color::focusFill),
          "A focused row grows 2% from its left edge and turns to the focus fill");
    Fixture toggled;
    setting.focus = 0;
    setting.toggle = true;
    setting.on = 1;
    kit::settingsRow(toggled.paint(), setting);
    check(shapeWith(toggled.scene, {824, 411, 68, 38}, kit::color::accent) &&
              shapeAt(toggled.scene, kit::focus::scaledAbout({824 + 36, 411 + 6, 26, 26}, 1.18f)) &&
              !textAt(toggled.scene, "Cinema"),
          "A toggle row shows the 68x38 switch with its knob at +36 grown 18%");
    Fixture off;
    kit::toggle(off.paint(), {824, 595, 68, 38}, 0);
    check(near(off.scene.nodes[0].ring.inner, 3) &&
              sameColor(off.scene.nodes[0].ring.color, kit::color::onSurface3) &&
              shapeAt(off.scene, {830, 601, 26, 26}) && off.scene.nodes.size() == 2,
          "An off toggle is a 3 px ring with the knob at +6");

    Fixture panel;
    kit::DetailPanel detail;
    detail.icon = "display";
    detail.title = "Picture mode";
    detail.body = "Choose how the screen looks.";
    detail.action = "Change";
    detail.chips = {"Cinema", "Game"};
    kit::detailPanel(panel.paint(), detail);
    check(sameRect(panel.scene.nodes[0].rect, kit::metrics::settings::detailPanel) &&
              panel.scene.nodes[0].radius == 28 &&
              shapeAt(panel.scene, kit::metrics::settings::panelIcon) &&
              sameRect(textAt(panel.scene, "Picture mode")->rect,
                       kit::metrics::settings::panelTitle) &&
              near(textAt(panel.scene, "Choose how the screen looks.")->rect.y,
                   kit::metrics::settings::panelText.y) &&
              textAt(panel.scene, "Cinema") && textAt(panel.scene, "Change") &&
              textAt(panel.scene, "OK"),
          "The detail panel lays out well, title, body, chips and hint like the prototype");
}

void loading() {
    // The loading indicator is a few dozen dots and a label, centred in its area and turning.
    kit::Loading loading;
    loading.area = {640, 288, 1200, 740};
    loading.label = "Loading...";
    Fixture start, turned, hidden;
    kit::loadingIndicator(start.paint(), loading);
    loading.phase = .25f;
    kit::loadingIndicator(turned.paint(), loading);
    loading.enter = {0, 0};
    kit::loadingIndicator(hidden.paint(), loading);
    size_t dots = countKind(start.scene, Node::Kind::Rectangle);
    check(dots >= 16 && dots <= 40 && start.scene.nodes.size() <= 41 &&
              turned.scene.nodes.size() == start.scene.nodes.size(),
          "The loading indicator is under forty nodes and needs no layer");
    float centreX = 0, centreY = 0, farthest = 0;
    for (const auto &node : start.scene.nodes)
        if (node.kind == Node::Kind::Rectangle) {
            centreX += node.rect.x + node.rect.w / 2;
            centreY += node.rect.y + node.rect.h / 2;
        }
    centreX /= static_cast<float>(dots);
    centreY /= static_cast<float>(dots);
    for (const auto &node : start.scene.nodes)
        if (node.kind == Node::Kind::Rectangle)
            farthest = std::max(farthest, std::fabs(node.rect.x + node.rect.w / 2 - 1240) +
                                              std::fabs(node.rect.y + node.rect.h / 2 - 636));
    check(textNode(start.scene, "Loading...") &&
              textNode(start.scene, "Loading...")->rect.y > 636 && farthest < 80 &&
              std::fabs(centreX - 1240) < 40 && std::fabs(centreY - 636) < 40,
          "The loading indicator sits in the middle of its area with the label under it");
    bool moved = false;
    for (size_t i = 0; i < start.scene.nodes.size() && i < turned.scene.nodes.size(); ++i)
        if (start.scene.nodes[i].kind == Node::Kind::Rectangle &&
            !near(start.scene.nodes[i].rect.x, turned.scene.nodes[i].rect.x, .5f))
            moved = true;
    check(moved, "The loading indicator's arc moves with the phase");
    check(hidden.scene.nodes.empty(), "A loading indicator that has not entered draws nothing");
}

void galleries() {
    // Every designed screen emits a bounded number of nodes, stays balanced and is reachable
    // from `--screen kit:<name>`.
    NoopServices services;
    for (const char *name : {"home", "apps", "live", "settings", "setup"}) {
        Scene scene;
        check(kit::designGallery(scene, name) && scene.nodes.size() > 30 &&
                  scene.nodes.size() < 700 && !scene.hits.empty(),
              "Each designed screen has a bounded frame of nodes");
        scene.shape({0, 0, 1, 1}, kit::color::ink);
        check(scene.nodes.back().group.identity() && scene.nodes.back().groupOpacity == 1 &&
                  !scene.nodes.back().clipped,
              "A designed screen leaves no group state pushed");
        Application app(services, std::string("kit:") + name);
        check(app.screenName() == std::string("kit:") + name && app.scene().nodes.size() > 30,
              "The application shows a designed screen by name");
        std::printf("kit:%s %zu nodes, %zu hits\n", name, scene.nodes.size() - 1,
                    scene.hits.size());
    }
    Scene unknown;
    check(!kit::designGallery(unknown, "nothing") && unknown.nodes.empty(),
          "An unknown screen name draws nothing");
}

void realGlyphs() {
    // The same components over the real atlas: text becomes glyph runs, icons atlas masks,
    // and measured tab widths land within a pixel or two of the prototype's DOM.
    std::string assets = NATIVETV_ASSETS;
    GlyphAtlas atlas(nullptr, assets + "/fonts");
    check(atlas.ok(), "The Google Sans Flex cuts load");
    TextRuns runs(atlas);
    IconAtlas icons(nullptr, assets + "/icons");
    kit::RunGlyphs glyphs(runs, atlas, icons);
    namespace home = kit::metrics::home;
    std::vector<Rect> tabs = kit::tabRects(glyphs, {"Home", "Apps", "Live"});
    check(sameRect(tabs[0], home::tabHome, 2) && sameRect(tabs[1], home::tabApps, 3) &&
              sameRect(tabs[2], home::tabLive, 4),
          "Google Sans Flex tab widths match the prototype's measured tabs");

    runs.beginFrame();
    Scene scene;
    kit::Paint paint{scene, glyphs};
    kit::PosterCard poster;
    poster.rest = kit::geometry::card(0, 0);
    poster.id = "card:0";
    poster.title = "Glass Season";
    kit::card(paint, poster);
    kit::AppCircle circle;
    circle.rest = kit::geometry::appCircle(1, 0);
    circle.icon = "play";
    kit::appCircle(paint, circle);
    check(countKind(scene, Node::Kind::Run) == 1 && countKind(scene, Node::Kind::Mask) == 1 &&
              countKind(scene, Node::Kind::Text) == 0,
          "Text and icons become glyph runs and atlas masks");
    const Node *run = nullptr;
    for (const auto &node : scene.nodes)
        if (node.kind == Node::Kind::Run)
            run = &node;
    check(run && run->run != 0 && sameRect(run->rect, {132, 777, 274, 40}),
          "A run starts at the text box's top-left");
    float width = glyphs.measure("Glass Season", kit::type::cardTitle);
    check(width > 110 && width < 240, "The poster title measures as a line of 28 px text");
}

void keys() {
    Fixture plain;
    kit::KeyCap key;
    key.rest = kit::geometry::keyRow(944, 340, 0, {"1"})[0];
    key.id = "key:0:0";
    key.label = "1";
    kit::keyCap(plain.paint(), key);
    check(sameRect(plain.scene.nodes[0].rect, kit::metrics::setup::key1) &&
              plain.scene.nodes[0].radius == 18 &&
              sameColor(plain.scene.nodes[0].color, kit::color::surface2) &&
              plain.scene.nodes[0].group.identity(),
          "A key is 80x74 with an 18 px radius on the raised surface");
    Fixture lit;
    key.focus = 1;
    kit::keyCap(lit.paint(), key);
    check(near(lit.scene.nodes[0].group.sx, 1.1f, 1e-5f) &&
              sameColor(lit.scene.nodes[1].color, kit::color::focusFill) &&
              sameColor(textAt(lit.scene, "1")->color, kit::color::onFocus),
          "A focused key grows 10% and turns light");
    Fixture pressed;
    key.press = 1;
    kit::keyCap(pressed.paint(), key);
    check(near(pressed.scene.nodes[0].group.sx, .94f, 1e-5f), "A pressed key dips to .94");
    Fixture done;
    kit::KeyCap finish;
    finish.rest = kit::metrics::setup::doneKey;
    finish.id = "key:4:1";
    finish.label = "Done";
    finish.icon = "check";
    finish.accent = true;
    kit::keyCap(done.paint(), finish);
    check(sameColor(done.scene.nodes[0].color, kit::color::accent) &&
              sameColor(textAt(done.scene, "Done")->color, kit::color::onAccent) &&
              countKind(done.scene, Node::Kind::Image) == 1,
          "The Done key is accent-filled with a check and its label");
}
} // namespace

int main() {
    try {
        tokens();
        motionTokens();
        focusMath();
        geometryAgainstMetrics();
        scrolling();
        homeLayout();
        cards();
        circlesAndTiles();
        barAndHero();
        rows();
        statsAndSettings();
        loading();
        keys();
        galleries();
        realGlyphs();

        Scene buttonPreview;
        kit::gallery(buttonPreview, "button");
        check(textNode(buttonPreview, "Default") && textNode(buttonPreview, "Focused") &&
                  textNode(buttonPreview, "Disabled") && hasHit(buttonPreview, "button:default") &&
                  hasHit(buttonPreview, "button:focused") &&
                  !hasHit(buttonPreview, "button:disabled"),
              "The kit button preview renders default, focused and disabled states");
        Scene drawerPreview;
        kit::gallery(drawerPreview, "drawer");
        check(textNode(drawerPreview, "Selected") && textNode(drawerPreview, "Focused") &&
                  textNode(drawerPreview, "Disabled") && hasHit(drawerPreview, "menu:0") &&
                  hasHit(drawerPreview, "menu:1") && hasHit(drawerPreview, "menu:2") &&
                  !hasHit(drawerPreview, "menu:3"),
              "The kit drawer preview demonstrates row states and disables pointer input");

        NoopServices services;
        Application buttonApp(services, "kit:button");
        buttonApp.click(1080, 260);
        check(buttonApp.screenName() == "kit:button",
              "The application controller ignores the disabled button preview");
        Application drawerApp(services, "kit:drawer");
        drawerApp.click(1552, 480);
        check(drawerApp.screenName() == "kit:drawer",
              "The application controller ignores the disabled drawer-row preview");

        std::puts("Native kit checks passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
