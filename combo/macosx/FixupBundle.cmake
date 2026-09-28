# ComboShip: install-time fixup of the macOS .app. Included from an install(CODE) rule in the
# top-level CMakeLists, so it runs inside cpack's Bundle staging, where CMAKE_INSTALL_PREFIX is
# <app>/Contents/Resources. The caller sets COMBO_BUNDLE_PLIST.

set(_macos "${CMAKE_INSTALL_PREFIX}/../MacOS")

# Copy every non-system dylib into the bundle and repoint the load commands at the copies — the
# same fixup_bundle upstream Shipwright's .app uses. Without it the binaries link straight into
# /opt/homebrew (and the SDL prefix) and the bundle cannot start on any other Mac. The modules are
# dlopen'd, not linked by the exe, so they must be named: fixup_bundle's own scan skips dylibs.
#
# Flat, into Contents/MacOS beside the modules, rather than the default Contents/Frameworks: with the
# default, references to a module (libsoh -> libultraship) are rewritten to Frameworks/ while the
# module itself stays in MacOS/, and the bundle cannot load libsoh.
include(BundleUtilities)
function(gp_item_default_embedded_path_override item default_embedded_path_var)
    set(${default_embedded_path_var} "@executable_path" PARENT_SCOPE)
endfunction()
set(_modules "")
foreach(_m libultraship.dylib comboui.dylib libsoh.dylib lib2ship.dylib)
    list(APPEND _modules "${_macos}/${_m}")
endforeach()
fixup_bundle("${_macos}/ComboShip" "${_modules}" "")

# sdl2-compat (Homebrew's `sdl2`) loads SDL3 with dlopen, which fixup_bundle cannot see, so a bundle
# carrying it passes verify_app and then fails to start anywhere.
# ponytail: refuse rather than also bundle SDL3 — the README already steers packaging to genuine
# SDL2. Copying libSDL3.dylib beside it (sdl2-compat tries @loader_path first) would lift this.
file(STRINGS "${_macos}/libSDL2-2.0.0.dylib" _sdl2_compat REGEX "sdl2-compat" LIMIT_COUNT 1)
if(_sdl2_compat)
    message(FATAL_ERROR "ComboShip: the bundled libSDL2 is sdl2-compat, which needs an SDL3 the .app "
                        "does not carry. Package against genuine SDL2 (see the README's SDL2 note).")
endif()

# One pass over every Mach-O in the bundle:
#  - Every @executable_path load command must name a file that is actually there. verify_app only
#    checks that references point into the bundle, so it passed the Frameworks/MacOS split above.
#  - LSMinimumSystemVersion = the highest LC_BUILD_VERSION minos. The bundled Homebrew dylibs are
#    built for the machine they came from, not for CMAKE_OSX_DEPLOYMENT_TARGET. (Pre-10.14
#    LC_VERSION_MIN_MACOSX binaries are below our floor anyway.) cpack copies the plist into the
#    .app only after this install step, so patching it here lands.
set(_minos "0")
file(GLOB _bins "${_macos}/*")
foreach(_bin IN LISTS _bins)
    execute_process(COMMAND otool -l "${_bin}" OUTPUT_VARIABLE _lc ERROR_QUIET)
    string(REGEX MATCHALL "name @executable_path/[^ ]+" _refs "${_lc}")
    foreach(_ref IN LISTS _refs)
        string(REPLACE "name @executable_path" "${_macos}" _path "${_ref}")
        if(NOT EXISTS "${_path}")
            message(FATAL_ERROR "ComboShip: ${_bin} loads ${_ref}, which is not in the bundle")
        endif()
    endforeach()
    string(REGEX MATCHALL "minos [0-9.]+" _found "${_lc}")
    foreach(_v IN LISTS _found)
        string(SUBSTRING "${_v}" 6 -1 _v)
        if(_v VERSION_GREATER _minos)
            set(_minos "${_v}")
        endif()
    endforeach()
endforeach()
file(READ "${COMBO_BUNDLE_PLIST}" _plist)
string(REGEX REPLACE "(<key>LSMinimumSystemVersion</key><string>)[^<]*" "\\1${_minos}" _plist "${_plist}")
file(WRITE "${COMBO_BUNDLE_PLIST}" "${_plist}")
message(STATUS "ComboShip: LSMinimumSystemVersion ${_minos}")
