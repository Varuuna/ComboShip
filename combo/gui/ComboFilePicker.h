// combo/gui/ComboFilePicker.h
// Shared native open-file dialog for comboui's setup screens (ROM extraction, settings import).
// Windows: comdlg32 GetOpenFileNameA. Elsewhere: pfd (zenity/kdialog on Linux, osascript on
// macOS) — the vendored portable-file-dialogs.h soh's own extractor uses; include dir set in
// combo/CMakeLists.txt.
#pragma once

#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32") // GetOpenFileNameA — comboui doesn't otherwise link comdlg32
#else
#include "portable-file-dialogs.h"
#include <cstdlib>
#endif

namespace ComboFilePicker {

// False only on a Linux system with none of pfd's dialog helpers (zenity/kdialog/...) installed;
// callers disable Browse and point at the alternative (drag & drop / Skip) instead of leaving a
// button that silently no-ops.
inline bool Available() {
#ifdef _WIN32
    return true;
#else
    static const bool sAvailable = pfd::settings::available();
    return sAvailable;
#endif
}

// Native open-file dialog. winFilter is GetOpenFileNameA's double-NUL filter string; pfdFilters
// is pfd's {label, pattern, ...} list. Returns "" if cancelled or no dialog helper is available.
inline std::string PickFile(const char* title, const char* winFilter, const std::vector<std::string>& pfdFilters) {
#ifdef _WIN32
    (void)pfdFilters;
    char file[MAX_PATH] = { 0 };
    OPENFILENAMEA ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = winFilter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) {
        return std::string(file);
    }
    return std::string();
#else
    (void)winFilter;
    // An AppImage run exports a bundle-scoped LD_LIBRARY_PATH that can break the host's
    // zenity/kdialog (GTK/Qt); AppRun saves the host's original value in
    // COMBO_HOST_LD_LIBRARY_PATH, so swap it in around the dialog spawn (pfd forks the helper
    // in the ctor) and restore before blocking on the result. Unset outside an AppImage.
    const char* hostLibPath = std::getenv("COMBO_HOST_LD_LIBRARY_PATH");
    std::string savedLibPath;
    bool swapped = false;
    if (hostLibPath != nullptr) {
        const char* current = std::getenv("LD_LIBRARY_PATH");
        savedLibPath = current != nullptr ? current : "";
        if (hostLibPath[0] != '\0') {
            setenv("LD_LIBRARY_PATH", hostLibPath, 1);
        } else {
            unsetenv("LD_LIBRARY_PATH");
        }
        swapped = true;
    }
    pfd::open_file dlg(title, "", pfdFilters);
    if (swapped) {
        setenv("LD_LIBRARY_PATH", savedLibPath.c_str(), 1);
    }
    auto selection = dlg.result();
    return selection.empty() ? std::string() : selection.front();
#endif
}

} // namespace ComboFilePicker
