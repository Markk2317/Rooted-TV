#include "service/youtube_accounts.hpp"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace rootedtv;
using namespace rootedtv::service;

namespace {
class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        char pattern[] = "/tmp/rootedtv-youtube-XXXXXX";
        char *created = mkdtemp(pattern);
        assert(created);
        path_ = created;
    }
    ~TemporaryDirectory() {
        for (const char *name : {"/youtube-accounts.json", "/youtube-rows.json"})
            (void)unlink((path_ + name).c_str());
        (void)rmdir(path_.c_str());
    }
    const std::string &path() const { return path_; }

  private:
    std::string path_;
};

bool waitFor(const std::function<bool()> &condition) {
    for (int attempt = 0; attempt < 300; ++attempt) {
        if (condition()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return condition();
}

std::string readFile(const std::string &path) {
    std::ifstream input(path.c_str());
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

// Two channels: one with a film-length video, a Short and a premiere to come; one with a video.
Json answerFor(const std::string &url, const std::string &form, const std::string &bearer,
               long &status, bool &deviceGranted, bool &tokenRevoked) {
    status = 200;
    if (url == "https://oauth2.googleapis.com/device/code")
        return Json::parse(R"({"device_code":"device-1","user_code":"ABCD-EFGH",)"
                           R"("verification_url":"https://www.google.com/device","expires_in":1800,"interval":1})");
    if (url == "https://oauth2.googleapis.com/token") {
        if (form.find("grant_type=refresh_token") != std::string::npos) {
            if (tokenRevoked) {
                status = 400;
                return Json::parse(R"({"error":"invalid_grant","error_description":"Token has been expired or revoked."})");
            }
            return Json::parse(R"({"access_token":"access-1","expires_in":3599})");
        }
        if (!deviceGranted) {
            status = 428;
            return Json::parse(R"({"error":"authorization_pending"})");
        }
        return Json::parse(R"({"access_token":"access-1","refresh_token":"refresh-1","expires_in":3599})");
    }
    if (url == "https://oauth2.googleapis.com/revoke") return Json::object();
    assert(bearer == "access-1");
    if (url.find("youtube/v3/channels?") != std::string::npos)
        return Json::parse(R"({"items":[{"snippet":{"title":"Samuele"}}]})");
    if (url.find("youtube/v3/subscriptions?") != std::string::npos) {
        assert(url.find("mine=true") != std::string::npos && url.find("order=unread") != std::string::npos);
        return Json::parse(R"({"items":[{"snippet":{"resourceId":{"channelId":"UCaaaaaaaaaaaaaaaaaaaaaa"}}},)"
                           R"({"snippet":{"resourceId":{"channelId":"not a channel"}}},)"
                           R"({"snippet":{"resourceId":{"channelId":"UCbbbbbbbbbbbbbbbbbbbbbb"}}}]})");
    }
    if (url.find("playlistId=UUaaaaaaaaaaaaaaaaaaaaaa") != std::string::npos)
        return Json::parse(R"({"items":[)"
                           R"({"contentDetails":{"videoId":"aaaaaaaaaa1","videoPublishedAt":"2026-11-05T10:00:00Z"}},)"
                           R"({"contentDetails":{"videoId":"aaaaaaaaaa2","videoPublishedAt":"2026-11-06T09:00:00Z"}},)"
                           R"({"contentDetails":{"videoId":"aaaaaaaaaa3","videoPublishedAt":"2026-11-06T11:00:00Z"}}]})");
    if (url.find("playlistId=UUbbbbbbbbbbbbbbbbbbbbbb") != std::string::npos)
        return Json::parse(R"({"items":[{"contentDetails":{"videoId":"bbbbbbbbbb1","videoPublishedAt":"2026-11-06T08:00:00Z"}}]})");
    if (url.find("youtube/v3/videos?") != std::string::npos)
        return Json::parse(R"({"items":[)"
                           R"({"id":"aaaaaaaaaa1","snippet":{"title":"Older","channelTitle":"A","liveBroadcastContent":"none"},"contentDetails":{"duration":"PT20M"}},)"
                           R"({"id":"aaaaaaaaaa2","snippet":{"title":"A Short","channelTitle":"A","liveBroadcastContent":"none"},"contentDetails":{"duration":"PT45S"}},)"
                           R"({"id":"aaaaaaaaaa3","snippet":{"title":"Premiere","channelTitle":"A","liveBroadcastContent":"upcoming"},"contentDetails":{"duration":"P0D"}},)"
                           R"({"id":"bbbbbbbbbb1","snippet":{"title":"Newest","channelTitle":"B","liveBroadcastContent":"none"},"contentDetails":{"duration":"PT1H2M3S"}}]})");
    status = 404;
    return Json::object();
}
} // namespace

int main() {
    assert(isoDurationSeconds("PT1H2M3S") == 3723);
    assert(isoDurationSeconds("PT45S") == 45);
    assert(isoDurationSeconds("P1DT2H") == 93600);
    assert(isoDurationSeconds("P0D") == 0);
    assert(isoDurationSeconds("1H") == -1 && isoDurationSeconds("PT") == 0 &&
           isoDurationSeconds("PT5") == -1 && isoDurationSeconds("PT1X") == -1);

    TemporaryDirectory directory;
    std::mutex mutex;
    std::vector<std::string> asked;
    bool deviceGranted = false, tokenRevoked = false;
    int64_t clock = 1793900000000LL;
    auto transport = [&](const std::string &url, const std::string &form, const std::string &bearer,
                         YoutubeAccounts::Reply &reply, std::string &) {
        std::lock_guard<std::mutex> lock(mutex);
        asked.push_back(url + (form.empty() ? "" : " " + form));
        reply.body = answerFor(url, form, bearer, reply.status, deviceGranted, tokenRevoked);
        return true;
    };
    const auto askedCount = [&](const std::string &part) {
        std::lock_guard<std::mutex> lock(mutex);
        size_t count = 0;
        for (const std::string &url : asked) count += url.find(part) != std::string::npos;
        return count;
    };
    const auto clockNow = [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        return clock;
    };
    const auto advance = [&](int64_t milliseconds) {
        std::lock_guard<std::mutex> lock(mutex);
        clock += milliseconds;
    };

    {
        YoutubeAccounts accounts(directory.path(), transport, clockNow);
        // Nothing to sign in with until the owner's client is saved.
        assert(!accounts.signIn("", "", "main")["ok"].boolean());
        assert(!accounts.status("main")["linked"].boolean());
        assert(!accounts.row("id", "secret", "main").valid());

        // The code comes back at once; the account is linked once it has been entered at Google.
        assert(accounts.signIn("client.apps.googleusercontent.com", "secret-123", "main")["ok"].boolean());
        Json status = accounts.status("main");
        assert(status["pending"]["code"].string() == "ABCD-EFGH");
        assert(status["pending"]["url"].string() == "https://www.google.com/device");
        assert(askedCount("youtube.readonly") == 1);
        advance(1500);
        assert(waitFor([&]() { return askedCount("grant-type%3Adevice_code") >= 1; }));
        assert(!accounts.status("main")["linked"].boolean() &&
               accounts.status("main")["pending"].isObject());
        {
            std::lock_guard<std::mutex> lock(mutex);
            deviceGranted = true;
        }
        advance(1500);
        assert(waitFor([&]() { return accounts.status("main")["linked"].boolean(); }));
        assert(!accounts.status("main")["pending"].valid());
        // Linking asks for the row straight away, which also names the account.
        assert(waitFor([&]() { return accounts.status("main")["name"].string() == "Samuele"; }));
        assert(waitFor([&]() { return accounts.row("client.apps.googleusercontent.com", "secret-123",
                                                   "main").valid(); }));
        const Json row = accounts.row("client.apps.googleusercontent.com", "secret-123", "main");
        assert(row["key"].string() == "youtube:subscriptions");
        // Newest first, without the Short or the premiere to come.
        assert(row["items"].size() == 2);
        assert(row["items"][0]["id"].string() == "bbbbbbbbbb1" && row["items"][0]["title"].string() == "Newest");
        assert(row["items"][0]["overview"].string() == "B" && row["items"][0]["type"].string() == "youtube");
        assert(row["items"][1]["id"].string() == "aaaaaaaaaa1");
        assert(row["items"][0]["image"].string() == "https://i.ytimg.com/vi/bbbbbbbbbb1/mqdefault.jpg");
        assert(askedCount("playlistId=UU") == 2); // the channel that is not one is skipped
        // Another profile has no account of its own.
        assert(!accounts.status("guest")["linked"].boolean() && !accounts.row("c", "s", "guest").valid());
    }

    // The token is kept apart from the rows, both readable by root alone, and survives a restart.
    struct stat info = {};
    assert(stat((directory.path() + "/youtube-accounts.json").c_str(), &info) == 0 &&
           (info.st_mode & 0777) == 0600);
    assert(readFile(directory.path() + "/youtube-accounts.json").find("refresh-1") != std::string::npos);
    assert(readFile(directory.path() + "/youtube-rows.json").find("refresh-1") == std::string::npos);
    {
        YoutubeAccounts restarted(directory.path(), transport, clockNow);
        const size_t before = askedCount("");
        assert(restarted.status("main")["linked"].boolean());
        assert(restarted.row("client.apps.googleusercontent.com", "secret-123", "main")["items"].size() == 2);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        assert(askedCount("") == before); // an hour has not passed
    }

    // Access withdrawn at Google: the next refresh finds out, and the profile is signed out.
    advance(61LL * 60 * 1000);
    {
        std::lock_guard<std::mutex> lock(mutex);
        tokenRevoked = true;
    }
    {
        YoutubeAccounts withdrawn(directory.path(), transport, clockNow);
        assert(withdrawn.row("client.apps.googleusercontent.com", "secret-123", "main").valid()); // the old row meanwhile
        assert(waitFor([&]() { return !withdrawn.status("main")["linked"].boolean(); }));
        assert(withdrawn.status("main")["error"].string() ==
               "Google no longer lets Rooted TV in. Sign in again.");
        assert(!withdrawn.row("client.apps.googleusercontent.com", "secret-123", "main").valid());
    }

    // Signing out forgets the token and the row on the TV and withdraws it at Google.
    {
        std::lock_guard<std::mutex> lock(mutex);
        tokenRevoked = false;
        deviceGranted = true;
    }
    {
        YoutubeAccounts again(directory.path(), transport, clockNow);
        assert(again.signIn("client.apps.googleusercontent.com", "secret-123", "main")["ok"].boolean());
        advance(1500);
        assert(waitFor([&]() { return again.status("main")["linked"].boolean(); }));
        again.signOut("main");
        assert(!again.status("main")["linked"].boolean());
        assert(askedCount("oauth2.googleapis.com/revoke token=refresh-1") == 1);
    }
    assert(readFile(directory.path() + "/youtube-accounts.json").find("refresh-1") == std::string::npos);
    std::printf("YouTube accounts passed.\n");
    return 0;
}
