#include "service/lg_catalog.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace nativetv {
namespace service {
namespace {
const int64_t kRememberedMs = 24LL * 60 * 60 * 1000;
const int64_t kFailureRememberedMs = 60LL * 1000;
const size_t kCacheMax = 300;

std::string formEncode(const std::string &text) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    for (unsigned char c : text) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += static_cast<char>(c);
        } else {
            encoded += '%';
            encoded += kHex[c >> 4];
            encoded += kHex[c & 15];
        }
    }
    return encoded;
}

std::string percentDecode(const std::string &text) {
    std::string decoded;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            decoded += static_cast<char>(std::strtol(text.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            decoded += text[i];
        }
    }
    return decoded;
}

// Letters and digits, ASCII ones in lower case and accented ones as they are. Punctuation past ASCII
// (a dash, curly quotes, guillemets) is left out like ASCII's, as one catalogue's "–" is another's
// "-".
std::string comparable(const std::string &title) {
    std::string result;
    for (size_t i = 0; i < title.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(title[i]);
        const unsigned char next = i + 1 < title.size() ? static_cast<unsigned char>(title[i + 1]) : 0;
        if (c == 0xc2 && next >= 0xa0 && next <= 0xbf) { // U+00A0 to U+00BF
            ++i;
            continue;
        }
        if (c == 0xe2 && (next == 0x80 || next == 0x81) && i + 2 < title.size()) { // U+2000 to U+207F
            i += 2;
            continue;
        }
        if (c >= 'A' && c <= 'Z') result += static_cast<char>(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80)
            result += static_cast<char>(c);
    }
    return result;
}

// sdx's reply carries LG's server's answer as text inside it.
Json serverAnswer(const Json &reply) {
    if (!reply["returnValue"].boolean() || reply["serverResponse"]["code"].string() != "200")
        return Json();
    const Json answer = Json::parse(reply["serverResponse"]["response"].string());
    return answer.isObject() ? answer : Json();
}

bool allOf(const std::string &text, bool (*allowed)(unsigned char)) {
    return !text.empty() && std::all_of(text.begin(), text.end(), allowed);
}

bool gtiCharacter(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
}

bool disneyCharacter(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '=' || c == '&' || c == '-' || c == '_';
}

bool digit(unsigned char c) { return c >= '0' && c <= '9'; }

double number(const Json &value) {
    if (value.isNumber()) return value.number();
    char *end = nullptr;
    const std::string text = value.string();
    const double parsed = std::strtod(text.c_str(), &end);
    return end && end != text.c_str() ? parsed : 0;
}
} // namespace

namespace {
const char *const kStoreImageHost = "ngfts.lge.com/fts/gftsDownload.lge?biz_code=APP_STORE&func_code=";

bool storeImageName(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-';
}
} // namespace

std::string lgStoreImageRef(const std::string &url) {
    for (const char *scheme : {"http://", "https://"}) {
        const std::string prefix = std::string(scheme) + kStoreImageHost;
        if (url.compare(0, prefix.size(), prefix) != 0) continue;
        for (const char *kind : {"icon", "preview"}) {
            const std::string code = std::string(kind) == "icon" ? "APP_ICON" : "APP_PREVIEW";
            const std::string rest = code + "&file_path=/appstore/app/" + kind + "/";
            if (url.compare(prefix.size(), rest.size(), rest) != 0) continue;
            const std::string path = url.substr(prefix.size() + rest.size()); // 20230315/37810399.png
            const size_t slash = path.find('/');
            const std::string day = path.substr(0, slash);
            const std::string file = slash == std::string::npos ? "" : path.substr(slash + 1);
            const size_t dot = file.rfind('.');
            const std::string name = dot == std::string::npos ? "" : file.substr(0, dot);
            const std::string extension = dot == std::string::npos ? "" : file.substr(dot);
            if (day.size() != 8 || !allOf(day, digit) || name.size() > 96 ||
                !allOf(name, storeImageName) || (extension != ".png" && extension != ".jpg"))
                return "";
            return "lgstore:" + std::string(kind) + "/" + path;
        }
    }
    return "";
}

std::string lgStoreImageUrl(const std::string &ref) {
    const std::string prefix = "lgstore:";
    if (ref.compare(0, prefix.size(), prefix) != 0) return "";
    const std::string path = ref.substr(prefix.size());
    const bool icon = path.compare(0, 5, "icon/") == 0;
    const std::string url = std::string("https://") + kStoreImageHost +
                            (icon ? "APP_ICON" : "APP_PREVIEW") + "&file_path=/appstore/app/" + path;
    return lgStoreImageRef(url) == ref ? url : "";
}

Json lgCatalogSearchRequest(const std::string &query, const std::string &domains, int maxResults) {
    Json payload = Json::object();
    payload.set("serviceName", Json::text("sdp_secure"));
    payload.set("url", Json::text("search/retrieval"));
    payload.set("methodType", Json::text("REQ_SSL_POST_METHOD"));
    // The fields LG's Search app sends; LG's server reads them as a form.
    payload.set("bodyData", Json::text("service=universal_search&version=v2&query=" + formEncode(query) +
                                       "&start_index=1&max_results=" + std::to_string(maxResults) +
                                       "&domain=" + formEncode(domains)));
    return payload;
}

Json lgStoreAppRequest(const std::string &appId) {
    Json payload = Json::object();
    payload.set("serviceName", Json::text("sdp_secure"));
    payload.set("url", Json::text("app/app?app_id=" + formEncode(appId)));
    payload.set("methodType", Json::text("REQ_SSL_GET_METHOD"));
    return payload;
}

Json lgCatalogDocuments(const Json &reply) {
    const Json answer = serverAnswer(reply);
    if (!answer["response"]["results"].isArray()) return Json();
    Json documents = Json::array();
    for (const Json &result : answer["response"]["results"].items()) {
        const std::string domain = result["domain"].string();
        for (const Json &doc : result["doc"].items()) {
            if (domain == "movie" || domain == "tvshow") {
                Json entry = Json::object();
                entry.set("domain", Json::text(domain));
                entry.set("title", Json::text(doc["title"].string()));
                entry.set("appId", Json::text(doc["cp_thumbnail"][0]["cp_id"].string()));
                entry.set("contentId", Json::text(doc["content_id"].string()));
                if (!entry["appId"].string().empty() && !entry["contentId"].string().empty())
                    documents.add(entry);
            } else if ((domain == "app" || domain == "premium") && doc.isObject()) {
                Json entry = Json::object();
                for (const std::string &name : doc.keys()) entry.set(name, doc[name]);
                entry.set("domain", Json::text(domain));
                documents.add(entry);
            }
        }
    }
    return documents;
}

Json lgCatalogTitleIds(const Json &documents, const std::string &type,
                       const std::vector<std::string> &titles) {
    const std::string domain = type == "tv" ? "tvshow" : "movie";
    std::vector<std::string> wanted;
    for (const std::string &title : titles)
        if (!comparable(title).empty()) wanted.push_back(comparable(title));
    Json ids = Json::object();
    for (const Json &doc : documents.items()) {
        const std::string appId = doc["appId"].string();
        if (doc["domain"].string() != domain || appId.empty() || ids[appId].valid() ||
            std::find(wanted.begin(), wanted.end(), comparable(doc["title"].string())) == wanted.end())
            continue;
        ids.set(appId, doc["contentId"]);
    }
    return ids;
}

std::string lgCatalogTarget(const std::string &service, const std::string &type,
                            const std::string &contentId) {
    if (service == "prime") {
        const std::string prefix = "amzn1.dv.gti.";
        if (contentId.compare(0, prefix.size(), prefix) != 0 || contentId.size() > 64 ||
            !allOf(contentId, gtiCharacter))
            return "";
        // Tried on webOS 3.4's Prime Video 3.12: "watch?gti=" plays a film once a profile is chosen;
        // a series' GTI opens its page, where the app's own Resume is.
        return type == "movie" ? "watch?gti=" + contentId : contentId;
    }
    if (service == "disney") {
        // Disney+ 24.5.8 plays "page=play&id=<uuid>&distributionPartner=lg" once a profile is chosen.
        if (contentId.size() > 160 || !allOf(contentId, disneyCharacter) ||
            contentId.find("id=") == std::string::npos)
            return "";
        return contentId;
    }
    if (service == "netflix") {
        // LG's ID is a catalogue URL, ".../catalog/titles/movies/81458416&source_type=4&...".
        const std::string decoded = percentDecode(contentId);
        for (const char *kind : {"/titles/movies/", "/titles/series/"}) {
            const size_t at = decoded.find(kind);
            if (at == std::string::npos) continue;
            size_t end = at + std::string(kind).size();
            const size_t start = end;
            while (end < decoded.size() && digit(static_cast<unsigned char>(decoded[end]))) ++end;
            const std::string id = decoded.substr(start, end - start);
            if (id.size() < 5 || id.size() > 12 || id[0] == '0') return "";
            return "m=https://www.netflix.com/title/" + id + "&source_type=4";
        }
    }
    return "";
}

Json lgStoreApps(const Json &documents, size_t limit) {
    Json apps = Json::array();
    std::vector<std::string> seen;
    for (const Json &doc : documents.items()) {
        const std::string domain = doc["domain"].string();
        const std::string id = doc["app_id"].string();
        if ((domain != "app" && domain != "premium") || id.empty() || id.size() > 128 ||
            std::find(seen.begin(), seen.end(), id) != seen.end())
            continue;
        if (apps.size() >= limit) break;
        seen.push_back(id);
        Json app = Json::object();
        app.set("id", Json::text(id));
        app.set("title", Json::text(doc["title"].string()));
        app.set("icon", Json::text(lgStoreImageRef(doc["thumbnail"].string())));
        app.set("color", Json::text(doc["icon_bg_color"].string()));
        app.set("price", Json::text(doc["price"].string()));
        app.set("free", Json::flag(doc["free_flag"].string() == "Y"));
        app.set("rating", Json::numeric(std::round(number(doc["star_score"]) * 10) / 10));
        apps.add(app);
    }
    return apps;
}

Json lgStoreApp(const Json &reply) {
    const Json app = serverAnswer(reply)["app"];
    if (!app.isObject() || app["id"].string().empty()) return Json();
    Json page = Json::object();
    page.set("id", app["id"]);
    page.set("title", Json::text(app["name"].string()));
    page.set("icon", Json::text(lgStoreImageRef(app["iconURL"].string())));
    page.set("color", Json::text(app["iconColor"].string()));
    page.set("seller", Json::text(app["sellerName"].string()));
    page.set("category", Json::text(app["categoryName"].string()));
    page.set("description", Json::text(app["description"].string()));
    page.set("version", Json::text(app["version"].string()));
    page.set("price", Json::text(app["displayPrice"].string()));
    page.set("free", Json::flag(app["bFree"].string() == "Y"));
    page.set("premium", Json::flag(app["bPremium"].boolean()));
    page.set("rating", Json::numeric(std::round(number(app["evaluationAverage"]) * 10) / 10));
    page.set("ratings", Json::numeric(number(app["evaluationCount"])));
    page.set("size", Json::numeric(number(app["appFileList"][0]["packFileSize"])));
    page.set("installedSize", Json::numeric(number(app["appFileList"][0]["unpackFileSize"])));
    Json pictures = Json::array();
    for (const Json &preview : app["appPreviewList"].items()) {
        const std::string picture = lgStoreImageRef(preview["previewURL"].string());
        if (pictures.size() < 6 && !picture.empty()) pictures.add(Json::text(picture));
    }
    page.set("pictures", pictures);
    return page;
}

Json lgInstallProgress(const Json &message) {
    Json progress = Json::object();
    const Json details = message["details"];
    const std::string state = details["state"].string();
    const std::string simple = details["simpleStatus"].string();
    progress.set("progress", Json::numeric(std::max(0.0, std::min(100.0, number(details["progress"])))));
    if (state == "installed") {
        progress.set("state", Json::text("installed"));
    } else if (!message["returnValue"].boolean(true) || details["errorCode"].valid() ||
               message["errorCode"].valid() || state.find("fail") != std::string::npos ||
               state.find("error") != std::string::npos || simple == "error") {
        progress.set("state", Json::text("failed"));
        std::string error = details["reason"].string();
        if (error.empty()) error = message["errorText"].string();
        if (error.empty()) error = state;
        progress.set("error", Json::text(error));
    } else {
        progress.set("state", Json::text(simple == "install" ? "installing" : "downloading"));
    }
    return progress;
}

LgCatalog::LgCatalog(Caller call, Clock clock) : call_(std::move(call)), clock_(std::move(clock)) {
    if (!clock_)
        clock_ = [] {
            return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        };
}

bool LgCatalog::cached(const std::string &key, Json &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = cache_.find(key);
    if (found == cache_.end()) return false;
    const int64_t lifetime = found->second.value.valid() ? kRememberedMs : kFailureRememberedMs;
    if (clock_() - found->second.at > lifetime) return false;
    value = found->second.value;
    return true;
}

void LgCatalog::remember(const std::string &key, const Json &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cache_.size() >= kCacheMax) cache_.clear();
    Entry entry;
    entry.at = clock_();
    entry.value = value;
    cache_[key] = entry;
}

Json LgCatalog::titleIds(const std::string &key, const std::string &type,
                         const std::vector<std::string> &titles) {
    Json ids;
    if (key.empty() || titles.empty() || titles.front().empty() || !call_) return Json::object();
    if (cached("title:" + key, ids)) return ids.valid() ? ids : Json::object();
    Json reply;
    const Json documents =
        call_(lgCatalogSearchRequest(titles.front(), type == "tv" ? "tvshow" : "movie", 30), reply)
            ? lgCatalogDocuments(reply)
            : Json();
    ids = documents.valid() ? lgCatalogTitleIds(documents, type, titles) : Json();
    remember("title:" + key, ids);
    return ids.valid() ? ids : Json::object();
}

Json LgCatalog::searchApps(const std::string &query, size_t limit) {
    Json apps;
    if (query.empty() || !call_) return Json();
    if (cached("apps:" + query, apps)) return apps;
    Json reply;
    const Json documents =
        call_(lgCatalogSearchRequest(query, "app,premium", 40), reply) ? lgCatalogDocuments(reply) : Json();
    apps = documents.valid() ? lgStoreApps(documents, limit) : Json();
    remember("apps:" + query, apps);
    return apps;
}

Json LgCatalog::app(const std::string &appId) {
    Json page;
    if (appId.empty() || !call_) return Json();
    if (cached("app:" + appId, page)) return page;
    Json reply;
    page = call_(lgStoreAppRequest(appId), reply) ? lgStoreApp(reply) : Json();
    remember("app:" + appId, page);
    return page;
}

} // namespace service
} // namespace nativetv
