# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(galleond main.cpp)
target_compile_definitions(galleond PRIVATE PROJECT_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(galleond PRIVATE galleon galleon_warnings Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::DBus)
install(TARGETS galleond RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
