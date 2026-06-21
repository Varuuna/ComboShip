// combo/gui/ComboTrackerVisibility.cpp
//
// ComboShip-owned: makes the Check/Item tracker popouts "follow" the active game. Only the
// foreground game's tracker windows are shown; the other game's are hidden. This is both the
// feature the user asked for (a tracker that switches OOT<->MM) AND a correctness requirement:
// the single shared libultraship Gui draws EVERY registered window each frame, and a background
// game's DrawElement would run against that DLL's stale per-module ImGui context.
//
// Lives in comboui.dll because it links libultraship (CVar API + the shared Gui) and stays mapped
// for the whole process. The launcher (combo/ComboShip.cpp) calls the exports at each game
// transition; comboui never draws these itself, so there is no ImGui-context concern here.
//
// OOT (soh) trackers use the "gOpenWindows.*" CVars and plain window names; MM (2ship) trackers
// use "gWindows.*" and the "##MM"-suffixed names from BenGui.cpp (see Part A — the suffix avoids
// the duplicate-name rejection in Gui::AddGuiWindow while keeping the visible title identical).

#include <libultraship/libultraship.h> // Ship::Context / Gui + CVarGet/SetInteger

namespace {

struct TrackerWin {
    const char* cvar; // visibility CVar (source of truth; MM's CheckTracker reads it directly)
    const char* name; // registered GuiWindow name (map key in the shared Gui)
};

// [game][window]: 0 = OOT, 1 = MM.
const TrackerWin kTrackers[2][4] = {
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

// Remembered user intent per window. -1 = not snapshotted yet (so the foreground pass leaves the
// loaded config value untouched on the very first call).
int sIntent[2][4] = { { -1, -1, -1, -1 }, { -1, -1, -1, -1 } };

void SetTracker(const TrackerWin& w, bool visible) {
    // Write the CVar (covers MM's CheckTracker, whose Draw() reads the CVar directly)...
    CVarSetInteger(w.cvar, visible ? 1 : 0);
    // ...and mirror it onto the window object so mIsVisible-gated Draws react this same frame.
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

} // namespace

// Called by the launcher whenever a game becomes the foreground game (0 = OOT, 1 = MM).
extern "C" __declspec(dllexport) void ComboUI_OnForegroundGame(int game) {
    if (game != 0 && game != 1) {
        return;
    }
    const int fg = game;
    const int bg = game ^ 1;

    // Background game: remember the user's current intent, then hide its trackers.
    for (int i = 0; i < 4; ++i) {
        sIntent[bg][i] = CVarGetInteger(kTrackers[bg][i].cvar, 0);
        SetTracker(kTrackers[bg][i], false);
    }
    // Foreground game: restore remembered intent. Skip until we've snapshotted at least once, so
    // the game that owns the foreground at startup keeps whatever its loaded config asked for.
    for (int i = 0; i < 4; ++i) {
        if (sIntent[fg][i] < 0) {
            continue;
        }
        SetTracker(kTrackers[fg][i], sIntent[fg][i] != 0);
    }
}

// Called by the launcher just before shutdown teardown. Restores both games' tracker CVars to the
// remembered intent so the game that happened to be backgrounded at exit does not persist its
// tracker as "off" (Hide() zeroes the CVar, and ~Context saves the live CVar values on exit).
extern "C" __declspec(dllexport) void ComboUI_RestoreTrackerIntent(void) {
    for (int g = 0; g < 2; ++g) {
        for (int i = 0; i < 4; ++i) {
            if (sIntent[g][i] >= 0) {
                CVarSetInteger(kTrackers[g][i].cvar, sIntent[g][i]);
            }
        }
    }
}
