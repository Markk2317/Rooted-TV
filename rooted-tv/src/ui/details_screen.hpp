#pragma once
#include "ui/screen.hpp"
#include <string>
#include <utility>

namespace rootedtv {
// Title details, as Google TV's: the backdrop, the title, its facts and story, its actions, and
// under them rows that rise to the top as the focus goes down (the cast and crew, the titles like
// it, the titles of its first two genres), and the full overview. A similar title opens on top of this one (up to eight deep) and Back
// peels them off again, as the page did in JavaScript.
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
    bool update(ScreenHost &host) override;
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
    // A row of titles under the cast: the titles like it, or those of one of its genres, which are
    // asked for once the details have said its genres.
    struct Shelf {
        std::string genre; // the Search page's genre key; empty for the titles like it
        Json found = Json::array(); // a genre's titles as the server gave them
        Json items = Json::array();
        size_t focus = 0;
        unsigned generation = 0; // a genre's titles on their way
    };
    struct Action {
        enum class Kind { Netflix, Service, Trailer, Watchlist, Watched, Like, Dislike, Overview, Retry } kind;
        Action(Kind kind, std::string label) : kind(kind), label(std::move(label)) {}
        std::string label;
        std::string service; // Service: the streaming service's key
        std::string icon;
        bool iconOnly = false; // drawn as a round button with its icon; the label is for the snapshot
    };
    struct Title {
        Json item;
        std::string status;
        bool loading = false, error = false, stale = false, moved = false;
        unsigned detailsGeneration = 0, netflixGeneration = 0;
        // row: 0 for the actions, else the section's place plus one.
        size_t row = 0, action = 0, cast = 0;
        std::vector<Action> actions;
        std::vector<Shelf> shelves;
    };
    // The rows under the actions, those the title has: kCastRow, then the shelves with titles (by
    // their place in `shelves`).
    static constexpr int kCastRow = -1;
    static std::vector<int> sectionsOf(const Title &title);
    // The focused row (kCastRow or a shelf), or none on the actions.
    static int focusedSection(const Title &title);
    static Shelf *focusedShelf(Title &title);
    static const Shelf *focusedShelf(const Title &title);
    void push(ScreenHost &host, const Json &item);
    void load(ScreenHost &host);
    // The rows of titles once the details are in: the titles like it, and its first genres' asked for.
    void fillShelves(ScreenHost &host, Title &title);
    void askGenres(ScreenHost &host, Title &title);
    void cancelGenres(ScreenHost &host);
    void rebuildActions(ScreenHost &host, Title &title);
    void markTitle(ScreenHost &host, Action::Kind kind);
    // The profile's marks on a title: what is being saved, else what Home's feed last said.
    struct Marks {
        bool watchlisted = false;
        bool watched = false;
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

    // Google TV's trailer behind the details: the film's clip ("trailer:movie:<id>"), asked for once
    // the page has been open trailerDelay, fades in over the art; trailerLower after it starts, or
    // after the last key, the page lowers to the title and its facts at the foot, and a key raises
    // it again without doing anything else. It ends with the clip, or once the rows take the page.
    void lower(ScreenHost &host, bool down);
    std::string trailer_; // the clip of the title on top; empty when it has none
    double trailerFrom_ = 0, keyAt_ = 0, shownAt_ = -1;
    bool trailerOver_ = false, lowered_ = false;
};
} // namespace rootedtv
