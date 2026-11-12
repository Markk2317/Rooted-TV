#include "daemon/lg_store_installs.hpp"
#include "service/lg_catalog.hpp"
#include <chrono>
#include <utility>

namespace nativetv {
namespace daemon {
namespace {
// An app of a few hundred megabytes on a slow line; the install service ends sooner on an error.
const std::chrono::minutes kInstallTimeout(20);
const size_t kFrameMax = 64 * 1024;

Json stateOf(const std::string &state, double progress, const std::string &error = "") {
    Json value = Json::object();
    value.set("state", Json::text(state));
    value.set("progress", Json::numeric(progress));
    if (!error.empty()) value.set("error", Json::text(error));
    return value;
}
} // namespace

LgStoreInstalls::LgStoreInstalls(ChildProcessRunner &children, std::string lunaSendPath)
    : children_(children), lunaSendPath_(std::move(lunaSendPath)) {}

LgStoreInstalls::~LgStoreInstalls() {
    std::shared_ptr<std::atomic<bool>> cancellation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancellation = cancellation_;
    }
    if (cancellation) cancellation->store(true);
    if (worker_.joinable()) worker_.join();
}

bool LgStoreInstalls::start(const std::string &appId, std::string &error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_.empty()) {
        error = running_ == appId ? "" : "another app is being installed";
        return running_ == appId;
    }
    if (worker_.joinable()) worker_.join();
    running_ = appId;
    states_[appId] = stateOf("downloading", 0);
    cancellation_ = std::make_shared<std::atomic<bool>>(false);
    worker_ = std::thread([this, appId] { run(appId); });
    return true;
}

Json LgStoreInstalls::status(const std::string &appId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = states_.find(appId);
    return found == states_.end() ? Json() : Json::parse(found->second.dump());
}

void LgStoreInstalls::update(const std::string &appId, const Json &progress) {
    std::lock_guard<std::mutex> lock(mutex_);
    states_[appId] = progress;
}

void LgStoreInstalls::run(const std::string &appId) {
    std::shared_ptr<std::atomic<bool>> cancellation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancellation = cancellation_;
    }
    // The store's own request: to the TV's internal storage, with the install service's toasts.
    Json payload = Json::object();
    payload.set("id", Json::text(appId));
    payload.set("subscribe", Json::flag(true));
    payload.set("silence", Json::flag(false));
    Json target = Json::object();
    target.set("deviceId", Json::text("INTERNAL_STORAGE"));
    payload.set("target", target);

    std::string frames;
    std::string ended;
    const auto readFrame = [&](const std::string &frame) {
        const Json message = Json::parse(frame);
        if (!message.isObject() || message["subscribed"].boolean()) return;
        const Json progress = service::lgInstallProgress(message);
        update(appId, progress);
        const std::string state = progress["state"].string();
        if (state == "installed" || state == "failed") {
            ended = state;
            cancellation->store(true);
        }
    };
    ChildOptions options;
    options.arguments = {lunaSendPath_, "-i", "luna://com.webos.appInstallService/install",
                         payload.dump()};
    options.timeout = kInstallTimeout;
    options.captureOutput = false;
    options.cancellation = cancellation;
    options.onStandardOutput = [&](const std::string &chunk) {
        if (!ended.empty()) return;
        frames += chunk;
        if (frames.size() > kFrameMax) frames.clear();
        for (size_t newline = frames.find('\n'); newline != std::string::npos && ended.empty();
             newline = frames.find('\n')) {
            const std::string frame = frames.substr(0, newline);
            frames.erase(0, newline + 1);
            if (!frame.empty()) readFrame(frame);
        }
    };
    const ChildResult result = children_.run(options);
    if (ended.empty() && !frames.empty()) readFrame(frames);
    if (ended.empty()) {
        const Json last = status(appId);
        update(appId, stateOf("failed", last["progress"].number(),
                              result.timedOut ? "the install took too long"
                                              : "the install service stopped answering"));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    running_.clear();
}

} // namespace daemon
} // namespace nativetv
