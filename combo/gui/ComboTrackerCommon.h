// combo/gui/ComboTrackerCommon.h
//
// Tracker-window plumbing shared by ComboTrackerVisibility (foreground follow) and
// ComboTrackerSwap (game swap): the CVar + Gui-map levers for the per-game tracker windows.

#pragma once

#include <libultraship/libultraship.h>

namespace ComboTracker {

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

// Write the CVar (covers MM's CheckTracker, whose Draw() reads it directly) and mirror it onto
// the window object so mIsVisible-gated Draws react this same frame.
inline void SetTracker(const Win& w, bool visible) {
    CVarSetInteger(w.cvar, visible ? 1 : 0);
    auto ctx = Ship::Context::GetInstance();
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
