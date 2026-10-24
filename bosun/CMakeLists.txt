# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

# Galleon Bosun: the settings window. The logic is a library so that the tests can use it.
add_library(bosun_core STATIC
    PackageUpdates.h PackageUpdates.cpp
    UpdateChecker.h UpdateChecker.cpp
    UpdateRelease.h UpdateRelease.cpp
    BosunBackend.h BosunBackend.cpp
    CropMath.h CropMath.cpp
    FileNames.h FileNames.cpp
    DaemonStatus.h
    PageName.h PageName.cpp
    TrayMenu.h TrayMenu.cpp
    VideoAdvice.h VideoAdvice.cpp
    WallpaperLibrary.h WallpaperLibrary.cpp
    VideoConverter.h VideoConverter.cpp
)
target_include_directories(bosun_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bosun_core PUBLIC galleond_core Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick PRIVATE galleon_warnings)
if(WIN32)
    target_sources(bosun_core PRIVATE WindowsDaemonStatus.cpp)
else()
    target_sources(bosun_core PRIVATE DaemonStatus.cpp)
    target_link_libraries(bosun_core PUBLIC Qt6::DBus)
endif()
set_target_properties(bosun_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

qt_add_resources(bosun_core "bosun_ui"
    PREFIX "/bosun"
    FILES qml/UpdatePanel.qml qml/GamingPage.qml qml/GameModePage.qml qml/WidgetsPage.qml qml/Bosun.qml qml/Pill.qml qml/Stepper.qml qml/Field.qml qml/Swatches.qml qml/KeyEditor.qml qml/TabEditor.qml qml/NewTab.qml qml/ScreensaversPage.qml qml/CropEditor.qml qml/WallpapersPage.qml
          icons/galleon-bosun.svg
)

# The icon in the notification area needs QSystemTrayIcon (Qt Widgets): only the program has it, not the library the tests use.
qt_add_executable(galleon-bosun main.cpp Tray.h Tray.cpp FilePicker.h FilePicker.cpp)
target_compile_definitions(galleon-bosun PRIVATE PROJECT_VERSION_STRING="${PROJECT_VERSION}")
target_compile_definitions(galleon-bosun PRIVATE GALLEON_PACKAGE_FORMAT="${GALLEON_PACKAGE_FORMAT}")
if(GALLEON_SOURCE_TREE_PATHS)
    # When run from the build tree, the project's own wallpapers; an installed one finds them in the data folders.
    target_compile_definitions(galleon-bosun PRIVATE GALLEON_SOURCE_WALLPAPERS="${CMAKE_SOURCE_DIR}/public/wallpaper")
endif()
target_link_libraries(galleon-bosun PRIVATE bosun_core Qt6::Widgets galleon_warnings)
install(TARGETS galleon-bosun RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES icons/galleon-bosun.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
if(NOT WIN32)
install(FILES galleon-bosun.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

endif()

if(WIN32)
    set_target_properties(galleon-bosun PROPERTIES WIN32_EXECUTABLE TRUE)
    configure_file(${PROJECT_SOURCE_DIR}/tools/packaging/windows/bosun-icon.rc.in ${CMAKE_CURRENT_BINARY_DIR}/bosun-icon.rc @ONLY)
    target_sources(galleon-bosun PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/bosun-icon.rc)
endif()
