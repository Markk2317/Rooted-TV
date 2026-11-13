#pragma once

#include <string>

namespace rootedtv {

// webOS 3.4 runs no root hook at boot (its Developer Mode start script must carry LG's signature,
// and an Activity Manager activity is gone once it has fired), so after a power-on nothing starts
// the server until the Homebrew Channel's startup script runs, which its own app does when opened.
// A client opened with nothing listening at `origin` asks the Homebrew Channel for that script: its
// autostart method runs it once a boot, and the server's boot hook with the others. The check and
// the call run on a thread of their own, so the first frame waits for neither.
void wakeServerIfDown(const std::string &origin);

} // namespace rootedtv
