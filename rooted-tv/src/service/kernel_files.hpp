#pragma once
#include <cstdint>
#include <string>

namespace rootedtv {
namespace service {
bool readKernelFile(const std::string &path, std::string &text);
bool parseUnsigned(const std::string &text, uint64_t &value, unsigned base = 10);
} // namespace service
} // namespace rootedtv
