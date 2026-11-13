#include "platform/client_config.hpp"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
void writeText(const std::string &path, const std::string &contents) {
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    assert(file.good());
    file << contents;
    assert(file.good());
}

bool rejects(const std::string &path) {
    try {
        (void)rootedtv::clientServerOrigin(path);
        return false;
    } catch (const std::runtime_error &) {
        return true;
    }
}
} // namespace

int main() {
    char templatePath[] = "/tmp/rootedtv-client-config-XXXXXX";
    char *directory = mkdtemp(templatePath);
    assert(directory);
    const std::string root(directory);
    const std::string path = rootedtv::clientServerConfigPathForDataRoot(root);
    assert(path == root + "/native/client-config.json");
    assert(rootedtv::clientServerOrigin(path) == "http://127.0.0.1:8080");

    std::string error;
    assert(rootedtv::writeClientServerPort(path, 19443, &error));
    assert(error.empty());
    assert(rootedtv::clientServerOrigin(path) == "http://127.0.0.1:19443");
    struct stat info = {};
    assert(stat(path.c_str(), &info) == 0);
    assert((info.st_mode & 0777) == 0644);

    const std::string beforeInvalidWrite = rootedtv::clientServerOrigin(path);
    assert(!rootedtv::writeClientServerPort(path, 0, &error));
    assert(!error.empty());
    assert(rootedtv::clientServerOrigin(path) == beforeInvalidWrite);
    assert(rootedtv::writeClientServerPort(path, 65535, &error));
    assert(rootedtv::clientServerOrigin(path) == "http://127.0.0.1:65535");

    const char *invalid[] = {"{}", "{\"port\":0}", "{\"port\":65536}",
                             "{\"port\":8080.5}", "{\"port\":\"8080\"}",
                             "{\"port\":8080} trailing"};
    for (const char *contents : invalid) {
        writeText(path, contents);
        assert(rejects(path));
    }

    const std::string symlink = root + "/native/client-link.json";
    assert(symlinkat("client-config.json", AT_FDCWD, symlink.c_str()) == 0);
    assert(rejects(symlink));
    unlink(symlink.c_str());
    unlink(path.c_str());
    rmdir(root.c_str());
    return 0;
}
