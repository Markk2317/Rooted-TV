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

using namespace nativetv::service;

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
    char pattern[] = "/tmp/nativetv-home-trailers-XXXXXX";
    const std::string root = mkdtemp(pattern);
    const std::string directory = root + "/home-trailers";
    // A download the daemon was stopped in the middle of is cleared at start.
    (void)mkdir(directory.c_str(), 0700);
    std::ofstream(directory + "/movie-5.m4v.part") << "half";

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
           path == directory + "/movie-603.m4v");
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
    std::printf("Home trailers passed.\n");
    return 0;
}
