// combo/gui/ComboTrackerVisibility.cpp
//
// ComboShip-owned: makes per-game GUI windows "follow" the active game. Only the foreground game's
// windows participate in the shared Gui's draw loop; the other game's are suppressed. This covers
// the Check/Item tracker popouts AND the toast/notification window. It is both the feature the user
// asked for (UI that switches OOT<->MM) AND a correctness requirement: the single shared libultraship
// Gui draws EVERY registered window each frame, and a background game's Draw/UpdateElement would run
// against that DLL's stale per-module ImGui context.
//
// Trackers and notifications use DIFFERENT levers:
//   - Trackers honor GuiWindow visibility, so toggling Show()/Hide() (+ the CVar) gates their Draw().
//   - Notification::Window OVERRIDES Draw() and ignores IsVisible(), and GuiElement::Update() runs
//     UpdateElement() unconditionally. So Hide() does NOT stop a notification window. The only reliable
//     lever is map presence: a RemoveGuiWindow'd window is skipped by the draw loop entirely (both
//     Update and Draw). We re-add the SAME already-initialized object on the way back (never a fresh
//     one), which avoids the 0xCD re-registration crash the resident-window model guards against
//     (see soh/soh/OTRGlobals.cpp:1753 / :2741). We hold it as a weak_ptr so the window stays owned
//     solely by its own DLL (its mNotificationWindow member) — no cross-DLL shared_ptr, no shutdown
//     dtor-order hazard.
//
// Lives in comboui.dll because it links libultraship (CVar API + the shared Gui) and stays mapped
// for the whole process. The launcher (combo/ComboShip.cpp) calls the exports at each game
// transition; comboui never draws these itself, so there is no ImGui-context concern here.
//
// OOT (soh) trackers use the "gOpenWindows.*" CVars and plain window names; MM (2ship) trackers
// use "gWindows.*" and the "##MM"-suffixed names from BenGui.cpp (see Part A — the suffix avoids
// the duplicate-name rejection in Gui::AddGuiWindow while keeping the visible title identical).

#include <libultraship/libultraship.h> // Ship::Context / Gui + CVarGet/SetInteger
#include <memory>                      // std::weak_ptr (notification-window handle)

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

// Notification windows: OOT registers the plain name; MM appends "##MM" (BenGui.cpp). Indexed by
// game: 0 = OOT, 1 = MM. These are gated by Gui-map presence, NOT visibility — see the file header.
const char* const kNotificationWin[2] = {
    "Notifications Window",     // OOT (soh)
    "Notifications Window##MM", // MM (2ship)
};

// weak_ptr handles so the windows stay owned solely by their own game DLL (their mNotificationWindow
// member keeps them alive); we only need a handle to re-add the backgrounded one later.
std::weak_ptr<Ship::GuiWindow> sNotif[2];

// Keep only the foreground game's notification window in the shared Gui's draw loop.
void SetForegroundNotification(int fg) {
    auto ctx = Ship::Context::GetInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return;
    }
    auto gui = ctx->GetWindow()->GetGui();
    const int bg = fg ^ 1;

    // Background game: capture a handle (so we can re-add it on return) and drop it from the draw loop.
    if (auto win = gui->GetGuiWindow(kNotificationWin[bg])) {
        sNotif[bg] = win;
        gui->RemoveGuiWindow(kNotificationWin[bg]);
    }
    // Foreground game: re-add the SAME (already-initialized) object if it was previously removed and is
    // not currently present. Init() on re-add is a guarded no-op; no fresh window is ever created.
    if (!gui->GetGuiWindow(kNotificationWin[fg])) {
        if (auto win = sNotif[fg].lock()) {
            gui->AddGuiWindow(win);
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

    // Notification window follows the active game too (so MM's toasts show only while MM is foreground
    // and OOT's only while OOT is). Independent of the tracker intent above.
    SetForegroundNotification(fg);
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
