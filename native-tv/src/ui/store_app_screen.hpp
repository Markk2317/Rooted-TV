#pragma once
#include "ui/screen.hpp"
#include <memory>
#include <string>

// An LG Content Store app's page, as Google TV's Play Store shows an app: its name large, who makes
// it, its kind and rating, two lines of its description, Install, its banner at the top right and
// its screenshots under. Install has the TV's install service fetch the app from LG, the button
// showing how far it has got, then Open. Opened from Search's results, inside the search page
// (embedded: Back then ends it, and finished() says so), or as the route "storeapp:<id>".
namespace nativetv {
class StoreAppScreen final : public ScreenView {
  public:
    StoreAppScreen(std::string id, Json card, bool embedded);
    std::string name() const override {
        return "storeapp";
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
    bool update(ScreenHost &host) override;
    void build(ScreenHost &host, kit::Paint paint) override;
    Json snapshot() const override;
    bool finished() const {
        return finished_;
    }

  private:
    enum class Button { Install, Open };
    void read(ScreenHost &host);
    void act(ScreenHost &host);
    void focusMoved(ScreenHost &host, bool jump);
    std::string state() const;
    std::vector<Button> buttons() const;

    std::string id_;
    Json app_;     // the card's fields until the page arrives, then the page's
    Json install_; // the install's last state, or null
    bool installed_ = false, loaded_ = false, failed_ = false, embedded_, finished_ = false;
    bool onShots_ = false, starting_ = false;
    size_t button_ = 0, shot_ = 0;
    double pollAt_ = 0;
    std::string error_;
};

std::unique_ptr<ScreenView> makeStoreAppScreen(const std::string &route);
} // namespace nativetv
