// Google TV's account chooser, for the profiles: the wordmark, "Choose a profile", the profiles as
// round avatars with their names and one to add another, and Manage profile under them, which
// slides in Google TV's settings panel for the profile in use: its name (OK renames it) and its
// colour. The focused avatar grows 10 % with a white ring and a glow, as Google TV's does.
//
// Switching and adding post to the profile route the JavaScript page used, through the
// optimistic-update layer: the chosen avatar is the one in use at once, a spinner shows until the
// server answers, and a refusal gives it back and says why.
#include "domain/models.hpp"
#include "domain/say.hpp"
#include "ui/application.hpp"
#include "ui/home_layout.hpp"
#include "ui/kit/overlay.hpp"
#include "ui/kit/tuning.hpp"
#include "ui/optimistic.hpp"
#include "ui/overlay_routes.hpp"
#include "ui/profile_names.hpp"
#include "ui/screen.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace rootedtv {
namespace {
namespace layout = kit::layout;
using kit::easing;
using say::msg;
constexpr const char *kStats = "profiles:stats", *kGo = "profiles.go", *kStatsMemory = "quick.stats",
                     *kManageMemory = "profiles.manage";
// The avatar colours the server takes (kColors in service/home_profiles.cpp), and their names.
constexpr const char *kColours[] = {"#a8c7fa", "#f2b8b5", "#6dd58c", "#ffb74d", "#7fcfff", "#d0bcff"};
constexpr size_t kColourCount = sizeof(kColours) / sizeof(kColours[0]);
std::string colourName(size_t index) {
    switch (index) {
    case 0: return msg("tv.profiles.colour.blue", "Blue");
    case 1: return msg("tv.profiles.colour.pink", "Pink");
    case 2: return msg("tv.profiles.colour.green", "Green");
    case 3: return msg("tv.profiles.colour.orange", "Orange");
    case 4: return msg("tv.profiles.colour.lightBlue", "Light blue");
    default: return msg("tv.profiles.colour.purple", "Purple");
    }
}
constexpr float kPanelX = kit::canvas::width - layout::quickRight - layout::quickW;

Color hexColour(const std::string &value) {
    return kit::rgb(static_cast<unsigned>(std::strtoul(value.c_str() + 1, nullptr, 16)));
}

Color profileColor(const Json &person, const std::string &shown = "") {
    std::string value = shown.empty() ? person["color"].string() : shown;
    if (value.size() == 7 && value[0] == '#' &&
        value.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos)
        return hexColour(value);
    return home::hsl(static_cast<float>(home::hashOf(person["id"].string()) % 360), .5f, .40f);
}
std::string initialOf(const std::string &name) {
    if (name.empty())
        return "?";
    size_t end = 1;
    while (end < name.size() && (static_cast<unsigned char>(name[end]) & 0xc0) == 0x80)
        ++end;
    std::string letter = name.substr(0, end);
    if (letter.size() == 1)
        letter[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(letter[0])));
    return letter;
}

class ProfilesScreen final : public ScreenView {
  public:
    std::string name() const override {
        return "profiles";
    }
    bool hasBar() const override {
        return false;
    }
    bool paintsWall() const override {
        return true;
    }
    void enter(ScreenHost &host) override {
        size_t count = host.view().catalogue["profiles"].size();
        selected_ = std::min(activeIndex(host), count);
        active_ = shownActive(host);
        colour_ = colourIndexOf(host);
        // Back from renaming returns to the profile's settings, where Rename was.
        manage_ = host.memory().erase(kManageMemory) > 0 && activeIndex(host) < count;
        onButton_ = manage_;
        row_ = 0;
        host.motion().set("profiles:panel", manage_ ? 1 : 0);
        // The first profile is named after the TV: its model comes from the stats.
        if (!host.memory().count(kStatsMemory))
            statsGeneration_ = host.services().read(kStats, Operation::Metrics);
        Timeline timeline;
        Tween rise(1, 0, kit::motion::ms::base, easing(kit::motion::ease::out));
        for (size_t i = 0; i <= count + 1; ++i) {
            host.motion().set(id("e", i), 1);
            timeline.add(id("e", i),
                         rise.delayedBy(kit::motion::stagger::pageBase +
                                        kit::motion::stagger::pageItem * static_cast<double>(std::min<size_t>(i, 14))));
        }
        timeline.play(host.motion());
        focusMoved(host, true);
    }
    void leave(ScreenHost &host) override {
        host.services().cancel(kStats);
        host.memory().erase(kGo); // a switch still on its way no longer opens Home from here
        host.motion().erasePrefix("profiles:");
    }
    bool update(ScreenHost &host) override {
        // The list can change under the focus (a profile removed from a phone): the focus stays on
        // a profile or on Add, and Manage and its panel go with the active profile.
        const size_t count = host.view().catalogue["profiles"].size();
        if (selected_ > count || ((onButton_ || manage_) && activeIndex(host) >= count)) {
            selected_ = std::min(selected_, count);
            onButton_ = onButton_ && activeIndex(host) < count;
            if (manage_ && activeIndex(host) >= count)
                closeManage(host);
            focusMoved(host, true);
        }
        busy_ = host.optimistic().pending("profiles.active") || host.optimistic().pending("profiles.add");
        active_ = shownActive(host);
        return busy_;
    }
    bool key(ScreenHost &host, Key key) override {
        if (manage_)
            return panelKey(host, key);
        size_t count = host.view().catalogue["profiles"].size();
        switch (key) {
        case Key::Left:
            if (!onButton_ && selected_)
                --selected_;
            break;
        case Key::Right:
            if (!onButton_ && selected_ < count)
                ++selected_;
            break;
        case Key::Down:
            // The avatars, then Manage profile while a profile is in use.
            if (activeIndex(host) < count)
                onButton_ = true;
            break;
        case Key::Up:
            onButton_ = false;
            break;
        case Key::Ok:
            if (onButton_)
                openManage(host);
            else
                choose(host);
            return true;
        default:
            return false;
        }
        focusMoved(host, false);
        return true;
    }
    bool back(ScreenHost &host) override {
        if (manage_)
            closeManage(host);
        else
            leaveFor(host);
        return true;
    }
    void click(ScreenHost &host, const std::string &hit) override {
        if (hit == "profiles:close") {
            closeManage(host);
            return;
        }
        if (hit.compare(0, 14, "profiles:item:") == 0) {
            row_ = std::min(static_cast<size_t>(std::atoi(hit.c_str() + 14)), kColourCount);
            focusMoved(host, false);
            panelKey(host, Key::Ok);
            return;
        }
        onButton_ = hit == "profiles:manage";
        if (hit == "profiles:add")
            selected_ = host.view().catalogue["profiles"].size();
        else if (hit.compare(0, 11, "profiles:p:") == 0)
            selected_ = static_cast<size_t>(std::atoi(hit.c_str() + 11));
        focusMoved(host, false);
        if (onButton_)
            openManage(host);
        else
            choose(host);
    }
    void response(ScreenHost &host, const Response &response) override {
        if (response.channel != kStats || response.generation < statsGeneration_)
            return;
        Json data = response.json();
        if (response.ok() && data.valid()) {
            host.memory()[kStatsMemory] = data;
            host.invalidate();
        }
    }
    void build(ScreenHost &host, kit::Paint paint) override {
        Scene &scene = paint.scene;
        std::vector<Json> people = host.view().catalogue["profiles"].items();
        const size_t count = people.size();
        scene.shape({0, 0, kit::canvas::width, kit::canvas::height}, kit::color::chooser);
        paint.glyphs.text(scene, {0, layout::chooserMarkY, kit::canvas::width, kit::type::chooserMark.line}, "Rooted TV",
                          kit::type::chooserMark, kit::color::chooserMark, Node::Align::Center);
        paint.glyphs.text(scene, {0, layout::chooserTitleY, kit::canvas::width, kit::type::chooserTitle.line},
                          msg("tv.profiles.title", "Choose a profile"), kit::type::chooserTitle,
                          kit::color::chooserTitle, Node::Align::Center);
        const float pitch = layout::chooserAvatar + layout::chooserGap;
        const float left = (kit::canvas::width - static_cast<float>(count + 1) * pitch + layout::chooserGap) / 2;
        for (size_t i = 0; i <= count; ++i)
            drawAvatar(host, paint, i, i < count ? &people[i] : nullptr,
                       {left + static_cast<float>(i) * pitch, layout::chooserAvatarY, layout::chooserAvatar,
                        layout::chooserAvatar});
        if (activeIndex(host) < count)
            drawManage(host, paint, count);
        drawPanel(host, paint);
    }
    Json snapshot() const override {
        Json value = Json::object();
        value.set("selected", Json::numeric(static_cast<double>(selected_)));
        value.set("onButton", Json::flag(onButton_));
        value.set("manage", Json::flag(manage_));
        value.set("row", Json::numeric(static_cast<double>(row_)));
        value.set("colour", Json::text(kColours[std::min(colour_, kColourCount - 1)]));
        value.set("busy", Json::flag(busy_));
        value.set("active", Json::text(active_));
        return value;
    }

  private:
    static std::string id(const char *name, size_t index) {
        return std::string("profiles:") + name + ":" + std::to_string(index);
    }
    static size_t activeIndex(ScreenHost &host) {
        std::vector<Json> people = host.view().catalogue["profiles"].items();
        for (size_t i = 0; i < people.size(); ++i)
            if (people[i]["active"].boolean())
                return i;
        return people.size();
    }

    // ---- The chooser ---------------------------------------------------------------------------

    // An avatar (a profile's colour and initial, or Add's dark disc and icon) with its name under
    // it; the focus grows both about the disc's centre.
    void drawAvatar(ScreenHost &host, kit::Paint paint, size_t index, const Json *person, Rect rest) {
        Scene &scene = paint.scene;
        const Motion &motion = host.motion();
        const float focus = kit::focus::clamp01(static_cast<float>(motion.value(id("f", index))));
        const float hidden = static_cast<float>(motion.value(id("e", index)));
        const float scale = 1 + (kit::motion::scale::chooser - 1) * focus;
        const std::string model = modelOf(host);
        scene.pushOpacity(1 - hidden);
        scene.pushTransform(Transform::translate(0, 28 * hidden));
        scene.pushTransform(Transform::scaleAbout(rest, .5f, .5f, scale));
        const Color tint = person ? profileColor(*person, shownColour(host, *person)) : kit::color::chooserAdd;
        Node &disc = scene.shape(rest, tint, rest.w / 2);
        if (person)
            disc.gradient = Gradient::between(145, tint, kit::focus::mix(tint, kit::color::ink, .45f));
        if (focus > 0) {
            disc.ring = {layout::chooserRing, 0, kit::focus::withAlpha(kit::color::chooserRing, focus)};
            disc.shadow = {0, 0, layout::chooserGlow, 0,
                           kit::focus::withAlpha(kit::alpha::chooserGlow, kit::alpha::chooserGlow.a * focus)};
        }
        if (!person)
            paint.glyphs.icon(scene,
                              {rest.x + (rest.w - layout::chooserAddIcon) / 2, rest.y + (rest.h - layout::chooserAddIcon) / 2,
                               layout::chooserAddIcon, layout::chooserAddIcon},
                              "personadd", kit::color::chooserAddIcon);
        else
            paint.glyphs.text(scene, {rest.x, rest.y + (rest.h - kit::type::display.line) / 2, rest.w,
                                      kit::type::display.line},
                              initialOf(shownProfileName(host.optimistic(), *person, model)), kit::type::display,
                              {1, 1, 1, 1}, Node::Align::Center);
        const std::string label =
            person ? shownProfileName(host.optimistic(), *person, model) : msg("tv.profiles.add", "Add profile");
        paint.glyphs.text(scene,
                          {rest.x + (rest.w - layout::chooserNameW) / 2, layout::chooserNameY, layout::chooserNameW,
                           kit::type::chooserName.line},
                          label, kit::type::chooserName, kit::color::chooserName, Node::Align::Center, 2);
        // A change on its way: a spinner where the answer is awaited.
        const bool active = person && (*person)["id"].string() == shownActive(host);
        if ((active && host.optimistic().pending("profiles.active")) ||
            (!person && host.optimistic().pending("profiles.add")))
            kit::spinner(paint, {rest.x + rest.w - 44, rest.y + rest.h - 44, 32, 32}, host.nowMs(),
                         kit::color::onSurface);
        scene.popTransform();
        scene.popTransform();
        scene.popOpacity();
        scene.hits.push_back({kit::focus::scaledAbout(rest, scale),
                              person ? "profiles:p:" + std::to_string(index) : std::string("profiles:add")});
    }
    // Google TV's Manage accounts: a dark pill under the avatars that turns light with the focus.
    void drawManage(ScreenHost &host, kit::Paint paint, size_t count) {
        Scene &scene = paint.scene;
        const float focus = kit::focus::clamp01(static_cast<float>(host.motion().value("profiles:fb")));
        const float hidden = static_cast<float>(host.motion().value(id("e", count + 1)));
        const std::string label = msg("tv.profiles.manage", "Manage profile");
        const float gap = layout::chooserButtonPad / 2,
                    width = 2 * layout::chooserButtonPad + layout::chooserButtonIcon + gap +
                            paint.glyphs.measure(label, kit::type::title);
        const Rect button = {(kit::canvas::width - width) / 2, layout::chooserButtonY, width, layout::chooserButtonH};
        scene.pushOpacity(1 - hidden);
        scene.pushTransform(Transform::translate(0, 28 * hidden));
        scene.pushTransform(Transform::scaleAbout(button, .5f, .5f, 1 + (kit::motion::scale::chooserButton - 1) * focus));
        scene.shape(button, kit::focus::mix(kit::color::chooserButton, kit::color::searchButton, focus), button.h / 2);
        const Color ink = kit::focus::inkAt(kit::color::searchButton, kit::color::onFocus, focus);
        const float x = button.x + layout::chooserButtonPad, middle = button.y + button.h / 2;
        paint.glyphs.icon(scene,
                          {x, middle - layout::chooserButtonIcon / 2, layout::chooserButtonIcon, layout::chooserButtonIcon},
                          "manageaccounts", ink);
        paint.glyphs.text(scene,
                          {x + layout::chooserButtonIcon + gap, middle - kit::type::title.line / 2, width,
                           kit::type::title.line},
                          label, kit::type::title, ink);
        scene.popTransform();
        scene.popTransform();
        scene.popOpacity();
        scene.hits.push_back({button, "profiles:manage"});
    }

    // ---- The profile's settings --------------------------------------------------------------

    // Google TV's settings panel at the right, over the chooser dimmed as Quick settings dims Home:
    // the profile's name, then its colours.
    void drawPanel(ScreenHost &host, kit::Paint paint) {
        const Motion &motion = host.motion();
        const float open = kit::focus::clamp01(static_cast<float>(motion.value("profiles:panel")));
        std::vector<Json> people = host.view().catalogue["profiles"].items();
        const size_t at = activeIndex(host);
        if (open <= .002f || at >= people.size())
            return;
        Scene &scene = paint.scene;
        kit::veil(paint, kit::alpha::quickVeil.a * open);
        const Rect panel = {kPanelX, layout::quickTop, layout::quickW, layout::quickH};
        for (Rect around : {Rect{0, 0, panel.x, kit::canvas::height},
                            Rect{panel.x + panel.w, 0, kit::canvas::width - panel.x - panel.w, kit::canvas::height},
                            Rect{panel.x, 0, panel.w, panel.y},
                            Rect{panel.x, panel.y + panel.h, panel.w, kit::canvas::height - panel.y - panel.h}})
            scene.hits.push_back({around, "profiles:close"});
        scene.pushTransform(Transform::translate((1 - open) * layout::quickW, 0));
        scene.pushOpacity(open);
        scene.shape(panel, kit::color::quickPage, layout::quickR);
        const std::string name = shownProfileName(host.optimistic(), people[at], modelOf(host)),
                          current = shownColour(host, people[at]);
        paint.glyphs.text(scene, {panel.x, panel.y + layout::quickTitleTop, panel.w, kit::type::quickTitle.line}, name,
                          kit::type::quickTitle, kit::color::quickTitle, Node::Align::Center);
        for (size_t i = 0; i <= kColourCount; ++i) {
            kit::PanelItem item;
            item.at = {panel.x + layout::quickPad,
                       panel.y + layout::quickItemsTop + static_cast<float>(i) * layout::quickItemPitch,
                       layout::quickW - 2 * layout::quickPad, layout::quickItemH};
            item.id = "profiles:item:" + std::to_string(i);
            item.focus = static_cast<float>(motion.value(id("r", i)));
            item.scale = 1 + (kit::motion::scale::quickTile - 1) * kit::focus::clamp01(item.focus);
            if (i == 0) {
                item.title = msg("tv.profiles.rename", "Rename");
                item.line = name;
            } else {
                item.title = colourName(i - 1);
                item.swatch = hexColour(kColours[i - 1]);
                item.mark = kit::PanelItem::Mark::Radio;
                item.on = current == kColours[i - 1] ? 1 : 0;
            }
            kit::panelItem(paint, item);
        }
        scene.popOpacity();
        scene.popTransform();
    }
    bool panelKey(ScreenHost &host, Key key) {
        switch (key) {
        case Key::Up:
            row_ = row_ ? row_ - 1 : 0;
            break;
        case Key::Down:
            row_ = std::min(row_ + 1, kColourCount);
            break;
        case Key::Left:
            closeManage(host);
            return true;
        case Key::Right:
            return true;
        case Key::Ok:
            if (row_ == 0) {
                host.memory()[kManageMemory] = Json::flag(true);
                host.open("rename");
            } else {
                colour_ = row_ - 1;
                chooseColour(host);
            }
            return true;
        default:
            return false;
        }
        if (row_ > 0)
            colour_ = row_ - 1;
        focusMoved(host, false);
        return true;
    }
    void openManage(ScreenHost &host) {
        manage_ = true;
        onButton_ = true;
        row_ = 0;
        host.motion().tween("profiles:panel", 1, kit::motion::ms::quick, easing(kit::motion::ease::quick));
        focusMoved(host, false);
    }
    void closeManage(ScreenHost &host) {
        manage_ = false;
        colour_ = colourIndexOf(host);
        host.motion().tween("profiles:panel", 0, kit::motion::ms::quick, easing(kit::motion::ease::quick));
        focusMoved(host, false);
    }

    void focusMoved(ScreenHost &host, bool jump) {
        size_t count = host.view().catalogue["profiles"].size();
        auto glide = [&](const std::string &name, double to, int ms, kit::Bezier curve) {
            if (jump)
                host.motion().set(name, to);
            else
                host.motion().tween(name, to, ms, easing(curve));
        };
        for (size_t i = 0; i <= count; ++i)
            glide(id("f", i), !onButton_ && !manage_ && i == selected_ ? 1 : 0, kit::motion::ms::focus,
                  kit::motion::ease::out);
        glide("profiles:fb", onButton_ && !manage_ ? 1 : 0, kit::motion::ms::focus, kit::motion::ease::out);
        for (size_t i = 0; i <= kColourCount; ++i)
            glide(id("r", i), manage_ && i == row_ ? 1 : 0, kit::motion::ms::quickFocus, kit::motion::ease::quickFocus);
        host.invalidate();
    }
    // The colour on screen for a profile: a change that is still on its way shows at once.
    static std::string shownColour(ScreenHost &host, const Json &person) {
        return host.optimistic().text("profile.colour." + person["id"].string(), person["color"].string());
    }
    static size_t colourIndexOf(ScreenHost &host) {
        std::vector<Json> people = host.view().catalogue["profiles"].items();
        const size_t at = activeIndex(host);
        if (at >= people.size())
            return 0;
        const std::string now = shownColour(host, people[at]);
        for (size_t i = 0; i < kColourCount; ++i)
            if (now == kColours[i])
                return i;
        return 0;
    }
    void chooseColour(ScreenHost &host) {
        std::vector<Json> people = host.view().catalogue["profiles"].items();
        const size_t at = activeIndex(host);
        if (at >= people.size())
            return;
        const std::string id = people[at]["id"].string(), colour = kColours[std::min(colour_, kColourCount - 1)];
        if (shownColour(host, people[at]) == colour)
            return;
        Write write;
        write.operation = Operation::SetProfile;
        write.body = [id](const Json &target) {
            Json body = Json::object();
            body.set("action", Json::text("rename"));
            body.set("id", Json::text(id));
            body.set("color", target);
            return body;
        };
        write.readChannel = "catalogue";
        write.readOperation = Operation::HomeCatalogue;
        write.failure = msg("tv.profiles.colourFailed", "The colour could not be saved");
        host.optimistic().set("profile.colour." + id, Json::text(colour), Json::text(people[at]["color"].string()), write);
        host.invalidate();
    }
    static std::string modelOf(ScreenHost &host) {
        auto found = host.memory().find(kStatsMemory);
        return found == host.memory().end() ? "" : found->second["device"]["model"].string();
    }
    static std::string serverActive(ScreenHost &host) {
        for (const auto &person : host.view().catalogue["profiles"].items())
            if (person["active"].boolean())
                return person["id"].string();
        return "";
    }
    static std::string shownActive(ScreenHost &host) {
        return host.optimistic().text("profiles.active", serverActive(host));
    }
    static Write profileWrite(ScreenHost &host, const std::string &token) {
        Write write;
        write.operation = Operation::SetProfile;
        write.readChannel = "catalogue";
        write.readOperation = Operation::HomeCatalogue;
        host.memory()[kGo] = Json::text(token);
        return write;
    }
    void choose(ScreenHost &host) {
        std::vector<Json> people = host.view().catalogue["profiles"].items();
        if (selected_ >= people.size()) {
            Write write = profileWrite(host, "add");
            write.body = [](const Json &) {
                Json body = Json::object();
                body.set("action", Json::text("add"));
                body.set("switch", Json::flag(true));
                return body;
            };
            // A new profile has its own layout: Home starts again from its first tab.
            write.done = [&host](bool ok, const Json &) {
                if (!ok)
                    return;
                host.toast(msg("tv.profiles.added", "Profile added"));
                if (host.memory()[kGo].string() == "add")
                    host.open("home");
            };
            host.optimistic().run("profiles.add", write);
        } else if (people[selected_]["id"].string() == shownActive(host)) {
            leaveFor(host);
            return;
        } else {
            const std::string chosen = people[selected_]["id"].string();
            Write write = profileWrite(host, chosen);
            write.body = [](const Json &target) {
                Json body = Json::object();
                body.set("action", Json::text("switch"));
                body.set("id", target);
                return body;
            };
            write.done = [&host, chosen](bool ok, const Json &) {
                if (ok && host.memory()[kGo].string() == chosen)
                    host.open("home");
            };
            host.optimistic().set("profiles.active", Json::text(chosen), Json::text(serverActive(host)), write);
        }
        busy_ = host.optimistic().pending("profiles.active") || host.optimistic().pending("profiles.add");
        active_ = shownActive(host);
        host.invalidate();
    }
    void leaveFor(ScreenHost &host) {
        host.open(returnRoute(host.cameFrom()));
    }

    size_t selected_ = 0, row_ = 0, colour_ = 0;
    bool onButton_ = false, manage_ = false, busy_ = false;
    std::string active_; // the profile in use as shown, for the snapshot
    unsigned statsGeneration_ = 0;
};
} // namespace

std::unique_ptr<ScreenView> makeProfilesScreen(const std::string &route) {
    return route == "profiles" ? std::unique_ptr<ScreenView>(new ProfilesScreen()) : nullptr;
}
} // namespace rootedtv
