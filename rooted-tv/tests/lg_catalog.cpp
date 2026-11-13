#include "service/home_open.hpp"
#include "service/lg_catalog.hpp"
#include <cassert>
#include <iostream>

using namespace rootedtv;
using namespace rootedtv::service;

namespace {

// sdx's reply, LG's answer as text inside it, as the C6 gave it.
Json reply(const Json &answer, const std::string &code = "200") {
    Json server = Json::object();
    server.set("code", Json::text(code));
    server.set("message", Json::text(code == "200" ? "OK" : "Error"));
    server.set("response", Json::text(answer.dump()));
    Json result = Json::object();
    result.set("returnValue", Json::flag(true));
    result.set("serverResponse", server);
    return result;
}

Json titleDoc(const std::string &title, const std::string &app, const std::string &content) {
    return Json::parse("{\"title\":\"" + title + "\",\"content_id\":\"" + content +
                       "\",\"content_set_id\":\"com.lge.meta.crawler.X_IT\","
                       "\"cp_thumbnail\":[{\"cp_id\":\"" + app + "\",\"cp_thumbnail_url\":\"x\"}]}");
}

Json searchAnswer() {
    Json movies = Json::array();
    movies.add(titleDoc("Il cavaliere oscuro", "amazon.global",
                        "amzn1.dv.gti.4aa9f68b-5e9d-1955-a451-d4d0087ef56c"));
    movies.add(titleDoc("The Dark Knight", "ui30", "movie_id13713$movies/the-dark-knight"));
    movies.add(titleDoc("Il cavaliere oscuro: Il ritorno", "amazon.global",
                        "amzn1.dv.gti.1ea9f6f7-7781-df4e-60bf-d24b8365b375"));
    movies.add(titleDoc("Il Cavaliere Oscuro", "amazon.global",
                        "amzn1.dv.gti.00000000-0000-0000-0000-000000000000"));
    movies.add(titleDoc("Encanto", "com.disney.disneyplus-prod",
                        "page=play&id=328b0ec7-6e50-4ead-aa7f-c8bb92e6f08a&distributionPartner=lg"));
    movies.add(titleDoc("Glass Onion - Knives Out", "netflix",
                        "-Q m=http%3A%2F%2Fapi-global.netflix.com%2Fcatalog%2Ftitles%2Fmovies%2F81458416"
                        "&source_type=4&trackId=13783779"));
    Json apps = Json::array();
    apps.add(Json::parse("{\"genre\":\"Intrattenimento\",\"icon_bg_color\":\"#191414\","
                         "\"title\":\"Spotify - Musica e podcast\",\"thumbnail\":"
                         "\"http://ngfts.lge.com/fts/gftsDownload.lge?biz_code=APP_STORE&func_code=APP_ICON&file_path="
                         "/appstore/app/icon/20230315/37810399.png\",\"price\":\"EUR 0,00\",\"free_flag\":\"Y\","
                         "\"star_score\":\"8.09\",\"app_id\":\"spotify-beehive\"}"));
    apps.add(Json::parse("{\"title\":\"Paid\",\"thumbnail\":\"javascript:alert(1)\","
                         "\"free_flag\":\"N\",\"price\":\"EUR 2,99\",\"star_score\":\"x\","
                         "\"app_id\":\"paid.app\"}"));
    Json web = Json::array();
    web.add(Json::parse("{\"query\":\"x\",\"url\":\"http://www.bing.com/search?q=\"}"));
    Json results = Json::array();
    const auto result = [&](const std::string &domain, const Json &docs) {
        Json entry = Json::object();
        entry.set("domain", Json::text(domain));
        entry.set("doc", docs);
        results.add(entry);
    };
    result("movie", movies);
    result("app", apps);
    result("web", web);
    Json response = Json::object();
    response.set("results", results);
    Json answer = Json::object();
    answer.set("response", response);
    return answer;
}

} // namespace

int main() {
    // The request is LG's Search app's own: a form, the query encoded.
    const Json request = lgCatalogSearchRequest("Il cavaliere oscuro & co", "movie", 30);
    assert(request["serviceName"].string() == "sdp_secure");
    assert(request["url"].string() == "search/retrieval");
    assert(request["methodType"].string() == "REQ_SSL_POST_METHOD");
    assert(request["bodyData"].string() ==
           "service=universal_search&version=v2&query=Il%20cavaliere%20oscuro%20%26%20co"
           "&start_index=1&max_results=30&domain=movie");
    assert(lgStoreAppRequest("cdp-30")["url"].string() == "app/app?app_id=cdp-30");
    assert(lgStoreAppRequest("cdp-30")["methodType"].string() == "REQ_SSL_GET_METHOD");

    const Json documents = lgCatalogDocuments(reply(searchAnswer()));
    assert(documents.size() == 8); // six titles and two apps; the web search is not one
    assert(documents[0]["appId"].string() == "amazon.global");
    assert(!lgCatalogDocuments(reply(searchAnswer(), "500")).valid());
    assert(!lgCatalogDocuments(Json::parse("{\"returnValue\":false}")).valid());

    // Matched by letters and digits without case; the first of an app wins, and a different
    // title of the same app (the sequel) is not taken.
    Json ids = lgCatalogTitleIds(documents, "movie", {"Il cavaliere oscuro", "The Dark Knight"});
    assert(ids["amazon.global"].string() == "amzn1.dv.gti.4aa9f68b-5e9d-1955-a451-d4d0087ef56c");
    assert(ids["ui30"].string() == "movie_id13713$movies/the-dark-knight");
    assert(!ids["netflix"].valid());
    assert(lgCatalogTitleIds(documents, "tv", {"Il cavaliere oscuro"}).keys().empty());
    ids = lgCatalogTitleIds(documents, "movie", {"Glass Onion: A Knives Out Mystery"});
    assert(!ids["netflix"].valid());
    ids = lgCatalogTitleIds(documents, "movie", {"Glass Onion: A Knives Out Mystery",
                                                 "Glass Onion – Knives Out"});
    assert(ids["netflix"].valid());
    assert(lgCatalogTitleIds(documents, "movie", {"«Encanto»"})["com.disney.disneyplus-prod"].valid());

    // Each app's own link: Prime Video plays a film, Disney+ plays LG's ID, Netflix its page.
    assert(lgCatalogTarget("prime", "movie", "amzn1.dv.gti.4aa9f68b-5e9d-1955-a451-d4d0087ef56c") ==
           "watch?gti=amzn1.dv.gti.4aa9f68b-5e9d-1955-a451-d4d0087ef56c");
    assert(lgCatalogTarget("prime", "tv", "amzn1.dv.gti.4aa9f68b-5e9d-1955-a451-d4d0087ef56c") ==
           "amzn1.dv.gti.4aa9f68b-5e9d-1955-a451-d4d0087ef56c");
    assert(lgCatalogTarget("prime", "movie", "B00ABC").empty());
    assert(lgCatalogTarget("prime", "movie", "amzn1.dv.gti.x\" y").empty());
    assert(lgCatalogTarget("disney", "movie",
                           "page=play&id=328b0ec7-6e50-4ead-aa7f-c8bb92e6f08a&distributionPartner=lg") ==
           "page=play&id=328b0ec7-6e50-4ead-aa7f-c8bb92e6f08a&distributionPartner=lg");
    assert(lgCatalogTarget("disney", "movie", "page=play&id=<script>").empty());
    assert(lgCatalogTarget("netflix", "movie", documents[5]["contentId"].string()) ==
           "m=https://www.netflix.com/title/81458416&source_type=4");
    assert(lgCatalogTarget("netflix", "movie", "-Q m=http://x/titles/movies/0123").empty());
    assert(lgCatalogTarget("other", "movie", "x").empty());

    // A plan for a service's title takes its app's ID; Disney+ is told the app handles it.
    Json installed = Json::array();
    for (const char *id : {"com.disney.disneyplus-prod", "amazon.global", "netflix"}) {
        Json app = Json::object();
        app.set("id", Json::text(id));
        installed.add(app);
    }
    HomeOpenPlan plan = planHomeServiceOpen("disney", installed);
    ids = lgCatalogTitleIds(documents, "movie", {"Encanto"});
    assert(resolveHomeCatalogueOpen(plan, ids, "movie"));
    assert(plan.launchParams["contentTarget"].string().compare(0, 10, "page=play&") == 0);
    assert(plan.launchParams["handledBy"].string() == "app");
    plan = planHomeServiceOpen("prime", installed);
    assert(!resolveHomeCatalogueOpen(plan, ids, "movie"));
    assert(!plan.launchParams.valid());
    // Netflix's red-key open (kind "netflix") takes the catalogue's link too.
    plan = planHomeOpen("netflix", "1", installed, Json::array(), Json::array());
    assert(plan.action == "netflix");
    assert(resolveHomeCatalogueOpen(plan, lgCatalogTitleIds(documents, "movie", {"Glass Onion Knives Out"}),
                                    "movie"));
    assert(plan.action == "launch");
    assert(plan.launchParams["contentTarget"].string() ==
           "m=https://www.netflix.com/title/81458416&source_type=4");
    // Wikidata's GTI plays a film too.
    plan = planHomeServiceOpen("prime", installed);
    Json lookup = Json::parse("{\"ok\":true,\"enabled\":true,"
                              "\"gti\":\"amzn1.dv.gti.4aa9f68b-5e9d-1955-a451-d4d0087ef56c\"}");
    resolveHomeServiceOpen(plan, lookup, "movie");
    assert(plan.launchParams["contentTarget"].string().compare(0, 10, "watch?gti=") == 0);

    // LG Content Store's apps: an icon that is not a web address is dropped.
    const Json apps = lgStoreApps(documents, 10);
    assert(apps.size() == 2);
    assert(apps[0]["id"].string() == "spotify-beehive");
    assert(apps[0]["free"].boolean());
    assert(apps[0]["rating"].number() == 8.1);
    assert(apps[0]["color"].string() == "#191414");
    assert(apps[0]["icon"].string() == "lgstore:icon/20230315/37810399.png");
    assert(apps[1]["icon"].string().empty());
    assert(!apps[1]["free"].boolean());
    assert(apps[1]["rating"].number() == 0);
    assert(lgStoreApps(documents, 1).size() == 1);

    Json page = Json::parse(
        "{\"app\":{\"id\":\"cdp-30\",\"name\":\"Plex\",\"version\":\"5.4.0\",\"categoryName\":"
        "\"Intrattenimento\",\"description\":\"Enjoy\",\"bFree\":\"Y\",\"displayPrice\":\"EUR 0,00\","
        "\"evaluationCount\":992,\"evaluationAverage\":6.77,\"sellerName\":\"Plex GmbH\","
        "\"iconColor\":\"#000000\",\"iconURL\":\"http://ngfts.lge.com/fts/gftsDownload.lge?biz_code="
        "APP_STORE&func_code=APP_ICON&file_path=/appstore/app/icon/20240906/38209928.png\",\"bPremium\":false,"
        "\"appPreviewList\":[{\"previewURL\":\"http://ngfts.lge.com/fts/gftsDownload.lge?biz_code="
        "APP_STORE&func_code=APP_PREVIEW&file_path=/appstore/app/preview/20240119/38023703.png\"},"
        "{\"previewURL\":\"x\"}],"
        "\"appFileList\":[{\"fileId\":1,\"packFileSize\":17718,\"unpackFileSize\":39664}]}}");
    const Json app = lgStoreApp(reply(page));
    assert(app["id"].string() == "cdp-30");
    assert(app["title"].string() == "Plex");
    assert(app["seller"].string() == "Plex GmbH");
    assert(app["rating"].number() == 6.8);
    assert(app["ratings"].number() == 992);
    assert(app["size"].number() == 17718);
    assert(app["installedSize"].number() == 39664);
    assert(app["pictures"].size() == 1);
    assert(app["pictures"][0].string() == "lgstore:preview/20240119/38023703.png");
    assert(app["icon"].string() == "lgstore:icon/20240906/38209928.png");

    // Only LG's own store pictures have a reference, and it gives the same address back.
    assert(lgStoreImageUrl("lgstore:icon/20240906/38209928.png") ==
           "https://ngfts.lge.com/fts/gftsDownload.lge?biz_code=APP_STORE&func_code=APP_ICON"
           "&file_path=/appstore/app/icon/20240906/38209928.png");
    assert(lgStoreImageRef(lgStoreImageUrl("lgstore:preview/20240119/38023703.png")) ==
           "lgstore:preview/20240119/38023703.png");
    assert(lgStoreImageUrl("lgstore:icon/2024/x.png").empty());
    assert(lgStoreImageUrl("lgstore:icon/20240906/../x.png").empty());
    assert(lgStoreImageUrl("lgstore:icon/20240906/x.png&u=evil").empty());
    assert(lgStoreImageRef("https://evil.example/fts/gftsDownload.lge?biz_code=APP_STORE&func_code="
                           "APP_ICON&file_path=/appstore/app/icon/20240906/38209928.png").empty());
    assert(lgStoreImageRef("https://ngfts.lge.com/fts/gftsDownload.lge?biz_code=APP_STORE&func_code="
                           "APP_ICON&file_path=/appstore/app/icon/20240906/38209928.svg").empty());
    assert(app["free"].boolean());
    assert(!lgStoreApp(reply(Json::object())).valid());

    // The install service's messages, as the C6 sent them installing ARTE.
    Json progress = lgInstallProgress(Json::parse(
        "{\"id\":\"a\",\"statusValue\":8,\"details\":{\"progress\":14,\"simpleStatus\":\"download\","
        "\"state\":\"ipk download current\"}}"));
    assert(progress["state"].string() == "downloading");
    assert(progress["progress"].number() == 14);
    progress = lgInstallProgress(Json::parse(
        "{\"statusValue\":17,\"details\":{\"progress\":100,\"simpleStatus\":\"install\","
        "\"state\":\"installing : installing\"}}"));
    assert(progress["state"].string() == "installing");
    progress = lgInstallProgress(Json::parse(
        "{\"statusValue\":30,\"details\":{\"progress\":100,\"simpleStatus\":\"install\","
        "\"state\":\"installed\"}}"));
    assert(progress["state"].string() == "installed");
    progress = lgInstallProgress(Json::parse(
        "{\"returnValue\":false,\"errorCode\":-1,\"errorText\":\"no space\"}"));
    assert(progress["state"].string() == "failed");
    assert(progress["error"].string() == "no space");

    // The catalogue asks LG once a day for a title, and again after a minute when it failed.
    int calls = 0;
    int64_t now = 1000;
    bool answers = false;
    LgCatalog catalog(
        [&](const Json &payload, Json &answer) {
            ++calls;
            assert(payload["serviceName"].string() == "sdp_secure");
            if (!answers) return false;
            answer = reply(searchAnswer());
            return true;
        },
        [&] { return now; });
    assert(catalog.titleIds("movie:155", "movie", {"Il cavaliere oscuro"}).keys().empty());
    assert(calls == 1);
    answers = true;
    assert(catalog.titleIds("movie:155", "movie", {"Il cavaliere oscuro"}).keys().empty());
    assert(calls == 1);
    now += 61 * 1000;
    assert(catalog.titleIds("movie:155", "movie", {"Il cavaliere oscuro"})["amazon.global"].valid());
    assert(calls == 2);
    now += 60 * 60 * 1000;
    assert(catalog.titleIds("movie:155", "movie", {"Il cavaliere oscuro"})["amazon.global"].valid());
    assert(calls == 2);
    assert(catalog.searchApps("spotify", 10).size() == 2);
    assert(catalog.searchApps("spotify", 10).size() == 2);
    assert(calls == 3);
    assert(catalog.titleIds("", "movie", {"x"}).keys().empty());
    assert(calls == 3);

    std::cout << "lg catalog tests passed\n";
    return 0;
}
