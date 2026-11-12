#pragma once

#include "core/json.hpp"
#include "daemon/child_process.hpp"
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace nativetv {
namespace daemon {

// Installs from LG Content Store, as the store's own Install button does: the TV's install service
// (com.webos.appInstallService/install) downloads the app from LG with the TV's credentials,
// checks it and installs it, saying how far it got. One install at a time; each app's last state
// is kept until the server restarts.
class LgStoreInstalls {
  public:
    LgStoreInstalls(ChildProcessRunner &children, std::string lunaSendPath);
    ~LgStoreInstalls();
    LgStoreInstalls(const LgStoreInstalls &) = delete;
    LgStoreInstalls &operator=(const LgStoreInstalls &) = delete;

    // Starts installing `appId`. False with `error` while another install runs.
    bool start(const std::string &appId, std::string &error);
    // {"state": "downloading", "installing", "installed" or "failed", "progress" 0 to 100, "error"}
    // for the app's last install here, or null when there was none.
    Json status(const std::string &appId) const;

  private:
    void run(const std::string &appId);
    void update(const std::string &appId, const Json &progress);

    ChildProcessRunner &children_;
    std::string lunaSendPath_;
    mutable std::mutex mutex_;
    std::map<std::string, Json> states_;
    std::string running_;
    std::thread worker_;
    std::shared_ptr<std::atomic<bool>> cancellation_;
};

} // namespace daemon
} // namespace nativetv
