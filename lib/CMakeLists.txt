# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: LGPL-3.0-or-later

add_library(galleon SHARED
    include/galleon/StreamDeck.h
    src/StreamDeck.cpp
)

target_include_directories(galleon PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(galleon
    PUBLIC Qt6::Core Qt6::Gui
    PRIVATE PkgConfig::HIDAPI
)

set_target_properties(galleon PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(TARGETS galleon LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/galleon DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
