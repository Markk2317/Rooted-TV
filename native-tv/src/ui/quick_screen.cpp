// The Quick settings panel, after Google TV's (launcherx's DashboardActivity, read off a Chromecast
// with Google TV): a floating panel at the right with the date and the clock over a gear (All
// settings, which opens Settings) and the profile; a grid of tiles under them, the volume and the
// light as tiles across the grid that Left and Right adjust, the picture mode, sound output, input
// and sleep timer with what is set now, the screensaver and Screen off; and Recent apps as a wide
// card. Their lists (picture modes, outputs, inputs, sleep times) take the panel's place, as Google
// TV's sleep timer does.
//
// Opening it shows each value last read at once (kept in the application between openings) or a
// spinner where there is none yet, and reads the TV behind it. Every change goes through the
// optimistic-update layer (optimistic.hpp): the value is on screen the moment the key is pressed,
// the writes leave at a limited pace and carry the newest value, and a refusal puts the value back
// and says so. Volume and light are absolute targets (/api/control "volume" and "backlight"), so a
// held key from 5 to 95 shows 5, 6, 7 ... 95 and nothing a late reply or a re-read brings can move
// it backwards.
#include "domain/models.hpp"
#include "domain/say.hpp"
#include "ui/application.hpp"
#include "ui/kit/overlay.hpp"
#include "ui/kit/tuning.hpp"
#include "ui/optimistic.hpp"
#include "ui/overlay_routes.hpp"
#include "ui/profile_names.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace nativetv {
namespace {
namespace layout = kit::layout;
namespace qcolor = kit::color;
using kit::easing;
using kit::focus::mix;
using say::msg;

constexpr float kPanelW = layout::quickW, kPanelH = layout::quickH,
                kPanelX = kit::canvas::width - layout::quickRight - layout::quickW, kPanelY = layout::quickTop,
                kWideW = layout::quickW - 2 * layout::quickPad;
constexpr int kLightStep = 5;
constexpr double kRetryMs = 3000;
constexpr const char *kStats = "quick:stats", *kNow = "quick:now";
constexpr const char *kStatsMemory = "quick.stats", *kNowMemory = "quick.now";

enum class Level { Main, Picture, Sound, Input, Sleep };
const char *levelName(Level level) {
    switch (level) {
    case Level::Picture:
        return "picture";
    case Level::Sound:
        return "sound";
    case Level::Input:
        return "input";
    case Level::Sleep:
        return "sleep";
    default:
        return "quick";
    }
}
// The item of the main page that opens a level.
const char *rowOpening(Level level) {
    switch (level) {
    case Level::Picture:
        return "picture";
    case Level::Sound:
        return "sound";
    case Level::Input:
        return "input";
    default:
        return "sleep";
    }
}
const char *kSleep[] = {"off", "10", "30", "60", "90", "120"};

double clamp(double value, double low, double high) {
    return std::max(low, std::min(high, value));
}
Rect onCanvas(Rect panel) {
    return {kPanelX + panel.x, kPanelY + panel.y, panel.w, panel.h};
}
Write controlWrite(const char *action) {
    Write write;
    write.operation = Operation::Control;
    const std::string name = action;
    write.body = [name](const Json &target) {
        Json body = Json::object();
        body.set("action", Json::text(name));
        body.set("value", target);
        return body;
    };
    write.readChannel = kStats;
    write.readOperation = Operation::Metrics;
    return write;
}
// Items whose value is read from the TV: they wait for the first read.
bool readsStats(const std::string &id) {
    return id == "volume" || id == "light" || id == "picture" || id == "sound" || id == "input" ||
           id == "sleep";
}
// "Wed 11 Nov", as the panel's head dates the clock.
std::string dateText(const ViewState &state) {
    const std::string days[] = {msg("tv.date.sun", "Sun"), msg("tv.date.mon", "Mon"), msg("tv.date.tue", "Tue"),
                                msg("tv.date.wed", "Wed"), msg("tv.date.thu", "Thu"), msg("tv.date.fri", "Fri"),
                                msg("tv.date.sat", "Sat")};
    const std::string months[] = {msg("tv.date.jan", "Jan"), msg("tv.date.feb", "Feb"), msg("tv.date.mar", "Mar"),
                                  msg("tv.date.apr", "Apr"), msg("tv.date.may", "May"), msg("tv.date.jun", "Jun"),
                                  msg("tv.date.jul", "Jul"), msg("tv.date.aug", "Aug"), msg("tv.date.sep", "Sep"),
                                  msg("tv.date.oct", "Oct"), msg("tv.date.nov", "Nov"), msg("tv.date.dec", "Dec")};
    return msg("tv.date.short", "{weekday} {day} {month}",
               fields({{"weekday", Json::text(days[((state.weekday % 7) + 7) % 7])},
                       {"day", Json::text(std::to_string(state.day))},
                       {"month", Json::text(months[((state.month % 12) + 12) % 12])}}));
}
std::string clockText(const ViewState &state) {
    char text[16];
    if (state.catalogue["layout"]["clockFormat"].string() == "12h")
        std::snprintf(text, sizeof(text), "%d:%02d", state.hour % 12 ? state.hour % 12 : 12, state.minute);
    else
        std::snprintf(text, sizeof(text), "%02d:%02d", state.hour, state.minute);
    return text;
}

class QuickScreen final : public ScreenView {
    struct Row {
        enum class Kind { Plain, Slider, Choice } kind = Kind::Plain;
        // Google TV's shapes: the head's round buttons, a tile across the grid, a tile in a
        // column, the wide card under the grid, and an item of a list.
        enum class Shape { Button, Wide, Tile, Card, Item } shape = Shape::Item;
        std::string id, icon, title, raw, trail;
        bool go = false, dim = false, on = false;
        int line = 0, column = -1; // the grid's line, and its column (-1 across both)
        Rect rest;                 // in the panel
    };

  public:
    explicit QuickScreen(Level level = Level::Main) : level_(level) {}

    std::string name() const override {
        return level_ == Level::Main ? "quick" : std::string("quick:") + levelName(level_);
    }
    bool hasBar() const override {
        return false;
    }
    bool paintsWall() const override {
        return true;
    }

    void enter(ScreenHost &host) override {
        // Google TV's panel comes in as it leaves: from its own width to the right, fading, on the
        // curve that is nearly there in a third of its time.
        host.motion().set("quick:x", kPanelW);
        host.motion().tween("quick:x", 0, kit::motion::ms::quick, easing(kit::motion::ease::quick));
        host.motion().set("quick:page", 1);
        // What was read last time shows at once; the read behind it brings today's values.
        auto &memory = host.memory();
        if (memory.count(kStatsMemory) && memory[kStatsMemory].isObject()) {
            stats_ = memory[kStatsMemory];
            loaded_ = true;
        }
        if (memory.count(kNowMemory) && memory[kNowMemory].isObject())
            nowPort_ = memory[kNowMemory]["port"].string();
        statsGeneration_ = host.services().read(kStats, Operation::Metrics);
        nowGeneration_ = host.services().read(kNow, Operation::HomeNow);
        syncShown(host);
        rebuild(host);
        focus_ = indexOf("volume", focus_);
        focusMoved(host, true);
    }
    void leave(ScreenHost &host) override {
        // Writes in flight are the layer's and finish without this page.
        for (const char *channel : {kStats, kNow})
            host.services().cancel(channel);
        host.motion().erasePrefix("quick:");
    }
    bool update(ScreenHost &host) override {
        double now = host.nowMs();
        if (closing_ && !closed_ && !host.motion().moving("quick:x")) {
            closed_ = true;
            host.open(destination_);
            return false;
        }
        if (retryAt_ > 0 && now >= retryAt_) {
            retryAt_ = 0;
            statsGeneration_ = host.services().read(kStats, Operation::Metrics);
        }
        if (needsRebuild_) {
            needsRebuild_ = false;
            rebuild(host);
        }
        syncShown(host);
        // The spinner turns while anything is being read.
        return !loaded_ || retryAt_ > 0;
    }

    // ---- Keys --------------------------------------------------------------------------------

    bool key(ScreenHost &host, Key key) override {
        if (closing_)
            return true;
        switch (key) {
        case Key::Up:
        case Key::Down:
            move(host, key == Key::Up ? -1 : 1);
            return true;
        case Key::PageUp:
        case Key::PageDown:
            for (int i = 0; i < 6; ++i)
                move(host, key == Key::PageUp ? -1 : 1);
            return true;
        case Key::Left:
        case Key::Right:
            sideways(host, key == Key::Right ? 1 : -1);
            return true;
        case Key::Ok:
            press(host);
            return true;
        default:
            return false;
        }
    }
    bool back(ScreenHost &host) override {
        if (closing_)
            return true;
        if (level_ != Level::Main)
            toMain(host);
        else
            close(host, returnRoute(host.cameFrom()));
        return true;
    }
    void click(ScreenHost &host, const std::string &id) override {
        if (closing_)
            return;
        if (id == "quick:close") {
            close(host, returnRoute(host.cameFrom()));
            return;
        }
        if (id.compare(0, 10, "quick:row:") != 0)
            return;
        size_t index = static_cast<size_t>(std::atoi(id.c_str() + 10));
        if (index >= rows_.size())
            return;
        focus_ = index;
        focusMoved(host, false);
        press(host);
    }

    // ---- Replies -----------------------------------------------------------------------------

    // What the TV said. The layer decides what shows while a change of ours is pending, so what is
    // read is simply taken.
    void response(ScreenHost &host, const Response &response) override {
        const std::string &channel = response.channel;
        Json data = response.json();
        bool ok = response.ok() && data.valid() && (!data.has("ok") || data["ok"].boolean());
        if (channel == "catalogue") {
            needsRebuild_ = true; // the inputs and the profile names come from it
        } else if (channel == kStats && response.generation >= statsGeneration_) {
            if (ok) {
                stats_ = data;
                host.memory()[kStatsMemory] = data;
                loaded_ = true;
                readFailed_ = false;
                retryAt_ = 0;
                needsRebuild_ = true;
            } else if (!loaded_) {
                // Nothing to show yet and the TV did not answer: dashes, and another try.
                loaded_ = true;
                readFailed_ = true;
                retryAt_ = host.nowMs() + kRetryMs;
                needsRebuild_ = true;
            }
        } else if (channel == kNow && response.generation >= nowGeneration_) {
            nowPort_ = ok && data["kind"].string() == "input" ? data["port"].string() : "";
            if (ok)
                host.memory()[kNowMemory] = data;
            needsRebuild_ = true;
        } else
            return;
        host.invalidate();
    }

    // ---- Drawing -----------------------------------------------------------------------------

    void build(ScreenHost &host, kit::Paint paint) override {
        const Motion &motion = host.motion();
        Scene &scene = paint.scene;
        float slide = static_cast<float>(motion.value("quick:x"));
        float open = kit::focus::clamp01(1 - slide / kPanelW);
        syncShown(host);
        if (host.cameFrom() == "home" && !host.view().home.tabs.empty())
            drawHome(paint, Frame{host.view(), motion, host.nowMs()});
        else
            kit::ambientWall(paint, 1, host.view().home.art.wallTint);
        // Google TV dims the page behind its panel to 40 %.
        kit::veil(paint, kit::alpha::quickVeil.a * open);
        const Rect panel = {kPanelX, kPanelY, kPanelW, kPanelH};
        for (Rect around : {Rect{0, 0, kPanelX, kit::canvas::height},
                            Rect{panel.x + panel.w, 0, kit::canvas::width - panel.x - panel.w, kit::canvas::height},
                            Rect{panel.x, 0, panel.w, panel.y},
                            Rect{panel.x, panel.y + panel.h, panel.w, kit::canvas::height - panel.y - panel.h}})
            scene.hits.push_back({around, "quick:close"});

        scene.pushTransform(Transform::translate(slide, 0));
        scene.pushOpacity(open);
        float page = static_cast<float>(motion.value("quick:bg"));
        scene.shape(panel, mix(qcolor::quick, qcolor::quickPage, page), layout::quickR);
        scene.hits.push_back({panel, "quick:panel"});
        scene.pushOpacity(static_cast<float>(motion.value("quick:page")));
        if (level_ == Level::Main)
            drawMain(host, paint);
        else
            drawList(host, paint);
        scene.popOpacity();
        scene.popOpacity();
        scene.popTransform();
    }
    Json snapshot() const override {
        Json value = Json::object();
        value.set("level", Json::text(levelName(level_)));
        value.set("selected", Json::numeric(static_cast<double>(focus_)));
        if (focus_ < rows_.size())
            value.set("focus", Json::text(rows_[focus_].title));
        value.set("volume", Json::numeric(shownVolume_));
        value.set("muted", Json::flag(shownMuted_));
        value.set("light", Json::numeric(shownLight_));
        value.set("sleep", Json::text(shownSleep_));
        value.set("picture", Json::text(shownPicture_));
        value.set("writing", Json::flag(writing_));
        value.set("loading", Json::flag(!loaded_));
        value.set("readFailed", Json::flag(readFailed_));
        value.set("closing", Json::flag(closing_));
        value.set("rows", Json::numeric(static_cast<double>(rows_.size())));
        value.set("volumeBackwards", Json::numeric(volumeBackwards_));
        value.set("lightBackwards", Json::numeric(lightBackwards_));
        return value;
    }

  private:
    // ---- What is shown -----------------------------------------------------------------------

    bool volumeKnown() const {
        return stats_["volume"].isNumber();
    }
    bool lightKnown() const {
        return stats_["picture"]["backlight"].isNumber();
    }
    // The values on screen, as the layer decides them: the wanted one while a change is pending.
    double volume(ScreenHost &host) {
        return host.optimistic().number("quick.volume", stats_["volume"].number(0));
    }
    double light(ScreenHost &host) {
        return host.optimistic().number("quick.light", stats_["picture"]["backlight"].number(0));
    }
    bool muted(ScreenHost &host) {
        return host.optimistic().flag("quick.mute", stats_["muted"].boolean());
    }
    std::string sleepValue(ScreenHost &host) {
        return host.optimistic().text("quick.sleep", stats_["sleepTimer"].string());
    }
    std::string pictureValue(ScreenHost &host) {
        return host.optimistic().text("quick.picture", stats_["picture"]["mode_raw"].string());
    }
    std::string soundValue(ScreenHost &host) {
        return host.optimistic().text("quick.sound", stats_["sound"]["output_raw"].string());
    }
    // For the snapshot and for the held-key checks: what is shown now, and whether it ever went
    // against the key that is held.
    void syncShown(ScreenHost &host) {
        double volumeNow = volume(host), lightNow = light(host);
        if (volumeDirection_ && (volumeNow - shownVolume_) * volumeDirection_ < 0)
            ++volumeBackwards_;
        if (lightDirection_ && (lightNow - shownLight_) * lightDirection_ < 0)
            ++lightBackwards_;
        shownVolume_ = volumeNow;
        shownLight_ = lightNow;
        shownMuted_ = muted(host);
        shownSleep_ = sleepValue(host);
        shownPicture_ = pictureValue(host);
        writing_ = false;
        for (const char *key : {"quick.volume", "quick.light", "quick.mute", "quick.sleep", "quick.picture", "quick.sound"})
            writing_ = writing_ || host.optimistic().pending(key);
    }
    std::string modeLabel(const std::string &raw) const {
        for (const auto &mode : stats_["picture"]["modes"].items())
            if (mode["value"].string() == raw)
                return mode["label"].string();
        return raw == stats_["picture"]["mode_raw"].string() ? stats_["picture"]["mode"].string() : raw;
    }
    std::string outputLabel(const std::string &raw) const {
        for (const auto &output : stats_["sound"]["outputs"].items())
            if (output["id"].string() == raw)
                return output["name"].string();
        return raw == stats_["sound"]["output_raw"].string() ? stats_["sound"]["output"].string() : raw;
    }
    static std::string sleepLabel(const std::string &value) {
        return value == "off" ? msg("tv.quick.off", "Off")
                              : msg("tv.quick.minutes", "{n} min", fields({{"n", Json::text(value)}}));
    }
    std::string title() const {
        switch (level_) {
        case Level::Picture:
            return msg("tv.quick.pictureMode", "Picture mode");
        case Level::Sound:
            return msg("tv.quick.sound", "Sound output");
        case Level::Input:
            return msg("tv.quick.input", "Input");
        case Level::Sleep:
            return msg("tv.quick.sleep", "Sleep timer");
        default:
            return msg("tv.quick.title", "Quick settings");
        }
    }
    // The cards of the Inputs tab: the devices in the layout's order, less the ones hidden.
    std::vector<Card> inputs(const ScreenHost &host) const {
        std::vector<CardRow> rows = homeRows("inputs", host.view().catalogue, host.view().discover,
                                             host.view().guide);
        return rows.empty() ? std::vector<Card>() : rows[0].cards;
    }
    bool inputOn(const Card &card) const {
        return !nowPort_.empty() && card.data["port"].string() == nowPort_;
    }
    static std::string inputTitle(const Card &card) {
        return card.data["unused"].boolean()
                   ? msg("tv.quick.port", "HDMI {port}", fields({{"port", card.data["port"]}}))
                   : card.title;
    }
    std::string activeProfile(const ScreenHost &host) const {
        std::string model = stats_["device"]["model"].string();
        for (const auto &person : host.view().catalogue["profiles"].items())
            if (person["active"].boolean())
                return profileName(person, model);
        return "";
    }
    std::string initial(const ScreenHost &host) const {
        std::string name = activeProfile(host);
        if (name.empty())
            return "M";
        size_t end = 1;
        while (end < name.size() && (static_cast<unsigned char>(name[end]) & 0xc0) == 0x80)
            ++end;
        return name.substr(0, end);
    }
    // What an item says of its value: a tile's second line, the volume's and the light's number.
    std::string trail(ScreenHost &host, const Row &row) {
        if (row.kind != Row::Kind::Plain)
            return row.trail;
        if (readFailed_ && readsStats(row.id))
            return "—";
        if (row.id == "picture") {
            std::string raw = pictureValue(host);
            return raw.empty() ? "—" : modeLabel(raw);
        }
        if (row.id == "sound") {
            std::string raw = soundValue(host);
            return raw.empty() ? stats_["audio_output"].string("—") : outputLabel(raw);
        }
        if (row.id == "input") {
            for (const Card &card : inputs(host))
                if (inputOn(card))
                    return inputTitle(card);
            return "";
        }
        if (row.id == "sleep") {
            std::string value = sleepValue(host);
            return value.empty() ? "—" : sleepLabel(value);
        }
        if (row.id == "profile")
            return activeProfile(host);
        return "";
    }
    bool rowOn(ScreenHost &host, const Row &row) {
        if (level_ == Level::Picture)
            return row.raw == pictureValue(host);
        if (level_ == Level::Sound)
            return row.raw == soundValue(host);
        if (level_ == Level::Sleep)
            return row.raw == sleepValue(host);
        return row.on;
    }
    size_t indexOf(const std::string &id, size_t fallback) const {
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].id == id)
                return i;
        return fallback;
    }
    void rebuild(ScreenHost &host) {
        std::vector<Row> rows;
        auto add = [&](Row::Kind kind, const std::string &id, const std::string &icon,
                       const std::string &title, const std::string &raw = "", bool go = false,
                       bool dim = false, bool on = false, const std::string &trail = "") -> Row & {
            Row row;
            row.kind = kind;
            row.id = id;
            row.icon = icon;
            row.title = title;
            row.raw = raw;
            row.go = go;
            row.dim = dim;
            row.on = on;
            row.trail = trail;
            rows.push_back(row);
            return rows.back();
        };
        const Json &s = stats_;
        if (level_ == Level::Main) {
            const float pad = layout::quickPad, top = layout::quickGridTop, pitch = layout::quickTilePitch;
            const float right = pad + kWideW - layout::quickTileW;
            auto place = [&](Row &row, Row::Shape shape, int line, int column, Rect rest) {
                row.shape = shape;
                row.line = line;
                row.column = column;
                row.rest = rest;
            };
            auto tile = [&](Row &row, int line, int column) {
                place(row, Row::Shape::Tile, line, column,
                      {column ? right : pad, top + (line - 1) * pitch, layout::quickTileW, layout::quickTileH});
            };
            bool oled = s["oled"].valid() && (s["oled"].isObject() || s["oled"].boolean());
            place(add(Row::Kind::Plain, "settings", "settings", msg("tv.quick.settings", "All settings")),
                  Row::Shape::Button, 0, 0,
                  {layout::quickGearX, layout::quickButtonTop, layout::quickButton, layout::quickButton});
            place(add(Row::Kind::Plain, "profile", "profile", msg("tv.quick.profile", "Profile"), "", true),
                  Row::Shape::Button, 0, 1,
                  {layout::quickAvatarX, layout::quickButtonTop, layout::quickButton, layout::quickButton});
            place(add(Row::Kind::Slider, "volume", "volume", msg("tv.quick.volume", "Volume")), Row::Shape::Wide, 1,
                  -1, {pad, top, kWideW, layout::quickTileH});
            place(add(Row::Kind::Slider, "light", "light",
                      oled ? msg("tv.quick.oledLight", "OLED light") : msg("tv.quick.backlight", "Backlight")),
                  Row::Shape::Wide, 2, -1, {pad, top + pitch, kWideW, layout::quickTileH});
            tile(add(Row::Kind::Plain, "picture", "picture", msg("tv.quick.pictureMode", "Picture mode"), "", true), 3, 0);
            tile(add(Row::Kind::Plain, "sound", "tv", msg("tv.quick.sound", "Sound output"), "", true), 3, 1);
            tile(add(Row::Kind::Plain, "input", "hdmi", msg("tv.quick.input", "Input"), "", true), 4, 0);
            tile(add(Row::Kind::Plain, "sleep", "bedtime", msg("tv.quick.sleep", "Sleep timer"), "", true), 4, 1);
            tile(add(Row::Kind::Plain, "saver", "", msg("tv.quick.screensaver", "Screensaver")), 5, 0);
            tile(add(Row::Kind::Plain, "off", "power", msg("tv.quick.screenOff", "Screen off")), 5, 1);
            place(add(Row::Kind::Plain, "recents", "apps", msg("tv.quick.recents", "Recent apps"), "", true),
                  Row::Shape::Card, 6, -1,
                  {pad, top + 4 * pitch + layout::quickTileH + layout::quickCardsGap, kWideW, layout::quickCardH});
        } else if (level_ == Level::Picture) {
            for (const auto &mode : s["picture"]["modes"].items())
                add(Row::Kind::Choice, mode["value"].string(), "picture", mode["label"].string(),
                    mode["value"].string());
        } else if (level_ == Level::Sound) {
            for (const auto &output : s["sound"]["outputs"].items())
                add(Row::Kind::Choice, output["id"].string(), "tv", output["name"].string(),
                    output["id"].string());
        } else if (level_ == Level::Input) {
            for (const Card &card : inputs(host))
                add(Row::Kind::Choice, card.id, "hdmi", inputTitle(card), card.id, false,
                    card.data["unused"].boolean(), inputOn(card),
                    card.data["unused"].boolean() ? msg("tv.quick.nothingConnected", "Nothing connected") : "");
        } else if (level_ == Level::Sleep) {
            for (const char *value : kSleep)
                add(Row::Kind::Choice, value, "bedtime", sleepLabel(value), value);
        }
        if (rows.empty())
            add(Row::Kind::Plain, "", "", msg("tv.quick.nothing", "Nothing to choose from"), "", false, true);
        // A list's items stand one under another, as Google TV's sleep timer lists its times.
        if (level_ != Level::Main)
            for (size_t i = 0; i < rows.size(); ++i) {
                rows[i].line = static_cast<int>(i);
                rows[i].rest = {layout::quickPad, layout::quickItemsTop + static_cast<float>(i) * layout::quickItemPitch,
                                kWideW, layout::quickItemH};
            }
        rows_ = rows;
        focus_ = std::min(focus_, rows_.size() - 1);
        host.invalidate();
    }
    bool rowDim(const Row &row) const {
        if (row.dim)
            return true;
        if (!loaded_ && readsStats(row.id) && level_ == Level::Main)
            return true;
        return (row.id == "volume" && !volumeKnown()) || (row.id == "light" && !lightKnown());
    }

    // ---- Motion ------------------------------------------------------------------------------

    void focusMoved(ScreenHost &host, bool jump) {
        Motion &motion = host.motion();
        for (size_t i = 0; i < rows_.size(); ++i) {
            double to = i == focus_ ? 1 : 0;
            std::string id = "quick:f:" + std::to_string(i);
            if (jump)
                motion.set(id, to);
            else
                motion.tween(id, to, kit::motion::ms::quickFocus, easing(kit::motion::ease::quickFocus));
        }
        if (focus_ < rows_.size() && rows_[focus_].column >= 0)
            column_ = rows_[focus_].column;
        // A list longer than the panel scrolls to keep the focused item two from the foot.
        double target = 0;
        if (level_ != Level::Main && !rows_.empty()) {
            const float pitch = layout::quickItemPitch, room = kPanelH - layout::quickItemsTop;
            const float visible = std::floor(room / pitch);
            float overflow = std::max(0.0f, static_cast<float>(rows_.size()) * pitch + layout::quickPad - room);
            target = -clamp((static_cast<double>(focus_) - (visible - 2)) * pitch, 0, overflow);
        }
        if (jump)
            motion.set("quick:sy", target);
        else
            motion.spring("quick:sy", target, kit::springParams(kit::motion::spring::list));
        host.invalidate();
    }
    // A new page: the panel's colour turns to the lists' and the items come in over it.
    void playPage(ScreenHost &host) {
        host.motion().set("quick:page", 0);
        host.motion().tween("quick:page", 1, kit::motion::ms::quickFocus, easing(kit::motion::ease::quickFocus));
        host.motion().tween("quick:bg", level_ == Level::Main ? 0 : 1, kit::motion::ms::quickFocus,
                            easing(kit::motion::ease::quickFocus));
    }

    // ---- Drawing the pages -------------------------------------------------------------------

    struct Look {
        Rect rest, at; // in the panel, and on the canvas
        float focus, scale;
    };
    Look lookOf(const Motion &motion, size_t index, float scroll = 0) const {
        Look look;
        look.rest = rows_[index].rest;
        look.rest.y += scroll;
        look.focus = kit::focus::clamp01(static_cast<float>(motion.value("quick:f:" + std::to_string(index))));
        float target = rows_[index].shape == Row::Shape::Button ? kit::motion::scale::quickButton
                                                                 : kit::motion::scale::quickTile;
        look.scale = 1 + (target - 1) * look.focus;
        look.at = onCanvas(look.rest);
        return look;
    }
    // A focusable surface: its rest tint, Google TV's light blue as it takes the focus, the lift of
    // its 4 dp elevation, and the hit that clicks it.
    void surface(Scene &scene, const Look &look, size_t index, float radius) {
        scene.shape(look.at, kit::alpha::quickTile, radius);
        if (look.focus > 0) {
            Node &lit = scene.shape(look.at, kit::focus::withAlpha(qcolor::quickFocus, look.focus), radius);
            lit.shadow = {0, 4, 16, 0, {0, 0, 0, .32f * look.focus}};
        }
        scene.hits.push_back({kit::focus::scaledAbout(look.at, look.scale), "quick:row:" + std::to_string(index)});
    }
    void drawMain(ScreenHost &host, kit::Paint paint) {
        Scene &scene = paint.scene;
        const ViewState &state = host.view();
        paint.glyphs.text(scene, onCanvas({layout::quickMarkX - 6, layout::quickDateTop, 300, kit::type::quickDate.line}),
                          dateText(state), kit::type::quickDate, kit::alpha::quickDate);
        paint.glyphs.text(scene, onCanvas({layout::quickMarkX - 6, layout::quickClockTop, 300, kit::type::quickClock.line}),
                          clockText(state), kit::type::quickClock, kit::alpha::quickInk);
        for (size_t i = 0; i < rows_.size(); ++i) {
            switch (rows_[i].shape) {
            case Row::Shape::Button:
                drawButton(host, paint, i);
                break;
            case Row::Shape::Wide:
                drawSlider(host, paint, i);
                break;
            case Row::Shape::Card:
                drawCard(host, paint, i);
                break;
            default:
                drawTile(host, paint, i);
            }
        }
        // Google TV signs its panel at the foot; a read that failed says so there instead.
        const Rect foot = onCanvas({layout::quickMarkX, layout::quickMarkTop, kWideW - 24, kit::type::quickMark.line});
        if (readFailed_)
            paint.glyphs.text(scene, foot, msg("tv.quick.noAnswer", "The TV did not answer. Trying again…"),
                              kit::type::quickCardSub, qcolor::warn);
        else
            paint.glyphs.text(scene, foot, "Rooted TV", kit::type::quickMark, kit::alpha::quickInk);
    }
    void drawButton(ScreenHost &host, kit::Paint paint, size_t index) {
        Scene &scene = paint.scene;
        const Row &row = rows_[index];
        Look look = lookOf(host.motion(), index);
        Transform scale = kit::focus::transformAt(look.at, look.scale);
        scene.pushTransform(scale);
        const float round = look.at.h / 2;
        if (row.id == "profile") {
            // The profile's face inside a ring that lights as it takes the focus.
            if (look.focus > 0)
                scene.shape(look.at, kit::focus::withAlpha(qcolor::quickLabel, .6f * look.focus), round);
            Rect face = {look.at.x + 4, look.at.y + 4, look.at.w - 8, look.at.h - 8};
            scene.shape(face, qcolor::quickWell, face.h / 2).gradient = kit::avatarGradient();
            paint.glyphs.text(scene, {face.x, face.y + (face.h - kit::type::quickInitial.line) / 2, face.w,
                                      kit::type::quickInitial.line},
                              initial(host), kit::type::quickInitial, qcolor::onSurface, Node::Align::Center);
        } else {
            scene.shape(look.at, kit::alpha::quickTile, round);
            if (look.focus > 0)
                scene.shape(look.at, kit::focus::withAlpha(qcolor::quickFocus, look.focus), round);
            paint.glyphs.icon(scene, {look.at.x + 10, look.at.y + 10, look.at.w - 20, look.at.h - 20}, row.icon,
                              mix(kit::alpha::quickInk, qcolor::quickIconOnFocus, look.focus));
        }
        scene.popTransform();
        scene.hits.push_back({kit::focus::scaledAbout(look.at, look.scale), "quick:row:" + std::to_string(index)});
        // Under the focused button, what it opens: "All settings", or the profile's name.
        if (look.focus > .004f) {
            const float width = 220, centre = look.at.x + look.at.w / 2;
            std::string label = row.id == "profile" ? trail(host, row) : row.title;
            if (label.empty())
                label = row.title;
            paint.glyphs.text(scene, onCanvas({centre - kPanelX - width / 2, layout::quickButtonLabelTop, width,
                                               kit::type::quickLabel.line}),
                              label, kit::type::quickLabel, kit::focus::withAlpha(qcolor::quickLabel, look.focus),
                              Node::Align::Center);
        }
    }
    // The second line, or the spinner while it is read.
    bool waiting(const Row &row) const {
        return !loaded_ && readsStats(row.id) && level_ == Level::Main;
    }
    void drawTile(ScreenHost &host, kit::Paint paint, size_t index) {
        Scene &scene = paint.scene;
        const Row &row = rows_[index];
        Look look = lookOf(host.motion(), index);
        Transform scale = kit::focus::transformAt(look.at, look.scale);
        scene.pushTransform(scale);
        scene.pushOpacity(rowDim(row) && !waiting(row) ? .6f : 1);
        surface(scene, look, index, layout::quickTileR);
        const Color ink = mix(kit::alpha::quickInk, qcolor::quickOnFocus, look.focus);
        float textX = layout::quickTileTextX;
        if (row.icon.empty()) {
            textX = layout::quickTileIconX + 2; // Google TV's screensaver tile has no icon
        } else {
            const float inset = (layout::quickTileIcon - 32) / 2;
            paint.glyphs.icon(scene,
                              {look.at.x + layout::quickTileIconX + inset, look.at.y + (look.at.h - 32) / 2, 32, 32},
                              row.icon, mix(kit::alpha::quickInk, qcolor::quickIconOnFocus, look.focus));
        }
        const float width = look.at.w - textX - layout::quickTileIconX;
        std::string second = waiting(row) ? "" : trail(host, row);
        if (row.go && !second.empty()) {
            paint.glyphs.text(scene, {look.at.x + textX, look.at.y + 22, width, kit::type::quickTile.line}, row.title,
                              kit::type::quickTile, ink);
            paint.glyphs.text(scene, {look.at.x + textX, look.at.y + 60, width, kit::type::quickTileSub.line}, second,
                              kit::type::quickTileSub, kit::focus::withAlpha(ink, .6f));
        } else {
            paint.glyphs.text(scene,
                              {look.at.x + textX, look.at.y + (look.at.h - kit::type::quickTile.line) / 2,
                               width - (waiting(row) ? 36 : 0), kit::type::quickTile.line},
                              row.title, kit::type::quickTile, ink);
        }
        if (waiting(row))
            kit::spinner(paint, {look.at.x + look.at.w - 20 - 28, look.at.y + (look.at.h - 28) / 2, 28, 28},
                         host.nowMs(), ink);
        scene.popOpacity();
        scene.popTransform();
    }
    void drawSlider(ScreenHost &host, kit::Paint paint, size_t index) {
        Scene &scene = paint.scene;
        const Row &row = rows_[index];
        Look look = lookOf(host.motion(), index);
        Transform scale = kit::focus::transformAt(look.at, look.scale);
        scene.pushTransform(scale);
        scene.pushOpacity(rowDim(row) && !waiting(row) ? .6f : 1);
        surface(scene, look, index, layout::quickTileR);
        const Color ink = mix(kit::alpha::quickInk, qcolor::quickOnFocus, look.focus);
        const float inset = (layout::quickTileIcon - 32) / 2;
        paint.glyphs.icon(scene, {look.at.x + layout::quickTileIconX + inset, look.at.y + (look.at.h - 32) / 2, 32, 32},
                          row.icon, mix(kit::alpha::quickInk, qcolor::quickIconOnFocus, look.focus));
        const float middle = look.at.y + look.at.h / 2, right = look.at.x + look.at.w - layout::quickTileIconX;
        paint.glyphs.text(scene, {look.at.x + layout::quickTileTextX, middle - kit::type::quickTile.line / 2, 200,
                                  kit::type::quickTile.line},
                          row.title, kit::type::quickTile, ink);
        if (waiting(row)) {
            kit::spinner(paint, {right - 28, middle - 14, 28, 28}, host.nowMs(), ink);
        } else {
            const bool isVolume = row.id == "volume";
            std::string value = isVolume ? volumeText() : lightKnown() ? std::to_string(static_cast<int>(shownLight_)) : "—";
            const float valueW = 84;
            paint.glyphs.text(scene, {right - valueW, middle - kit::type::quickTile.line / 2, valueW, kit::type::quickTile.line},
                              value, kit::type::quickTile, ink, Node::Align::Right);
            // The bar follows the value with a short glide, so each key repeat moves it a little.
            double level = isVolume ? (volumeKnown() ? shownVolume_ : 0) : (lightKnown() ? shownLight_ : 0);
            std::string barId = std::string("quick:bar:") + row.id;
            host.motion().tween(barId, clamp(level, 0, 100), 90, easing(kit::motion::ease::out));
            float amount = static_cast<float>(clamp(host.motion().value(barId), 0, 100) / 100);
            if (isVolume && shownMuted_)
                amount = 0;
            const float trackH = layout::quickTrackH;
            Rect track = {right - valueW - 16 - layout::quickTrackW, middle - trackH / 2, layout::quickTrackW, trackH};
            scene.shape(track, mix(kit::focus::withAlpha(kit::alpha::quickInk, .3f),
                                   kit::focus::withAlpha(qcolor::quickOnFocus, .14f), look.focus),
                        trackH / 2);
            if (amount > 0)
                scene.shape({track.x, track.y, std::max(trackH, track.w * amount), track.h},
                            mix(qcolor::quickOnWell, qcolor::quickWell, look.focus), trackH / 2);
        }
        scene.popOpacity();
        scene.popTransform();
    }
    void drawCard(ScreenHost &host, kit::Paint paint, size_t index) {
        Scene &scene = paint.scene;
        const Row &row = rows_[index];
        Look look = lookOf(host.motion(), index);
        Transform scale = kit::focus::transformAt(look.at, look.scale);
        scene.pushTransform(scale);
        surface(scene, look, index, layout::quickTileR);
        const float icon = layout::quickCardIcon;
        Rect well = {look.at.x + layout::quickCardIconX, look.at.y + (look.at.h - icon) / 2, icon, icon};
        scene.shape(well, mix(kit::alpha::quickTile, Color{0, 0, 0, .07f}, look.focus), icon / 2);
        paint.glyphs.icon(scene, {well.x + 16, well.y + 16, icon - 32, icon - 32}, row.icon,
                          mix(kit::alpha::quickInk, qcolor::quickWell, look.focus));
        paint.glyphs.text(scene,
                          {look.at.x + layout::quickCardTextX, look.at.y + (look.at.h - kit::type::quickCard.line) / 2,
                           look.at.w - layout::quickCardTextX - 24, kit::type::quickCard.line},
                          row.title, kit::type::quickCard, mix(kit::alpha::quickInk, qcolor::quickOnFocus, look.focus));
        scene.popTransform();
    }
    void drawList(ScreenHost &host, kit::Paint paint) {
        Scene &scene = paint.scene;
        const Motion &motion = host.motion();
        paint.glyphs.text(scene, onCanvas({0, layout::quickTitleTop, kPanelW, kit::type::quickTitle.line}), title(),
                          kit::type::quickTitle, qcolor::quickTitle, Node::Align::Center);
        const float scroll = static_cast<float>(motion.value("quick:sy"));
        const Rect view = onCanvas({0, layout::quickItemsTop - 16, kPanelW, kPanelH - layout::quickItemsTop + 16});
        scene.pushClip(view);
        for (size_t i = 0; i < rows_.size(); ++i) {
            const Row &row = rows_[i];
            Look look = lookOf(motion, i, scroll);
            if (look.at.y + look.at.h < view.y - 20 || look.at.y > view.y + view.h + 20)
                continue;
            kit::PanelItem item;
            item.at = look.at;
            item.id = "quick:row:" + std::to_string(i);
            item.title = row.title;
            item.line = row.trail;
            item.focus = look.focus;
            item.scale = look.scale;
            item.dim = row.dim;
            if (row.kind == Row::Kind::Choice) {
                item.mark = kit::PanelItem::Mark::Radio;
                item.on = rowOn(host, row) ? 1.0f : 0.0f;
            }
            kit::panelItem(paint, item);
        }
        scene.popClip();
    }
    std::string volumeText() const {
        if (!volumeKnown())
            return "—";
        return shownMuted_ ? msg("tv.quick.muted", "Muted") : std::to_string(static_cast<int>(shownVolume_));
    }

    // ---- Doing things ------------------------------------------------------------------------

    // Up and Down go a line of the grid, to the item in the column last chosen; a list goes one item.
    void move(ScreenHost &host, int direction) {
        if (rows_.empty())
            return;
        if (level_ != Level::Main) {
            size_t next = static_cast<size_t>(clamp(static_cast<double>(focus_) + direction, 0,
                                                    static_cast<double>(rows_.size() - 1)));
            if (next != focus_) {
                focus_ = next;
                focusMoved(host, false);
            }
            return;
        }
        int lastLine = 0;
        for (const Row &row : rows_)
            lastLine = std::max(lastLine, row.line);
        for (int line = rows_[focus_].line + direction; line >= 0 && line <= lastLine; line += direction) {
            size_t chosen = rows_.size();
            for (size_t i = 0; i < rows_.size(); ++i)
                if (rows_[i].line == line && (rows_[i].column < 0 || rows_[i].column == column_ || chosen == rows_.size()))
                    chosen = i;
            if (chosen < rows_.size()) {
                focus_ = chosen;
                focusMoved(host, false);
                return;
            }
        }
    }
    void sideways(ScreenHost &host, int direction) {
        if (focus_ >= rows_.size())
            return;
        const Row &row = rows_[focus_];
        if (level_ != Level::Main) {
            if (direction < 0)
                toMain(host);
            return;
        }
        if (row.id == "volume" || row.id == "light") {
            host.quietKey();
            if (row.id == "volume")
                stepVolume(host, direction);
            else
                stepLight(host, direction * kLightStep);
        } else if (row.column >= 0)
            for (size_t i = 0; i < rows_.size(); ++i)
                if (rows_[i].line == row.line && rows_[i].column == row.column + direction) {
                    focus_ = i;
                    focusMoved(host, false);
                    return;
                }
    }
    void close(ScreenHost &host, const std::string &destination) {
        if (closing_)
            return;
        closing_ = true;
        destination_ = destination;
        host.motion().tween("quick:x", kPanelW, kit::motion::ms::quick, easing(kit::motion::ease::quick));
        host.invalidate();
    }
    void openLevel(ScreenHost &host, Level level) {
        level_ = level;
        focus_ = 0;
        rebuild(host);
        // A list of choices opens on the one in force; a list of switches, on its first.
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].kind == Row::Kind::Choice && rowOn(host, rows_[i]))
                focus_ = i;
        host.motion().erasePrefix("quick:f:");
        focusMoved(host, true);
        playPage(host);
    }
    void toMain(ScreenHost &host) {
        std::string opener = rowOpening(level_);
        level_ = Level::Main;
        rebuild(host);
        focus_ = indexOf(opener, focus_);
        host.motion().erasePrefix("quick:f:");
        focusMoved(host, true);
        playPage(host);
    }
    // Held, the key repeats: each repeat changes the shown value at once and the layer sends the
    // newest target at most every 120 ms.
    void stepVolume(ScreenHost &host, int direction) {
        if (!volumeKnown() || !loaded_)
            return;
        double now = volume(host), target = clamp(now + direction, 0, 100);
        volumeDirection_ = direction;
        if (target == now)
            return;
        host.optimistic().set("quick.volume", Json::numeric(target), Json::numeric(stats_["volume"].number(0)),
                              controlWrite("volume"));
        syncShown(host);
        host.invalidate();
    }
    void stepLight(ScreenHost &host, int amount) {
        if (!lightKnown() || !loaded_)
            return;
        double now = light(host), target = clamp(now + amount, 0, 100);
        lightDirection_ = amount > 0 ? 1 : -1;
        if (target == now)
            return;
        host.optimistic().set("quick.light", Json::numeric(target),
                              Json::numeric(stats_["picture"]["backlight"].number(0)), controlWrite("backlight"));
        syncShown(host);
        host.invalidate();
    }
    void press(ScreenHost &host) {
        if (focus_ >= rows_.size())
            return;
        const Row &row = rows_[focus_];
        if (rowDim(row) && !row.go)
            return;
        if (level_ != Level::Main) {
            choose(host, row);
            return;
        }
        if (!loaded_ && readsStats(row.id))
            return;
        if (row.id == "volume") {
            if (!volumeKnown())
                return;
            host.optimistic().set("quick.mute", Json::flag(!muted(host)), Json::flag(stats_["muted"].boolean()),
                                  controlWrite("mute"));
            syncShown(host);
            host.invalidate();
        } else if (row.id == "picture")
            openLevel(host, Level::Picture);
        else if (row.id == "sound")
            openLevel(host, Level::Sound);
        else if (row.id == "input")
            openLevel(host, Level::Input);
        else if (row.id == "sleep")
            openLevel(host, Level::Sleep);
        else if (row.id == "settings")
            close(host, "settings");
        else if (row.id == "saver" || row.id == "off") {
            // The TV acts; the panel is already gone, and a refusal is said by the layer.
            host.optimistic().run("quick.act", controlWrite(row.id == "saver" ? "screensaver" : "screenOff"));
            close(host, returnRoute(host.cameFrom()));
        } else if (row.id == "recents")
            close(host, "recents");
        else if (row.id == "profile")
            close(host, "profiles");
    }
    void choose(ScreenHost &host, const Row &row) {
        if (level_ == Level::Input) {
            if (row.dim)
                return;
            Write open;
            open.operation = Operation::OpenHomeItem;
            const std::string id = row.raw;
            open.body = [id](const Json &) {
                Json body = Json::object();
                body.set("kind", Json::text("dev"));
                body.set("id", Json::text(id));
                return body;
            };
            host.optimistic().run("quick.act", open);
            close(host, returnRoute(host.cameFrom()));
            return;
        }
        if (level_ == Level::Picture)
            host.optimistic().set("quick.picture", Json::text(row.raw),
                                  Json::text(stats_["picture"]["mode_raw"].string()), controlWrite("pictureMode"));
        else if (level_ == Level::Sound)
            host.optimistic().set("quick.sound", Json::text(row.raw),
                                  Json::text(stats_["sound"]["output_raw"].string()), controlWrite("soundOutput"));
        else
            host.optimistic().set("quick.sleep", Json::text(row.raw), Json::text(stats_["sleepTimer"].string()),
                                  controlWrite("sleepTimer"));
        syncShown(host);
        toMain(host);
    }

    Level level_;
    Json stats_ = Json::object();
    std::string nowPort_;
    std::vector<Row> rows_;
    size_t focus_ = 0;
    int column_ = 0; // the grid's column Up and Down keep to
    bool loaded_ = false, readFailed_ = false, needsRebuild_ = false, closing_ = false, closed_ = false;
    double retryAt_ = 0;
    unsigned statsGeneration_ = 0, nowGeneration_ = 0;
    std::string destination_;
    // What is shown, for the snapshot and the held-key counts.
    double shownVolume_ = 0, shownLight_ = 0;
    bool shownMuted_ = false, writing_ = false;
    std::string shownSleep_, shownPicture_;
    int volumeDirection_ = 0, lightDirection_ = 0;
    unsigned volumeBackwards_ = 0, lightBackwards_ = 0;
};
} // namespace

std::unique_ptr<ScreenView> makeQuickScreen(const std::string &route) {
    return route == "quick" ? std::unique_ptr<ScreenView>(new QuickScreen()) : nullptr;
}
} // namespace nativetv
