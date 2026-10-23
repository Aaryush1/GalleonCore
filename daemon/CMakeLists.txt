# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

# The daemon's logic, as a library so that the tests can use it.
add_library(galleond_core STATIC
    GameMode.h GameMode.cpp
    FpsReader.h FpsReader.cpp
    HomeWidgets.h HomeWidgets.cpp
    LauncherCache.h LauncherCache.cpp
    AppsBackend.h AppsBackend.cpp
    CoverLoader.h CoverLoader.cpp
    KeyInjector.h PlatformBackends.h
    DesktopEntries.h
    PageOrder.h PageOrder.cpp
    PagesConfig.h PagesConfig.cpp
    InitialPages.h InitialPages.cpp
    PagesEditor.h PagesEditor.cpp
    PagesController.h PagesController.cpp
    WallpaperCatalog.h WallpaperCatalog.cpp
    ScreenRenderer.h ScreenRenderer.cpp
    TileRenderer.h TileRenderer.cpp
    VideoProbe.h VideoProbe.cpp
    VideoThumbnails.h VideoThumbnails.cpp
    ScreenController.h ScreenController.cpp
    SteamLibrary.h SteamLibrary.cpp
    UsageStats.h UsageStats.cpp
    Wiring.h Wiring.cpp
    ScreensaverSplitter.h ScreensaverSplitter.cpp
    ScreensaverController.h ScreensaverController.cpp
)
target_include_directories(galleond_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(galleond_core PUBLIC galleon galleon_media galleon_keys galleon_platform Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Network PRIVATE galleon_warnings)
if(WIN32)
    target_sources(galleond_core PRIVATE WindowsDesktopEntries.cpp)
    target_link_libraries(galleond_core PUBLIC galleon_smtc PRIVATE shell32 ole32 uuid)
    target_compile_definitions(galleond_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_sources(galleond_core PRIVATE LinuxKeyInjector.h LinuxKeyInjector.cpp LinuxPlatformBackends.cpp
        MprisController.h MprisController.cpp DesktopEntries.cpp)
    target_link_libraries(galleond_core PUBLIC Qt6::DBus)
endif()
set_target_properties(galleond_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

qt_add_resources(galleond_core "galleond_ui"
    PREFIX "/galleond"
    FILES qml/Preview.qml qml/Apps.qml
)

qt_add_executable(galleond main.cpp)
target_compile_definitions(galleond PRIVATE PROJECT_VERSION_STRING="${PROJECT_VERSION}")
if(GALLEON_SOURCE_TREE_PATHS)
    # Defaults for running from the build tree; an installed galleond looks in
    # <data dir>/galleoncore first.
    target_compile_definitions(galleond PRIVATE
        GALLEON_SOURCE_WIDGETS="${CMAKE_SOURCE_DIR}/widgets"
        GALLEON_SOURCE_WALLPAPERS="${CMAKE_SOURCE_DIR}/public/wallpaper")
endif()
target_link_libraries(galleond PRIVATE galleond_core galleon_warnings)
install(TARGETS galleond RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(WIN32)
    set_target_properties(galleond PROPERTIES WIN32_EXECUTABLE TRUE)
    configure_file(${PROJECT_SOURCE_DIR}/tools/packaging/windows/bosun-icon.rc.in ${CMAKE_CURRENT_BINARY_DIR}/bosun-icon.rc @ONLY)
    target_sources(galleond PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/bosun-icon.rc)
endif()

qt_add_resources(galleond_core "home_widgets"
    PREFIX "/homewidgets"
    BASE "${CMAKE_SOURCE_DIR}/widgets"
    FILES "${CMAKE_SOURCE_DIR}/widgets/HomeOverlay.qml" "${CMAKE_SOURCE_DIR}/widgets/GameScreen.qml" "${CMAKE_SOURCE_DIR}/widgets/CarGauge.qml"
)
