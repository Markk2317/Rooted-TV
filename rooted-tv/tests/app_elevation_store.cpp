#include "daemon/app_elevation_store.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {
void check(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    char folder[] = "/tmp/rootedtv-elevated-test-XXXXXX";
    char *root = mkdtemp(folder);
    check(root != nullptr, "could not create elevated services test directory");
    const std::string path = std::string(root) + "/elevated.json";
    rootedtv::Json services;
    std::string error;
    check(rootedtv::daemon::readElevatedServices(
              path, "org.example.app", services, &error) &&
              services.isArray() && services.size() == 0,
          "missing elevation state should read as an empty list");

    rootedtv::Json selected = rootedtv::Json::array();
    selected.add(rootedtv::Json::text("org.example.app.service"));
    selected.add(rootedtv::Json::text("org.example.worker"));
    check(rootedtv::daemon::recordElevatedServices(
              path, "org.example.app", selected, &error),
          "valid elevation state should persist: " + error);
    check(rootedtv::daemon::readElevatedServices(
              path, "org.example.app", services, &error) &&
              services.dump() == selected.dump(),
          "elevation state should round trip");

    rootedtv::Json other = rootedtv::Json::array();
    other.add(rootedtv::Json::text("org.example.other.service"));
    check(rootedtv::daemon::recordElevatedServices(
              path, "org.example.other", other, &error),
          "another package's elevation state should persist");
    check(rootedtv::daemon::readElevatedServices(
              path, "org.example.app", services, &error) &&
              services.dump() == selected.dump(),
          "updating one package must preserve the other package state");

    rootedtv::Json invalid = rootedtv::Json::array();
    invalid.add(rootedtv::Json::text("../outside"));
    check(!rootedtv::daemon::recordElevatedServices(
              path, "org.example.app", invalid, &error),
          "invalid service names should be rejected");
    check(!rootedtv::daemon::readElevatedServices(
              path, "../outside", services, &error),
          "invalid app ids should be rejected");
    unlink((path + ".last-good").c_str());
    unlink(path.c_str());
    rmdir(root);
    std::cout << "native app elevation store tests passed\n";
    return 0;
}
