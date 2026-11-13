#pragma once

#include "daemon/command_policy.hpp"
#include "core/json.hpp"

#include <mutex>
#include <string>

namespace rootedtv {
namespace daemon {
class HomeScreenManager;
class HomeTakeoverManager;
class LunaClient;
class SamRescanManager;

// Sequences the Home mount with SAM's firmware-specific manifest reread.
class HomeTakeoverLifecycleManager {
  public:
    HomeTakeoverLifecycleManager(HomeScreenManager &homeScreen,
                                 HomeTakeoverManager &takeover,
                                 SamRescanManager &samRescan,
                                 LunaClient &luna,
                                 std::string systemRoot);

    Json takeover(bool blankStartLayout,
                  const CommandPolicyContext &context);
    Json release(const CommandPolicyContext &context);

  private:
    bool appInfo(Json &info);
    bool manifestAccepted(bool rootedTvManifest);
    bool reread(bool rootedTvManifest,
                const CommandPolicyContext &context);
    bool waitForManifest(bool rootedTvManifest);
    void rollbackTakeover(const CommandPolicyContext &context);

    HomeScreenManager &homeScreen_;
    HomeTakeoverManager &takeover_;
    SamRescanManager &samRescan_;
    LunaClient &luna_;
    bool blockFirst_ = false;
    std::mutex mutex_;
};
} // namespace daemon
} // namespace rootedtv
