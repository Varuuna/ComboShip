include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/cvars.cmake)

add_library(libultraship SHARED)
set_target_properties(libultraship PROPERTIES PREFIX "")
set_property(TARGET libultraship PROPERTY CXX_STANDARD 20)
# Need to set C11 for libgfxd (_Alignas)
set_property(TARGET libultraship PROPERTY C_STANDARD 11)
# ComboShip: force auto-export OFF. Upstream sets WINDOWS_EXPORT_ALL_SYMBOLS and libtcc sets it ON
# globally, but that conflicts with our custom /DEF pipeline below: it appends its own __create_def
# AFTER our '='-stripping filter, leaving a bogus '=' COFF symbol (LNK2001: unresolved external
# symbol =). We own the export .def ourselves, so this must stay FALSE.
set_target_properties(libultraship PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS FALSE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Export libultraship's full symbol surface so soh.dll and 2ship.dll share ONE copy of
    # every engine global — most importantly Context::mContext, the singleton that lets MM
    # reuse OOT's window during the combo transition instead of opening a new one.
    #
    # We deliberately do NOT use WINDOWS_EXPORT_ALL_SYMBOLS. That property appends its own
    # `__create_def` step as the LAST pre-link command, so any PRE_LINK filter runs BEFORE
    # the .def is (re)generated and can never strip the bogus '=' absolute COFF symbol that
    # the scan emits — the linker then fails with `LNK2001: unresolved external symbol =`.
    # Instead we own the whole pipeline: generate the .def from libultraship's own object
    # files (the exact same input WINDOWS_EXPORT_ALL_SYMBOLS scans, so the export set is
    # identical), strip '=', then feed the result to the linker via /DEF.
    set(LUS_OBJ_LIST "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/lus_objects.txt")
    set(LUS_DEF_FILE "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/lus_exports.def")

    # Newline-separated list of libultraship's compiled objects (resolved per-config).
    file(GENERATE OUTPUT "${LUS_OBJ_LIST}"
        CONTENT "$<JOIN:$<TARGET_OBJECTS:libultraship>,\n>\n")

    add_custom_command(TARGET libultraship PRE_LINK
        COMMAND ${CMAKE_COMMAND} -E __create_def "${LUS_DEF_FILE}" "${LUS_OBJ_LIST}"
        COMMAND ${CMAKE_COMMAND} -D "DEF_FILE=${LUS_DEF_FILE}"
            -P "${CMAKE_SOURCE_DIR}/CMake/filter_def.cmake"
        COMMENT "Generating + stripping libultraship export def"
        VERBATIM
    )
    target_link_options(libultraship PRIVATE "/DEF:${LUS_DEF_FILE}")
endif()

set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../include)
option(USE_OPENGLES "Enable GLES3" OFF)
option(GFX_DEBUG_DISASSEMBLER "Enable libgfxd" OFF)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
use_props(${PROJECT_NAME} "${CMAKE_CONFIGURATION_TYPES}" "${DEFAULT_CXX_PROPS}")
endif()

#=================== Top-Level ===================

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ship/install_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/ship/install_config.h @ONLY)

if(ENABLE_SCRIPTING)
    file(GLOB SCRIPT_PUB_KEYS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../keys/script/*.pem")
    set(EMBEDDED_KEYS_HEADER ${CMAKE_CURRENT_BINARY_DIR}/ship/DefaultKeys.h)
    set(GENERATE_KEYS_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_keys_header.py)
    get_filename_component(EMBEDDED_KEYS_DIR ${EMBEDDED_KEYS_HEADER} DIRECTORY)

    add_custom_command(
        OUTPUT ${EMBEDDED_KEYS_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${EMBEDDED_KEYS_DIR}
        COMMAND Python3::Interpreter ${GENERATE_KEYS_SCRIPT}
                ${EMBEDDED_KEYS_HEADER}
                ${SCRIPT_PUB_KEYS}
        DEPENDS ${GENERATE_KEYS_SCRIPT}
                ${SCRIPT_PUB_KEYS}
        COMMENT "Generating DefaultKeys.h from all keys in /keys/script/"
        VERBATIM
    )

    add_custom_target(GenerateScriptKeys DEPENDS ${EMBEDDED_KEYS_HEADER})
    add_dependencies(libultraship GenerateScriptKeys)
endif()

#=================== Fast ===================

add_subdirectory("fast")

#=================== LUS ===================

add_subdirectory("libultraship")

#=================== Ship ===================

add_subdirectory("ship")

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/ship/utils/AppleFolderManager.mm PROPERTIES COMPILE_FLAGS -fno-objc-arc)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/ship/utils/macUtils.mm PROPERTIES COMPILE_FLAGS -fno-objc-arc)
endif()

#=================== Packages & Includes ===================

target_include_directories(libultraship
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${INCLUDE_DIR} ${ADDITIONAL_LIB_INCLUDES}
)

#=================== Linking ===================
if(INCLUDE_MPQ_SUPPORT)
    if (NOT CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        target_link_libraries(libultraship PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,storm>")
    else()
        target_link_libraries(libultraship PUBLIC storm)
    endif()
endif()

target_link_libraries(libultraship
    PUBLIC ImGui
)

# PUBLIC so soh.dll / 2ship.dll inherit these: their own code (ResourceMgr JPEG, OTRExporter)
# calls into stb / libzip directly. As a STATIC lib these came in transitively; as a SHARED
# lib libultraship no longer re-exposes PRIVATE deps, so consumers must link them too.
target_link_libraries(libultraship PUBLIC stb)

if (GFX_DEBUG_DISASSEMBLER)
    target_link_libraries(libultraship PRIVATE libgfxd)
endif()

find_package(libzip REQUIRED)
target_link_libraries(libultraship PUBLIC libzip::zip)

find_package(nlohmann_json REQUIRED)
target_link_libraries(libultraship PUBLIC nlohmann_json::nlohmann_json)

find_package(tinyxml2 REQUIRED)
target_link_libraries(libultraship PUBLIC tinyxml2::tinyxml2)

find_package(spdlog REQUIRED)
target_link_libraries(libultraship PUBLIC spdlog::spdlog)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(libultraship PRIVATE Threads::Threads)
    find_library(COCOA Cocoa)
    target_link_libraries(libultraship PRIVATE ${COCOA})

endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    find_Library(OSX_FOUNDATION Foundation)
    find_Library(OSX_AVFOUNDATION AVFoundation)
    find_library(METAL Metal)
    find_library(QUARTZCORE QuartzCore)
    find_library(COREAUDIO CoreAudio)
    find_library(AUDIOTOOLBOX AudioToolbox)
    target_link_libraries(libultraship PRIVATE ${OSX_FOUNDATION} ${OSX_AVFOUNDATION} ${METAL} ${QUARTZCORE} ${COREAUDIO} ${AUDIOTOOLBOX} ${CMAKE_DL_LIBS})
endif()

if (USE_OPENGLES)
    if (CMAKE_SYSTEM_NAME STREQUAL "Android")
        find_library(OPENGLES_LIBRARY GLESv3)
        target_link_libraries(libultraship PRIVATE GLESv3)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
        target_link_libraries(libultraship PRIVATE OpenGL::GLES2)
    else()
        find_library(OPENGLES_LIBRARY GLESv2)
        target_link_libraries(libultraship PRIVATE GLESv2)
    endif()
endif()

target_link_libraries(libultraship PUBLIC prism monocypher)

if(ENABLE_SCRIPTING)
    target_link_libraries(libultraship PUBLIC libtcc libtcc1)
    target_compile_definitions(libultraship PUBLIC ENABLE_SCRIPTING)
endif()

#=================== Compile Options & Defs ===================

target_compile_definitions(libultraship PRIVATE ${GBI_UCODE})

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32
        _CONSOLE
        _CRT_SECURE_NO_WARNINGS
        ENABLE_DX11
        NOMINMAX
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set_target_properties(${PROJECT_NAME} PROPERTIES
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    )
endif()

set(SPDLOG_LEVEL_TRACE 0)
set(SPDLOG_LEVEL_DEBUG 1)
set(SPDLOG_LEVEL_INFO 2)
set(SPDLOG_LEVEL_WARN 3)
set(SPDLOG_LEVEL_ERROR 4)
set(SPDLOG_LEVEL_CRITICAL 5)
set(SPDLOG_LEVEL_OFF 6)
set(LOG_LEVELS "SPDLOG_LEVEL_TRACE;SPDLOG_LEVEL_DEBUG;SPDLOG_LEVEL_INFO;SPDLOG_LEVEL_WARN;SPDLOG_LEVEL_ERROR;SPDLOG_LEVEL_CRITICAL;SPDLOG_LEVEL_OFF")
set(SPDLOG_MIN_CUTOFF SPDLOG_LEVEL_TRACE CACHE STRING "The minimum log level that will be available to a build. Can be overridden in cmake or in InitLogging")
set_property(CACHE SPDLOG_MIN_CUTOFF PROPERTY STRINGS ${LOG_LEVELS})
if(NOT SPDLOG_MIN_CUTOFF IN_LIST LOG_LEVELS)
    message(FATAL_ERROR "SPDLOG_MIN_CUTOFF must be one of ${LOG_LEVELS}")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(libultraship PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
        SPDLOG_ACTIVE_LEVEL=${SPDLOG_MIN_CUTOFF}
        __IOS__
    )

    set_xcode_property(${PROJECT_NAME} PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID} All)

    if(NOT SIGN_LIBRARY)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        )

        if(TARGET SDL2)
            set_target_properties(SDL2 PROPERTIES
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
                XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
            )
        endif()

        set_target_properties(zip PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        )
    endif()
else()
    target_compile_definitions(libultraship PRIVATE
        ENABLE_OPENGL
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
        SPDLOG_ACTIVE_LEVEL=${SPDLOG_MIN_CUTOFF}
        $<$<BOOL:${USE_OPENGLES}>:USE_OPENGLES>
        $<$<BOOL:${GFX_DEBUG_DISASSEMBLER}>:GFX_DEBUG_DISASSEMBLER>
    )
endif()

if(MSVC)
    target_compile_options(libultraship PRIVATE
        $<$<CONFIG:Debug>:
            /Od;
            /Oi-
        >
        $<$<CONFIG:Release>:
            /Oi;
            /Gy
        >
        /permissive-;
        /MP;
        /sdl;
        /W3;
        ${DEFAULT_CXX_DEBUG_INFORMATION_FORMAT};
        ${DEFAULT_CXX_EXCEPTION_HANDLING};
    )
    target_link_options(libultraship PRIVATE
        $<$<CONFIG:Release>:
            /OPT:REF;
            /OPT:ICF
        >
        /SUBSYSTEM:CONSOLE
    )
endif()

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU|Clang")
	target_compile_options(libultraship PRIVATE
		-Wall
		-Wextra
		-Wno-error
		-Wno-unused-variable
		-Wno-unused-parameter
		-Wno-unused-function
		-Wno-parentheses
		-Wno-narrowing
		-Wno-missing-field-initializers
        $<$<COMPILE_LANGUAGE:C,OBJC>:-Wno-implicit-function-declaration>
        $<$<COMPILE_LANGUAGE:C,OBJC>:-Wno-int-conversion>
	)
    if (MSVC)
    target_compile_options(libultraship PRIVATE
        -Wno-implicit-function-declaration
    )
    endif()
endif()

