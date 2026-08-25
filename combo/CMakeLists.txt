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
    gui/ComboResolutionEditor.cpp
    gui/ComboWidgetStyle.cpp
    gui/ComboExtractScreen.cpp
    gui/ComboSettingsImportScreen.cpp
    gui/ComboTrackerVisibility.cpp
    gui/ComboTrackerBridge.cpp
    gui/ComboTrackerSwap.cpp
    gui/ComboAudioBridge.cpp
    gui/ComboAnchorRoomWindow.cpp
    gui/ComboNotesWindow.cpp
    gui/ComboHintTracker.cpp
    gui/ComboTimersWindow.cpp
    gui/ComboCosmeticsSync.cpp
)
add_library(comboui SHARED ${COMBOUI_SOURCES})
set_target_properties(comboui PROPERTIES PREFIX "")
# ImGui's std::string InputText helper ships in the fetched tree but not in libultraship's ImGui
# target. GetProperties re-exports imgui_SOURCE_DIR here (FetchContent ran in libultraship's scope).
include(FetchContent)
FetchContent_GetProperties(ImGui)
target_sources(comboui PRIVATE ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
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

# COMBO_RELEASE_VERSION: launcher-only gate for combosave compat (the games don't gate, so no macro there).
target_compile_definitions(ComboShip PRIVATE COMBO_RELEASE_VERSION="${COMBO_RELEASE_VERSION}")

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

# ComboShip: standalone HEADLESS cross-world seed generator/validator (combo/ComboRandoHeadless.cpp).
# Loads soh.dll/2ship.dll at runtime and drives the rando-only headless init + cross-world fill — no
# window, ResourceManager, audio, or ComboShip.exe boot. Console tool; links only nlohmann_json since
# CrossWorldRando.h is header-only. Run from x64/<config> (next to the game DLLs).
# EXCLUDE_FROM_ALL: a dev/validation tool most builders don't need, so it's kept out of the default
# build entirely — build it explicitly with `cmake --build <dir> --target comborando` when wanted.
add_executable(comborando EXCLUDE_FROM_ALL ComboRandoHeadless.cpp)
target_include_directories(comborando PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comborando PRIVATE nlohmann_json::nlohmann_json)
add_dependencies(comborando soh 2ship)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_target_properties(comborando PROPERTIES
        OUTPUT_NAME "comborando"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/x64/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/x64/Release"
    )
endif()

# Windows-specific settings
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # ComboShip: build the exe straight into the shared runtime dir (x64/<config>), matching how the
    # game DLLs (soh/2ship/libultraship/comboui) set their own output dir. x64/<config> is then the
    # SINGLE deploy location — the exe and every DLL already land here, so there is no second copy in
    # the per-target build tree (build/x64/combo/<config>) to go stale and cause a stale-DLL launch.
    set_target_properties(ComboShip PROPERTIES
        OUTPUT_NAME "ComboShip"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/x64/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/x64/Release"
    )

    # Release: no console window (kept in Debug for logs). Same /SUBSYSTEM:WINDOWS soh uses; soh relies
    # on SDL2main for WinMain, but ComboShip has its own main() (see above), so /ENTRY:mainCRTStartup
    # keeps it. Errors still persist to *_extract_error.log.
    if(MSVC)
        target_link_options(ComboShip PRIVATE
            $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS;/ENTRY:mainCRTStartup>)
    endif()

    # The exe and all game DLLs already land in x64/<config> (their own output dirs), so only the asset
    # tree and the PORT/custom .o2r archives need deploying. $<TARGET_FILE_DIR:ComboShip> == x64/<config>,
    # so this is config-correct for Debug AND Release.
    #
    # Extractor assets (flat structure matching CallZapd expectations). ComboShip runs BOTH the OOT (soh)
    # and MM (2ship) extractors, so merge both games' extractor + xml dirs into one assets/ tree.
    # Clear the destination assets/ FIRST: copy_directory merges and never deletes, so a file
    # renamed/removed upstream (e.g. mm object_rs -> object_rsn) would leave a STALE .xml that ZAPD then
    # fails on ("Raw data size: 0x0", reached EOF). Clearing makes the source tree authoritative each
    # build. (The user's ROM/port archives live in the dir root, NOT under assets/, so this is safe.)
    # See docs/merges/2026-06-20.md.
    add_custom_command(TARGET ComboShip POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:ComboShip>/assets
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
    )

    # PORT/custom .o2r deploy — guarded so a missing archive doesn't fail the build (see comment above).
    # copy_if_different aborts if the source is absent; only emit these commands when the files exist at
    # configure time. A dev who has run GenerateSohOtr/Generate2ShipOtr has them; the compile-only CI
    # gate has neither and skips this block. If CI later generates the .o2r, reconfigure picks them up.
    if(EXISTS "${CMAKE_SOURCE_DIR}/soh/soh.o2r")
        add_custom_command(TARGET ComboShip POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/soh/soh.o2r $<TARGET_FILE_DIR:ComboShip>/soh.o2r
        )
    else()
        message(STATUS "ComboShip: soh/soh.o2r not found — skipping .o2r deploy (run GenerateSohOtr to produce it).")
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/mm/2ship.o2r")
        add_custom_command(TARGET ComboShip POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/mm/2ship.o2r $<TARGET_FILE_DIR:ComboShip>/2ship.o2r
        )
    else()
        message(STATUS "ComboShip: mm/2ship.o2r not found — skipping .o2r deploy (run Generate2ShipOtr to produce it).")
    endif()
endif()

# Install
install(TARGETS ComboShip DESTINATION . COMPONENT combo)
