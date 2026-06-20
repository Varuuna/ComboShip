cmake_minimum_required(VERSION 3.26.0 FATAL_ERROR)

project(ComboShip LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 23)

# ── ComboShip builds soh/2ship as SHARED DLLs (upstream builds them as exes), so MSVC emits DLL-boundary
#    warnings their exe build never sees — and soh's /WX (#6733) makes them hard errors (e.g. C4273
#    'inconsistent dll linkage' on the CRT floorf/ceilf forward-decls in OTRGlobals.cpp). Silence that
#    *class* only for the shared game targets; /WX still catches genuine issues (it caught a real
#    float->ImDrawFlags bug in our menu code). Combo-owned — no edits to the vendored CMake. We append
#    after soh's /WX (combo/ is add_subdirectory'd last), so /wd wins for these codes. See docs/UPSTREAM_MERGES.md.
if(MSVC)
    foreach(_combo_shared_target soh 2ship)
        if(TARGET ${_combo_shared_target})
            target_compile_options(${_combo_shared_target} PRIVATE
                /wd4273   # inconsistent dll linkage (CRT fns re-declared in a DLL TU)
                /wd4251   # needs to have dll-interface to be used by clients (STL in exported class)
                /wd4275   # non dll-interface class used as base for dll-interface class
            )
        endif()
    endforeach()
endif()

# ─── comboui.dll: combo-owned unified settings menu (links libultraship + ImGui) ───
set(COMBOUI_SOURCES
    gui/ComboMenu.cpp
    gui/ComboMenuModel.cpp
    gui/ComboWidgetRender.cpp
    gui/ComboWidgetStyle.cpp
)
add_library(comboui SHARED ${COMBOUI_SOURCES})
set_target_properties(comboui PROPERTIES PREFIX "")
# ComboShip: deploy comboui.dll directly into the shared runtime dir (x64/<config>),
# matching how soh/2ship set their own RUNTIME_OUTPUT_DIRECTORY. Without this, a
# per-target `--target comboui` build lands in build/x64/combo/<config> and never
# reaches the exe's load path until the ComboShip exe is rebuilt — a stale-DLL trap.
set_target_properties(comboui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/x64/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/x64/Release"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/x64/Debug"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/x64/Release"
)
target_compile_definitions(comboui PRIVATE COMBO_BUILD)
target_include_directories(comboui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/menu
    ${CMAKE_CURRENT_SOURCE_DIR}/../libultraship/include
)
target_link_libraries(comboui PRIVATE libultraship)
add_dependencies(comboui libultraship)

# Source files
set(COMBO_SOURCES
    ComboShip.cpp
)

# Create executable
add_executable(ComboShip ${COMBO_SOURCES})

# Portable relative-include fix: MSVC VS generator implicitly adds the source file's directory,
# but Ninja/Unix builds do not. Explicitly add the combo source dir so that includes like
# "rando/CrossForeign.h" resolve on all generators.
target_include_directories(ComboShip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# nlohmann/json is used by combo/rando/CrossForeign.h (header-only, pulled transitively through vcpkg)
find_package(nlohmann_json REQUIRED)
target_link_libraries(ComboShip PRIVATE nlohmann_json::nlohmann_json)

# ComboShip owns the persistent Anchor network connection (Phase 1), so it links SDL_net directly.
# Mirrors soh's linkage (SDL2_net::SDL2_net-static on the static-md triplet, plain otherwise). We do
# NOT link SDL2main here — ComboShip provides its own main().
find_package(SDL2 REQUIRED)
find_package(SDL2_net REQUIRED)
if(TARGET SDL2_net::SDL2_net-static)
    target_link_libraries(ComboShip PRIVATE SDL2::SDL2 SDL2_net::SDL2_net-static)
else()
    target_link_libraries(ComboShip PRIVATE SDL2::SDL2 SDL2_net::SDL2_net)
endif()

# Add dependencies to ensure games are built first
add_dependencies(ComboShip
    soh
    2ship
    libultraship
    comboui
)

# Windows-specific settings
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_target_properties(ComboShip PROPERTIES
        OUTPUT_NAME "ComboShip"
    )
    
    # Copy ComboShip.exe, game DLLs, and libultraship.dll to Combo/x64/Debug and to ComboShip's build dir.
    # The PORT/custom asset archives (soh/soh.o2r, mm/2ship.o2r) are produced by the GenerateSohOtr /
    # Generate2ShipOtr targets (extract_assets.py --norom — NO ROM needed; they pack the tracked
    # assets/custom tree). They are gitignored as large generated binaries, so they exist on a dev
    # machine that has run those targets but are absent on the compile-only CI gate, which runs neither.
    # Their copy is therefore guarded by an EXISTS check below (separate POST_BUILD block) so that gate
    # does not fail. The ROM-extracted archives (oot.o2r, mm.o2r) are the user's to generate at first launch.
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
        # Copy combo UI DLL to x64/Debug
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:comboui>
            ${CMAKE_SOURCE_DIR}/x64/Debug/comboui.dll
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
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:comboui>
            $<TARGET_FILE_DIR:ComboShip>
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

    # PORT/custom .o2r deploy — guarded so a missing archive doesn't fail the build (see comment above).
    # copy_if_different aborts if the source is absent; only emit these commands when the files exist at
    # configure time. A dev who has run GenerateSohOtr/Generate2ShipOtr has them; the compile-only CI
    # gate has neither and skips this block. If CI later generates the .o2r, reconfigure picks them up.
    if(EXISTS "${CMAKE_SOURCE_DIR}/soh/soh.o2r")
        add_custom_command(TARGET ComboShip POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/soh/soh.o2r $<TARGET_FILE_DIR:ComboShip>/soh.o2r
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/soh/soh.o2r ${CMAKE_SOURCE_DIR}/x64/Debug/soh.o2r
        )
    else()
        message(STATUS "ComboShip: soh/soh.o2r not found — skipping .o2r deploy (run GenerateSohOtr to produce it).")
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/mm/2ship.o2r")
        add_custom_command(TARGET ComboShip POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/mm/2ship.o2r $<TARGET_FILE_DIR:ComboShip>/2ship.o2r
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/mm/2ship.o2r ${CMAKE_SOURCE_DIR}/x64/Debug/2ship.o2r
        )
    else()
        message(STATUS "ComboShip: mm/2ship.o2r not found — skipping .o2r deploy (run Generate2ShipOtr to produce it).")
    endif()
endif()

# Install
install(TARGETS ComboShip DESTINATION . COMPONENT combo)
