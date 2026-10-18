# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

# OpenRGB plugin, built on its own (not part of the main build): it needs the
# OpenRGB source tree for its headers, and an OpenRGB built with the same Qt.
#
#   cmake -S openrgb-plugin -B build-plugin -DOPENRGB_SOURCE_DIR=~/src/OpenRGB
#   cmake --build build-plugin && ctest --test-dir build-plugin

cmake_minimum_required(VERSION 3.21)
project(GalleonOpenRGBPlugin VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

set(OPENRGB_SOURCE_DIR "" CACHE PATH "Path to an OpenRGB source checkout")
if(NOT EXISTS "${OPENRGB_SOURCE_DIR}/OpenRGBPluginInterface.h")
    message(FATAL_ERROR "Set OPENRGB_SOURCE_DIR to an OpenRGB checkout")
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)

add_library(galleon_fakekeys STATIC FakeKeys.cpp FakeKeys.h)
target_include_directories(galleon_fakekeys PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(galleon_fakekeys PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(galleon_openrgb_plugin MODULE
    GalleonPlugin.cpp GalleonPlugin.h
    FakeController.cpp FakeController.h
    galleon-plugin.json
)
target_include_directories(galleon_openrgb_plugin SYSTEM PRIVATE
    ${OPENRGB_SOURCE_DIR}
    ${OPENRGB_SOURCE_DIR}/RGBController
    ${OPENRGB_SOURCE_DIR}/dependencies/json
)
target_link_libraries(galleon_openrgb_plugin PRIVATE
    galleon_fakekeys Qt6::Core Qt6::Gui Qt6::Widgets
)
set_target_properties(galleon_openrgb_plugin PROPERTIES OUTPUT_NAME galleoncore-openrgb)

enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(test_fakekeys tests/test_fakekeys.cpp)
target_link_libraries(test_fakekeys PRIVATE galleon_fakekeys Qt6::Test)
add_test(NAME test_fakekeys COMMAND test_fakekeys)
set_tests_properties(test_fakekeys PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
