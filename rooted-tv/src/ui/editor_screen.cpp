// The two text editors: renaming the profile in use, and asking the Gemini assistant. A field, the
// on-screen keyboard pad Setup uses and, for the assistant, its answer over the titles it names; a
// title opens its details on top, which keeps the question and answer for Back. (Search is Google
// TV's own page, search_screen.cpp.)
//
// A reply is taken only if it answers the latest question. A rename goes through the
// optimistic-update layer: Save returns to the profiles at once with the new name on them, and if
// the server refuses the name goes back and the layer says why.
#include "domain/models.hpp"
#include "domain/say.hpp"
#include "ui/application.hpp"
#include "ui/details_screen.hpp"
#include "ui/home_layout.hpp"
#include "ui/keyboard_pad.hpp"
#include "ui/kit/entry.hpp"
#include "ui/kit/overlay.hpp"
#include "ui/kit/tuning.hpp"
#include "ui/optimistic.hpp"
#include "ui/overlay_routes.hpp"
#include "ui/profile_names.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace rootedtv {
namespace {
namespace layout = kit::layout;
using kit::easing;
using say::msg;

constexpr const char *kAsk = "assistant:ask", *kPad = "editor:kb",
                     *kStatsMemory = "quick.stats";
constexpr float kLabelY = 150, kFieldY = 204, kFieldW = 890, kFieldH = 84, kStatusY = 300, kKeysY = 344,
                kResultsX = 1020, kCardW = 396, kCardH = 222, kCardGap = 28, kCardPitch = kCardH + 32,
                kResultsTop = 204, kResultsBottom = 1000;
constexpr size_t kNameMax = 20, kQuestionMax = 300;
// The assistant's answer sits over its titles: up to six lines of body text.
constexpr int kAnswerLines = 6;

std::string trim(const std::string &value) {
    size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
Color profileColor(const Json &person) {
    std::string value = person["color"].string();
    if (value.size() == 7 && value[0] == '#' &&
        value.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos)
        return kit::rgb(static_cast<unsigned>(std::strtoul(value.c_str() + 1, nullptr, 16)));
    return home::hsl(static_cast<float>(home::hashOf(person["id"].string()) % 360), .5f, .40f);
}

class EditorScreen final : public ScreenView {
    // Where the focus is: on the pad, on the Clear button under it, on the titles the assistant
    // named, or on the retry button of a question that failed.
    enum class Zone { Keys, Clear, Results, Retry };

  public:
    // The rename page, or the assistant's (`rename` false).
    explicit EditorScreen(bool rename) : rename_(rename), assistant_(!rename) {}

    std::string name() const override {
        return details_ ? "details" : rename_ ? "rename" : "assistant";
    }
    bool hasBar() const override {
        return false;
    }
    bool paintsWall() const override {
        return details_ != nullptr;
    }

    void enter(ScreenHost &host) override {
        const ViewState &view = host.view();
        if (rename_) {
            for (const auto &person : view.catalogue["profiles"].items())
                if (person["active"].boolean())
                    profile_ = person;
        }
        KeyboardPad::Options options;
        options.id = kPad;
        options.origin = {kit::canvas::safeX, kKeysY, layout::keyW, layout::keyH};
        options.maximum = rename_ ? kNameMax : kQuestionMax;
        pad_.begin(host.motion(), rename_ ? shownName(host) : "", options);
        status_ = rename_ ? (pad_.value().empty() ? msg("tv.editor.enterName", "Enter a name") : hintForName())
                          : askHint();
        Timeline timeline;
        Tween rise(1, 0, kit::motion::ms::base, easing(kit::motion::ease::out));
        for (int block = 0; block < 3; ++block) {
            host.motion().set("editor:e:" + std::to_string(block), 1);
            timeline.add("editor:e:" + std::to_string(block),
                         rise.delayedBy(kit::motion::stagger::pageBase + 80.0 * block));
        }
        timeline.play(host.motion());
    }
    void leave(ScreenHost &host) override {
        if (details_) {
            details_->leave(host);
            details_.reset();
        }
        host.services().cancel(kAsk);
        host.motion().erasePrefix("editor:");
    }
    bool update(ScreenHost &host) override {
        if (details_)
            return false;
        double now = host.nowMs();
        // The caret blinks by itself: one frame at each change, none in between.
        bool lit = pad_.caretLit(now);
        if (lit != caretLit_) {
            caretLit_ = lit;
            host.invalidate();
        }
        return asking_;
    }

    // ---- Keys --------------------------------------------------------------------------------

    bool key(ScreenHost &host, Key key) override {
        if (details_)
            return details_->key(host, key);
        bool used = key == Key::Up || key == Key::Down || key == Key::Left || key == Key::Right ||
                    key == Key::Ok;
        if (key == Key::Delete) {
            zoneTo(host, Zone::Keys);
            edited(host, pad_.key(host.motion(), host.nowMs(), key));
            return true;
        }
        switch (zone_) {
        case Zone::Keys:
            keyZone(host, key);
            break;
        case Zone::Clear:
            if (key == Key::Up)
                zoneTo(host, Zone::Keys);
            else if (key == Key::Ok)
                clearText(host);
            break;
        case Zone::Results:
            resultZone(host, key);
            break;
        case Zone::Retry:
            if (key == Key::Left || key == Key::Up)
                zoneTo(host, Zone::Keys);
            else if (key == Key::Ok)
                ask(host);
            break;
        }
        host.invalidate();
        return used;
    }
    bool back(ScreenHost &host) override {
        if (details_) {
            details_->back(host);
            dropFinishedDetails(host);
            return true;
        }
        if (zone_ != Zone::Keys) {
            zoneTo(host, Zone::Keys);
            return true;
        }
        close(host);
        return true;
    }
    bool text(ScreenHost &host, const std::string &typed) override {
        if (details_)
            return true;
        zoneTo(host, Zone::Keys);
        edited(host, pad_.type(host.nowMs(), typed));
        return true;
    }
    void click(ScreenHost &host, const std::string &id) override {
        if (details_) {
            details_->click(host, id);
            dropFinishedDetails(host);
            return;
        }
        if (id == "editor:back") {
            close(host);
            return;
        }
        if (id == "editor:retry") {
            ask(host);
        } else if (id == "editor:clear") {
            clearText(host);
        } else if (id.compare(0, 14, "editor:result:") == 0) {
            zone_ = Zone::Results;
            selected_ = std::min(static_cast<size_t>(std::atoi(id.c_str() + 14)),
                                 results_.empty() ? 0 : results_.size() - 1);
            focusResult(host, false);
            openDetails(host);
        } else if (id.compare(0, std::string(kPad).size() + 1, std::string(kPad) + ":") == 0) {
            zoneTo(host, Zone::Keys);
            edited(host, pad_.click(host.motion(), host.nowMs(), id));
        }
    }

    // ---- Replies -----------------------------------------------------------------------------

    void response(ScreenHost &host, const Response &response) override {
        if (details_) {
            details_->response(host, response);
            dropFinishedDetails(host);
        }
        if (response.channel == kAsk && askGeneration_ && response.generation == askGeneration_) {
            askGeneration_ = 0;
            asking_ = false;
            Json data = response.json();
            results_.clear();
            selected_ = 0;
            if (!response.ok() || !data.valid() || !data["ok"].boolean()) {
                askFailed_ = true;
                answer_.clear();
                status_ = data["error"].string(msg("tv.editor.askFailed", "The assistant did not answer. Try again."));
            } else {
                askFailed_ = false;
                answered_ = true;
                answer_ = data["answer"].string();
                for (const auto &item : data["titles"].items()) {
                    Card card;
                    card.id = item["id"].string();
                    card.kind = "title";
                    card.title = item["title"].string();
                    card.subtitle = titleFacts(item);
                    card.image = artwork(item);
                    card.data = item;
                    results_.push_back(card);
                }
                status_ = askHint();
            }
            if (results_.empty() && zone_ == Zone::Results)
                zoneTo(host, Zone::Keys);
            focusResult(host, true);
            host.invalidate();
        }
    }

    // ---- Drawing -----------------------------------------------------------------------------

    void build(ScreenHost &host, kit::Paint paint) override {
        if (details_) {
            details_->build(host, paint);
            return;
        }
        const Motion &motion = host.motion();
        Scene &scene = paint.scene;
        double headEnter = motion.value("editor:e:0"), keysEnter = motion.value("editor:e:1"),
               sideEnter = motion.value("editor:e:2");
        scene.pushOpacity(static_cast<float>(1 - headEnter));
        scene.pushTransform(Transform::translate(0, static_cast<float>(18 * headEnter)));
        paint.glyphs.text(scene, {kit::canvas::safeX, kLabelY, kFieldW, kit::type::title.line},
                          rename_ ? msg("tv.editor.renameTitle", "Rename profile")
                                  : msg("tv.editor.askTitle", "Ask Gemini"),
                          kit::type::title, kit::color::onSurface2);
        kit::textField(paint, pad_.field(host.nowMs(), {kit::canvas::safeX, kFieldY, kFieldW, kFieldH},
                                         rename_ ? msg("tv.editor.namePlaceholder", "Name")
                                                 : msg("tv.editor.askPlaceholder", "Something to watch tonight")));
        paint.glyphs.text(scene, {kit::canvas::safeX, kStatusY, kFieldW, kit::type::labelM.line}, status_,
                          kit::type::labelM,
                          askFailed_ ? kit::color::bad : kit::color::onSurface3);
        scene.popTransform();
        scene.popOpacity();

        scene.pushOpacity(static_cast<float>(1 - keysEnter));
        scene.pushTransform(Transform::translate(0, static_cast<float>(28 * keysEnter)));
        pad_.setDone(doneLabel(), doneIcon());
        pad_.paint(paint, motion);
        kit::PillButton clear;
        clear.rest = {kit::canvas::safeX, kKeysY + 5 * kit::geometry::keyRowPitch() + 6, 190, 56};
        clear.id = "editor:clear";
        clear.label = msg("tv.editor.clear", "Clear");
        clear.icon = "close";
        clear.focus = static_cast<float>(motion.value("editor:cf"));
        kit::pillButton(paint, clear);
        scene.popTransform();
        scene.popOpacity();

        scene.pushOpacity(static_cast<float>(1 - sideEnter));
        scene.pushTransform(Transform::translate(0, static_cast<float>(28 * sideEnter)));
        if (rename_)
            drawPreview(paint);
        else
            drawResults(host, paint);
        scene.popTransform();
        scene.popOpacity();
        kit::keyHint(paint, kit::canvas::safeX, 1002, "Back", msg("tv.editor.return", "Return"),
                     "editor:back");
    }
    Json snapshot() const override {
        if (details_) {
            Json value = details_->snapshot();
            value.set("origin", Json::text(rename_ ? "rename" : "assistant"));
            return value;
        }
        Json value = Json::object();
        value.set("query", Json::text(pad_.value()));
        value.set("results", Json::numeric(static_cast<double>(results_.size())));
        value.set("selected", Json::numeric(static_cast<double>(selected_)));
        value.set("zone", Json::text(zone_ == Zone::Keys      ? "keys"
                                     : zone_ == Zone::Clear   ? "clear"
                                     : zone_ == Zone::Results ? "results"
                                                              : "retry"));
        value.set("focusedKey", Json::text(pad_.focusedKey()));
        value.set("contentStatus", Json::text(status_));
        value.set("contentState", Json::text(state()));
        if (assistant_)
            value.set("answer", Json::text(answer_));
        return value;
    }

  private:
    // ---- State -------------------------------------------------------------------------------

    // The profile's name as the profiles page shows it: the TV's name for the untouched first one.
    std::string shownName(ScreenHost &host) {
        auto found = host.memory().find(kStatsMemory);
        std::string model = found == host.memory().end() ? "" : found->second["device"]["model"].string();
        return shownProfileName(host.optimistic(), profile_, model);
    }

    static std::string hintForName() {
        return msg("tv.editor.limit", "Up to 20 characters");
    }
    static std::string askHint() {
        return msg("tv.editor.askHint", "Ask for something to watch, or to open a title or change the volume");
    }
    std::string state() const {
        if (rename_)
            return trim(pad_.value()).empty() ? "empty" : "editing";
        if (asking_)
            return "loading";
        if (askFailed_)
            return "error";
        if (answered_ && results_.empty())
            return "empty";
        return results_.empty() ? "idle" : "ready";
    }
    std::string doneLabel() const {
        if (rename_)
            return msg("tv.editor.save", "Save");
        return results_.empty() || asked_ != trim(pad_.value()) ? msg("tv.editor.ask", "Ask")
                                                                : msg("tv.editor.resultsKey", "Results");
    }
    std::string doneIcon() const {
        if (rename_)
            return "check";
        return results_.empty() || asked_ != trim(pad_.value()) ? "sparkle" : "arrowright";
    }
    void close(ScreenHost &host) {
        host.open(rename_ ? "profiles" : returnRoute(host.cameFrom()));
    }

    // The pad said what a key did.
    void edited(ScreenHost &host, KeyboardPad::Result result) {
        if (result == KeyboardPad::Result::Edited)
            changed(host);
        else if (result == KeyboardPad::Result::Done)
            primary(host);
        host.invalidate();
    }
    // Typing: the question is asked with Ask, not as it is typed, and what was answered stays
    // meanwhile; a name says how long it may be.
    void changed(ScreenHost &host) {
        if (assistant_)
            status_ = askHint();
        else
            status_ = trim(pad_.value()).empty() ? msg("tv.editor.enterName", "Enter a name") : hintForName();
        host.invalidate();
    }
    void clearText(ScreenHost &host) {
        if (pad_.value().empty())
            return;
        pad_.setValue("", host.nowMs());
        changed(host);
    }
    // The question goes to the assistant, which may also act on the TV; its answer and titles follow.
    void ask(ScreenHost &host) {
        const std::string question = trim(pad_.value());
        if (question.empty()) {
            status_ = askHint();
            return;
        }
        host.services().cancel(kAsk);
        asked_ = question;
        answer_.clear();
        results_.clear();
        selected_ = 0;
        asking_ = true;
        askFailed_ = answered_ = false;
        status_ = msg("tv.editor.asking", "Asking…");
        askGeneration_ = host.services().write(kAsk, Operation::Assistant,
                                                  fields({{"question", Json::text(question)}}));
        if (!askGeneration_) {
            asking_ = false;
            askFailed_ = true;
            status_ = msg("tv.editor.askFailed", "The assistant did not answer. Try again.");
        }
        host.invalidate();
    }
    void primary(ScreenHost &host) {
        if (assistant_) {
            if (!results_.empty() && asked_ == trim(pad_.value()))
                zoneTo(host, Zone::Results);
            else
                ask(host);
            return;
        }
        if (rename_) {
            std::string name = trim(pad_.value());
            if (name.empty()) {
                status_ = msg("tv.editor.enterName", "Enter a name");
                return;
            }
            // The name is on the profiles page the moment this one closes; the write follows.
            const std::string id = profile_["id"].string();
            Write write;
            write.operation = Operation::SetProfile;
            write.body = [id](const Json &target) {
                Json body = Json::object();
                body.set("action", Json::text("rename"));
                body.set("id", Json::text(id));
                body.set("name", target);
                return body;
            };
            write.readChannel = "catalogue";
            write.readOperation = Operation::HomeCatalogue;
            write.failure = msg("tv.editor.saveFailed", "The name could not be saved. Try again.");
            host.optimistic().set("profile.name." + id, Json::text(name), Json::text(shownName(host)), write);
            host.open("profiles");
        }
    }
    void openDetails(ScreenHost &host) {
        if (selected_ >= results_.size())
            return;
        const Json &item = results_[selected_].data;
        details_.reset(new DetailsScreen(item["type"].string(), item["id"].string(), item, true));
        details_->enter(host);
        host.invalidate();
    }
    void dropFinishedDetails(ScreenHost &host) {
        if (!details_ || !details_->finished())
            return;
        details_->leave(host);
        details_.reset();
        // The results are as they were left; the card that was opened has the focus.
        focusResult(host, true);
        host.invalidate();
    }

    // ---- Focus -------------------------------------------------------------------------------

    void zoneTo(ScreenHost &host, Zone zone) {
        zone_ = zone;
        pad_.activate(host.motion(), zone == Zone::Keys);
        host.motion().tween("editor:cf", zone == Zone::Clear ? 1 : 0, kit::motion::ms::focus,
                            easing(kit::motion::ease::out));
        focusResult(host, false);
    }
    void focusResult(ScreenHost &host, bool jump) {
        Motion &motion = host.motion();
        for (size_t i = 0; i < results_.size(); ++i) {
            double to = zone_ == Zone::Results && i == selected_ ? 1 : 0;
            std::string id = "editor:r:" + std::to_string(i);
            if (jump)
                motion.set(id, to);
            else
                motion.tween(id, to, kit::motion::ms::focus, easing(kit::motion::ease::out));
        }
        double target = -static_cast<double>(kCardPitch) *
                        static_cast<double>(selected_ / 2 > 0 ? selected_ / 2 - 1 : 0);
        if (jump)
            motion.set("editor:rs", target);
        else
            motion.spring("editor:rs", target, kit::springParams(kit::motion::spring::list));
        host.invalidate();
    }
    void keyZone(ScreenHost &host, Key key) {
        KeyboardPad::Result result = pad_.key(host.motion(), host.nowMs(), key);
        if (result != KeyboardPad::Result::None) {
            edited(host, result);
            return;
        }
        // The pad's edge: the keys that would leave it go to what is beside or under it.
        if (key == Key::Right && !results_.empty())
            zoneTo(host, Zone::Results);
        else if (key == Key::Right && askFailed_ && !rename_)
            zoneTo(host, Zone::Retry);
        else if (key == Key::Down)
            zoneTo(host, Zone::Clear);
    }
    void resultZone(ScreenHost &host, Key key) {
        size_t count = results_.size();
        if (!count) {
            zoneTo(host, Zone::Keys);
            return;
        }
        switch (key) {
        case Key::Left:
            if (selected_ % 2)
                --selected_;
            else {
                zoneTo(host, Zone::Keys);
                return;
            }
            break;
        case Key::Right:
            if (selected_ % 2 == 0 && selected_ + 1 < count)
                ++selected_;
            break;
        case Key::Up:
            if (selected_ >= 2)
                selected_ -= 2;
            break;
        case Key::Down:
            if (selected_ + 2 < count)
                selected_ += 2;
            else if ((selected_ / 2 + 1) * 2 < count)
                selected_ = count - 1;
            break;
        case Key::Ok:
            openDetails(host);
            return;
        default:
            return;
        }
        focusResult(host, false);
    }

    // ---- Parts -------------------------------------------------------------------------------

    void drawPreview(kit::Paint paint) {
        Scene &scene = paint.scene;
        float diameter = 220, cx = 1430;
        Rect disc = {cx - diameter / 2, 300, diameter, diameter};
        Color base = profileColor(profile_);
        Node &shape = scene.shape(disc, base, diameter / 2);
        shape.gradient = Gradient::between(145, base, kit::focus::mix(base, kit::color::ink, .45f));
        std::string name = trim(pad_.value()), initial = "?";
        if (!name.empty()) {
            size_t end = 1;
            while (end < name.size() && (static_cast<unsigned char>(name[end]) & 0xc0) == 0x80)
                ++end;
            initial = name.substr(0, end);
            if (end == 1)
                initial[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(initial[0])));
        }
        paint.glyphs.text(scene, {disc.x, disc.y + (diameter - kit::type::display.line) / 2, diameter,
                                  kit::type::display.line},
                          initial, kit::type::display, {1, 1, 1, 1}, Node::Align::Center);
        paint.glyphs.text(scene, {cx - 340, 560, 680, kit::type::headline.line},
                          name.empty() ? msg("tv.editor.noName", "No name yet") : name, kit::type::headline,
                          name.empty() ? kit::color::onSurface3 : kit::color::onSurface, Node::Align::Center);
        paint.glyphs.text(scene, {cx - 340, 560 + kit::type::headline.line + 8, 680, kit::type::body.line},
                          msg("tv.editor.preview", "This is how the profile appears."), kit::type::body,
                          kit::color::onSurface2, Node::Align::Center);
    }
    // Where the result cards start: under the assistant's answer, when there is one.
    float cardsTop() const {
        return assistant_ && !answer_.empty()
                   ? kResultsTop + kit::type::bodyL.line * kAnswerLines + 32
                   : kResultsTop;
    }
    void drawResults(ScreenHost &host, kit::Paint paint) {
        const Motion &motion = host.motion();
        Scene &scene = paint.scene;
        Rect panel = {kResultsX, kResultsTop, 820, 380};
        if (assistant_ && !answer_.empty())
            paint.glyphs.text(scene, {kResultsX, kResultsTop, 820, kit::type::bodyL.line * kAnswerLines},
                              answer_, kit::type::bodyL, kit::color::onSurface, Node::Align::Left,
                              kAnswerLines);
        if (results_.empty() && assistant_ && !answer_.empty())
            return;
        if (results_.empty()) {
            kit::StatusPanel box;
            box.rest = panel;
            if (asking_) {
                box.icon = "sparkle";
                box.title = msg("tv.editor.asking", "Asking…");
                box.message = msg("tv.editor.askingBody", "Gemini is thinking about your question.");
                box.loadingMs = host.nowMs();
            } else if (askFailed_) {
                box.icon = "info";
                box.error = true;
                box.title = msg("tv.editor.askFailedTitle", "No answer");
                box.message = status_;
            } else {
                box.icon = "sparkle";
                box.title = msg("tv.editor.askTitle", "Ask Gemini");
                box.message = askHint();
            }
            kit::statusPanel(paint, box);
            if (askFailed_) {
                kit::PillButton retry;
                retry.rest = {panel.x + 36, panel.y + panel.h - 36 - 56, 220, 56};
                retry.id = "editor:retry";
                retry.label = msg("tv.editor.retryAsk", "Ask again");
                retry.primary = true;
                retry.focus = zone_ == Zone::Retry ? 1.0f : 0.0f;
                kit::pillButton(paint, retry);
            }
            return;
        }
        const float top = cardsTop();
        scene.pushClip({kResultsX - 20, top - 24, 820 + 40, kResultsBottom - top + 24});
        float scroll = static_cast<float>(motion.value("editor:rs"));
        for (size_t i = 0; i < results_.size(); ++i) {
            Rect rest = {kResultsX + static_cast<float>(i % 2) * (kCardW + kCardGap),
                         top + static_cast<float>(i / 2) * kCardPitch + scroll, kCardW, kCardH};
            if (rest.y + rest.h < top - 60 || rest.y > kResultsBottom + 40)
                continue;
            kit::PosterCard card;
            card.rest = rest;
            card.id = "editor:result:" + std::to_string(i);
            card.image = results_[i].image;
            card.title = results_[i].title;
            card.focus = static_cast<float>(motion.value("editor:r:" + std::to_string(i)));
            kit::card(paint, card);
        }
        scene.popClip();
    }

    bool rename_, assistant_;
    std::string asked_, answer_; // the question the assistant last answered, and its answer
    KeyboardPad pad_;
    Zone zone_ = Zone::Keys;
    size_t selected_ = 0;
    bool asking_ = false, askFailed_ = false, answered_ = false, caretLit_ = true;
    unsigned askGeneration_ = 0;
    std::string status_;
    Json profile_;
    std::vector<Card> results_;
    std::unique_ptr<DetailsScreen> details_;
};
} // namespace

std::unique_ptr<ScreenView> makeEditorScreen(const std::string &route) {
    if (route == "rename" || route == "assistant")
        return std::unique_ptr<ScreenView>(new EditorScreen(route == "rename"));
    return nullptr;
}
} // namespace rootedtv
