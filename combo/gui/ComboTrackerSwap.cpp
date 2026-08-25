// combo/gui/ComboTrackerSwap.cpp — see ComboTrackerSwap.h for rationale.
#include "ComboTrackerSwap.h"
#include "ComboTrackerCommon.h"
#include "ComboTrackerBridge.h" // canonical window-type defaults
#include "ComboForeground.h"
#include "ComboWidgetStyle.h"
#include <libultraship/libultraship.h>
#include <ship/resource/CrossRMRegistry.h>
#include <imgui_internal.h> // FindWindowByName / SetWindowPos / ImGuiWindow rects
#ifdef _WIN32
#include <windows.h> // GetModuleHandleA/GetProcAddress (lazy MM save load for dormant draws)
#endif

namespace {

using ComboTracker::kKinds;
using ComboTracker::kTrackers;
using ComboTracker::OotActiveSlot;

constexpr float kHoldSeconds = 0.5f;
constexpr float kHoldDragCancelSqr = 5.0f * 5.0f; // px² of movement that turns a hold into a drag
constexpr float kFirstShowGapPx = 8.0f;           // gap between OOT's and MM's windows on first show

// Peek gesture state per tracker kind (HideBackground mode only).
struct Peek {
    bool holdArmed = false; // press started on this tracker's body
    bool held = false;      // hold fired; the dormant game's window shows until release
};
Peek sPeeks[ComboTracker::kSwapCount];

// Combined Triforce progress (#136): counts live in the two game DLLs, the goal in soh's copy of the
// slot's seed. Returns false unless the loaded seed's goal is a hunt.
bool ComboTriforceProgress(int& have, int& need) {
#ifdef _WIN32
    static int (*sOot)(void) = nullptr;
    static int (*sMm)(void) = nullptr;
    static int (*sGoal)(int*) = nullptr;
    static bool sTried = false;
    if (!sTried) {
        sTried = true;
        if (HMODULE soh = GetModuleHandleA("soh.dll")) {
            sOot = (int (*)(void))GetProcAddress(soh, "SOH_GetTriforcePieceCount");
            sGoal = (int (*)(int*))GetProcAddress(soh, "SOH_GetComboGoal");
        }
        if (HMODULE mm = GetModuleHandleA("2ship.dll")) {
            sMm = (int (*)(void))GetProcAddress(mm, "MM_GetTriforcePieceCount");
        }
    }
    if (!sOot || !sMm || !sGoal || !sGoal(&need) || need <= 0) {
        return false;
    }
    have = sOot() + sMm();
    return true;
#else
    return false;
#endif
}

// A dormant MM draw can precede any MM visit, and until then nothing has loaded the slot's MM save
// into MM's dormant gSaveContext (it holds boot defaults — the tracker would read all-blank). Load
// it here, once per slot. Never after MM has been foreground: its live memory is newer than disk.
void EnsureMmSaveLoadedForDormantDraw() {
#ifdef _WIN32
    if (ComboUI::MmEverForeground()) {
        return;
    }
    static int (*sLoadMmSave)(int) = nullptr;
    static bool sTried = false;
    if (!sTried) {
        sTried = true;
        if (HMODULE mm = GetModuleHandleA("2ship.dll")) {
            sLoadMmSave = (int (*)(int))GetProcAddress(mm, "MM_LoadSaveForCombo");
        }
    }
    static int sLoadedSlot = -1;
    int slot = OotActiveSlot();
    if (!sLoadMmSave || slot < 0 || slot == sLoadedSlot) {
        return;
    }
    // Nonzero = nothing loaded (missing/broken MM half); it parks fileNum at 0xFF and clears saveType, so
    // the trackers go blank instead of showing the previous slot's save. Latch the slot anyway: retrying
    // every draw would re-read the container and spam the log.
    sLoadMmSave(slot);
    sLoadedSlot = slot;
#endif
}

// Each game's own "only show while paused" setting, per tracker kind. OOT's is a no-op unless the
// window type is floating (0) — upstream nests it that way; MM's is VisibilityMode 1.
struct PausedOnly {
    const char* ootCvar;
    const char* ootWindowTypeCvar; // canonical combo CVar (the bridge mirrors it into OOT's)
    int ootWindowTypeDefault;
    const char* mmCvar;
};
constexpr PausedOnly kPausedOnly[ComboTracker::kSwapCount] = {
    { "gTrackers.ItemTracker.ShowOnlyPaused", "gCombo.Tracker.WindowType", ComboTracker::kDefaultWindowType,
      "gSettings.ItemTracker.VisibilityMode" },
    { "gTrackers.CheckTracker.ShowOnlyPaused", "gCombo.CheckTracker.WindowType", ComboTracker::kDefaultCheckWindowType,
      "gRando.CheckTracker.VisibilityMode" },
};

// True when the dormant game's tracker of this kind is set to only show while its game is paused.
bool DormantPausedOnly(int kindIdx, int bg) {
    const PausedOnly& p = kPausedOnly[kindIdx];
    if (bg == 0) {
        return CVarGetInteger(p.ootCvar, 0) != 0 && CVarGetInteger(p.ootWindowTypeCvar, p.ootWindowTypeDefault) == 0;
    }
    return CVarGetInteger(p.mmCvar, 0) == 1; // MM's button modes (2/3) stay always-show while dormant
}

// Write the derived visibility only on change (SetTracker writes the CVar and Show/Hides the
// GuiWindow; doing that unconditionally every frame would fight the games' own Draw gating).
void ApplyDerived(const ComboTracker::Win& w, bool visible) {
    if ((CVarGetInteger(w.cvar, 0) != 0) != visible) {
        ComboTracker::SetTracker(w, visible);
    }
}

// AlwaysAutoResize windows size from the PREVIOUS frame's content, so a newly appearing tracker
// paints its backdrop across a stale rect for one frame. Hide that frame while still measuring.
void SkipSizingFrame(const char* imguiWin) {
    if (!ImGui::GetCurrentContext()) {
        return;
    }
    if (ImGuiWindow* w = ImGui::FindWindowByName(imguiWin)) {
        w->HiddenFramesCannotSkipItems = 1;
    }
}

// First time MM's window shows on a config, place it beside OOT's instead of on the ImGui default
// (stacked over OOT's). Runs while MM's window is visible until both windows exist, then latches.
void PlaceMmWindowOnFirstShow(int kindIdx) {
    const ComboTracker::Kind& kind = kKinds[kindIdx];
    static bool sLatched[ComboTracker::kSwapCount] = {};
    if (sLatched[kindIdx]) {
        return;
    }
    if (CVarGetInteger(kind.mmPlacedCvar, 0)) {
        sLatched[kindIdx] = true;
        return;
    }
    ImGuiWindow* mm = ImGui::FindWindowByName(KindWindowName(kind, 1));
    if (!mm) {
        return; // split-group item layout never creates the main window — keep trying while shown
    }
    ImGuiWindow* oot = ImGui::FindWindowByName(KindWindowName(kind, 0));
    if (!oot) {
        return;
    }
    // AlwaysAutoResize windows carry a placeholder rect through their first (hidden) sizing
    // frames; placing from that puts MM inside OOT's eventual frame. Wait while OOT is actively
    // sizing — a dormant OOT window (HideBackground) keeps its last measured rect and is fine.
    if (oot->WasActive && oot->Hidden) {
        return;
    }
    ImGui::SetWindowPos(mm, ImVec2(oot->Pos.x + oot->Size.x + kFirstShowGapPx, oot->Pos.y), ImGuiCond_Always);
    CVarSetInteger(kind.mmPlacedCvar, 1);
    sLatched[kindIdx] = true;
}

// Per-frame derivation of both games' visibility CVars from the master model.
void Reconcile(int kindIdx) {
    const ComboTracker::Kind& kind = kKinds[kindIdx];
    const int fg = ComboUI::GetForegroundGame();
    const int bg = fg ^ 1;

    const bool master = CVarGetInteger(kind.enabledCvar, 0) != 0;
    const bool hideBg = CVarGetInteger(kind.hideBgCvar, 0) != 0;

    bool wantFg = master;
    // A dormant tracker set to "only while paused" follows the FOREGROUND game's pause state (its
    // own is stale); peek-hold still overrides.
    const bool onlyPaused = DormantPausedOnly(kindIdx, bg);
    bool wantBg = master && ((!hideBg && (!onlyPaused || ComboTracker::ForegroundPaused(fg))) || sPeeks[kindIdx].held);
    // A dormant game's tracker needs its ResourceManager registered (icons) and, for dormant MM,
    // an active OOT save whose MM counterpart it can show — not the title/file-select screens.
    if (wantBg && !Ship::CrossRMRegistry::Get(bg == 0 ? "oot" : "mm")) {
        wantBg = false;
    }
    if (wantBg && bg == 1) {
        if (OotActiveSlot() < 0) {
            wantBg = false;
        } else {
            EnsureMmSaveLoadedForDormantDraw();
        }
    }

    const bool fgWas = CVarGetInteger(kTrackers[fg][kind.column].cvar, 0) != 0;
    const bool bgWas = CVarGetInteger(kTrackers[bg][kind.column].cvar, 0) != 0;
    ApplyDerived(kTrackers[fg][kind.column], wantFg);
    ApplyDerived(kTrackers[bg][kind.column], wantBg);
    // Any newly appearing window gets a hidden sizing frame (AlwaysAutoResize stale-rect flash).
    if (wantFg && !fgWas) {
        SkipSizingFrame(KindWindowName(kind, fg));
    }
    if (wantBg && !bgWas) {
        SkipSizingFrame(KindWindowName(kind, bg));
    }
    // MM's window can only newly exist on frames it draws.
    if ((fg == 1 && wantFg) || (bg == 1 && wantBg)) {
        PlaceMmWindowOnFirstShow(kindIdx);
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

// Logic-only window: no Begin/End, just the per-frame reconcile + peek gesture. Registered under
// a name that sorts before "Check Tracker" and "Item Tracker" (leading space) so the derived
// visibility applies before either tracker draws the same frame.
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
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return;
    }
    // comboui's per-module GImGui must point at the shared context (same pattern as ComboMenu).
    ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());

    for (int k = 0; k < ComboTracker::kSwapCount; ++k) {
        Reconcile(k);
    }

    // Combined Triforce progress (#136) — neither game's own tracker can show it (each counts only
    // its own pieces). Draggable overlay; ImGui persists the position.
    int have = 0, need = 0;
    if (OotActiveSlot() >= 0 && CVarGetInteger("gCombo.Tracker.TriforceLine", 1) && ComboTriforceProgress(have, need)) {
        // Themed like the combo menu's frames (ComboWidgetStyle) so the overlay matches the rest of the UI.
        const ImVec4 theme = ComboRando::ComboMenu_ThemeColor();
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(theme.x, theme.y, theme.z, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.3f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
        if (ImGui::Begin("Combo Triforce", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
            ImGui::Text("Triforce: %d / %d", have, need);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }

    // HideBackground peek: click-hold the visible tracker's body to also show the dormant game's
    // tracker until release. Detection is passive (works through NoInputs click-through); the
    // press still reaches the game. A drag (mouse moved) cancels the hold so draggable/docked
    // trackers keep working.
    ImGuiIO& io = ImGui::GetIO();
    const int fg = ComboUI::GetForegroundGame();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // At most one tracker arms per press; overlapping bodies resolve to the topmost.
        Peek* best = nullptr;
        int bestOrder = -1;
        for (int k = 0; k < ComboTracker::kSwapCount; ++k) {
            Peek& pk = sPeeks[k];
            pk.holdArmed = false;
            if (CVarGetInteger("gOpenWindows.Menu", 0)) { // fullscreen combo menu covers everything
                continue;
            }
            // The peek only exists in HideBackground mode with the tracker on.
            if (!CVarGetInteger(kKinds[k].enabledCvar, 0) || !CVarGetInteger(kKinds[k].hideBgCvar, 0)) {
                continue;
            }
            // Known gap: the item tracker's separate-section / split-group layouts have no main
            // window — no peek there.
            ImGuiWindow* w = ImGui::FindWindowByName(KindWindowName(kKinds[k], fg));
            ImGuiWindow* hovered = ImGui::GetCurrentContext()->HoveredWindow;
            if (w && w->WasActive && !w->Hidden && w->InnerRect.Contains(io.MousePos) &&
                (!hovered || hovered->RootWindow == w->RootWindow)) {
                int order = DisplayOrder(w);
                if (order > bestOrder) {
                    best = &pk;
                    bestOrder = order;
                }
            }
        }
        if (best) {
            best->holdArmed = true;
        }
    }
    for (int k = 0; k < ComboTracker::kSwapCount; ++k) {
        Peek& pk = sPeeks[k];
        if (pk.holdArmed && !pk.held && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] > kHoldDragCancelSqr) {
                pk.holdArmed = false; // it's a drag, not a hold
            } else if (io.MouseDownDuration[ImGuiMouseButton_Left] >= kHoldSeconds) {
                pk.held = true;
                Reconcile(k); // show the dormant tracker this same frame
                // A draggable tracker's press also started an ImGui window-move; end it so the
                // rest of the hold doesn't drag the tracker along (cursor-glue).
                ImGuiContext* g = ImGui::GetCurrentContext();
                ImGuiWindow* tracker = ImGui::FindWindowByName(KindWindowName(kKinds[k], fg));
                if (g->MovingWindow && tracker && g->MovingWindow->RootWindow == tracker->RootWindow) {
                    g->MovingWindow = nullptr;
                    ImGui::ClearActiveID();
                }
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (pk.held) {
                pk.held = false;
                Reconcile(k); // hide it again
            }
            pk.holdArmed = false;
        }
    }
}

} // namespace

bool ComboTracker::ForegroundPaused(int fg) {
#ifdef _WIN32
    static int (*sFn[2])(void) = {};
    static bool sTried[2] = {};
    if (fg != 0 && fg != 1) {
        return true;
    }
    if (!sTried[fg]) {
        sTried[fg] = true;
        if (HMODULE h = GetModuleHandleA(fg == 0 ? "soh.dll" : "2ship.dll")) {
            sFn[fg] = (int (*)(void))GetProcAddress(h, fg == 0 ? "SOH_IsPausedForCombo" : "MM_IsPausedForCombo");
        }
    }
    if (sFn[fg]) {
        return sFn[fg]() != 0;
    }
#endif
    return true;
}

bool ComboTracker::GetMasterVisible(int tracker) {
    return CVarGetInteger(kKinds[tracker].enabledCvar, 0) != 0;
}

void ComboTracker::SetMasterVisible(int tracker, bool visible) {
    CVarSetInteger(kKinds[tracker].enabledCvar, visible ? 1 : 0);
    // The reconcile window applies it next frame.
}

void ComboTracker::RegisterSwap() {
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return;
    }
    // One-shot migration from the retired swap-era scheme (latched by EnabledSeeded).
    static const char* const kLegacyPrefix[2] = { "gCombo.Tracker.", "gCombo.CheckTracker." };
    for (int k = 0; k < kSwapCount; ++k) {
        const std::string pre = kLegacyPrefix[k];
        const std::string seeded = pre + "EnabledSeeded";
        if (CVarGetInteger(seeded.c_str(), 0)) {
            continue;
        }
        CVarSetInteger(seeded.c_str(), 1);
        // Recover crash-persisted forced visibility, then drop the retired CVars.
        for (int g = 0; g < 2; ++g) {
            const std::string stash = pre + (g == 0 ? "TrueIntentOot" : "TrueIntentMm");
            int v = CVarGetInteger(stash.c_str(), 0);
            if (v) {
                CVarSetInteger(kTrackers[g][kKinds[k].column].cvar, v - 1);
            }
            CVarClear(stash.c_str());
        }
        CVarClear((pre + "ShownGame").c_str());
        CVarClear((pre + "HoldMomentary").c_str());
        // Master enable seeds from the per-game CVars an existing config carries.
        bool on = CVarGetInteger(kTrackers[0][kKinds[k].column].cvar, 0) != 0 ||
                  CVarGetInteger(kTrackers[1][kKinds[k].column].cvar, 0) != 0;
        CVarSetInteger(kKinds[k].enabledCvar, on ? 1 : 0);
    }
    if (!sSwapWindow) {
        // Empty CVar name: visibility is never consulted (Draw is overridden), so don't persist
        // one. The leading space keeps it sorted before both tracker windows in the Gui map.
        sSwapWindow = std::make_shared<SwapWindow>("", true, " Combo Tracker Swap");
    }
    ctx->GetWindow()->GetGui()->AddGuiWindow(sSwapWindow);
}
