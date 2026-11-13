#include "service/home_picks.hpp"

#include "core/md5.hpp"
#include "domain/names.hpp"

#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <set>
#include <unistd.h>

namespace nativetv {
namespace service {
namespace {
int64_t systemNow() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string yearOf(const Json &item) {
    const Json year = item["year"];
    if (year.isNumber()) return std::to_string(static_cast<long>(year.number()));
    return year.string();
}

std::string serviceName(const std::string &key) {
    for (const auto &service : names::streamingServices())
        if (service.key == key) return service.name;
    return "";
}

std::string hoursOf(double milliseconds) {
    const long minutes = static_cast<long>(milliseconds / 60000 + .5);
    if (minutes < 60) return std::to_string(minutes) + " min";
    return std::to_string(minutes / 60) + " h" +
           (minutes % 60 ? " " + std::to_string(minutes % 60) + " min" : std::string());
}
} // namespace

HomePicks::HomePicks(std::string dataRoot, Assistant::Transport transport,
                     std::function<int64_t()> clock)
    : path_((dataRoot.empty() ? "/var/lib/tvweb" : std::move(dataRoot)) + "/home-picks.json"),
      transport_(transport ? std::move(transport) : Assistant::https()),
      clock_(clock ? std::move(clock) : systemNow) {}

HomePicks::~HomePicks() {
    wait();
}

void HomePicks::wait() {
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        worker.swap(worker_);
    }
    if (worker.joinable()) worker.join();
}

std::string HomePicks::keyOf(const Json &item) {
    const Json id = item["id"];
    const std::string number = id.isNumber() ? std::to_string(static_cast<long long>(id.number()))
                                             : id.string();
    return item["type"].string() + ":" + number;
}

std::string HomePicks::prompt(const Json &history, const Json &services, const Json &candidates,
                              const std::string &region, const std::string &language) {
    std::string text =
        "You choose \"Top picks for you\" for one viewer on a TV home screen, as Google TV does.\n"
        "The viewer's titles (what they watched, liked or saved to watch):\n";
    if (!history.size()) text += "- none yet\n";
    for (const Json &entry : history.items())
        text += "- " + entry["title"].string() + " (" +
                (entry["type"].string() == "tv" ? "series" : "film") +
                (yearOf(entry).empty() ? "" : ", " + yearOf(entry)) + "): " + entry["why"].string() +
                "\n";
    std::string watchedOn;
    for (const Json &service : services.items()) {
        const std::string name = serviceName(service["service"].string());
        if (!name.empty())
            watchedOn += (watchedOn.empty() ? "" : ", ") + name + " (" +
                         hoursOf(service["playedMs"].number()) + ")";
    }
    if (!watchedOn.empty())
        text += "Where they watch, by time played in the last 30 days: " + watchedOn + ".\n";
    text += "Titles on offer in country " + (region.empty() ? std::string("unknown") : region) +
            ", as key | title (kind, year) [where to watch]:\n";
    for (const Json &item : candidates.items()) {
        const std::string service = serviceName(item["service"].string());
        text += keyOf(item) + " | " + item["title"].string() + " (" +
                (item["type"].string() == "tv" ? "series" : "film") +
                (yearOf(item).empty() ? "" : ", " + yearOf(item)) + ")" +
                (service.empty() ? "" : " [" + service + "]") + "\n";
    }
    text += "Choose up to " + std::to_string(kPicksMax) +
            " titles from the offer this viewer is most likely to enjoy, best first, none they "
            "already have, favouring the services they watch most. For each, give a short reason in " +
            (language.empty() ? std::string("English") : "the language " + language) +
            " of at most six words, such as "
            "\"Because you watched <title>\" naming one of their titles, or \"Popular on <service>\" "
            "or \"Trending this week\" when none fits. Answer only with a JSON array of objects "
            "{\"key\": <key from the offer>, \"because\": <reason>}.";
    return text;
}

Json HomePicks::parse(const std::string &answer, const Json &candidates) {
    std::set<std::string> offered;
    for (const Json &item : candidates.items()) offered.insert(keyOf(item));
    // The answer is JSON, perhaps inside a fenced block.
    const size_t start = answer.find('['), end = answer.rfind(']');
    Json result = Json::array();
    if (start == std::string::npos || end == std::string::npos || end < start) return result;
    const Json parsed = Json::parse(answer.substr(start, end - start + 1));
    std::set<std::string> taken;
    for (const Json &pick : parsed.items()) {
        const std::string key = pick["key"].string();
        if (!offered.count(key) || !taken.insert(key).second) continue;
        std::string because = pick["because"].string();
        if (because.size() > 60) because.clear();
        Json entry = Json::object();
        entry.set("key", Json::text(key));
        entry.set("because", Json::text(because));
        result.add(entry);
        if (result.size() == kPicksMax) break;
    }
    return result;
}

Json HomePicks::load() const {
    std::ifstream input(path_.c_str(), std::ios::binary);
    if (!input) return Json::object();
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const Json parsed = Json::parse(contents);
    return parsed.isObject() ? parsed : Json::object();
}

void HomePicks::save(const Json &document) const {
    const std::string temporary = path_ + ".tmp";
    const std::string contents = document.dump();
    const int fd = open(temporary.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) return;
    size_t written = 0;
    bool ok = true;
    while (written < contents.size()) {
        const ssize_t count = write(fd, contents.data() + written, contents.size() - written);
        if (count <= 0) { ok = false; break; }
        written += static_cast<size_t>(count);
    }
    if (close(fd) != 0) ok = false;
    if (!ok || rename(temporary.c_str(), path_.c_str()) != 0) (void)unlink(temporary.c_str());
}

Json HomePicks::picks(const std::string &key, const std::string &model, const std::string &profile,
                      const Json &history, const Json &services, const Json &candidates,
                      const std::string &region, const std::string &language) {
    if (key.empty() || !candidates.size()) return Json::array();
    // Asked again when the profile's titles or its services' order change, not when the offer
    // moves on (the trending titles change daily, and a day is how long an answer is kept anyway)
    // nor with every minute played.
    std::string order;
    for (const Json &service : services.items()) order += service["service"].string() + ",";
    const std::string inputs =
        md5Hex(history.dump() + "|" + order + "|" + region + (language.empty() ? "" : "|" + language));
    const int64_t now = clock_();
    std::lock_guard<std::mutex> lock(mutex_);
    const Json kept = load()[profile];
    const bool fresh = kept["inputs"].string() == inputs &&
                       now - static_cast<int64_t>(kept["at"].number()) < kLifetimeMs;
    const bool failedRecently = failedInputs_ == inputs && now - failedAt_ < kRetryMs;
    if (!fresh && !failedRecently && !busy_.load()) {
        if (worker_.joinable()) worker_.join();
        busy_.store(true);
        Json limited = Json::array();
        for (const Json &item : candidates.items())
            if (limited.size() < kCandidatesMax) limited.add(item);
        worker_ = std::thread(&HomePicks::ask, this, key, model, profile, inputs, history, services,
                              limited, region, language);
    }
    return kept["picks"].isArray() ? kept["picks"] : Json::array();
}

void HomePicks::ask(std::string key, std::string model, std::string profile, std::string inputs,
                    Json history, Json services, Json candidates, std::string region,
                    std::string language) {
    Json request = Json::object();
    Json contents = Json::array();
    Json user = Json::object();
    user.set("role", Json::text("user"));
    Json parts = Json::array();
    Json part = Json::object();
    part.set("text", Json::text(prompt(history, services, candidates, region, language)));
    parts.add(part);
    user.set("parts", parts);
    contents.add(user);
    request.set("contents", contents);
    Json generation = Json::object();
    generation.set("temperature", Json::numeric(0.3));
    generation.set("responseMimeType", Json::text("application/json"));
    request.set("generationConfig", generation);
    const std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" +
                            (model.empty() ? std::string("gemini-flash-latest") : model) +
                            ":generateContent";
    Assistant::Reply reply;
    std::string error;
    Json picks = Json::array();
    if (transport_(url, key, request.dump(), reply, error) && reply.status == 200) {
        std::string answer;
        for (const Json &answerPart : reply.body["candidates"][0]["content"]["parts"].items())
            if (answerPart["text"].isString() && !answerPart["thought"].boolean())
                answer += answerPart["text"].string();
        picks = parse(answer, candidates);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (picks.size()) {
        Json document = load();
        Json entry = Json::object();
        entry.set("inputs", Json::text(inputs));
        entry.set("at", Json::numeric(static_cast<double>(clock_())));
        entry.set("picks", picks);
        document.set(profile, entry);
        save(document);
    } else {
        failedInputs_ = inputs;
        failedAt_ = clock_();
    }
    busy_.store(false);
}

} // namespace service
} // namespace nativetv
