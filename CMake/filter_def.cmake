# filter_def.cmake
# Strips MSVC-internal absolute COFF symbols made up entirely of '=' characters
# (e.g. the bogus `=` / `==` entries `__create_def` emits from switch-table markers)
# out of the generated .def. These are not addressable symbols; left in the EXPORTS
# section the linker parses them as alias entries and fails with
# `LNK2001: unresolved external symbol =`.
# Usage: cmake -D DEF_FILE=<path/to/exports.def> -P filter_def.cmake
if(NOT EXISTS "${DEF_FILE}")
    return()
endif()

file(STRINGS "${DEF_FILE}" lines)
set(filtered "")
foreach(line IN LISTS lines)
    string(STRIP "${line}" stripped)
    # Drop any export line whose symbol token is only '=' characters.
    if(NOT stripped MATCHES "^=+$")
        list(APPEND filtered "${line}")
    endif()
endforeach()
string(JOIN "\n" content ${filtered})
file(WRITE "${DEF_FILE}" "${content}\n")
