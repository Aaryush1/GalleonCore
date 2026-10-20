# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.21)
project(GalleonCore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTING "Build the unit tests" ON)
option(GALLEON_WERROR "Treat compiler warnings as errors" OFF)
option(GALLEON_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(GALLEON_SOURCE_TREE_PATHS "Let galleond and galleon-bosun find the widgets and wallpapers in the source tree when run from the build folder (turn OFF for a package: the paths of the build machine must not end up in the programs)" ON)
option(GALLEON_FUZZ "Build the libFuzzer targets (Clang only)" OFF)

# Linux desktop integration remains the default. Windows desktop builds are
# explicit while native integration and deployment are being validated.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(galleon_desktop_default ON)
else()
    set(galleon_desktop_default OFF)
endif()
option(GALLEON_BUILD_DESKTOP "Build galleond and Bosun (Linux and Windows)" ${galleon_desktop_default})
if(GALLEON_BUILD_DESKTOP AND NOT (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    message(FATAL_ERROR "Desktop backends support Linux and Windows; use -DGALLEON_BUILD_DESKTOP=OFF for the portable library")
endif()

# Strict warnings, applied to our own targets only (Qt headers are SYSTEM).
add_library(galleon_warnings INTERFACE)
if(MSVC)
    target_compile_options(galleon_warnings INTERFACE /W4 /permissive- $<$<BOOL:${GALLEON_WERROR}>:/WX>)
else()
    target_compile_options(galleon_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual
        $<$<BOOL:${GALLEON_WERROR}>:-Werror>)
endif()

if(GALLEON_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network)
if(GALLEON_BUILD_DESKTOP)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Qml Quick Widgets)
    if(NOT WIN32)
        find_package(Qt6 6.4 REQUIRED COMPONENTS DBus)
    endif()
endif()
if(WIN32)
    find_package(hidapi CONFIG REQUIRED)
    set(GALLEON_HIDAPI_TARGET hidapi::hidapi)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw)
    set(GALLEON_HIDAPI_TARGET PkgConfig::HIDAPI)
endif()

include(GNUInstallDirs)

add_subdirectory(lib)
add_subdirectory(media)
if(GALLEON_BUILD_DESKTOP)
    add_subdirectory(daemon)
    add_subdirectory(bosun)
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

if(GALLEON_FUZZ)
    add_subdirectory(fuzz)
endif()

if(GALLEON_BUILD_DESKTOP)
install(DIRECTORY widgets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/galleoncore/widgets)
install(DIRECTORY public/wallpaper/ DESTINATION ${CMAKE_INSTALL_DATADIR}/galleoncore/wallpaper)
if(NOT WIN32)
install(FILES tools/udev/70-galleon.rules tools/udev/71-galleon-uinput.rules DESTINATION lib/udev/rules.d)
install(FILES tools/systemd/galleond.service DESTINATION lib/systemd/user)
endif()
endif()
