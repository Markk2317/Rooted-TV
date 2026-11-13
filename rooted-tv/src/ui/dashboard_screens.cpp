#include "ui/dashboard_screens.hpp"

namespace rootedtv {
void registerDashboardScreens(ScreenRegistry &registry) {
    registry.add(makeLiveScreen);
    registry.add(makeSettingsScreen);
    registry.add(makeCatalogScreen);
}
} // namespace rootedtv
