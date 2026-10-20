# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

# The tests put fake music players and services on the D-Bus session bus. Run on
# the user's own bus, they would be seen by a galleond that is running (it took
# them for real players and changed theme). A private bus keeps them apart.
find_program(GALLEON_DBUS_RUN_SESSION dbus-run-session)
if(NOT GALLEON_DBUS_RUN_SESSION)
    message(WARNING "dbus-run-session not found: the tests will use the session bus of the user running them")
endif()

function(galleon_add_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE galleon galleon_warnings Qt6::Test)
    if(GALLEON_DBUS_RUN_SESSION)
        add_test(NAME ${name} COMMAND ${GALLEON_DBUS_RUN_SESSION} -- $<TARGET_FILE:${name}>)
    else()
        add_test(NAME ${name} COMMAND ${name})
    endif()
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/xdg-config;ASAN_OPTIONS=detect_leaks=1;LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp"
    )
endfunction()

galleon_add_test(test_layout)
galleon_add_test(test_protocol)
galleon_add_test(test_streamdeck FakeTransport.h)

# Daemon logic. The source folders let the tests use the real widget and the
# project's wallpapers.
function(galleond_add_test name)
    galleon_add_test(${name})
    target_link_libraries(${name} PRIVATE galleond_core)
    target_compile_definitions(${name} PRIVATE
        GALLEON_SOURCE_WIDGETS="${CMAKE_SOURCE_DIR}/widgets"
        GALLEON_SOURCE_WALLPAPERS="${CMAKE_SOURCE_DIR}/public/wallpaper"
    )
endfunction()

galleond_add_test(test_wallpapercatalog)
galleond_add_test(test_screenrenderer)
galleond_add_test(test_pagesconfig)
galleond_add_test(test_desktopentries)
galleond_add_test(test_pageseditor)
galleond_add_test(test_appsbackend)
galleond_add_test(test_tilerenderer)
galleond_add_test(test_mprisController)
galleond_add_test(test_pagescontroller FakeTransport.h)
galleond_add_test(test_coverloader)
galleond_add_test(test_screencontroller FakeTransport.h)
galleond_add_test(test_videothumbnails)
galleond_add_test(test_videoprobe)
galleond_add_test(test_nowplaying_flow FakeTransport.h)
galleond_add_test(test_screensaver FakeTransport.h)
galleond_add_test(test_modes_flow FakeTransport.h)
galleond_add_test(test_usage)
galleond_add_test(test_pageorder)

# Galleon Bosun, the settings window.
galleon_add_test(test_bosun)
target_link_libraries(test_bosun PRIVATE bosun_core)
galleon_add_test(test_videoadvice)
target_link_libraries(test_videoadvice PRIVATE bosun_core)
