# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

# Galleon Bosun: the settings window. The logic is a library so that the tests can use it.
add_library(bosun_core STATIC
    BosunBackend.h BosunBackend.cpp
)
target_include_directories(bosun_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bosun_core PUBLIC galleond_core Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick PRIVATE galleon_warnings)
set_target_properties(bosun_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

qt_add_resources(bosun_core "bosun_ui"
    PREFIX "/bosun"
    FILES qml/Bosun.qml icons/galleon-bosun.svg
)

add_executable(galleon-bosun main.cpp)
target_compile_definitions(galleon-bosun PRIVATE PROJECT_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(galleon-bosun PRIVATE bosun_core galleon_warnings)
install(TARGETS galleon-bosun RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES icons/galleon-bosun.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES galleon-bosun.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
