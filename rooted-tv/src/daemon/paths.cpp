#include "daemon/paths.hpp"
#include <cstdlib>

namespace rootedtv {
namespace daemon {
std::string dataDirectory() {
    const char *configured = std::getenv("TVWEB_DATA_DIR");
    if (configured && *configured)
        return configured;
    return "/var/lib/tvweb";
}
} // namespace daemon
} // namespace rootedtv
