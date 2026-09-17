// combo/gui/ComboTrackerVisibility.cpp
// ComboShip-owned: makes the per-game SETTINGS popouts and the toast/notification window follow the
// active game across transitions. The MAIN tracker windows are derived every frame from the
// both-games master model (ComboTrackerSwap); this module only snapshots/restores the settings-window
// intents. Notifications need a different lever than trackers (map presence via RemoveGuiWindow, not
// Hide()) — rationale + the 0xCD re-registration hazard: see docs/deviations/tracker.md.
// Lives in comboui.dll (links libultraship, stays mapped); the launcher calls the exports per transition.
// OOT trackers use gOpenWindows.* + plain names; MM uses gWindows.* + "##MM"-suffixed names.

#include <libultraship/libultraship.h> // Ship::Context / Gui + CVarGet/SetInteger
#include "ComboExport.h"
#include <memory>               // std::weak_ptr (notification-window handle)
#include "ComboForeground.h"    // ComboUI::GetForegroundGame (cached below)
#include "ComboAudioBridge.h"   // ComboAudio::SyncAllToMM (on MM entry)
#include "ComboTrackerCommon.h" // kTrackers/SetTracker (shared with ComboTrackerSwap)
#include "ComboTrackerBridge.h" // ComboTracker::SyncAppearance (re-assert on transitions)
#include "ComboNotesWindow.h"   // ComboNotes::FlushPending (persist notes across transitions)
#include "ComboResolve.h"       // Combo_ResolveSym for the MM_ReloadControls entry point

namespace {

// Cached foreground game (0 = OOT, 1 = MM), updated by ComboUI_OnForegroundGame. Defaults to OOT,
// the foreground game at startup after the eager MM boot. Read via ComboUI::GetForegroundGame.
int sForegroundGame = 0;
bool sMmEverForeground = false;

using ComboTracker::kTrackers;
using ComboTracker::SetTracker;

// The settings popouts still follow the foreground game (the main trackers are reconciled by
// ComboTrackerSwap instead). kTrackers columns of the two settings windows.
constexpr int kSettingsColumns[2] = { 1, 3 };

// Remembered user intent per settings window. -1 = not snapshotted yet (so the foreground pass
// leaves the loaded config value untouched on the very first call).
int sIntent[2][2] = { { -1, -1 }, { -1, -1 } };

// Notification windows: OOT registers the plain name; MM appends "##MM" (BenGui.cpp). Indexed by
// game: 0 = OOT, 1 = MM. These are gated by Gui-map presence, NOT visibility — see the file header.
const char* const kNotificationWin[2] = {
    "Notifications Window",     // OOT (soh)
    "Notifications Window##MM", // MM (2ship)
};

// weak_ptr handles so the windows stay owned solely by their own game DLL (their mNotificationWindow
// member keeps them alive); we only need a handle to re-add the backgrounded one later.
std::weak_ptr<Ship::GuiWindow> sNotif[2];

// Reload MM's controller bindings from the shared gSettings.Controllers.* CVars. Resolved once from
// 2ship.dll. Called on MM entry so rebinds made via the Shared (OOT) controls UI while MM was dormant
// take effect when MM resumes (MM's ControlDeck caches mappings; it only re-reads on Init / this call).
void ReloadMmControls() {
    static void (*sFn)() = nullptr;
    static bool sTried = false;
    if (!sTried) {
        sTried = true;
        sFn = (void (*)())Combo_ResolveSym("2ship", "MM_ReloadControls");
    }
    if (sFn) {
        sFn();
    }
}

// Keep only the foreground game's notification window in the shared Gui's draw loop.
void SetForegroundNotification(int fg) {
    auto ctx = Ship::Context::GetRawInstance();
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

// Foreground-game query consumed by the audio bridge, controls reload, and dev-tool gating.
int ComboUI::GetForegroundGame() {
    return sForegroundGame;
}

bool ComboUI::MmEverForeground() {
    return sMmEverForeground;
}

// C-ABI variant so the game DLLs can query the foreground game (0 = OOT, 1 = MM) across the DLL
// boundary — MM uses it to gate its live-world dev viewers (Actor/Collision/Message) from drawing
// against dormant/swapped play state when its tab is opened while OOT is foreground.
extern "C" COMBO_EXPORT int ComboUI_GetForegroundGame(void) {
    return sForegroundGame;
}

// Called by the launcher whenever a game becomes the foreground game (0 = OOT, 1 = MM).
extern "C" COMBO_EXPORT void ComboUI_OnForegroundGame(int game) {
    if (game != 0 && game != 1) {
        return;
    }
    const int fg = game;
    const int bg = game ^ 1;
    sForegroundGame = fg;
    if (fg == 1) {
        sMmEverForeground = true;
    }

    // Background game: remember the user's settings-window intent, then hide its popouts. The main
    // tracker windows are NOT touched — ComboTrackerSwap derives them every frame.
    for (int i = 0; i < 2; ++i) {
        sIntent[bg][i] = CVarGetInteger(kTrackers[bg][kSettingsColumns[i]].cvar, 0);
        SetTracker(kTrackers[bg][kSettingsColumns[i]], false);
    }
    // Foreground game: restore remembered intent. Skip until we've snapshotted at least once, so
    // the game that owns the foreground at startup keeps whatever its loaded config asked for.
    for (int i = 0; i < 2; ++i) {
        if (sIntent[fg][i] < 0) {
            continue;
        }
        SetTracker(kTrackers[fg][kSettingsColumns[i]], sIntent[fg][i] != 0);
    }

    // Notification window follows the active game too (so MM's toasts show only while MM is foreground
    // and OOT's only while OOT is). Independent of the tracker intent above.
    SetForegroundNotification(fg);

    // ComboShip: on MM entry (boot/resume), reconcile the settings MM consumes from its own CVars with
    // the canonical Shared values changed while MM was dormant — push audio volumes (apply per-port
    // scales) and reload controller bindings. Graphics needs no equivalent (the shared window applies
    // OOT's graphics callbacks process-wide), and Controls' data already lives in shared CVars.
    if (fg == 1) {
        ComboAudio::SyncAllToMM();
        ReloadMmControls();
    }

    // Re-assert shared tracker appearance (per-game edits made while dormant lose). A sticky swap
    // re-applies itself next frame via the swap window's reconcile.
    ComboTracker::SyncAppearance();

    // ComboShip (#165): persist any pending note text at the transition.
    ComboNotes::FlushPending();
}

// Launcher, just before shutdown: restore the backgrounded game's settings-window CVars so they
// don't persist as "off" (main tracker CVars are derived and recomputed on next boot).
extern "C" COMBO_EXPORT void ComboUI_RestoreTrackerIntent(void) {
    // ComboShip (#165): the launcher calls this pre-shutdown, so a just-typed note still lands.
    ComboNotes::FlushPending();
    // Only the background game's CVars are forced-hidden; the foreground game's live values already
    // reflect the user's latest toggles (a stale snapshot would clobber them).
    const int bg = sForegroundGame ^ 1;
    for (int i = 0; i < 2; ++i) {
        if (sIntent[bg][i] >= 0) {
            CVarSetInteger(kTrackers[bg][kSettingsColumns[i]].cvar, sIntent[bg][i]);
        }
    }
}
