// combo/gui/ComboTrackerCommon.h
//
// Tracker-window plumbing shared across comboui: the CVar + Gui-map levers for the per-game tracker
// windows, plus the active-slot resolve every panel needs.

#pragma once

#include <libultraship/libultraship.h>
#ifdef _WIN32
#include <windows.h> // GetModuleHandleA/GetProcAddress (SOH_GetActiveFileNum)
#endif

namespace ComboTracker {

// Active OOT save slot (0-2), or -1 at the title/file-select screens. The slot is combo-wide, so
// every comboui caller shares this one latched resolve (inline statics are per-DLL, not per-TU).
inline int OotActiveSlot() {
#ifdef _WIN32
    static int (*sGetFileNum)(void) = nullptr;
    static bool sTried = false;
    if (!sTried) {
        sTried = true;
        if (HMODULE soh = GetModuleHandleA("soh.dll")) {
            sGetFileNum = (int (*)(void))GetProcAddress(soh, "SOH_GetActiveFileNum");
        }
    }
    if (sGetFileNum) {
        int slot = sGetFileNum();
        return (slot >= 0 && slot <= 2) ? slot : -1;
    }
#endif
    return -1;
}

struct Win {
    const char* cvar; // visibility CVar (source of truth; MM's CheckTracker reads it directly)
    const char* name; // registered GuiWindow name (map key in the shared Gui)
};

// [game][window]: 0 = OOT, 1 = MM. The swap touches the kItemTracker and kCheckTracker columns
// (main tracker windows); the settings windows are foreground-followed only.
inline constexpr Win kTrackers[2][4] = {
    {
        { "gOpenWindows.ItemTracker", "Item Tracker" },
        { "gOpenWindows.ItemTrackerSettings", "Item Tracker Settings" },
        { "gOpenWindows.CheckTracker", "Check Tracker" },
        { "gOpenWindows.CheckTrackerSettings", "Check Tracker Settings" },
    },
    {
        { "gWindows.ItemTracker", "Item Tracker##MM" },
        { "gWindows.ItemTrackerSettings", "Item Tracker Settings##MM" },
        { "gWindows.CheckTracker", "Check Tracker##MM" },
        { "gWindows.CheckTrackerSettings", "Check Tracker Settings##MM" },
    },
};

inline constexpr int kItemTracker = 0;
inline constexpr int kCheckTracker = 2;

// One tracker kind under the both-games model: a master enable shows BOTH games' windows (each
// with its own ImGui identity/rect); HideBackground restricts drawing to the foreground game.
// The ImGui::Begin identities equal the Gui-map names in kTrackers[game][column].
struct Kind {
    int column;               // kTrackers column of the main tracker window
    const char* enabledCvar;  // master on/off, spans both games
    const char* hideBgCvar;   // 1 = only the foreground game's window draws (peek shows the other)
    const char* mmPlacedCvar; // latch: MM's window was placed next to OOT's on its first show
};

inline constexpr Kind kKinds[2] = {
    { kItemTracker, "gCombo.Tracker.Enabled", "gCombo.Tracker.HideBackground", "gCombo.Tracker.MmPlaced" },
    { kCheckTracker, "gCombo.CheckTracker.Enabled", "gCombo.CheckTracker.HideBackground",
      "gCombo.CheckTracker.MmPlaced" },
};

// ImGui::Begin identity of a kind's main tracker window for a game (== its Gui-map name).
inline const char* KindWindowName(const Kind& kind, int game) {
    return kTrackers[game][kind.column].name;
}

// Write the CVar (covers MM's CheckTracker, whose Draw() reads it directly) and mirror it onto
// the window object so mIsVisible-gated Draws react this same frame.
inline void SetTracker(const Win& w, bool visible) {
    CVarSetInteger(w.cvar, visible ? 1 : 0);
    auto ctx = Ship::Context::GetRawInstance();
    if (ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
        if (auto win = ctx->GetWindow()->GetGui()->GetGuiWindow(w.name)) {
            if (visible) {
                win->Show();
            } else {
                win->Hide();
            }
        }
    }
}

} // namespace ComboTracker
