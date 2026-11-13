#include "service/home_trailers.hpp"
#include <cassert>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace rootedtv::service;

namespace {
size_t clips(const std::string &directory) {
    size_t count = 0;
    if (DIR *listing = opendir(directory.c_str())) {
        while (const dirent *entry = readdir(listing))
            count += std::string(entry->d_name).compare(0, 6, "movie-") == 0;
        closedir(listing);
    }
    return count;
}
void removeAll(const std::string &root) {
    const std::string directory = root + "/home-trailers";
    if (DIR *listing = opendir(directory.c_str())) {
        while (const dirent *entry = readdir(listing))
            (void)unlink((directory + "/" + entry->d_name).c_str());
        closedir(listing);
    }
    (void)rmdir(directory.c_str());
    (void)rmdir(root.c_str());
}
} // namespace

int main() {
    char pattern[] = "/tmp/rootedtv-home-trailers-XXXXXX";
    const std::string root = mkdtemp(pattern);
    const std::string directory = root + "/home-trailers";
    // A download the daemon was stopped in the middle of is cleared at start, and so is an iTunes
    // preview kept before the trailers came from Apple TV.
    (void)mkdir(directory.c_str(), 0700);
    std::ofstream(directory + "/movie-5.mp4.part") << "half";
    std::ofstream(directory + "/movie-6.m4v") << "an old preview";
    std::ofstream(directory + "/movie-8.mp4") << std::string(HomeTrailers::kClipBytes + 1, 'x');

    std::mutex mutex;
    std::vector<std::string> resolved, downloaded;
    int64_t clock = 1000;
    bool failDownloads = false;
    HomeTrailers trailers(
        root,
        [&](const std::string &id) {
            std::lock_guard<std::mutex> lock(mutex);
            resolved.push_back(id);
            return id == "404" ? std::string() : "https://video-ssl.itunes.apple.com/" + id + ".m4v";
        },
        [&](const std::string &url, const std::string &path, size_t bytes, std::string &error) {
            std::lock_guard<std::mutex> lock(mutex);
            assert(bytes == HomeTrailers::kClipBytes);
            downloaded.push_back(url);
            if (failDownloads) {
                error = "offline";
                return false;
            }
            std::ofstream(path.c_str(), std::ios::binary) << std::string("\0\0\0\x20" "ftypM4V ", 12);
            return true;
        },
        [&clock]() { return clock; });
    assert(clips(directory) == 0);

    std::string path;
    assert(trailers.request("603", path) == HomeTrailers::State::Pending && path.empty());
    trailers.settle();
    assert(trailers.request("603", path) == HomeTrailers::State::Ready &&
           path == directory + "/movie-603.mp4");
    std::string bytes;
    assert(HomeTrailers::read(path, bytes) && bytes.compare(4, 4, "ftyp") == 0);
    assert(downloaded.size() == 1 && downloaded[0] == "https://video-ssl.itunes.apple.com/603.m4v");

    // A film with no preview is not asked about again for hours; ids are TMDB's digits only.
    assert(trailers.request("404", path) == HomeTrailers::State::Pending);
    trailers.settle();
    assert(trailers.request("404", path) == HomeTrailers::State::None);
    assert(resolved.size() == 2);
    for (const char *invalid : {"", "0", "07", "12a", "../../etc", "12345678901"})
        assert(trailers.request(invalid, path) == HomeTrailers::State::None);
    clock += 7LL * 60 * 60 * 1000;
    assert(trailers.request("404", path) == HomeTrailers::State::Pending);
    trailers.settle();
    assert(resolved.size() == 3);

    // A failed download is tried again after ten minutes.
    failDownloads = true;
    assert(trailers.request("77", path) == HomeTrailers::State::Pending);
    trailers.settle();
    assert(trailers.request("77", path) == HomeTrailers::State::None);
    clock += 11LL * 60 * 1000;
    failDownloads = false;
    assert(trailers.request("77", path) == HomeTrailers::State::Pending);
    trailers.settle();
    assert(trailers.request("77", path) == HomeTrailers::State::Ready);

    // The top picks are fetched ahead; no more than kFilesMax clips are kept.
    trailers.prewarm({"1", "2", "3", "4", "5", "6", "7", "603"});
    trailers.settle();
    assert(clips(directory) == HomeTrailers::kFilesMax);
    assert(trailers.request("7", path) == HomeTrailers::State::Ready);
    assert(trailers.request("603", path) != HomeTrailers::State::None);
    trailers.settle();
    removeAll(root);

    // Apple TV's HLS: the widest H.264 SDR variant up to the width asked for, past the I-frame
    // lists, Dolby Vision and HEVC, its address beside the master playlist's.
    const std::string base = "https://play-edge.itunes.apple.com/hls/playlist.m3u8?cc=US&a=1&id=2";
    const std::string master =
        "#EXTM3U\r\n"
        "#EXT-X-I-FRAME-STREAM-INF:CODECS=\"avc1.64001f\",RESOLUTION=1190x496,URI=\"https://vod.tv.apple.com/i.m3u8\"\r\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=385840,CODECS=\"avc1.64001f,mp4a.40.5\",VIDEO-RANGE=SDR,RESOLUTION=556x232\r\n"
        "https://vod.tv.apple.com/a/556.m3u8\r\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=3074315,CODECS=\"avc1.64001f,mp4a.40.2\",VIDEO-RANGE=SDR,RESOLUTION=1190x496\r\n"
        "v1190.m3u8\r\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=3810485,CODECS=\"avc1.640020,mp4a.40.2\",VIDEO-RANGE=SDR,RESOLUTION=1484x618\r\n"
        "https://vod.tv.apple.com/a/1484.m3u8\r\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=3000000,CODECS=\"hvc1.2.4.L123.B0\",VIDEO-RANGE=SDR,RESOLUTION=1180x492\r\n"
        "https://vod.tv.apple.com/a/hevc.m3u8\r\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=3000000,CODECS=\"avc1.64001f\",VIDEO-RANGE=PQ,RESOLUTION=1100x460\r\n"
        "https://vod.tv.apple.com/a/hdr.m3u8\r\n";
    assert(hlsVariant(master, base, 1200) == "https://play-edge.itunes.apple.com/hls/v1190.m3u8");
    assert(hlsVariant(master, base, 1500) == "https://vod.tv.apple.com/a/1484.m3u8");
    assert(hlsVariant(master, base, 500).empty());
    assert(hlsVariant("#EXT-X-STREAM-INF:CODECS=\"avc1.64001f\",RESOLUTION=1190x496\nhttps://example.com/v.m3u8\n",
                      base, 1200)
               .empty());
    // The init segment, then the segments beside the media playlist.
    const std::string mediaBase = "https://vod.tv.apple.com/assets/v1190.m3u8";
    const std::string media = "#EXTM3U\n#EXT-X-MAP:URI=\"P_-0.mp4\"\n#EXTINF:9.6,\n#EXT-X-BITRATE:2519\n"
                              "P_-1.mp4\n#EXTINF:9.3,\nP_-2.mp4\n#EXT-X-ENDLIST\n";
    const std::vector<std::string> parts = hlsSegments(media, mediaBase);
    assert(parts.size() == 3 && parts[0] == "https://vod.tv.apple.com/assets/P_-0.mp4" &&
           parts[2] == "https://vod.tv.apple.com/assets/P_-2.mp4");
    assert(hlsSegments("#EXTM3U\nP_-1.mp4\n", mediaBase).empty());
    assert(hlsSegments("#EXT-X-MAP:URI=\"P_-0.mp4\"\nhttps://example.com/x.mp4\n", mediaBase).empty());
    std::printf("Home trailers passed.\n");
    return 0;
}
