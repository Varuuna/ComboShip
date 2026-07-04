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
#include "ComboForeground.h"           // ComboUI::GetForegroundGame (cached below)
#include "ComboAudioBridge.h"          // ComboAudio::SyncAllToMM (on MM entry)
#include "ComboTrackerCommon.h"        // kTrackers/SetTracker (shared with ComboTrackerSwap)
#include "ComboTrackerBridge.h"        // ComboTracker::SyncAppearance (re-assert on transitions)
#include "ComboTrackerSwap.h"          // SuspendSwap/ApplySwap around the intent snapshot/restore
#ifdef _WIN32
#include <windows.h> // GetModuleHandleA/GetProcAddress for the MM_ReloadControls entry point
#endif

namespace {

// Cached foreground game (0 = OOT, 1 = MM), updated by ComboUI_OnForegroundGame. Defaults to OOT,
// the foreground game at startup after the eager MM boot. Read via ComboUI::GetForegroundGame.
int sForegroundGame = 0;
bool sMmEverForeground = false;

using ComboTracker::kTrackers;
using ComboTracker::SetTracker;

// Remembered user intent per window. -1 = not snapshotted yet (so the foreground pass leaves the
// loaded config value untouched on the very first call).
int sIntent[2][4] = { { -1, -1, -1, -1 }, { -1, -1, -1, -1 } };

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
#ifdef _WIN32
    static void (*sFn)() = nullptr;
    static bool sTried = false;
    if (!sTried) {
        sTried = true;
        if (HMODULE h = GetModuleHandleA("2ship.dll")) {
            sFn = (void (*)())GetProcAddress(h, "MM_ReloadControls");
        }
    }
    if (sFn) {
        sFn();
    }
#endif
}

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
extern "C" __declspec(dllexport) int ComboUI_GetForegroundGame(void) {
    return sForegroundGame;
}

// Called by the launcher whenever a game becomes the foreground game (0 = OOT, 1 = MM).
extern "C" __declspec(dllexport) void ComboUI_OnForegroundGame(int game) {
    if (game != 0 && game != 1) {
        return;
    }
    const int fg = game;
    const int bg = game ^ 1;
    sForegroundGame = fg;
    if (fg == 1) {
        sMmEverForeground = true;
    }

    // Restore true tracker CVars before snapshotting, so a swapped state is never recorded as intent.
    ComboTracker::SuspendSwap();

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
}

// Called by the launcher just before shutdown teardown. Restores both games' tracker CVars to the
// remembered intent so the game that happened to be backgrounded at exit does not persist its
// tracker as "off" (Hide() zeroes the CVar, and ~Context saves the live CVar values on exit).
extern "C" __declspec(dllexport) void ComboUI_RestoreTrackerIntent(void) {
    // A swap forces both item-tracker CVars; put the true values back before they get persisted.
    ComboTracker::SuspendSwap();
    // Only the background game's CVars are forced-hidden; the foreground game's live values already
    // reflect the user's latest toggles (a stale snapshot would clobber them).
    const int bg = sForegroundGame ^ 1;
    for (int i = 0; i < 4; ++i) {
        if (sIntent[bg][i] >= 0) {
            CVarSetInteger(kTrackers[bg][i].cvar, sIntent[bg][i]);
        }
    }
}
