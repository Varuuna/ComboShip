include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.16.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(lus_tests
    binary_io_tests.cpp
    string_helper_tests.cpp
    crc64_tests.cpp
    stox_tests.cpp
    utils_tests.cpp
    bitconverter_tests.cpp
    event_system_tests.cpp
    sound_matrix_decoder_tests.cpp
    path_file_helper_tests.cpp
    archive_resource_tests.cpp
    glob_tests.cpp
    splittext_tests.cpp
    path_diskfile_tests.cpp
    resource_type_tests.cpp
    archive_self_tests.cpp
    resource_manager_scope_tests.cpp
    combo_menu_export_tests.cpp
    combo_seed_math_tests.cpp
)

if(ENABLE_SCRIPTING)
    target_sources(lus_tests PRIVATE script_test.cpp)
    if(WIN32)
        add_custom_command(TARGET lus_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:libtcc>
                $<TARGET_FILE_DIR:lus_tests>
        )
    endif()
endif()

set_property(TARGET lus_tests PROPERTY CXX_STANDARD 20)

target_link_libraries(lus_tests PRIVATE
    libultraship
    gtest_main
)

target_compile_definitions(lus_tests PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_OFF)

# ComboShip: combo-owned menu serializer is header-only; tests mock the game-side menu types.
# core/ is on the path for the same reason — the seed derivations are header-only and shared with
# comborando, so they can be pinned here without linking the launcher.
target_include_directories(lus_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../combo/menu
    ${CMAKE_CURRENT_SOURCE_DIR}/../../combo/core)

include(GoogleTest)
gtest_discover_tests(lus_tests)
