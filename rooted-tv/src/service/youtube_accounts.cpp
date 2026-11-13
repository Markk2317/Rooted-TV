#include "service/youtube_accounts.hpp"
#include "domain/say.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <curl/curl.h>

namespace rootedtv {
namespace service {
namespace {
const char *const kScope = "https://www.googleapis.com/auth/youtube.readonly";
const char *const kDeviceCodeUrl = "https://oauth2.googleapis.com/device/code";
const char *const kTokenUrl = "https://oauth2.googleapis.com/token";
const char *const kRevokeUrl = "https://oauth2.googleapis.com/revoke";
const char *const kApi = "https://www.googleapis.com/youtube/v3/";
const int64_t kRowLifetimeMs = 60LL * 60 * 1000;
const int64_t kRetryMs = 10LL * 60 * 1000;
const size_t kChannels = 25;
const size_t kPerChannel = 3;
const size_t kPerRow = 16;
// Shorts are a minute or less; the row is for the TV.
const long long kShortestSeconds = 61;
const size_t kMaximumResponseBytes = 2 * 1024 * 1024;

bool initializeCurl() {
    static std::once_flag once;
    static CURLcode result = CURLE_FAILED_INIT;
    std::call_once(once, []() { result = curl_global_init(CURL_GLOBAL_DEFAULT); });
    return result == CURLE_OK;
}

size_t appendBody(char *data, size_t size, size_t count, void *context) {
    if (size && count > std::numeric_limits<size_t>::max() / size) return 0;
    const size_t bytes = size * count;
    std::string *body = static_cast<std::string *>(context);
    if (bytes > kMaximumResponseBytes - std::min(body->size(), kMaximumResponseBytes)) return 0;
    body->append(data, bytes);
    return bytes;
}

int cancelTransfer(void *context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<const std::atomic<bool> *>(context)->load() ? 1 : 0;
}

std::string urlEncode(const std::string &text) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;
    for (unsigned char value : text) {
        if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '-' || value == '_' || value == '.' ||
            value == '~')
            encoded << static_cast<char>(value);
        else
            encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<unsigned>(value);
    }
    return encoded.str();
}

bool curlSend(const std::string &url, const std::string &form, const std::string &bearer,
              const std::atomic<bool> &stopping, YoutubeAccounts::Reply &reply,
              std::string &error) {
    if (!initializeCurl()) {
        error = "HTTP initialization failed";
        return false;
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
        error = "Could not create a connection";
        return false;
    }
    std::string body;
    struct curl_slist *headers = nullptr;
    if (!bearer.empty())
        headers = curl_slist_append(headers, ("Authorization: Bearer " + bearer).c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if (!form.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(form.size()));
    }
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#pragma GCC diagnostic pop
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "tvweb");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, cancelTransfer);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stopping);
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    if (headers) curl_slist_free_all(headers);
    if (code != CURLE_OK) {
        error = curl_easy_strerror(code);
        return false;
    }
    reply.status = status;
    reply.body = Json::parse(body);
    return true;
}

int64_t systemNow() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// At most `maximum` characters, never cutting one in two.
std::string utf8Prefix(const std::string &text, size_t maximum) {
    size_t offset = 0, characters = 0;
    while (offset < text.size() && characters < maximum) {
        const unsigned char first = static_cast<unsigned char>(text[offset]);
        size_t width = (first & 0xe0) == 0xc0 ? 2 : (first & 0xf0) == 0xe0 ? 3
                     : (first & 0xf8) == 0xf0 ? 4 : 1;
        if (offset + width > text.size()) break;
        offset += width;
        ++characters;
    }
    return text.substr(0, offset);
}

bool validVideoId(const std::string &id) {
    return id.size() == 11 && std::all_of(id.begin(), id.end(), [](unsigned char value) {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
               (value >= '0' && value <= '9') || value == '_' || value == '-';
    });
}

bool validChannelId(const std::string &id) {
    return id.size() == 24 && id.compare(0, 2, "UC") == 0 &&
           std::all_of(id.begin(), id.end(), [](unsigned char value) {
               return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                      (value >= '0' && value <= '9') || value == '_' || value == '-';
           });
}

// Google's own words for a failure, else the HTTP status.
std::string googleError(const YoutubeAccounts::Reply &reply) {
    const Json body = reply.body;
    if (body["error_description"].isString()) return body["error_description"].string();
    if (body["error"]["message"].isString()) return body["error"]["message"].string();
    if (body["error"].isString()) return body["error"].string();
    return "HTTP " + std::to_string(reply.status);
}

Json copyOf(const Json &value) { return value.valid() ? Json::parse(value.dump()) : Json(); }

bool writePrivate(const std::string &path, const std::string &contents) {
    const std::string temporary = path + ".tmp";
    const int fd = open(temporary.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    size_t written = 0;
    bool ok = fchmod(fd, 0600) == 0;
    while (ok && written < contents.size()) {
        const ssize_t count = write(fd, contents.data() + written, contents.size() - written);
        if (count <= 0) ok = false;
        else written += static_cast<size_t>(count);
    }
    if (close(fd) != 0) ok = false;
    if (ok && rename(temporary.c_str(), path.c_str()) == 0) return true;
    (void)unlink(temporary.c_str());
    return false;
}

Json readObject(const std::string &path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) return Json::object();
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const Json parsed = Json::parse(contents);
    return parsed.isObject() ? parsed : Json::object();
}

// The object without `key`; Json has no erase.
Json without(const Json &object, const std::string &key) {
    Json result = Json::object();
    for (const std::string &name : object.keys())
        if (name != key) result.set(name, copyOf(object[name]));
    return result;
}
} // namespace

long long isoDurationSeconds(const std::string &duration) {
    if (duration.empty() || duration[0] != 'P') return -1;
    long long total = 0, number = 0;
    bool digits = false, time = false;
    for (size_t index = 1; index < duration.size(); ++index) {
        const char character = duration[index];
        if (character >= '0' && character <= '9') {
            number = number * 10 + (character - '0');
            digits = true;
            if (number > 100000000LL) return -1;
            continue;
        }
        if (character == 'T' && !digits) {
            time = true;
            continue;
        }
        if (!digits) return -1;
        if (character == 'D' && !time) total += number * 86400;
        else if (character == 'H' && time) total += number * 3600;
        else if (character == 'M' && time) total += number * 60;
        else if (character == 'S' && time) total += number;
        else return -1;
        number = 0;
        digits = false;
    }
    return digits ? -1 : total;
}

YoutubeAccounts::YoutubeAccounts(std::string dataRoot, Transport transport, Clock clock)
    : transport_(std::move(transport)), clock_(std::move(clock)) {
    const std::string root = dataRoot.empty() ? "/var/lib/tvweb" : dataRoot;
    accountsPath_ = root + "/youtube-accounts.json";
    rowsPath_ = root + "/youtube-rows.json";
    if (!clock_) clock_ = systemNow;
    worker_ = std::thread(&YoutubeAccounts::workerLoop, this);
}

YoutubeAccounts::~YoutubeAccounts() {
    {
        // Under the lock, or the worker can test the flag, miss the wake-up and wait for ever.
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_.store(true);
    }
    changed_.notify_all();
    if (worker_.joinable()) worker_.join();
}

bool YoutubeAccounts::send(const std::string &url, const std::string &form,
                           const std::string &bearer, Reply &reply, std::string &error) {
    error.clear();
    if (stopping_.load()) {
        error = "service is stopping";
        return false;
    }
    if (transport_) return transport_(url, form, bearer, reply, error);
    return curlSend(url, form, bearer, stopping_, reply, error);
}

void YoutubeAccounts::loadLocked() {
    if (loaded_) return;
    loaded_ = true;
    accounts_ = readObject(accountsPath_);
    rows_ = readObject(rowsPath_);
}

void YoutubeAccounts::saveAccountsLocked() { (void)writePrivate(accountsPath_, accounts_.dump()); }

void YoutubeAccounts::saveRowsLocked() { (void)writePrivate(rowsPath_, rows_.dump()); }

Json YoutubeAccounts::signIn(const std::string &clientId, const std::string &clientSecret,
                             const std::string &profile) {
    Json result = Json::object();
    if (clientId.empty() || clientSecret.empty() || profile.empty()) {
        result.set("ok", Json::flag(false));
        result.set("error", Json::text(say::msg(
            "srv.youtube.noClient", "Save the Google client ID and secret first")));
        return result;
    }
    Reply reply;
    std::string error;
    const bool answered = send(kDeviceCodeUrl,
                               "client_id=" + urlEncode(clientId) + "&scope=" + urlEncode(kScope),
                               "", reply, error);
    const Json body = reply.body;
    if (!answered || reply.status != 200 || !body["device_code"].isString() ||
        !body["user_code"].isString()) {
        result.set("ok", Json::flag(false));
        Json variables = Json::object();
        variables.set("reason", Json::text(answered ? googleError(reply) : error));
        result.set("error", Json::text(say::msg(
            "srv.youtube.signInFailed", "Google did not give a sign-in code: {reason}", variables)));
        return result;
    }
    Pending pending;
    pending.profile = profile;
    pending.clientId = clientId;
    pending.clientSecret = clientSecret;
    pending.deviceCode = body["device_code"].string();
    pending.userCode = body["user_code"].string();
    pending.url = body["verification_url"].string("https://www.google.com/device");
    const int64_t now = clock_();
    pending.expiresAt = now + static_cast<int64_t>(body["expires_in"].number(1800)) * 1000;
    pending.intervalSeconds = std::max(1, static_cast<int>(body["interval"].number(5)));
    pending.nextPollAt = now + pending.intervalSeconds * 1000LL;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = pending;
        hasPending_ = true;
        errors_.erase(profile);
    }
    changed_.notify_all();
    result.set("ok", Json::flag(true));
    return result;
}

void YoutubeAccounts::signOut(const std::string &profile) {
    std::string refreshToken;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loadLocked();
        refreshToken = accounts_[profile]["refreshToken"].string();
        if (accounts_.has(profile)) {
            accounts_ = without(accounts_, profile);
            saveAccountsLocked();
        }
        if (rows_.has(profile)) {
            rows_ = without(rows_, profile);
            saveRowsLocked();
        }
        if (hasPending_ && pending_.profile == profile) hasPending_ = false;
        errors_.erase(profile);
        wanted_.erase(profile);
    }
    // Withdrawn at Google too, where it can be; the token is gone from the TV either way.
    if (!refreshToken.empty()) {
        Reply reply;
        std::string error;
        (void)send(kRevokeUrl, "token=" + urlEncode(refreshToken), "", reply, error);
    }
}

Json YoutubeAccounts::status(const std::string &profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    loadLocked();
    const Json account = accounts_[profile];
    Json result = Json::object();
    result.set("linked", Json::flag(!account["refreshToken"].string().empty()));
    result.set("name", Json::text(account["name"].string()));
    const int64_t now = clock_();
    if (hasPending_ && pending_.profile == profile && now < pending_.expiresAt) {
        Json pending = Json::object();
        pending.set("code", Json::text(pending_.userCode));
        pending.set("url", Json::text(pending_.url));
        pending.set("expiresInMs", Json::numeric(static_cast<double>(pending_.expiresAt - now)));
        result.set("pending", pending);
    } else {
        result.set("pending", Json());
    }
    const auto error = errors_.find(profile);
    result.set("error", error == errors_.end() ? Json() : Json::text(error->second));
    return result;
}

Json YoutubeAccounts::row(const std::string &clientId, const std::string &clientSecret,
                          const std::string &profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    loadLocked();
    if (accounts_[profile]["refreshToken"].string().empty() || clientId.empty()) return Json();
    const int64_t now = clock_();
    const Json cached = rows_[profile];
    const bool fresh = cached.isObject() && now - static_cast<int64_t>(cached["at"].number()) <
                                                kRowLifetimeMs;
    const auto attempted = attemptedAt_.find(profile);
    if (!fresh && !wanted_.count(profile) &&
        (attempted == attemptedAt_.end() || now - attempted->second >= kRetryMs)) {
        wanted_[profile] = {clientId, clientSecret};
        changed_.notify_all();
    }
    if (!cached["items"].isArray() || !cached["items"].size()) return Json();
    Json result = Json::object();
    result.set("key", Json::text("youtube:subscriptions"));
    result.set("items", copyOf(cached["items"]));
    return result;
}

void YoutubeAccounts::workerLoop() {
    for (;;) {
        bool polling = false;
        Pending pending;
        std::string profile;
        Wanted wanted;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            for (;;) {
                if (stopping_.load()) return;
                const int64_t now = clock_();
                if (hasPending_ && now >= pending_.nextPollAt) {
                    polling = true;
                    pending = pending_;
                    break;
                }
                if (!wanted_.empty()) {
                    profile = wanted_.begin()->first;
                    wanted = wanted_.begin()->second;
                    wanted_.erase(wanted_.begin());
                    attemptedAt_[profile] = now;
                    break;
                }
                if (hasPending_)
                    changed_.wait_for(lock, std::chrono::milliseconds(
                        std::max<int64_t>(50, pending_.nextPollAt - now)));
                else
                    changed_.wait(lock);
            }
        }
        if (polling) poll(pending);
        else refresh(profile, wanted);
    }
}

void YoutubeAccounts::poll(Pending pending) {
    Reply reply;
    std::string error;
    const bool answered = send(kTokenUrl,
        "client_id=" + urlEncode(pending.clientId) + "&client_secret=" +
            urlEncode(pending.clientSecret) + "&device_code=" + urlEncode(pending.deviceCode) +
            "&grant_type=" + urlEncode("urn:ietf:params:oauth:grant-type:device_code"),
        "", reply, error);
    const std::string code = reply.body["error"].string();
    const int64_t now = clock_();
    std::lock_guard<std::mutex> lock(mutex_);
    loadLocked();
    // Signed out, or a new code asked for, while this one was being asked about.
    if (!hasPending_ || pending_.deviceCode != pending.deviceCode) return;
    const auto finish = [&](const std::string &message) {
        hasPending_ = false;
        if (!message.empty()) errors_[pending.profile] = message;
    };
    if (answered && reply.status == 200 && reply.body["refresh_token"].isString()) {
        Json account = Json::object();
        account.set("refreshToken", reply.body["refresh_token"]);
        account.set("name", Json::text(""));
        account.set("linkedAt", Json::numeric(static_cast<double>(now)));
        accounts_.set(pending.profile, account);
        saveAccountsLocked();
        finish("");
        // The row, and with it the account's name, straight away.
        wanted_[pending.profile] = {pending.clientId, pending.clientSecret};
        attemptedAt_.erase(pending.profile);
        if (rows_.has(pending.profile)) {
            rows_ = without(rows_, pending.profile);
            saveRowsLocked();
        }
        changed_.notify_all();
        return;
    }
    if (now >= pending.expiresAt)
        return finish(say::msg("srv.youtube.codeExpired",
                               "The code expired before it was entered. Sign in again."));
    if (!answered || code == "authorization_pending" || code == "slow_down") {
        if (code == "slow_down") pending_.intervalSeconds += 5;
        pending_.nextPollAt = now + pending_.intervalSeconds * 1000LL;
        return;
    }
    if (code == "access_denied")
        return finish(say::msg("srv.youtube.denied", "The sign-in was refused on Google's page."));
    Json variables = Json::object();
    variables.set("reason", Json::text(googleError(reply)));
    finish(say::msg("srv.youtube.linkFailed", "Google did not link the account: {reason}",
                    variables));
}

void YoutubeAccounts::refresh(const std::string &profile, const Wanted &wanted) {
    std::string refreshToken;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loadLocked();
        refreshToken = accounts_[profile]["refreshToken"].string();
    }
    if (refreshToken.empty()) return;
    const auto fail = [&](const std::string &message) {
        std::lock_guard<std::mutex> lock(mutex_);
        errors_[profile] = message;
    };

    Reply reply;
    std::string error;
    if (!send(kTokenUrl,
              "client_id=" + urlEncode(wanted.clientId) + "&client_secret=" +
                  urlEncode(wanted.clientSecret) + "&refresh_token=" + urlEncode(refreshToken) +
                  "&grant_type=refresh_token",
              "", reply, error))
        return fail(error);
    if (reply.status != 200 || !reply.body["access_token"].isString()) {
        // Access withdrawn at Google, or a token Google let lapse: the link is gone.
        if (reply.body["error"].string() == "invalid_grant") {
            std::lock_guard<std::mutex> lock(mutex_);
            if (accounts_[profile]["refreshToken"].string() == refreshToken) {
                accounts_ = without(accounts_, profile);
                saveAccountsLocked();
            }
            errors_[profile] = say::msg("srv.youtube.signedOut",
                                        "Google no longer lets Rooted TV in. Sign in again.");
            return;
        }
        return fail(googleError(reply));
    }
    const std::string token = reply.body["access_token"].string();

    // The account's channel name, once, to say who is signed in.
    bool named = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        named = !accounts_[profile]["name"].string().empty();
    }
    if (!named && send(std::string(kApi) + "channels?part=snippet&mine=true", "", token, reply,
                       error) && reply.status == 200) {
        const std::string name = reply.body["items"][0]["snippet"]["title"].string();
        std::lock_guard<std::mutex> lock(mutex_);
        if (!name.empty() && accounts_[profile]["refreshToken"].string() == refreshToken) {
            Json account = copyOf(accounts_[profile]);
            account.set("name", Json::text(utf8Prefix(name, 80)));
            accounts_.set(profile, account);
            saveAccountsLocked();
        }
    }

    // The channels with something new first, then each one's latest uploads.
    if (!send(std::string(kApi) + "subscriptions?part=snippet&mine=true&order=unread&maxResults=" +
                  std::to_string(kChannels),
              "", token, reply, error))
        return fail(error);
    if (reply.status != 200) return fail(googleError(reply));
    std::vector<std::pair<std::string, std::string>> uploads; // published, video
    for (const Json &subscription : reply.body["items"].items()) {
        const std::string channel = subscription["snippet"]["resourceId"]["channelId"].string();
        if (!validChannelId(channel)) continue;
        Reply listed;
        if (!send(std::string(kApi) + "playlistItems?part=contentDetails&maxResults=" +
                      std::to_string(kPerChannel) + "&playlistId=UU" + channel.substr(2),
                  "", token, listed, error) ||
            listed.status != 200)
            continue;
        for (const Json &entry : listed.body["items"].items()) {
            const std::string video = entry["contentDetails"]["videoId"].string();
            if (validVideoId(video))
                uploads.push_back({entry["contentDetails"]["videoPublishedAt"].string(), video});
        }
    }
    // Newest first: the dates are ISO 8601 in UTC, so they sort as text.
    std::stable_sort(uploads.begin(), uploads.end(),
                     [](const std::pair<std::string, std::string> &left,
                        const std::pair<std::string, std::string> &right) {
                         return left.first > right.first;
                     });
    if (uploads.size() > 50) uploads.resize(50);

    std::vector<Json> items;
    if (!uploads.empty()) {
        std::string ids;
        for (const auto &upload : uploads) ids += (ids.empty() ? "" : ",") + upload.second;
        if (!send(std::string(kApi) + "videos?part=snippet,contentDetails&id=" + ids, "", token,
                  reply, error))
            return fail(error);
        if (reply.status != 200) return fail(googleError(reply));
        std::map<std::string, Json> videos;
        for (const Json &video : reply.body["items"].items())
            videos[video["id"].string()] = video;
        for (const auto &upload : uploads) {
            const auto found = videos.find(upload.second);
            if (found == videos.end()) continue;
            const Json snippet = found->second["snippet"];
            if (snippet["liveBroadcastContent"].string() == "upcoming") continue;
            const long long seconds =
                isoDurationSeconds(found->second["contentDetails"]["duration"].string());
            if (seconds >= 0 && seconds < kShortestSeconds) continue;
            const std::string id = upload.second;
            Json item = Json::object();
            item.set("id", Json::text(id));
            item.set("type", Json::text("youtube"));
            item.set("title", Json::text(utf8Prefix(snippet["title"].string(), 120)));
            item.set("year", Json::text(""));
            item.set("rating", Json());
            item.set("overview", Json::text(utf8Prefix(snippet["channelTitle"].string(), 120)));
            item.set("poster", Json::text(""));
            item.set("backdrop", Json::text(""));
            item.set("image", Json::text("https://i.ytimg.com/vi/" + id + "/mqdefault.jpg"));
            item.set("wide", Json::text("https://i.ytimg.com/vi/" + id + "/maxresdefault.jpg"));
            items.push_back(item);
            if (items.size() == kPerRow) break;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (accounts_[profile]["refreshToken"].string() != refreshToken) return; // signed out meanwhile
    Json list = Json::array();
    for (const Json &item : items) list.add(item);
    Json cached = Json::object();
    cached.set("at", Json::numeric(static_cast<double>(clock_())));
    cached.set("items", list);
    rows_.set(profile, cached);
    saveRowsLocked();
    errors_.erase(profile);
}

} // namespace service
} // namespace rootedtv
