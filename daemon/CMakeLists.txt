# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

# The daemon's logic, as a library so that the tests can use it.
add_library(galleond_core STATIC
    AppsBackend.h AppsBackend.cpp
    CoverLoader.h CoverLoader.cpp
    KeyInjector.h KeyInjector.cpp
    DesktopEntries.h DesktopEntries.cpp
    MediaController.h
    MprisController.h MprisController.cpp
    PagesConfig.h PagesConfig.cpp
    PagesEditor.h PagesEditor.cpp
    PagesController.h PagesController.cpp
    WallpaperCatalog.h WallpaperCatalog.cpp
    ScreenRenderer.h ScreenRenderer.cpp
    TileRenderer.h TileRenderer.cpp
    VideoThumbnails.h VideoThumbnails.cpp
    ScreenController.h ScreenController.cpp
    ScreensaverSplitter.h ScreensaverSplitter.cpp
    ScreensaverController.h ScreensaverController.cpp
)
target_include_directories(galleond_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(galleond_core PUBLIC galleon Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::DBus Qt6::Network PRIVATE galleon_warnings)
set_target_properties(galleond_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

qt_add_resources(galleond_core "galleond_ui"
    PREFIX "/galleond"
    FILES qml/Preview.qml qml/Apps.qml
)

add_executable(galleond main.cpp)
target_compile_definitions(galleond PRIVATE
    PROJECT_VERSION_STRING="${PROJECT_VERSION}"
    # Defaults for running from the build tree; an installed galleond looks in
    # <data dir>/galleoncore first.
    GALLEON_SOURCE_WIDGETS="${CMAKE_SOURCE_DIR}/widgets"
    GALLEON_SOURCE_WALLPAPERS="${CMAKE_SOURCE_DIR}/public/wallpaper"
)
target_link_libraries(galleond PRIVATE galleond_core galleon_warnings Qt6::DBus)
install(TARGETS galleond RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
