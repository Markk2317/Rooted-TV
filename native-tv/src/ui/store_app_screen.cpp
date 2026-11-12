// LG Content Store's app page, laid out as Google TV's Play Store page of an app (measured on a
// Chromecast with Google TV): the name in very large type at the top left, who makes it and its
// kind, the rating, two lines of the description and where it comes from, then Install; the app's
// banner at the top right, and its screenshots in a row under the page, which rises to them.
#include "ui/store_app_screen.hpp"
#include "domain/models.hpp"
#include "domain/say.hpp"
#include "ui/application.hpp"
#include "ui/home_layout.hpp"
#include "ui/kit/tuning.hpp"
#include "ui/optimistic.hpp"
#include "ui/overlay_routes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace nativetv {
namespace {
namespace layout = kit::layout;
using kit::easing;
using say::msg;

constexpr const char *kRead = "storeapp:read", *kAnswer = "storeapp.answer";
// While the install runs, its state is read this often.
constexpr double kPollMs = 1000;

Json vars(std::initializer_list<std::pair<const char *, std::string>> values) {
    Json result = Json::object();
    for (const auto &value : values)
        result.set(value.first, Json::text(value.second));
    return result;
}

std::string sizeText(double bytes) {
    if (bytes <= 0)
        return "";
    if (bytes < 1024 * 1024)
        return std::to_string(std::max(1L, std::lround(bytes / 1024))) + " KB";
    return std::to_string(std::lround(bytes / (1024 * 1024))) + " MB";
}
} // namespace

StoreAppScreen::StoreAppScreen(std::string id, Json card, bool embedded)
    : id_(std::move(id)), app_(card.isObject() ? std::move(card) : Json::object()), embedded_(embedded) {}

void StoreAppScreen::enter(ScreenHost &host) {
    Motion &motion = host.motion();
    motion.set("storeapp:in", 1);
    motion.tween("storeapp:in", 0, kit::motion::ms::base, easing(kit::motion::ease::out));
    motion.set("storeapp:vy", 0);
    read(host);
    focusMoved(host, true);
}

void StoreAppScreen::leave(ScreenHost &host) {
    host.services().cancel(kRead);
    host.motion().erasePrefix("storeapp:");
}

void StoreAppScreen::read(ScreenHost &host) {
    if (!host.services().read(kRead, Operation::StoreApp, fields({{"id", Json::text(id_)}}))) {
        failed_ = true;
        error_ = msg("tv.store.unavailable", "LG Content Store did not answer. Press OK to try again.");
    }
}

std::string StoreAppScreen::state() const {
    if (starting_)
        return "downloading";
    return install_["state"].string();
}

std::vector<StoreAppScreen::Button> StoreAppScreen::buttons() const {
    return {installed_ && state() != "downloading" && state() != "installing" ? Button::Open : Button::Install};
}

void StoreAppScreen::act(ScreenHost &host) {
    if (failed_ && !loaded_) {
        failed_ = false;
        error_.clear();
        read(host);
        return;
    }
    const std::string now = state();
    if (now == "downloading" || now == "installing")
        return;
    if (buttons()[button_] == Button::Open) {
        Write write;
        write.operation = Operation::Control;
        const std::string id = id_;
        write.body = [id](const Json &) {
            return fields({{"action", Json::text("launchApp")}, {"value", Json::text(id)}});
        };
        write.failure = msg("tv.apps.openFailed", "That did not open");
        host.optimistic().run("storeapp.open", write);
        return;
    }
    // The button says it is under way at once; the server's answer and then the polls follow. The
    // answer is left in the application's memory, since the page may be gone by then.
    error_.clear();
    Write write;
    write.operation = Operation::StoreInstall;
    const std::string id = id_;
    write.body = [id](const Json &) { return fields({{"id", Json::text(id)}}); };
    write.failure = msg("tv.store.installFailed", "The install could not start");
    ScreenHost *world = &host;
    write.done = [world, id](bool ok, const Json &reply) {
        world->memory()[kAnswer] = fields({{"id", Json::text(id)}, {"ok", Json::flag(ok)}, {"reply", reply}});
    };
    starting_ = host.optimistic().run("storeapp.install." + id_, write);
    host.invalidate();
}

bool StoreAppScreen::key(ScreenHost &host, Key key) {
    const size_t shots = app_["pictures"].size();
    if (onShots_) {
        if (key == Key::Left && shot_ > 0)
            --shot_;
        else if (key == Key::Right && shot_ + 1 < shots)
            ++shot_;
        else if (key == Key::Up)
            onShots_ = false;
    } else if (key == Key::Down && shots > 0) {
        onShots_ = true;
    } else if (key == Key::Left && button_ > 0) {
        --button_;
    } else if (key == Key::Right && button_ + 1 < buttons().size()) {
        ++button_;
    } else if (key == Key::Ok) {
        act(host);
    }
    focusMoved(host, false);
    host.invalidate();
    return true; // no bar to go up to
}

bool StoreAppScreen::back(ScreenHost &host) {
    if (onShots_) {
        onShots_ = false;
        focusMoved(host, false);
        return true;
    }
    if (embedded_)
        finished_ = true;
    else
        host.open(returnRoute(host.cameFrom()));
    return true;
}

void StoreAppScreen::click(ScreenHost &host, const std::string &id) {
    if (id == "storeapp:button") {
        onShots_ = false;
        button_ = 0;
        act(host);
    } else if (id.compare(0, 14, "storeapp:shot:") == 0) {
        onShots_ = true;
        shot_ = std::min(static_cast<size_t>(std::atoi(id.c_str() + 14)),
                         std::max<size_t>(app_["pictures"].size(), 1) - 1);
    }
    focusMoved(host, false);
    host.invalidate();
}

void StoreAppScreen::response(ScreenHost &host, const Response &response) {
    if (response.channel != kRead)
        return;
    const Json data = response.json();
    if (!response.ok() || !data["ok"].boolean()) {
        if (!loaded_) {
            failed_ = true;
            error_ = msg("tv.store.unavailable", "LG Content Store did not answer. Press OK to try again.");
        }
        // A poll that failed is tried again.
        if (state() == "downloading" || state() == "installing")
            pollAt_ = host.nowMs() + kPollMs;
        host.invalidate();
        return;
    }
    const std::string was = state();
    loaded_ = true;
    failed_ = false;
    if (!starting_)
        error_.clear();
    app_ = data["app"];
    installed_ = data["installed"].boolean();
    if (data["install"].valid() && !starting_)
        install_ = data["install"];
    const std::string now = state();
    if (now == "downloading" || now == "installing")
        pollAt_ = host.nowMs() + kPollMs;
    if ((was == "downloading" || was == "installing") && now == "installed") {
        host.toast(msg("tv.store.installed", "{name} is installed", vars({{"name", app_["title"].string()}})));
        // Your apps and the Apps tab list it from the next catalogue.
        host.services().read("catalogue", Operation::HomeCatalogue);
    }
    if (now == "failed" && was != "failed")
        error_ = install_["error"].string().empty() ? msg("tv.store.installFailed", "The install did not finish")
                                                    : install_["error"].string();
    button_ = std::min(button_, buttons().size() - 1);
    focusMoved(host, false);
    host.invalidate();
}

bool StoreAppScreen::update(ScreenHost &host) {
    // The install's answer: under way (the polls follow it), or refused (the layer has said why).
    auto answer = host.memory().find(kAnswer);
    if (answer != host.memory().end() && answer->second["id"].string() == id_) {
        const Json kept = answer->second;
        host.memory().erase(answer);
        starting_ = false;
        if (kept["ok"].boolean() && kept["reply"]["install"].isObject()) {
            install_ = kept["reply"]["install"];
            pollAt_ = host.nowMs() + kPollMs;
        } else {
            error_ = kept["reply"]["error"].string().empty()
                         ? msg("tv.store.installFailed", "The install could not start")
                         : kept["reply"]["error"].string();
        }
        host.invalidate();
    }
    if (pollAt_ > 0 && host.nowMs() >= pollAt_) {
        pollAt_ = 0;
        read(host);
    }
    return pollAt_ > 0 || starting_;
}

void StoreAppScreen::focusMoved(ScreenHost &host, bool jump) {
    Motion &motion = host.motion();
    const auto to = [&](const std::string &name, double value) {
        if (jump)
            motion.set(name, value);
        else
            motion.tween(name, value, value > 0 ? kit::motion::ms::cardFocus : kit::motion::ms::cardUnfocus,
                         easing(value > 0 ? kit::motion::ease::cardFocus : kit::motion::ease::cardUnfocus));
    };
    to("storeapp:b", onShots_ ? 0 : 1);
    for (size_t i = 0; i < app_["pictures"].size(); ++i)
        to("storeapp:s:" + std::to_string(i), onShots_ && i == shot_ ? 1 : 0);
    const double rise = onShots_ ? -layout::storeShotsRise : 0;
    if (jump)
        motion.set("storeapp:vy", rise);
    else
        motion.tween("storeapp:vy", rise, kit::motion::ms::base, easing(kit::motion::ease::inOut));
    const double scroll = kit::geometry::rowScrollTarget(static_cast<int>(shot_),
                                                         static_cast<int>(app_["pictures"].size()),
                                                         layout::cardLW, layout::cardLGap);
    if (jump)
        motion.set("storeapp:sx", scroll);
    else
        motion.tween("storeapp:sx", scroll, kit::motion::ms::base, easing(kit::motion::ease::out));
}

void StoreAppScreen::build(ScreenHost &host, kit::Paint paint) {
    const Motion &motion = host.motion();
    Scene &scene = paint.scene;
    scene.background = kit::color::ink;
    const float in = static_cast<float>(motion.value("storeapp:in"));
    const kit::Enter enter = {1 - in, 28 * in};
    const float rise = static_cast<float>(motion.value("storeapp:vy"));
    scene.pushOpacity(enter.alpha);
    scene.pushTransform(Transform::translate(0, rise + enter.dy));

    // The banner: the app's icon on its own colour, as the Play Store shows an app's.
    const Rect banner = {layout::storeBannerX, layout::storeBannerY, layout::storeBannerW, layout::storeBannerH};
    Card colour;
    colour.id = id_;
    colour.data = app_;
    scene.shape(banner, home::appColor(colour), kit::layout::cardR);
    const std::string icon = app_["icon"].string();
    const float side = layout::storeBannerIcon;
    if (!icon.empty())
        scene.picture({banner.x + (banner.w - side) / 2, banner.y + (banner.h - side) / 2, side, side}, 0, 0).value =
            icon;
    else
        paint.glyphs.icon(scene, {banner.x + (banner.w - side / 2) / 2, banner.y + (banner.h - side / 2) / 2,
                                  side / 2, side / 2},
                          "apps", kit::color::onSurface);

    const std::string title = app_["title"].string().empty() ? id_ : app_["title"].string();
    paint.glyphs.text(scene, {layout::homeX, layout::storeTitleTop, layout::storeTitleW, kit::type::storeTitle.line},
                      title, kit::type::storeTitle, kit::color::onSurface, Node::Align::Left, 1);

    // Who makes it, its kind and its price; under them its stars, out of five (LG's are out of ten).
    std::vector<std::string> facts;
    for (const char *field : {"seller", "category"})
        if (!app_[field].string().empty())
            facts.push_back(app_[field].string());
    if (loaded_)
        facts.push_back(app_["free"].boolean() ? msg("tv.store.free", "Free") : app_["price"].string());
    std::string line;
    for (const std::string &fact : facts)
        line += (line.empty() ? "" : "  \xc2\xb7  ") + fact;
    kit::factsLine(paint, {layout::homeX, layout::storeFactsTop, layout::storeTitleW, kit::type::detailsFacts.line},
                   line, kit::type::detailsFacts, kit::color::onSurface);
    const double stars = app_["rating"].number() / 2;
    if (stars > 0) {
        const float side = layout::storeStar, y = layout::storeRatingTop + (kit::type::detailsFacts.line - side) / 2;
        float x = layout::homeX;
        for (int i = 0; i < 5; ++i, x += side + 4)
            paint.glyphs.icon(scene, {x, y, side, side}, "star",
                              stars >= i + .5 ? kit::color::onSurface : kit::color::onSurface3);
        paint.glyphs.text(scene, {x + 8, layout::storeRatingTop, 200, kit::type::detailsFacts.line},
                          compactNumber(stars, 1), kit::type::detailsFacts, kit::color::onSurface);
    }

    const std::string description = app_["description"].string();
    if (!description.empty())
        paint.glyphs.text(scene, {layout::homeX, layout::storePlotTop, layout::detailsPlotW,
                                  kit::type::storeBody.line * 2},
                          description, kit::type::storeBody, kit::color::onSurface2, Node::Align::Left, 2);
    std::string source = msg("tv.store.source", "LG Content Store");
    if (!app_["version"].string().empty())
        source += "  \xc2\xb7  " + msg("tv.store.version", "Version {version}",
                                       vars({{"version", app_["version"].string()}}));
    if (!sizeText(app_["installedSize"].number()).empty())
        source += "  \xc2\xb7  " + sizeText(app_["installedSize"].number());
    paint.glyphs.text(scene, {layout::homeX, layout::storeSourceTop, layout::storeTitleW, kit::type::body.line},
                      source, kit::type::body, kit::color::onSurface3, Node::Align::Left, 1);

    // Install, or how far it has got, or Open: Google TV's pill, light with the focus.
    const std::string now = state();
    const double progress = install_["progress"].number();
    std::string label = msg("tv.store.install", "Install");
    if (now == "downloading")
        label = msg("tv.store.downloading", "Downloading {percent}%",
                    vars({{"percent", std::to_string(static_cast<int>(progress))}}));
    else if (now == "installing")
        label = msg("tv.store.installing", "Installing…");
    else if (buttons()[0] == Button::Open)
        label = msg("tv.store.open", "Open");
    else if (failed_ && !loaded_)
        label = msg("tv.details.retry", "Retry");
    const float focus = kit::focus::clamp01(static_cast<float>(motion.value("storeapp:b")));
    const float actionH = layout::detailsActionH;
    const float width = paint.glyphs.measure(label, kit::type::bodyL) + 2 * layout::detailsActionPad;
    const Rect rest = {layout::homeX, layout::storeActionsTop, width, actionH};
    const Color ink = kit::focus::mix(kit::color::onSurface2, kit::color::onFocus, focus);
    scene.pushTransform(Transform::scaleAbout(rest, .5f, .5f, 1 + .1f * focus));
    scene.shape(rest, kit::focus::mix({1, 1, 1, .1f}, kit::color::focusFill, focus), actionH / 2);
    if (now == "downloading" && progress > 0) {
        // How much has come, as a band along the pill's foot.
        const float band = 6;
        scene.shape({rest.x + actionH / 2, rest.y + actionH - band - 10, (rest.w - actionH) * static_cast<float>(progress / 100), band},
                     kit::focus::mix(kit::color::onSurface, kit::color::onFocus, focus), band / 2);
    }
    paint.glyphs.text(scene, {rest.x + layout::detailsActionPad, rest.y + (actionH - kit::type::bodyL.line) / 2,
                              width - 2 * layout::detailsActionPad + 4, kit::type::bodyL.line},
                      label, kit::type::bodyL, ink);
    scene.popTransform();
    scene.hits.push_back({rest, "storeapp:button"});
    if (!error_.empty())
        paint.glyphs.text(scene, {layout::homeX, layout::storeActionsTop + actionH + 16, layout::storeTitleW,
                                  kit::type::labelM.line},
                          error_, kit::type::labelM, kit::color::bad);

    // The screenshots, a row the page rises to.
    const Json pictures = app_["pictures"];
    if (pictures.size()) {
        kit::Row label2;
        label2.placed = true;
        label2.y = layout::storeShotsTop;
        home::RowLook up;
        up.up = 1; // the Play Store's row title, as large as Home's risen rows'
        label2.px = home::labelPx(up);
        label2.label = msg("tv.store.screenshots", "Screenshots");
        kit::row(paint, label2);
        const float scroll = static_cast<float>(motion.value("storeapp:sx"));
        for (size_t i = 0; i < pictures.size(); ++i) {
            kit::PosterCard shot;
            shot.rest = {layout::homeX + static_cast<float>(i) * (layout::cardLW + layout::cardLGap) + scroll,
                         layout::storeShotsTop + kit::type::rowTitle.line + layout::cardCaptionGap, layout::cardLW,
                         layout::cardLH};
            shot.id = "storeapp:shot:" + std::to_string(i);
            shot.image = pictures[i].string();
            shot.focus = static_cast<float>(motion.value("storeapp:s:" + std::to_string(i)));
            kit::card(paint, shot);
        }
    }
    scene.popTransform();
    scene.popOpacity();
}

Json StoreAppScreen::snapshot() const {
    Json value = Json::object();
    value.set("id", Json::text(id_));
    value.set("title", app_["title"]);
    value.set("installed", Json::flag(installed_));
    value.set("install", Json::text(state()));
    value.set("focus", Json::text(onShots_ ? "screenshots" : "button"));
    value.set("contentState", Json::text(loaded_ ? "ready" : failed_ ? "error" : "loading"));
    if (!error_.empty())
        value.set("error", Json::text(error_));
    return value;
}

std::unique_ptr<ScreenView> makeStoreAppScreen(const std::string &route) {
    const std::string prefix = "storeapp:";
    if (route.compare(0, prefix.size(), prefix) != 0 || route.size() == prefix.size())
        return nullptr;
    return std::unique_ptr<ScreenView>(new StoreAppScreen(route.substr(prefix.size()), Json(), false));
}
} // namespace nativetv
