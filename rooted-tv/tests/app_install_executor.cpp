#include "daemon/app_install_executor.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    rootedtv::daemon::ChildRunnerOptions options;
    options.startupGap = std::chrono::milliseconds(0);
    options.regularGap = std::chrono::milliseconds(0);
    options.startupWindow = std::chrono::milliseconds(0);
    rootedtv::daemon::ChildProcessRunner runner(options);
    rootedtv::daemon::AppInstallExecutor executor(
        runner, "/tmp/rootedtv-missing-install-stage", "/missing/luna-send");
    rootedtv::daemon::AppInstallResult result;
    std::string error;
    check(!executor.run("com.example.app", "../outside.ipk",
                        std::string(64, 'a'), std::chrono::milliseconds(1000),
                        result, &error),
          "an install file outside staging must be rejected");
    check(!error.empty(), "rejected install requests should explain the failure");
    check(!executor.run("com.example.app", "missing.ipk",
                        std::string(64, 'a'), std::chrono::milliseconds(120001),
                        result, &error),
          "install timeout must be capped");
    check(error.find("timeout") != std::string::npos,
          "invalid timeout should produce a useful error");
    std::cout << "native app install executor validation passed\n";
    return 0;
}
