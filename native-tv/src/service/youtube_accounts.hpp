#pragma once

#include "core/json.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace nativetv {
namespace service {

// A Google account linked to each Native Tv Home profile, through Google's sign-in for TVs with
// the owner's own OAuth client ("TVs and Limited Input devices") and read-only YouTube access, and
// the row of new videos from that account's YouTube subscriptions. The refresh tokens are kept in
// youtube-accounts.json beside the server, mode 0600, and go nowhere but Google.
class YoutubeAccounts {
  public:
    struct Reply {
        long status = 0;
        Json body;
    };
    // POSTs `form` when it is not empty, else GETs; `bearer` is the access token, when there is
    // one. False when no HTTP answer came back.
    using Transport = std::function<bool(const std::string &url, const std::string &form,
                                         const std::string &bearer, Reply &reply,
                                         std::string &error)>;
    using Clock = std::function<int64_t()>;

    explicit YoutubeAccounts(std::string dataRoot, Transport transport = Transport(),
                             Clock clock = Clock());
    ~YoutubeAccounts();
    YoutubeAccounts(const YoutubeAccounts &) = delete;
    YoutubeAccounts &operator=(const YoutubeAccounts &) = delete;

    // Asks Google for a code for `profile`, which the owner enters at Google's address; the worker
    // then waits for it and links the account. {"ok": true} or {"ok": false, "error"}.
    Json signIn(const std::string &clientId, const std::string &clientSecret,
                const std::string &profile);
    void signOut(const std::string &profile);
    // {"linked", "name", "pending": {"code", "url", "expiresInMs"} or null, "error"}.
    Json status(const std::string &profile);
    // {"key": "youtube:subscriptions", "items": [...]} for the profile, or null. A row older than an
    // hour, or none yet, is asked for behind the answer.
    Json row(const std::string &clientId, const std::string &clientSecret,
             const std::string &profile);

  private:
    struct Pending {
        std::string profile, clientId, clientSecret, deviceCode, userCode, url;
        int64_t expiresAt = 0, nextPollAt = 0;
        int intervalSeconds = 5;
    };
    struct Wanted {
        std::string clientId, clientSecret;
    };

    void workerLoop();
    void poll(Pending pending);
    void refresh(const std::string &profile, const Wanted &wanted);
    bool send(const std::string &url, const std::string &form, const std::string &bearer,
              Reply &reply, std::string &error);
    void loadLocked();
    void saveAccountsLocked();
    void saveRowsLocked();

    std::string accountsPath_, rowsPath_;
    Transport transport_;
    Clock clock_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    bool loaded_ = false;
    Json accounts_; // {profile: {"refreshToken", "name", "linkedAt"}}
    Json rows_;     // {profile: {"at", "items"}}
    bool hasPending_ = false;
    Pending pending_;
    std::map<std::string, Wanted> wanted_;
    std::map<std::string, int64_t> attemptedAt_;
    std::map<std::string, std::string> errors_;
};

// Seconds in an ISO 8601 duration as YouTube gives it ("PT1H2M3S", "P1DT2H"); -1 when unreadable.
long long isoDurationSeconds(const std::string &duration);

} // namespace service
} // namespace nativetv
