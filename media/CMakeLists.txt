# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

# The Qt-facing adapter and its test doubles build on Linux as well. Native
# Windows API calls are isolated in a separate translation unit.
add_library(galleon_media STATIC ${PROJECT_SOURCE_DIR}/daemon/MediaController.h)
set_target_properties(galleon_media PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(galleon_media PUBLIC ${PROJECT_SOURCE_DIR}/daemon)
target_link_libraries(galleon_media PUBLIC Qt6::Core)
add_library(galleon_smtc STATIC SmtcController.h SmtcController.cpp SmtcSession.h AudioEndpoint.h AudioEndpoint.cpp)
target_include_directories(galleon_smtc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/daemon)
target_link_libraries(galleon_smtc PUBLIC galleon_media PRIVATE galleon_warnings)
if(WIN32)
    target_sources(galleon_smtc PRIVATE WindowsSmtcSession.cpp WindowsCoreAudio.cpp)
    target_compile_definitions(galleon_smtc PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(galleon_smtc PRIVATE windowsapp ole32)
endif()

# Shared key policy builds even when the desktop applications are disabled.
add_library(galleon_keys STATIC ${PROJECT_SOURCE_DIR}/daemon/KeyInjector.cpp)
target_include_directories(galleon_keys PUBLIC ${PROJECT_SOURCE_DIR}/daemon)
target_link_libraries(galleon_keys PUBLIC Qt6::Core PRIVATE galleon_warnings)
set_target_properties(galleon_keys PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(WIN32)
    target_sources(galleon_keys PRIVATE ${PROJECT_SOURCE_DIR}/daemon/WindowsKeyInjector.cpp)
    target_compile_definitions(galleon_keys PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(galleon_keys PRIVATE user32)
endif()

add_library(galleon_platform STATIC ${PROJECT_SOURCE_DIR}/daemon/LocalControl.h ${PROJECT_SOURCE_DIR}/daemon/LocalControl.cpp)
target_include_directories(galleon_platform PUBLIC ${PROJECT_SOURCE_DIR}/daemon)
target_link_libraries(galleon_platform PUBLIC Qt6::Core Qt6::Network PRIVATE galleon_warnings)
set_target_properties(galleon_platform PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(WIN32)
    target_sources(galleon_platform PRIVATE ${PROJECT_SOURCE_DIR}/daemon/WindowsFileTrust.cpp)
    target_compile_definitions(galleon_platform PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(galleon_platform PRIVATE advapi32)
else()
    target_sources(galleon_platform PRIVATE ${PROJECT_SOURCE_DIR}/daemon/LinuxFileTrust.cpp)
endif()
