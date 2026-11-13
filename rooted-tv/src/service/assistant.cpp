#include "service/assistant.hpp"
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <curl/curl.h>

namespace rootedtv {
namespace service {
namespace {
const size_t kMaximumBytes = 1024 * 1024;
const int kRoundsMax = 6;

size_t appendBody(char *data, size_t size, size_t count, void *context) {
    if (size && count > std::numeric_limits<size_t>::max() / size) return 0;
    const size_t bytes = size * count;
    std::string *body = static_cast<std::string *>(context);
    if (body->size() + bytes > kMaximumBytes) return 0;
    body->append(data, bytes);
    return bytes;
}

bool curlPost(const std::string &url, const std::string &key, const std::string &body,
              Assistant::Reply &reply, std::string &error) {
    static std::once_flag once;
    std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
    CURL *curl = curl_easy_init();
    if (!curl) {
        error = "Could not create a connection";
        return false;
    }
    std::string answer;
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("x-goog-api-key: " + key).c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#pragma GCC diagnostic pop
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "tvweb");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &answer);
    const CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &reply.status);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    if (code != CURLE_OK) {
        error = curl_easy_strerror(code);
        return false;
    }
    reply.body = Json::parse(answer);
    return true;
}

Json text(const std::string &value) {
    Json part = Json::object();
    part.set("text", Json::text(value));
    return part;
}

Json property(const std::string &type, const std::string &description) {
    Json value = Json::object();
    value.set("type", Json::text(type));
    value.set("description", Json::text(description));
    return value;
}

Json function(const std::string &name, const std::string &description, const Json &properties,
              const std::vector<std::string> &required) {
    Json parameters = Json::object();
    parameters.set("type", Json::text("object"));
    parameters.set("properties", properties);
    Json needed = Json::array();
    for (const std::string &field : required) needed.add(Json::text(field));
    parameters.set("required", needed);
    Json declaration = Json::object();
    declaration.set("name", Json::text(name));
    declaration.set("description", Json::text(description));
    declaration.set("parameters", parameters);
    return declaration;
}
} // namespace

Assistant::Transport Assistant::https() {
    return curlPost;
}

Assistant::Assistant(Transport transport) : transport_(std::move(transport)) {
    if (!transport_) transport_ = curlPost;
}

Json Assistant::tools() {
    Json declarations = Json::array();
    Json search = Json::object();
    search.set("query", property("string", "Words of the title, or a person's name."));
    declarations.add(function("search_titles",
        "Finds films and series by title on TMDB. Each result has type (movie or tv) and id.",
        search, {"query"}));
    Json shown = Json::object();
    Json titles = property("array", "The titles to show, best first, at most eight.");
    Json item = Json::object();
    item.set("type", Json::text("object"));
    Json itemProperties = Json::object();
    itemProperties.set("type", property("string", "movie or tv"));
    itemProperties.set("id", property("integer", "The title's TMDB id from search_titles."));
    item.set("properties", itemProperties);
    titles.set("items", item);
    shown.set("titles", titles);
    declarations.add(function("show_titles",
        "Shows titles on the TV under the answer, as cards the viewer can open. Use it whenever "
        "the answer names films or series found with search_titles.",
        shown, {"titles"}));
    Json open = Json::object();
    open.set("type", property("string", "movie or tv"));
    open.set("id", property("integer", "The title's TMDB id from search_titles."));
    declarations.add(function("open_title",
        "Opens a film or series in the streaming app installed on this TV that has it in this "
        "country (Netflix, Prime Video or Disney+). Answers whether it opened, and where.",
        open, {"type", "id"}));
    Json control = Json::object();
    Json action = property("string", "What to do.");
    Json actions = Json::array();
    for (const char *name : {"volume", "volume_up", "volume_down", "mute", "unmute", "screen_off",
                             "input", "open_app", "play", "pause"})
        actions.add(Json::text(name));
    action.set("enum", actions);
    control.set("action", action);
    control.set("value", property("string",
        "For volume: 0 to 100. For input: the HDMI number, 1 to 4. For open_app: the app's name."));
    declarations.add(function("tv_control", "Controls this TV.", control, {"action"}));
    Json tool = Json::object();
    tool.set("functionDeclarations", declarations);
    Json list = Json::array();
    list.add(tool);
    return list;
}

Json Assistant::ask(const std::string &key, const std::string &model, const std::string &question,
                    const std::string &context, const Tool &tool) {
    Json result = Json::object();
    const auto failed = [&result](const std::string &error) {
        result.set("ok", Json::flag(false));
        result.set("error", Json::text(error));
        return result;
    };
    if (key.empty()) return failed("No Gemini key is saved.");
    std::string trimmed = question;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
    if (trimmed.empty() || trimmed.size() > 1000) return failed("Ask a question of up to 500 characters.");

    Json request = Json::object();
    Json instructions = Json::object();
    Json instructionParts = Json::array();
    instructionParts.add(text(
        "You are the assistant on the home screen of a TV (Rooted TV on an LG TV). " + context +
        " Answer in the language of the question, in two or three short sentences: the answer is "
        "read on a TV across the room. Use search_titles to find films and series and show_titles "
        "to put the ones you name on screen. Open a title or control the TV only when asked to. "
        "Never make up titles: name only what search_titles found."));
    instructions.set("parts", instructionParts);
    request.set("systemInstruction", instructions);
    Json contents = Json::array();
    Json user = Json::object();
    user.set("role", Json::text("user"));
    Json userParts = Json::array();
    userParts.add(text(trimmed));
    user.set("parts", userParts);
    contents.add(user);
    request.set("tools", tools());
    Json generation = Json::object();
    generation.set("temperature", Json::numeric(0.4));
    generation.set("maxOutputTokens", Json::numeric(800));
    request.set("generationConfig", generation);
    const std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" +
                            (model.empty() ? std::string("gemini-flash-latest") : model) +
                            ":generateContent";

    for (int round = 0; round < kRoundsMax; ++round) {
        request.set("contents", contents);
        Reply reply;
        std::string error;
        if (!transport_(url, key, request.dump(), reply, error)) return failed(error);
        if (reply.status != 200) {
            const std::string message = reply.body["error"]["message"].string();
            return failed(message.empty() ? "Gemini answered HTTP " + std::to_string(reply.status)
                                          : "Gemini: " + message);
        }
        const Json content = reply.body["candidates"][0]["content"];
        if (!content["parts"].isArray()) return failed("Gemini gave no answer.");
        std::string answer;
        Json responses = Json::array();
        for (const Json &part : content["parts"].items()) {
            if (part["text"].isString() && !part["thought"].boolean()) answer += part["text"].string();
            const Json call = part["functionCall"];
            if (!call.isObject()) continue;
            Json done;
            try { done = tool ? tool(call["name"].string(), call["args"]) : Json(); } catch (...) {}
            if (!done.isObject()) {
                done = Json::object();
                done.set("error", Json::text("not available"));
            }
            Json response = Json::object();
            response.set("name", call["name"]);
            response.set("response", done);
            Json responsePart = Json::object();
            responsePart.set("functionResponse", response);
            responses.add(responsePart);
        }
        if (!responses.size()) {
            result.set("ok", Json::flag(true));
            result.set("answer", Json::text(answer));
            return result;
        }
        // The model's turn goes back as it came, signatures and all, then the tools' answers.
        Json modelTurn = Json::parse(content.dump());
        if (!modelTurn["role"].isString()) modelTurn.set("role", Json::text("model"));
        contents.add(modelTurn);
        Json toolTurn = Json::object();
        toolTurn.set("role", Json::text("user"));
        toolTurn.set("parts", responses);
        contents.add(toolTurn);
    }
    return failed("Gemini did not finish its answer.");
}

} // namespace service
} // namespace rootedtv
