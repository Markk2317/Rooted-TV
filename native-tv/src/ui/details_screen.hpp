#pragma once
#include "ui/screen.hpp"
#include <string>
#include <utility>

namespace nativetv {
// Title details: the backdrop, the lines about the film or series, its actions, a row of similar
// titles and the full overview. A similar title opens on top of this one (up to eight deep) and
// Back peels them off again, as the page did in JavaScript.
//
// Reached from Home as "details:<type>:<id>"; the search editor hosts one as a child
// (`embedded`) so the search it sits on keeps its query and results while it is open.
class DetailsScreen final : public ScreenView {
  public:
    DetailsScreen(const std::string &type, const std::string &id, const Json &item, bool embedded);
    std::string name() const override {
        return "details";
    }
    bool hasBar() const override {
        return false;
    }
    bool paintsWall() const override {
        return true;
    }
    void enter(ScreenHost &host) override;
    void leave(ScreenHost &host) override;
    bool key(ScreenHost &host, Key key) override;
    bool back(ScreenHost &host) override;
    void click(ScreenHost &host, const std::string &id) override;
    void response(ScreenHost &host, const Response &response) override;
    void build(ScreenHost &host, kit::Paint paint) override;
    Json snapshot() const override;

    // True once the last title has been closed; an embedded screen is then dropped by its host.
    bool finished() const {
        return finished_;
    }

  private:
    struct Action {
        enum class Kind { Netflix, Service, Trailer, Watchlist, Like, Dislike, Overview, Retry } kind;
        Action(Kind kind, std::string label) : kind(kind), label(std::move(label)) {}
        std::string label;
        std::string service; // Service: the streaming service's key
        std::string icon;
        bool iconOnly = false; // drawn as a round button with its icon; the label is for the snapshot
    };
    struct Title {
        Json item;
        std::string status;
        bool loading = false, error = false, stale = false, onSimilar = false, moved = false;
        unsigned detailsGeneration = 0, netflixGeneration = 0;
        size_t action = 0, similar = 0;
        std::vector<Action> actions;
    };
    void push(ScreenHost &host, const Json &item);
    void load(ScreenHost &host);
    void rebuildActions(ScreenHost &host, Title &title);
    void markTitle(ScreenHost &host, Action::Kind kind);
    // The profile's marks on a title: what is being saved, else what Home's feed last said.
    struct Marks {
        bool watchlisted = false;
        std::string rating; // "like", "dislike" or ""
    };
    static Marks marksOf(ScreenHost &host, const Json &item);
    void activate(ScreenHost &host);
    void focusMoved(ScreenHost &host, bool jump);
    void playEntrance(ScreenHost &host);
    void close(ScreenHost &host);
    void openOverview(ScreenHost &host);
    void toggleOverview(ScreenHost &host, bool open);
    Title &top() {
        return stack_.back();
    }
    const Title &top() const {
        return stack_.back();
    }

    std::string type_, id_;
    Json first_;
    bool embedded_;
    bool finished_ = false;
    std::vector<Title> stack_;
    bool overview_ = false;
    size_t overviewPage_ = 0;
    std::vector<std::string> overviewPages_;
};
} // namespace nativetv
