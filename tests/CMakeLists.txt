# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

function(galleon_add_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE galleon galleon_warnings Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ASAN_OPTIONS=detect_leaks=1;LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp"
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
galleond_add_test(test_tilerenderer)
