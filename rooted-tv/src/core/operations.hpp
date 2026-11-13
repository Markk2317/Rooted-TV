#pragma once

namespace rootedtv {
enum class Operation {
    None,
    Capabilities,
    Metrics,
    SystemSettings,
    Apps,
    OledCare,
    Game,
    ScreenSaver,
    HomeStatus,
    HomeCatalogue,
    HomeDiscover,
    HomeGuide,
    HomePicture,
    HomeNow,
    HomeBeat,
    Privacy,
    ServiceMenu,
    Setup,
    AppCatalog,
    InstallStatus,
    StoreSearch,  // /api/apps/store/search: LG Content Store's and the Homebrew Channel's apps
    StoreApp,     // /api/apps/store/app: an LG Content Store app's page and its install
    StoreInstall, // /api/apps/store/install: install a free app of LG Content Store
    TitleDetails,
    SearchTitles,
    NetflixTitle,
    Recents,
    Control,
    OpenHomeItem,
    SetProfile,
    SetHomeLayout,
    EnableHome,
    DisableHome,
    SetHomeStartLayout,
    SetHomeStartup,
    UninstallApp,
    SetAppHidden,
    SetAppVisible,
    SetTileHiding,
    SetLgHomeLayout,
    SetPreload,
    SetBackgroundService,
    SetScreenTime, // /api/screentime: the screen-time limits and the idle power-off
    FetchPackage,
    ConfirmPackage,
    CancelPackage,
    Settings, // /api/settings: the dashboard's MQTT and device settings (setup's broker entry)
    AdminUnlock, // /api/admin/unlock: the four-digit admin PIN, answered with a session
    Assistant,   // /api/assistant: a question for the Gemini assistant
    Dictionary   // /assets/i18n/<lang>.json: the TV client's words in the TV's language
};
}
