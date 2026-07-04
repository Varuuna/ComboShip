// combo/gui/ComboTrackerSwap.cpp — see ComboTrackerSwap.h for rationale.
#include "ComboTrackerSwap.h"
#include "ComboTrackerCommon.h"
#include "ComboForeground.h"
#include <libultraship/libultraship.h>
#include <ship/resource/CrossRMRegistry.h>
#include <imgui_internal.h> // FindWindowByName / ImGuiWindow rects
#ifdef _WIN32
#include <windows.h> // GetModuleHandleA/GetProcAddress (lazy MM save load for the peek)
#endif

namespace {

constexpr const char* kShownGameCVar = "gCombo.Tracker.ShownGame";

// While a swap is active the real per-game visibility CVars hold forced values; the true values
// are stashed here (value+1, 0 = no stash) so a crash-time CVar save can be undone at next boot.
constexpr const char* kTrueIntentCVar[2] = { "gCombo.Tracker.TrueIntentOot", "gCombo.Tracker.TrueIntentMm" };

bool sActive = false;
int sFg = 0;                // foreground game at activation
int sShown = 0;             // dormant game being displayed
int sSaved[2] = { 0, 0 };   // both games' true tracker CVar values (mirrored in kTrueIntentCVar)
bool sShownVisible = false; // what we last wrote to the shown game's CVar

// Click-hold gesture state.
constexpr float kHoldSeconds = 0.5f;
constexpr float kHoldDragCancelSqr = 5.0f * 5.0f; // px² of movement that turns a hold into a drag
bool sHoldArmed = false;                          // press started on the tracker body
bool sHoldFired = false;                          // this press already triggered the swap
int sHoldPrevShown = -1;                          // ShownGame before the hold (momentary revert)

using ComboTracker::kItemTracker;
using ComboTracker::kTrackers;

void StashTrueIntent(int game, int value) {
    sSaved[game] = value;
    CVarSetInteger(kTrueIntentCVar[game], value + 1);
}

// ShownGame resolved to the game to display. Falls back to follow-foreground while the selected
// game's ResourceManager isn't registered yet (early boot) — its icons couldn't resolve.
int Effective(int fg) {
    int sg = CVarGetInteger(kShownGameCVar, -1);
    if (sg != 0 && sg != 1) {
        return fg;
    }
    if (sg != fg && !Ship::CrossRMRegistry::Get(sg == 0 ? "oot" : "mm")) {
        return fg;
    }
    return sg;
}

void RestoreSaved() {
    ComboTracker::SetTracker(kTrackers[0][kItemTracker], sSaved[0] != 0);
    ComboTracker::SetTracker(kTrackers[1][kItemTracker], sSaved[1] != 0);
    CVarSetInteger(kTrueIntentCVar[0], 0);
    CVarSetInteger(kTrueIntentCVar[1], 0);
    sActive = false;
}

// A peek can precede any MM visit, and until then nothing has loaded the slot's MM save into MM's
// dormant gSaveContext (it holds boot defaults — the tracker would read all-blank). Load it here,
// once per slot. Never after MM has been foreground: its live memory is newer than the disk file.
void EnsureMmSaveLoadedForPeek() {
#ifdef _WIN32
    if (ComboUI::MmEverForeground()) {
        return;
    }
    static int (*sGetFileNum)(void) = nullptr;
    static void (*sLoadMmSave)(int) = nullptr;
    static bool sTried = false;
    if (!sTried) {
        sTried = true;
        if (HMODULE soh = GetModuleHandleA("soh.dll")) {
            sGetFileNum = (int (*)(void))GetProcAddress(soh, "SOH_GetActiveFileNum");
        }
        if (HMODULE mm = GetModuleHandleA("2ship.dll")) {
            sLoadMmSave = (void (*)(int))GetProcAddress(mm, "MM_LoadSaveForCombo");
        }
    }
    static int sLoadedSlot = -1;
    if (!sGetFileNum || !sLoadMmSave) {
        return;
    }
    int slot = sGetFileNum();
    if (slot < 0 || slot > 2 || slot == sLoadedSlot) {
        return;
    }
    sLoadMmSave(slot);
    sLoadedSlot = slot;
#endif
}

// AlwaysAutoResize windows size from the PREVIOUS frame's content, so when the displayed game
// changes, the incoming tracker paints its backdrop across the outgoing tracker's stale rect for
// one frame (visible black flash with MM's opaque bg). Hide that frame while still measuring
// content — ImGui's own appearing-window mechanism.
void SkipTrackerSizingFrame() {
    if (!ImGui::GetCurrentContext()) {
        return;
    }
    if (ImGuiWindow* w = ImGui::FindWindowByName("Item Tracker")) {
        w->HiddenFramesCannotSkipItems = 1;
    }
}

// Per-frame reconcile of the displayed tracker with ShownGame + foreground.
void ApplySwap() {
    int fg = ComboUI::GetForegroundGame();
    int eff = Effective(fg);
    bool want = (eff != fg);
    if (sActive && (!want || sShown != eff || sFg != fg)) {
        RestoreSaved();
        SkipTrackerSizingFrame();
    }
    if (want && !sActive) {
        if (eff == 1) {
            EnsureMmSaveLoadedForPeek();
        }
        sFg = fg;
        sShown = eff;
        StashTrueIntent(0, CVarGetInteger(kTrackers[0][kItemTracker].cvar, 0));
        StashTrueIntent(1, CVarGetInteger(kTrackers[1][kItemTracker].cvar, 0));
        SetTracker(kTrackers[fg][kItemTracker], false);
        // Master on/off = the foreground game's intent at activation.
        sShownVisible = sSaved[fg] != 0;
        SetTracker(kTrackers[eff][kItemTracker], sShownVisible);
        SkipTrackerSizingFrame();
        sActive = true;
    } else if (sActive) {
        // The foreground checkbox reads 0 while swapped (its window must stay hidden — Show/Hide
        // always writes the CVar); re-checking it turns the displayed tracker ON. Unchecking the
        // displayed game's own box (its per-game menu tab) turns it OFF.
        if (CVarGetInteger(kTrackers[sFg][kItemTracker].cvar, 0) != 0) {
            StashTrueIntent(sFg, 1);
            SetTracker(kTrackers[sFg][kItemTracker], false);
            sShownVisible = true;
            SetTracker(kTrackers[sShown][kItemTracker], true);
            SkipTrackerSizingFrame();
        } else if (sShownVisible && CVarGetInteger(kTrackers[sShown][kItemTracker].cvar, 0) == 0) {
            StashTrueIntent(sFg, 0);
            sShownVisible = false;
        }
    }
}

// Logic-only window: no Begin/End, just the per-frame reconcile + gesture. Registered under a name
// that sorts before "Item Tracker" so a swap applies before either tracker draws the same frame.
class SwapWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void Draw() override;

  protected:
    void InitElement() override {
    }
    void UpdateElement() override {
    }
    void DrawElement() override {
    }
};

std::shared_ptr<SwapWindow> sSwapWindow;

void SwapWindow::Draw() {
    auto ctx = Ship::Context::GetInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return;
    }
    // comboui's per-module GImGui must point at the shared context (same pattern as ComboMenu).
    ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());

    ApplySwap();

    // Click-and-hold the tracker body for 1 second to switch game. Detection is passive (works
    // through NoInputs click-through); the press still reaches the game. A drag (mouse moved)
    // cancels the hold so draggable/docked trackers keep working.
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        sHoldArmed = false;
        sHoldFired = false;
        if (!CVarGetInteger("gOpenWindows.Menu", 0)) { // fullscreen combo menu covers everything
            // Both games' main tracker is the same ImGui window ("Item Tracker" — MM's ##MM suffix
            // is only on the Gui-map key), so one rect test covers both. InnerRect = body only.
            // Known gap: separate-section / split-group layouts have no main window — the menu's
            // "Tracker shows" combo is the fallback there.
            ImGuiWindow* w = ImGui::FindWindowByName("Item Tracker");
            ImGuiWindow* hovered = ImGui::GetCurrentContext()->HoveredWindow;
            sHoldArmed = w && w->WasActive && !w->Hidden && w->InnerRect.Contains(io.MousePos) &&
                         (!hovered || hovered->RootWindow == w->RootWindow);
        }
    }
    if (sHoldArmed && !sHoldFired && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] > kHoldDragCancelSqr) {
            sHoldArmed = false; // it's a drag, not a hold
        } else if (io.MouseDownDuration[ImGuiMouseButton_Left] >= kHoldSeconds) {
            sHoldFired = true;
            sHoldPrevShown = CVarGetInteger(kShownGameCVar, -1);
            int fg = ComboUI::GetForegroundGame();
            // Toggling back to the foreground's own tracker restores follow mode.
            CVarSetInteger(kShownGameCVar, Effective(fg) == fg ? (fg ^ 1) : -1);
            ApplySwap();
            // A draggable tracker's click also started an ImGui window-move; end it so the rest of
            // the hold doesn't keep the window in drag state (docking-drag visuals, cursor-glue).
            ImGuiContext* g = ImGui::GetCurrentContext();
            ImGuiWindow* tracker = ImGui::FindWindowByName("Item Tracker");
            if (g->MovingWindow && tracker && g->MovingWindow->RootWindow == tracker->RootWindow) {
                g->MovingWindow = nullptr;
                ImGui::ClearActiveID();
            }
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        // Momentary mode: the hold is a peek; revert to the pre-hold selection on release.
        if (sHoldFired && CVarGetInteger("gCombo.Tracker.HoldMomentary", 0)) {
            CVarSetInteger(kShownGameCVar, sHoldPrevShown);
            ApplySwap();
        }
        sHoldArmed = false;
        sHoldFired = false;
    }
}

} // namespace

void ComboTracker::SuspendSwap() {
    if (sActive) {
        RestoreSaved();
    }
}

bool ComboTracker::GetMasterVisible() {
    if (sActive) {
        return sSaved[sFg] != 0;
    }
    return CVarGetInteger(kTrackers[ComboUI::GetForegroundGame()][kItemTracker].cvar, 0) != 0;
}

void ComboTracker::SetMasterVisible(bool visible) {
    if (sActive) {
        StashTrueIntent(sFg, visible ? 1 : 0);
        sShownVisible = visible;
        SetTracker(kTrackers[sShown][kItemTracker], visible);
        return;
    }
    SetTracker(kTrackers[ComboUI::GetForegroundGame()][kItemTracker], visible);
}

void ComboTracker::RegisterSwap() {
    auto ctx = Ship::Context::GetInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return;
    }
    // Crash recovery: if the app died mid-swap, the forced CVars may have been persisted; put the
    // stashed true values back before the first reconcile.
    for (int g = 0; g < 2; ++g) {
        int v = CVarGetInteger(kTrueIntentCVar[g], 0);
        if (v) {
            CVarSetInteger(kTrackers[g][kItemTracker].cvar, v - 1);
            CVarSetInteger(kTrueIntentCVar[g], 0);
        }
    }
    if (!sSwapWindow) {
        // Empty CVar name: visibility is never consulted (Draw is overridden), so don't persist one.
        sSwapWindow = std::make_shared<SwapWindow>("", true, "Combo Tracker Swap");
    }
    ctx->GetWindow()->GetGui()->AddGuiWindow(sSwapWindow);
}
