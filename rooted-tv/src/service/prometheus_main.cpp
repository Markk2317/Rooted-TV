#include "service/prometheus.hpp"
#include <iostream>
#include <iterator>
#include <string>

int main() {
    const std::string input((std::istreambuf_iterator<char>(std::cin)),
                            std::istreambuf_iterator<char>());
    const rootedtv::Json payload = rootedtv::Json::parse(input);
    if (!payload.isObject()) {
        std::cerr << "expected a JSON object on stdin\n";
        return 2;
    }
    std::cout << rootedtv::service::renderPrometheus(
        payload["stats"], payload["version"].string(), payload["syslog"],
        payload["ntp"], payload["watch"]);
    return 0;
}
