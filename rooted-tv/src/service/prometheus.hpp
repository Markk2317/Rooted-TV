#pragma once

#include "core/json.hpp"
#include <string>

namespace rootedtv {
namespace service {

std::string renderPrometheus(const Json &stats, const std::string &version,
                             const Json &syslog = Json(), const Json &ntp = Json(),
                             const Json &watch = Json());
std::string escapePrometheusLabel(const std::string &value);

} // namespace service
} // namespace rootedtv
