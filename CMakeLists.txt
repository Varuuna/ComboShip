cmake_minimum_required(VERSION 3.26.0 FATAL_ERROR)

set(CMAKE_SYSTEM_VERSION 10.0 CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD 20 CACHE STRING "The C++ standard to use")
set(CMAKE_C_STANDARD 23 CACHE STRING "The C standard to use")

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")

project(ComboShip VERSION 1.0.0 LANGUAGES C CXX)

# Include both game-specific cvars
include(CMake/soh-cvars.cmake)
include(CMake/2ship-cvars.cmake)
include(CMake/lus-cvars.cmake)

set(PROJECT_BUILD_NAME "Combo Build" CACHE STRING "" FORCE)
set(PROJECT_TEAM "github.com/harbourmasters" CACHE STRING "" FORCE)

# Git information
execute_process(
    COMMAND git branch --show-current
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(CMAKE_PROJECT_GIT_BRANCH "${GIT_BRANCH}" CACHE STRING "Git branch" FORCE)

execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get only the first 7 characters of the hash
string(SUBSTRING "${GIT_COMMIT_HASH}" 0 7 SHORT_COMMIT_HASH)

set(CMAKE_PROJECT_GIT_COMMIT_HASH "${SHORT_COMMIT_HASH}" CACHE STRING "Git commit hash" FORCE)

execute_process(
    COMMAND git describe --tags --abbrev=0 --exact-match HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT GIT_COMMIT_TAG)
    set(GIT_COMMIT_TAG "" CACHE STRING "Git commit tag" FORCE)
endif()

set(CMAKE_PROJECT_GIT_COMMIT_TAG "${GIT_COMMIT_TAG}" CACHE STRING "Git commit tag" FORCE)

# Set startup project for Visual Studio
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT combo)

# Compiler options
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/MP>)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
# ComboShip: upstream Shipwright/2ship set this in THEIR root CMakeLists; we have our own root,
# so mirror it here. Required by soh's (and mm's) randomizer X-macro enum headers, which use the
# C++20 __VA_OPT__ preprocessor feature that MSVC only supports under the conforming preprocessor.
# Surfaced by the soh upstream merge (see docs/UPSTREAM_MERGES.md).
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>)

# Warning suppression option (from OOT)
option(SUPPRESS_WARNINGS "Suppress warnings in LUS and src (decomp)" ON)
if(SUPPRESS_WARNINGS)
    MESSAGE("Suppressing warnings in LUS and src")
    if(MSVC)
        set(WARNING_OVERRIDE /w)
    else()
        set(WARNING_OVERRIDE -w)
    endif()
else()
    MESSAGE("Skipping warning suppression")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if (CMAKE_C_COMPILER_LAUNCHER MATCHES "ccache|sccache")
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
    endif()
endif()

if (CMAKE_SYSTEM_NAME MATCHES "Windows|Linux")
    if(NOT DEFINED BUILD_CROWD_CONTROL)
        set(BUILD_CROWD_CONTROL OFF)
    endif()
endif()

# Enable the Gfx debugger in LUS to use libgfxd from ZAPDTR
set(GFX_DEBUG_DISASSEMBLER ON)

# Tell LUS we're using F3DEX_GBI_2 (in a way that doesn't break libgfxd)
set(GBI_UCODE F3DEX_GBI_2)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

# Enable MPQ and OTR support
set(INCLUDE_MPQ_SUPPORT ON)

################################################################################
# Set target arch type if empty. Visual studio solution generator provides it.
################################################################################
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(NOT CMAKE_VS_PLATFORM_NAME)
        set(CMAKE_VS_PLATFORM_NAME "x64")
    endif()
    message("${CMAKE_VS_PLATFORM_NAME} architecture in use")

    if(NOT ("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64"
        OR "${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32"))
        message(FATAL_ERROR "${CMAKE_VS_PLATFORM_NAME} arch is not supported!")
    endif()
endif()

################################################################################
# VCPKG dependencies for Windows
################################################################################
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
include(CMake/automate-vcpkg.cmake)

if ("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
	set(VCPKG_TRIPLET x86-windows-static)
	set(VCPKG_TARGET_TRIPLET x86-windows-static)
elseif ("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
	# static-md = STATIC libraries + DYNAMIC CRT. The combo build splits into libultraship.dll +
	# soh.dll + 2ship.dll that pass C++ objects across module boundaries, so every dependency must
	# share ONE dynamic UCRT heap (a static-CRT triplet gives each module its own heap → the
	# `__acrt_first_block == header` debug-heap assert). The libraries themselves must stay STATIC,
	# not DLLs: tinyxml2 is also vendored in-tree (ZAPDTR/lib/tinyxml2) and a DLL build collides
	# with the vendored copy (LNK2005). Static libs let the duplicate inline symbols COMDAT-fold.
	set(VCPKG_TRIPLET x64-windows-static-md)
	set(VCPKG_TARGET_TRIPLET x64-windows-static-md)
endif()

vcpkg_bootstrap()
vcpkg_install_packages(zlib bzip2 libzip libpng sdl2 sdl2-net glew glfw3 nlohmann-json tinyxml2 spdlog libogg libvorbis opus opusfile)
endif()

################################################################################
# Global configuration types
################################################################################
if (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    set(CMAKE_C_FLAGS_DEBUG "-g -ffast-math -DDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -ffast-math -DDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -ffast-math -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -ffast-math -DNDEBUG")
elseif(MSVC)
    # We need to remove RTC1 as it is not compatible with O2, which we set for some files in DEBUG
    string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    set(CMAKE_C_FLAGS_RELEASE "/O2 /DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
else()
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
    set(CMAKE_OBJCXX_FLAGS_RELEASE "-O2 -DNDEBUG")
endif()

if(NOT CMAKE_BUILD_TYPE )
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
endif()

################################################################################
# Common utils
################################################################################
include(CMake/Utils.cmake)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    get_linux_lsb_release_information()
    message(STATUS "Linux ${LSB_RELEASE_ID_SHORT} ${LSB_RELEASE_VERSION_SHORT} ${LSB_RELEASE_CODENAME_SHORT}")
else()
    message(STATUS ${CMAKE_SYSTEM_NAME})
endif()

################################################################################
# Additional Global Settings(add specific info there)
################################################################################
include(CMake/GlobalSettingsInclude.cmake OPTIONAL)

################################################################################
# Use solution folders feature
################################################################################
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

################################################################################
# Set CONTROLLERBUTTONS_T (from OOT)
################################################################################
add_compile_definitions(CONTROLLERBUTTONS_T=uint32_t)
add_compile_definitions(COMBO_BUILD)

find_package(Python3 REQUIRED)

################################################################################
# Sub-projects - Shared dependencies
################################################################################
# COMBO_BUILD splits the engine into libultraship.dll + soh.dll + 2ship.dll that exchange
# C++ objects across module boundaries, so every target must share ONE dynamic UCRT heap.
# Default all sub-projects to the dynamic CRT; targets that explicitly pin the static CRT
# (the games, ZAPD, prism) are individually corrected to dynamic as well. A mixed CRT here
# causes the runtime debug-heap assert `__acrt_first_block == header`.
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ComboShip: spdlog log-level numeric values. libultraship/src/CMakeLists.txt sets these but only
# in its own subdirectory scope, so sibling subdirs (soh, mm) don't inherit them -> their
# LOG_LEVEL_GAME_PRINTS=${SPDLOG_LEVEL_OFF} compile-def expands to empty -> #if C1017 in
# soh/include/functions.h. Define them here at root scope so every game subdir inherits them.
# (Surfaced by the soh upstream merge once libultraship's logging CMake was rescoped. See
# docs/UPSTREAM_MERGES.md.)
set(SPDLOG_LEVEL_TRACE 0)
set(SPDLOG_LEVEL_DEBUG 1)
set(SPDLOG_LEVEL_INFO 2)
set(SPDLOG_LEVEL_WARN 3)
set(SPDLOG_LEVEL_ERROR 4)
set(SPDLOG_LEVEL_CRITICAL 5)
set(SPDLOG_LEVEL_OFF 6)

# Shared libultraship (in Combo directory)
add_subdirectory(libultraship ${CMAKE_BINARY_DIR}/libultraship)
target_compile_options(libultraship PRIVATE "${WARNING_OVERRIDE}")
target_compile_definitions(libultraship PUBLIC INCLUDE_MPQ_SUPPORT)

# Shared ZAPD (in Combo directory)
add_subdirectory(ZAPDTR/ZAPD ${CMAKE_BINARY_DIR}/ZAPD)

# Shared OTRExporter (in Combo directory)
add_subdirectory(OTRExporter ${CMAKE_BINARY_DIR}/OTRExporter)
# OTRExporter blanks CMAKE_CXX_FLAGS in its own scope, which drops the inherited dynamic-CRT
# default; force the runtime library on the target so it links the dynamic CRT like everything
# else (otherwise OTRExporter.lib triggers LNK2038/LNK2005 CRT mismatch in soh.dll / 2ship.dll).
if(MSVC AND TARGET OTRExporter)
    set_target_properties(OTRExporter PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

################################################################################
# Sub-projects - Game-specific builds
################################################################################
add_subdirectory(soh ${CMAKE_BINARY_DIR}/soh)

add_subdirectory(mm ${CMAKE_BINARY_DIR}/mm)

add_subdirectory(combo ${CMAKE_BINARY_DIR}/combo)

################################################################################
# Meta target to build both games and the combo executable
################################################################################
add_custom_target(combo ALL DEPENDS soh 2ship ComboShip)

################################################################################
# Asset archive generation targets
################################################################################
add_custom_target(
    GenerateSohOtr
    COMMAND ${CMAKE_COMMAND} -E rm -f soh.o2r
    COMMAND ${CMAKE_COMMAND} -E rm -r -f ${CMAKE_CURRENT_SOURCE_DIR}/soh/assets/custom/shaders/
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/src/graphic/Fast3D/shaders/ ${CMAKE_CURRENT_SOURCE_DIR}/soh/assets/custom/shaders/
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/OTRExporter/extract_assets.py -z "$<TARGET_FILE:ZAPD>" --norom --custom-otr-file soh.o2r "--custom-assets-path" ${CMAKE_CURRENT_SOURCE_DIR}/soh/assets/custom --port-ver "${CMAKE_PROJECT_VERSION}"
    COMMAND ${CMAKE_COMMAND} -DSYSTEM_NAME=${CMAKE_SYSTEM_NAME} -DTARGET_DIR="$<TARGET_FILE_DIR:ZAPD>" -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DBINARY_DIR=${CMAKE_BINARY_DIR} -DONLYSOHOTR=On -P ${CMAKE_CURRENT_SOURCE_DIR}/CMake/copy-existing-otrs.cmake
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/soh
    COMMENT "Generating soh.o2r..."
    DEPENDS ZAPD)

add_custom_target(
    Generate2ShipOtr
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<VERSION_LESS:${CMAKE_VERSION},3.17>,remove,rm> -f 2ship.o2r
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/OTRExporter/extract_assets.py -z "$<TARGET_FILE:ZAPD>" --norom --custom-otr-file 2ship.o2r "--custom-assets-path" ${CMAKE_CURRENT_SOURCE_DIR}/mm/assets/custom --port-ver "${CMAKE_PROJECT_VERSION}"
    COMMAND ${CMAKE_COMMAND} -DSYSTEM_NAME=${CMAKE_SYSTEM_NAME} -DTARGET_DIR="$<TARGET_FILE_DIR:ZAPD>" -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DBINARY_DIR=${CMAKE_BINARY_DIR} -DONLY2SHIPOTR=On -P ${CMAKE_CURRENT_SOURCE_DIR}/CMake/copy-existing-otrs.cmake
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/mm
    COMMENT "Generating 2ship.o2r..."
    DEPENDS ZAPD)

################################################################################
# Installation and Packaging
################################################################################
if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    # Install both games
    install(FILES "${CMAKE_BINARY_DIR}/soh/soh.o2r" DESTINATION . COMPONENT ship)
    install(FILES "${CMAKE_BINARY_DIR}/mm/2ship.o2r" DESTINATION . COMPONENT ship)
    install(TARGETS ZAPD DESTINATION ./assets/extractor COMPONENT extractor)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/soh/assets/extractor/" DESTINATION ./assets/oot COMPONENT extractor)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/soh/assets/xml/" DESTINATION ./assets/xml/oot COMPONENT extractor)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/mm/assets/extractor/" DESTINATION ./assets/mm COMPONENT extractor)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/mm/assets/xml/" DESTINATION ./assets/xml/mm COMPONENT extractor)
endif()

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/soh/assets/extractor/" DESTINATION ./assets/oot COMPONENT combo)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/soh/assets/xml/" DESTINATION ./assets/xml/oot COMPONENT combo)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/mm/assets/extractor/" DESTINATION ./assets COMPONENT combo)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/mm/assets/xml/" DESTINATION ./assets/xml COMPONENT combo)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Windows|NintendoSwitch|CafeOS")
    install(FILES ${CMAKE_SOURCE_DIR}/README.md DESTINATION . COMPONENT combo RENAME readme.txt )
    install(CODE "file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/mods\")" COMPONENT combo)
    install(CODE "file(TOUCH \"\${CMAKE_INSTALL_PREFIX}/mods/custom_mod_files_go_here.txt\")" COMPONENT combo)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(CPACK_GENERATOR "External")
elseif(CMAKE_SYSTEM_NAME MATCHES "Windows|NintendoSwitch|CafeOS")
    set(CPACK_GENERATOR "ZIP")
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(CPACK_GENERATOR "Bundle")
endif()

set(CPACK_PROJECT_CONFIG_FILE ${CMAKE_SOURCE_DIR}/CMake/Packaging-2.cmake)
include(CMake/Packaging.cmake)