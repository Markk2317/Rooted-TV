// Google TV's page of an app category, which a button of its Apps tab opens: the category's name
// large over a row of its apps, the first focused and its name under it, and no bar. Google's row
// is the Play Store's top apps of the category; here it is the TV's own apps of that kind, in Your
// apps' order. OK opens the focused app, Back goes back to the Apps tab as it was.
#include "domain/models.hpp"
#include "domain/say.hpp"
#include "ui/application.hpp"
#include "ui/optimistic.hpp"
#include "ui/home_layout.hpp"
#include "ui/kit/tuning.hpp"
#include "ui/overlay_routes.hpp"
#include <cstdlib>

namespace nativetv {
namespace {
namespace layout = kit::layout;
using kit::easing;

class AppsCategoryScreen : public ScreenView {
  public:
    explicit AppsCategoryScreen(const std::string &kind) : kind_(kind) {}
    std::string name() const override {
        return "apps:" + kind_;
    }
    bool hasBar() const override {
        return false;
    }
    void enter(ScreenHost &host) override {
        const ViewState &view = host.view();
        for (const CardRow &row : homeRows("apps", view.catalogue, view.discover, view.guide))
            if (row.key == "appkind" && row.group == kind_)
                row_ = row;
        title_ = row_.title;
        row_.title = "Your apps";
        Motion &motion = host.motion();
        motion.set("appkind:in", 1);
        motion.tween("appkind:in", 0, kit::motion::ms::base, easing(kit::motion::ease::out));
        focusMoved(host, true);
    }
    void leave(ScreenHost &host) override {
        host.motion().erasePrefix("appkind:");
    }
    bool key(ScreenHost &host, Key key) override {
        if (key == Key::Left && column_ > 0)
            --column_;
        else if (key == Key::Right && column_ + 1 < row_.cards.size())
            ++column_;
        else if (key == Key::Ok)
            open(host);
        else
            return key == Key::Up || key == Key::Down; // one row, and no bar to go up to
        focusMoved(host, false);
        return true;
    }
    bool back(ScreenHost &host) override {
        host.open(returnRoute(host.cameFrom()));
        return true;
    }
    void click(ScreenHost &host, const std::string &id) override {
        if (id.compare(0, 13, "appkind:card:") != 0)
            return;
        const size_t column = static_cast<size_t>(std::atoi(id.c_str() + 13));
        if (column >= row_.cards.size())
            return;
        column_ = column;
        focusMoved(host, false);
        open(host);
    }
    void build(ScreenHost &host, kit::Paint paint) override {
        const Motion &motion = host.motion();
        const float in = static_cast<float>(motion.value("appkind:in"));
        const kit::Enter enter = {1 - in, 28 * in};
        paint.glyphs.text(paint.scene,
                          {layout::homeX, layout::appsPageTitleTop + enter.dy, 1600, kit::type::appsPageTitle.line},
                          title_, kit::type::appsPageTitle, kit::focus::withAlpha(kit::color::onSurface, enter.alpha));
        if (row_.cards.empty())
            return;
        kit::Row label;
        label.placed = true;
        label.y = layout::appsPageRowTop;
        label.px = home::labelPx(risen());
        label.grow = 1;
        label.label = row_.title;
        label.enter = enter;
        kit::row(paint, label);
        const float itemsY = layout::appsPageRowTop + home::itemsOffset(row_, risen());
        const float scroll = static_cast<float>(motion.value("appkind:rx"));
        for (size_t c = 0; c < row_.cards.size(); ++c) {
            Rect rest = home::item(row_, itemsY, c);
            rest.x += scroll;
            if (rest.x + rest.w < -140 || rest.x > kit::canvas::width + 140)
                continue;
            const Card &card = row_.cards[c];
            const float focus = static_cast<float>(motion.value("appkind:f:" + std::to_string(c)));
            kit::AppCard app;
            app.rest = rest;
            app.id = "appkind:card:" + std::to_string(c);
            app.name = card.title;
            app.image = card.image;
            app.icon = card.id == "lg-home" ? "home" : "apps";
            app.color = home::appColor(card);
            app.focus = focus;
            app.caption = focus;
            app.enter = enter;
            kit::appCard(paint, app);
        }
    }
    Json snapshot() const override {
        Json value = Json::object();
        value.set("kind", Json::text(kind_));
        value.set("title", Json::text(title_));
        value.set("apps", Json::numeric(static_cast<double>(row_.cards.size())));
        value.set("column", Json::numeric(static_cast<double>(column_)));
        if (column_ < row_.cards.size())
            value.set("focused", Json::text(row_.cards[column_].id));
        return value;
    }

  private:
    std::string kind_, title_;
    CardRow row_;
    size_t column_ = 0;

    static home::RowLook risen() {
        home::RowLook look;
        look.up = look.risen = look.grow = look.captions = 1;
        return look;
    }
    void focusMoved(ScreenHost &host, bool jump) {
        Motion &motion = host.motion();
        for (size_t c = 0; c < row_.cards.size(); ++c) {
            const bool on = c == column_;
            const std::string name = "appkind:f:" + std::to_string(c);
            if (jump)
                motion.set(name, on ? 1 : 0);
            else
                motion.tween(name, on ? 1 : 0, on ? kit::motion::ms::cardFocus : kit::motion::ms::cardUnfocus,
                             easing(on ? kit::motion::ease::cardFocus : kit::motion::ease::cardUnfocus));
        }
        const double scroll = kit::geometry::rowScrollTarget(static_cast<int>(column_),
                                                             static_cast<int>(row_.cards.size()),
                                                             home::itemWidth(row_), home::itemGap(row_));
        if (jump)
            motion.set("appkind:rx", scroll);
        else
            motion.spring("appkind:rx", scroll, kit::springParams(kit::motion::spring::row));
        host.invalidate();
    }
    void open(ScreenHost &host) {
        if (column_ >= row_.cards.size())
            return;
        const Command &command = row_.cards[column_].command;
        if (!command.route.empty()) {
            host.open(command.route);
            return;
        }
        if (command.operation == Operation::None)
            return;
        // A second press while the first is on its way is dropped; a refusal is said by the layer.
        const Json body = command.body;
        Write write;
        write.operation = command.operation;
        write.body = [body](const Json &) { return body; };
        write.failure = say::msg("tv.apps.openFailed", "That did not open");
        host.optimistic().run("appkind.open", write);
    }
};
} // namespace

std::unique_ptr<ScreenView> makeAppsCategoryScreen(const std::string &route) {
    if (route.compare(0, 5, "apps:") != 0 || route.size() == 5)
        return nullptr;
    return std::unique_ptr<ScreenView>(new AppsCategoryScreen(route.substr(5)));
}
} // namespace nativetv
