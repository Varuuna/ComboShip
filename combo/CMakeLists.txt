cmake_minimum_required(VERSION 3.26.0 FATAL_ERROR)

project(ComboShip LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 23)

# Source files
set(COMBO_SOURCES
    ComboShip.cpp
)

# Create executable
add_executable(ComboShip ${COMBO_SOURCES})

# Portable relative-include fix: MSVC VS generator implicitly adds the source file's directory,
# but Ninja/Unix builds do not. Explicitly add the combo source dir so that includes like
# "rando/CrossMailbox.h" resolve on all generators.
target_include_directories(ComboShip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# nlohmann/json is used by combo/rando/CrossMailbox.h (header-only, pulled transitively through vcpkg)
find_package(nlohmann_json REQUIRED)
target_link_libraries(ComboShip PRIVATE nlohmann_json::nlohmann_json)

# Add dependencies to ensure games are built first
add_dependencies(ComboShip
    soh
    2ship
    libultraship
)

# Windows-specific settings
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_target_properties(ComboShip PROPERTIES
        OUTPUT_NAME "ComboShip"
    )
    
    # Copy ComboShip.exe, game DLLs, and libultraship.dll to Combo/x64/Debug and to ComboShip's build dir.
    # The PORT/custom asset archives (soh.o2r, 2ship.o2r) are checked into the source tree
    # (soh/soh.o2r, mm/2ship.o2r) and are copied alongside the exe here so the build is runnable.
    # The ROM-extracted archives (oot.o2r, mm.o2r) are NOT our responsibility — the user generates
    # those from their own ROMs at first launch.
    add_custom_command(TARGET ComboShip POST_BUILD
        # Copy ComboShip.exe to x64/Debug
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ComboShip>
            ${CMAKE_SOURCE_DIR}/x64/Debug/ComboShip.exe
        # Copy game DLLs to x64/Debug
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:soh>
            ${CMAKE_SOURCE_DIR}/x64/Debug/soh.dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:2ship>
            ${CMAKE_SOURCE_DIR}/x64/Debug/2ship.dll
        # Copy shared engine DLL to x64/Debug
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:libultraship>
            ${CMAKE_SOURCE_DIR}/x64/Debug/libultraship.dll
        # Also copy DLLs to ComboShip's build directory for local testing
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:soh>
            $<TARGET_FILE_DIR:ComboShip>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:2ship>
            $<TARGET_FILE_DIR:ComboShip>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:libultraship>
            $<TARGET_FILE_DIR:ComboShip>
        # Copy the PORT/custom .o2r assets (config-independent) next to the exe and to x64/Debug.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/soh/soh.o2r
            $<TARGET_FILE_DIR:ComboShip>/soh.o2r
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/mm/2ship.o2r
            $<TARGET_FILE_DIR:ComboShip>/2ship.o2r
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/soh/soh.o2r
            ${CMAKE_SOURCE_DIR}/x64/Debug/soh.o2r
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/mm/2ship.o2r
            ${CMAKE_SOURCE_DIR}/x64/Debug/2ship.o2r
        # Extractor assets (flat structure matching CallZapd expectations). ComboShip runs BOTH
        # the OOT (soh) and MM (2ship) extractors, so merge both games' extractor + xml dirs into
        # one assets/ tree. copy_directory merges (no delete), so soh + mm coexist.
        # --- next to the exe (config-correct: covers Debug AND Release build dirs) ---
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/soh/assets/extractor
            $<TARGET_FILE_DIR:ComboShip>/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/soh/assets/xml
            $<TARGET_FILE_DIR:ComboShip>/assets/xml
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/mm/assets/extractor
            $<TARGET_FILE_DIR:ComboShip>/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/mm/assets/xml
            $<TARGET_FILE_DIR:ComboShip>/assets/xml
        # --- legacy x64/Debug runtime dir (both games) ---
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/soh/assets/extractor
            ${CMAKE_SOURCE_DIR}/x64/Debug/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/soh/assets/xml
            ${CMAKE_SOURCE_DIR}/x64/Debug/assets/xml
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/mm/assets/extractor
            ${CMAKE_SOURCE_DIR}/x64/Debug/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/mm/assets/xml
            ${CMAKE_SOURCE_DIR}/x64/Debug/assets/xml
    )
endif()

# Install
install(TARGETS ComboShip DESTINATION . COMPONENT combo)
