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

using ComboTracker::kTrackers;

// One managed tracker: config (which per-game windows, which CVars) + swap/gesture state. The
// item and check trackers swap independently — each has its own ShownGame selection and hold.
struct Swap {
    // Config.
    int column;                    // kTrackers column (kItemTracker / kCheckTracker)
    const char* imguiWindow;       // both games Begin() the same ImGui window name
    const char* shownGameCVar;     // -1 = follow foreground, 0 = OOT, 1 = MM
    const char* trueIntentCVar[2]; // crash-recovery stash (value+1, 0 = no stash), per game
    const char* holdMomentaryCVar; // hold is a peek: revert ShownGame on release

    // While a swap is active the real per-game visibility CVars hold forced values; the true
    // values live here (mirrored in trueIntentCVar so a crash-time CVar save can be undone).
    bool active = false;
    int fg = 0;                // foreground game at activation
    int shown = 0;             // dormant game being displayed
    int saved[2] = { 0, 0 };   // both games' true tracker CVar values
    bool shownVisible = false; // what we last wrote to the shown game's CVar

    // Click-hold gesture state.
    bool holdArmed = false; // press started on this tracker's body
    bool holdFired = false; // this press already triggered the swap
    int holdPrevShown = -1; // ShownGame before the hold (momentary revert)
};

Swap sSwaps[ComboTracker::kSwapCount] = {
    { ComboTracker::kItemTracker,
      "Item Tracker",
      "gCombo.Tracker.ShownGame",
      { "gCombo.Tracker.TrueIntentOot", "gCombo.Tracker.TrueIntentMm" },
      "gCombo.Tracker.HoldMomentary" },
    { ComboTracker::kCheckTracker,
      "Check Tracker",
      "gCombo.CheckTracker.ShownGame",
      { "gCombo.CheckTracker.TrueIntentOot", "gCombo.CheckTracker.TrueIntentMm" },
      "gCombo.CheckTracker.HoldMomentary" },
};

constexpr float kHoldSeconds = 0.5f;
constexpr float kHoldDragCancelSqr = 5.0f * 5.0f; // px² of movement that turns a hold into a drag

void StashTrueIntent(Swap& sw, int game, int value) {
    sw.saved[game] = value;
    CVarSetInteger(sw.trueIntentCVar[game], value + 1);
}

// ShownGame resolved to the game to display. Falls back to follow-foreground while the selected
// game's ResourceManager isn't registered yet (early boot) — its icons couldn't resolve.
int Effective(const Swap& sw, int fg) {
    int sg = CVarGetInteger(sw.shownGameCVar, -1);
    if (sg != 0 && sg != 1) {
        return fg;
    }
    if (sg != fg && !Ship::CrossRMRegistry::Get(sg == 0 ? "oot" : "mm")) {
        return fg;
    }
    return sg;
}

void RestoreSaved(Swap& sw) {
    ComboTracker::SetTracker(kTrackers[0][sw.column], sw.saved[0] != 0);
    ComboTracker::SetTracker(kTrackers[1][sw.column], sw.saved[1] != 0);
    CVarSetInteger(sw.trueIntentCVar[0], 0);
    CVarSetInteger(sw.trueIntentCVar[1], 0);
    sw.active = false;
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
void SkipTrackerSizingFrame(const Swap& sw) {
    if (!ImGui::GetCurrentContext()) {
        return;
    }
    if (ImGuiWindow* w = ImGui::FindWindowByName(sw.imguiWindow)) {
        w->HiddenFramesCannotSkipItems = 1;
    }
}

// Per-frame reconcile of the displayed tracker with ShownGame + foreground.
void ApplySwap(Swap& sw) {
    int fg = ComboUI::GetForegroundGame();
    int eff = Effective(sw, fg);
    bool want = (eff != fg);
    if (sw.active && (!want || sw.shown != eff || sw.fg != fg)) {
        RestoreSaved(sw);
        SkipTrackerSizingFrame(sw);
    }
    if (want && !sw.active) {
        if (eff == 1) {
            EnsureMmSaveLoadedForPeek();
        }
        sw.fg = fg;
        sw.shown = eff;
        StashTrueIntent(sw, 0, CVarGetInteger(kTrackers[0][sw.column].cvar, 0));
        StashTrueIntent(sw, 1, CVarGetInteger(kTrackers[1][sw.column].cvar, 0));
        SetTracker(kTrackers[fg][sw.column], false);
        // Master on/off = the foreground game's intent at activation.
        sw.shownVisible = sw.saved[fg] != 0;
        SetTracker(kTrackers[eff][sw.column], sw.shownVisible);
        SkipTrackerSizingFrame(sw);
        sw.active = true;
    } else if (sw.active) {
        // The foreground checkbox reads 0 while swapped (its window must stay hidden — Show/Hide
        // always writes the CVar); re-checking it turns the displayed tracker ON. Unchecking the
        // displayed game's own box (its per-game menu tab) turns it OFF.
        if (CVarGetInteger(kTrackers[sw.fg][sw.column].cvar, 0) != 0) {
            StashTrueIntent(sw, sw.fg, 1);
            SetTracker(kTrackers[sw.fg][sw.column], false);
            sw.shownVisible = true;
            SetTracker(kTrackers[sw.shown][sw.column], true);
            SkipTrackerSizingFrame(sw);
        } else if (sw.shownVisible && CVarGetInteger(kTrackers[sw.shown][sw.column].cvar, 0) == 0) {
            StashTrueIntent(sw, sw.fg, 0);
            sw.shownVisible = false;
        }
    }
}

// Back-to-front position in the ImGui window list — higher = drawn on top. Used to arm at most
// one tracker when overlapping bodies both contain the press.
int DisplayOrder(ImGuiWindow* w) {
    ImGuiContext* g = ImGui::GetCurrentContext();
    for (int i = 0; i < g->Windows.Size; ++i) {
        if (g->Windows[i] == w) {
            return i;
        }
    }
    return -1;
}

// Logic-only window: no Begin/End, just the per-frame reconcile + gesture. Registered under a
// name that sorts before "Check Tracker" and "Item Tracker" (leading space) so a swap applies
// before either tracker draws the same frame.
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

    for (Swap& sw : sSwaps) {
        ApplySwap(sw);
    }

    // Click-and-hold a tracker body to switch its game. Detection is passive (works through
    // NoInputs click-through); the press still reaches the game. A drag (mouse moved) cancels
    // the hold so draggable/docked trackers keep working.
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // At most one tracker arms per press; overlapping bodies resolve to the topmost.
        Swap* best = nullptr;
        int bestOrder = -1;
        for (Swap& sw : sSwaps) {
            sw.holdArmed = false;
            sw.holdFired = false;
            if (CVarGetInteger("gOpenWindows.Menu", 0)) { // fullscreen combo menu covers everything
                continue;
            }
            // Both games' tracker is the same ImGui window (MM's ##MM suffix is only on the
            // Gui-map key), so one rect test covers both. InnerRect = body only.
            // Known gap: the item tracker's separate-section / split-group layouts have no main
            // window — the menu's "Tracker shows" combo is the fallback there.
            ImGuiWindow* w = ImGui::FindWindowByName(sw.imguiWindow);
            ImGuiWindow* hovered = ImGui::GetCurrentContext()->HoveredWindow;
            if (w && w->WasActive && !w->Hidden && w->InnerRect.Contains(io.MousePos) &&
                (!hovered || hovered->RootWindow == w->RootWindow)) {
                int order = DisplayOrder(w);
                if (order > bestOrder) {
                    best = &sw;
                    bestOrder = order;
                }
            }
        }
        if (best) {
            best->holdArmed = true;
        }
    }
    for (Swap& sw : sSwaps) {
        if (sw.holdArmed && !sw.holdFired && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] > kHoldDragCancelSqr) {
                sw.holdArmed = false; // it's a drag, not a hold
            } else if (io.MouseDownDuration[ImGuiMouseButton_Left] >= kHoldSeconds) {
                sw.holdFired = true;
                sw.holdPrevShown = CVarGetInteger(sw.shownGameCVar, -1);
                int fg = ComboUI::GetForegroundGame();
                // Toggling back to the foreground's own tracker restores follow mode.
                CVarSetInteger(sw.shownGameCVar, Effective(sw, fg) == fg ? (fg ^ 1) : -1);
                ApplySwap(sw);
                // A draggable tracker's click also started an ImGui window-move; end it so the
                // rest of the hold doesn't keep the window in drag state (docking-drag visuals,
                // cursor-glue).
                ImGuiContext* g = ImGui::GetCurrentContext();
                ImGuiWindow* tracker = ImGui::FindWindowByName(sw.imguiWindow);
                if (g->MovingWindow && tracker && g->MovingWindow->RootWindow == tracker->RootWindow) {
                    g->MovingWindow = nullptr;
                    ImGui::ClearActiveID();
                }
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            // Momentary mode: the hold is a peek; revert to the pre-hold selection on release.
            if (sw.holdFired && CVarGetInteger(sw.holdMomentaryCVar, 0)) {
                CVarSetInteger(sw.shownGameCVar, sw.holdPrevShown);
                ApplySwap(sw);
            }
            sw.holdArmed = false;
            sw.holdFired = false;
        }
    }
}

} // namespace

void ComboTracker::SuspendSwap() {
    for (Swap& sw : sSwaps) {
        if (sw.active) {
            RestoreSaved(sw);
        }
    }
}

bool ComboTracker::GetMasterVisible(int tracker) {
    Swap& sw = sSwaps[tracker];
    if (sw.active) {
        return sw.saved[sw.fg] != 0;
    }
    return CVarGetInteger(kTrackers[ComboUI::GetForegroundGame()][sw.column].cvar, 0) != 0;
}

void ComboTracker::SetMasterVisible(int tracker, bool visible) {
    Swap& sw = sSwaps[tracker];
    if (sw.active) {
        StashTrueIntent(sw, sw.fg, visible ? 1 : 0);
        sw.shownVisible = visible;
        SetTracker(kTrackers[sw.shown][sw.column], visible);
        return;
    }
    SetTracker(kTrackers[ComboUI::GetForegroundGame()][sw.column], visible);
}

void ComboTracker::RegisterSwap() {
    auto ctx = Ship::Context::GetInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return;
    }
    // Crash recovery: if the app died mid-swap, the forced CVars may have been persisted; put the
    // stashed true values back before the first reconcile.
    for (Swap& sw : sSwaps) {
        for (int g = 0; g < 2; ++g) {
            int v = CVarGetInteger(sw.trueIntentCVar[g], 0);
            if (v) {
                CVarSetInteger(kTrackers[g][sw.column].cvar, v - 1);
                CVarSetInteger(sw.trueIntentCVar[g], 0);
            }
        }
    }
    if (!sSwapWindow) {
        // Empty CVar name: visibility is never consulted (Draw is overridden), so don't persist
        // one. The leading space keeps it sorted before both tracker windows in the Gui map.
        sSwapWindow = std::make_shared<SwapWindow>("", true, " Combo Tracker Swap");
    }
    ctx->GetWindow()->GetGui()->AddGuiWindow(sSwapWindow);
}
