#include "service/assistant.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace rootedtv;
using namespace rootedtv::service;

int main() {
    std::vector<Json> sent;
    std::vector<std::string> keys;
    int round = 0;
    Assistant assistant([&](const std::string &url, const std::string &key, const std::string &body,
                            Assistant::Reply &reply, std::string &) {
        assert(url == "https://generativelanguage.googleapis.com/v1beta/models/gemini-flash-latest:generateContent");
        keys.push_back(key);
        sent.push_back(Json::parse(body));
        reply.status = 200;
        // First Gemini searches, then shows a title (with a signature to be sent back), then answers.
        if (round == 0)
            reply.body = Json::parse(R"({"candidates":[{"content":{"role":"model","parts":[{"functionCall":{"name":"search_titles","args":{"query":"rocky"}}}]}}]})");
        else if (round == 1)
            reply.body = Json::parse(R"({"candidates":[{"content":{"role":"model","parts":[{"thoughtSignature":"abc","functionCall":{"name":"show_titles","args":{"titles":[{"type":"movie","id":1366}]}}}]}}]})");
        else
            reply.body = Json::parse(R"({"candidates":[{"content":{"role":"model","parts":[{"text":"Rocky is on Prime Video."}]}}]})");
        ++round;
        return true;
    });
    std::vector<std::string> called;
    const Json answer = assistant.ask("key-1", "", "  un film di boxe  ", "The TV is in IT.",
        [&called](const std::string &name, const Json &args) {
            called.push_back(name + ":" + args.dump());
            Json result = Json::object();
            result.set("results", Json::parse(R"([{"id":1366,"type":"movie","title":"Rocky"}])"));
            return result;
        });
    assert(answer["ok"].boolean() && answer["answer"].string() == "Rocky is on Prime Video.");
    assert(called.size() == 2 && called[0].find("search_titles") == 0 && called[1].find("show_titles") == 0);
    assert(keys.size() == 3 && keys[0] == "key-1");
    // The question goes in trimmed, with the instructions and the tools.
    assert(sent[0]["contents"][0]["parts"][0]["text"].string() == "un film di boxe");
    assert(sent[0]["systemInstruction"]["parts"][0]["text"].string().find("The TV is in IT.") != std::string::npos);
    assert(sent[0]["tools"][0]["functionDeclarations"].size() == 4);
    // Each call goes back as Gemini made it, signature and all, followed by the tool's answer.
    const Json third = sent[2]["contents"];
    assert(third.size() == 5 && third[3]["parts"][0]["thoughtSignature"].string() == "abc" &&
           third[4]["role"].string() == "user" &&
           third[4]["parts"][0]["functionResponse"]["name"].string() == "show_titles");

    // Gemini's own error is passed on; no key, or no question, asks nothing.
    Assistant refusing([](const std::string &, const std::string &, const std::string &,
                          Assistant::Reply &reply, std::string &) {
        reply.status = 400;
        reply.body = Json::parse(R"({"error":{"message":"API key not valid."}})");
        return true;
    });
    const Json refused = refusing.ask("bad", "gemini-test", "ciao", "", nullptr);
    assert(!refused["ok"].boolean() && refused["error"].string() == "Gemini: API key not valid.");
    assert(!assistant.ask("", "", "ciao", "", nullptr)["ok"].boolean());
    assert(!assistant.ask("k", "", "   ", "", nullptr)["ok"].boolean());
    std::printf("Assistant passed.\n");
    return 0;
}
