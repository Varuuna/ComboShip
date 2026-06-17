set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY 0)
# ComboShip: shared base component list. This "2s2h"-only default is a 2Ship-era leftover; it is
# the BASE that the Linux (External/DEB/RPM) and macOS (Bundle) branches below build on, so its
# value is intentionally left alone until those platforms are reworked for the combo runtime.
# The Windows ZIP branch fully OVERRIDES it with the real combo set (combo/ship/2s2h) — see below.
set(CPACK_COMPONENTS_ALL "2s2h")

if (CPACK_GENERATOR STREQUAL "External")
  list(APPEND CPACK_COMPONENTS_ALL "extractor" "appimage")
endif()

# ComboShip: the upstream default packs only the "2s2h" component (just 2ship.dll) — a leftover
# from the vendored 2Ship project. The Windows ZIP must bundle the FULL combo runtime:
#   combo = ComboShip.exe + libultraship.dll + comboui.dll + assets/ + readme.txt + mods/
#   ship  = soh.dll  + soh.o2r  + gamecontrollerdb.txt
#   2s2h  = 2ship.dll + 2ship.o2r
# (oot.o2r / mm.o2r are ROM-derived and intentionally NOT packaged — testers extract their own.)
if (CPACK_GENERATOR MATCHES "ZIP")
  set(CPACK_COMPONENTS_ALL "combo" "ship" "2s2h")
endif()

if (CPACK_GENERATOR MATCHES "DEB|RPM")
# https://unix.stackexchange.com/a/11552/254512
set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/ship/bin")#/${CMAKE_PROJECT_VERSION}")
set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY 0)
elseif (CPACK_GENERATOR MATCHES "ZIP")
set(CPACK_PACKAGING_INSTALL_PREFIX "")
# ComboShip: produce ONE merged zip containing ONLY our components.
#  - COMPONENT_INSTALL ON keeps CPack scoped to CPACK_COMPONENTS_ALL (combo/ship/2s2h); without it
#    a monolithic install greedily pulls in every other install() rule in the build tree — the
#    gtest/gmock test-framework libs (lib/), etc.
#  - ALL_COMPONENTS_IN_ONE then merges those three components into a single archive instead of the
#    default one-zip-per-component.
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
endif()

if (CPACK_GENERATOR MATCHES "External")
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
SET(CPACK_MONOLITHIC_INSTALL 1)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/bin")
endif()

# ComboShip: TODO (macOS) — this branch is still upstream 2Ship; the .app bundle is named/iconed
# "2s2h". Left intact deliberately; rework name/plist/icon for the combo when enabling macOS builds.
if (CPACK_GENERATOR MATCHES "Bundle")
    set(CPACK_BUNDLE_NAME "2s2h")
    set(CPACK_BUNDLE_PLIST "macosx/Info.plist")
    set(CPACK_BUNDLE_ICON "macosx/2s2h.icns")
    set(CPACK_BUNDLE_APPLE_CERT_APP "-")
endif()
