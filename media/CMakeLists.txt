# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

# The Qt-facing adapter and its test doubles build on Linux as well. Native
# Windows API calls are isolated in a separate translation unit.
add_library(galleon_media STATIC ${PROJECT_SOURCE_DIR}/daemon/MediaController.h)
set_target_properties(galleon_media PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(galleon_media PUBLIC ${PROJECT_SOURCE_DIR}/daemon)
target_link_libraries(galleon_media PUBLIC Qt6::Core)
add_library(galleon_smtc STATIC SmtcController.h SmtcController.cpp SmtcSession.h)
target_include_directories(galleon_smtc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/daemon)
target_link_libraries(galleon_smtc PUBLIC galleon_media PRIVATE galleon_warnings)
if(WIN32)
    target_sources(galleon_smtc PRIVATE WindowsSmtcSession.cpp)
    target_compile_definitions(galleon_smtc PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(galleon_smtc PRIVATE windowsapp)
endif()
