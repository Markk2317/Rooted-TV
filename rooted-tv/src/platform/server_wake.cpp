#include "platform/server_wake.hpp"

#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char **environ;

namespace rootedtv {
namespace {
// Whether something accepts a connection at host:port within the time given.
bool listening(const std::string &host, const std::string &port, int timeoutMs) {
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *found = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &found) != 0)
        return true; // nothing to ask about an origin that does not resolve
    bool connected = false;
    for (addrinfo *address = found; address && !connected; address = address->ai_next) {
        const int socket = ::socket(address->ai_family, address->ai_socktype | SOCK_CLOEXEC,
                                    address->ai_protocol);
        if (socket < 0)
            continue;
        fcntl(socket, F_SETFL, fcntl(socket, F_GETFL) | O_NONBLOCK);
        if (connect(socket, address->ai_addr, address->ai_addrlen) == 0) {
            connected = true;
        } else if (errno == EINPROGRESS) {
            pollfd waiting = {socket, POLLOUT, 0};
            int error = 0;
            socklen_t length = sizeof(error);
            connected = poll(&waiting, 1, timeoutMs) == 1 &&
                        getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &length) == 0 &&
                        error == 0;
        }
        close(socket);
    }
    freeaddrinfo(found);
    return connected;
}

void askForStartupScript() {
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    for (int descriptor = 0; descriptor <= 2; ++descriptor)
        posix_spawn_file_actions_addopen(&actions, descriptor, "/dev/null",
                                         descriptor ? O_WRONLY : O_RDONLY, 0);
    const char *arguments[] = {"luna-send-pub", "-n", "1", "-w", "20000",
                               "luna://org.webosbrew.hbchannel.service/autostart", "{}", nullptr};
    pid_t child = 0;
    if (posix_spawn(&child, "/usr/bin/luna-send-pub", &actions, nullptr,
                    const_cast<char *const *>(arguments), environ) == 0) {
        int status = 0;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
    }
    posix_spawn_file_actions_destroy(&actions);
}
} // namespace

void wakeServerIfDown(const std::string &origin) {
    // "http://host:port", as clientServerOrigin and --server give it.
    const size_t scheme = origin.find("://");
    const size_t start = scheme == std::string::npos ? 0 : scheme + 3;
    const size_t end = origin.find('/', start);
    const std::string authority = origin.substr(start, end == std::string::npos ? std::string::npos
                                                                                : end - start);
    const size_t colon = authority.rfind(':');
    if (colon == std::string::npos || authority.empty() || authority[0] == '[')
        return;
    const std::string host = authority.substr(0, colon), port = authority.substr(colon + 1);
    std::thread([host, port]() {
        if (!listening(host, port, 1500))
            askForStartupScript();
    }).detach();
}

} // namespace rootedtv
