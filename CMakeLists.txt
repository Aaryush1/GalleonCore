# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.21)
project(GalleonCore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw)

include(GNUInstallDirs)

add_subdirectory(lib)
add_subdirectory(daemon)

install(DIRECTORY widgets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/galleoncore/widgets)
install(FILES tools/udev/70-galleon.rules DESTINATION lib/udev/rules.d)
