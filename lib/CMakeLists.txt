# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: LGPL-3.0-or-later

# Start Windows with a static core; a public DLL ABI will be designed alongside
# the desktop port. Linux keeps the existing shared-library ABI and packages.
if(WIN32)
    set(galleon_library_type STATIC)
else()
    set(galleon_library_type SHARED)
endif()
add_library(galleon ${galleon_library_type}
    include/galleon/Layout.h
    include/galleon/Protocol.h
    include/galleon/Transport.h
    include/galleon/HidTransport.h
    include/galleon/StreamDeck.h
    src/Layout.cpp
    src/Protocol.cpp
    src/HidTransport.cpp
    src/StreamDeck.cpp
)

target_include_directories(galleon PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(galleon
    PUBLIC Qt6::Core Qt6::Gui
    PRIVATE ${GALLEON_HIDAPI_TARGET} galleon_warnings
)

set_target_properties(galleon PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(TARGETS galleon
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/galleon DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
