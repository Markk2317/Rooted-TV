#include "service/screensaver_photos.hpp"
#include "service/home_wallpapers.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace nativetv {
namespace service {
namespace {
std::string directoryOf(const std::string &dataRoot) {
    return (dataRoot.empty() ? std::string("/var/lib/tvweb") : dataRoot) + "/screensaver-photos";
}

std::string pathOf(const std::string &dataRoot, unsigned id) {
    return directoryOf(dataRoot) + "/photo-" + std::to_string(id) + ".jpg";
}

// "photo-12.jpg" is 12; anything else 0.
unsigned idOf(const std::string &name) {
    const std::string prefix = "photo-", suffix = ".jpg";
    if (name.size() <= prefix.size() + suffix.size() || name.compare(0, prefix.size(), prefix) ||
        name.compare(name.size() - suffix.size(), suffix.size(), suffix))
        return 0;
    const std::string digits = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
    if (digits.size() > 9 || digits[0] == '0' ||
        !std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
        return 0;
    return static_cast<unsigned>(std::stoul(digits));
}

uint64_t sizeOf(const std::string &path) {
    struct stat info = {};
    return stat(path.c_str(), &info) == 0 ? static_cast<uint64_t>(info.st_size) : 0;
}
} // namespace

std::vector<unsigned> screensaverPhotoIds(const std::string &dataRoot) {
    std::vector<unsigned> ids;
    DIR *directory = opendir(directoryOf(dataRoot).c_str());
    if (!directory) return ids;
    while (struct dirent *entry = readdir(directory)) {
        const unsigned id = idOf(entry->d_name);
        if (id) ids.push_back(id);
    }
    closedir(directory);
    std::sort(ids.begin(), ids.end());
    return ids;
}

Json screensaverPhotoStatus(const std::string &dataRoot) {
    Json ids = Json::array();
    uint64_t bytes = 0;
    for (unsigned id : screensaverPhotoIds(dataRoot)) {
        ids.add(Json::numeric(id));
        bytes += sizeOf(pathOf(dataRoot, id));
    }
    Json status = Json::object();
    status.set("count", Json::numeric(static_cast<double>(ids.size())));
    status.set("bytes", Json::numeric(static_cast<double>(bytes)));
    status.set("ids", ids);
    return status;
}

bool saveScreensaverPhoto(const std::string &dataRoot, const std::string &bytes,
                          std::string &error) {
    if (!validHomeWallpaperJpeg(bytes)) {
        error = "Choose a valid JPEG up to 1920 × 1080 pixels.";
        return false;
    }
    const std::vector<unsigned> ids = screensaverPhotoIds(dataRoot);
    uint64_t used = 0;
    for (unsigned id : ids) used += sizeOf(pathOf(dataRoot, id));
    if (ids.size() >= kScreensaverPhotosMax || used + bytes.size() > kScreensaverPhotosBytes) {
        error = "The screen saver holds 60 photos and 60 MB at most. Remove some first.";
        return false;
    }
    const std::string directory = directoryOf(dataRoot);
    if (mkdir(directory.c_str(), 0755) != 0 && errno != EEXIST) {
        error = "Could not make the photos folder.";
        return false;
    }
    // Room for the photo and as much again, so the TV's own data keeps its space.
    struct statvfs space = {};
    if (statvfs(directory.c_str(), &space) != 0 ||
        static_cast<uint64_t>(space.f_bavail) * space.f_frsize < 2 * bytes.size() + 8 * 1024 * 1024) {
        error = "Not enough free space on the TV for the photo.";
        return false;
    }
    const unsigned next = ids.empty() ? 1 : ids.back() + 1;
    const std::string path = pathOf(dataRoot, next), temporary = path + ".tmp";
    const int fd = open(temporary.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
    if (fd < 0) {
        error = "Could not save the photo.";
        return false;
    }
    size_t written = 0;
    bool ok = true;
    while (written < bytes.size()) {
        const ssize_t count = write(fd, bytes.data() + written, bytes.size() - written);
        if (count <= 0) { ok = false; break; }
        written += static_cast<size_t>(count);
    }
    if (close(fd) != 0) ok = false;
    if (!ok || rename(temporary.c_str(), path.c_str()) != 0) {
        (void)unlink(temporary.c_str());
        error = "Could not save the photo.";
        return false;
    }
    return true;
}

ScreensaverPhoto readScreensaverPhoto(const std::string &dataRoot, unsigned id) {
    ScreensaverPhoto photo;
    if (!id) return photo;
    const std::string path = pathOf(dataRoot, id);
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) return photo;
    std::ostringstream contents;
    contents << input.rdbuf();
    photo.bytes = contents.str();
    if (photo.bytes.empty()) return photo;
    struct stat info = {};
    (void)stat(path.c_str(), &info);
    photo.etag = "\"p" + std::to_string(id) + "-" + std::to_string(static_cast<long long>(info.st_mtime)) +
                 "-" + std::to_string(photo.bytes.size()) + "\"";
    photo.found = true;
    return photo;
}

bool removeScreensaverPhotos(const std::string &dataRoot, unsigned id) {
    bool ok = true;
    for (unsigned existing : screensaverPhotoIds(dataRoot))
        if (!id || existing == id)
            ok = unlink(pathOf(dataRoot, existing).c_str()) == 0 && ok;
    return ok;
}

} // namespace service
} // namespace nativetv
