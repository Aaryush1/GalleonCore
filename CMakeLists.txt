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
option(GALLEON_FUZZ "Build the libFuzzer targets (Clang only)" OFF)

# Strict warnings, applied to our own targets only (Qt headers are SYSTEM).
add_library(galleon_warnings INTERFACE)
target_compile_options(galleon_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual
    $<$<BOOL:${GALLEON_WERROR}>:-Werror>
)

if(GALLEON_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw)

include(GNUInstallDirs)

add_subdirectory(lib)
add_subdirectory(daemon)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

if(GALLEON_FUZZ)
    add_subdirectory(fuzz)
endif()

install(DIRECTORY widgets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/galleoncore/widgets)
install(FILES tools/udev/70-galleon.rules DESTINATION lib/udev/rules.d)
