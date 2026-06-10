// combo/gui/ComboMenu.cpp
#include "ComboMenu.h"
#include "ComboMenuModel.h"
#include "ComboWidgetRender.h"
#include <imgui.h>
#include <memory>
#include <string>
#include <cstring>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
typedef void (*FnDrawSettings)(const char*, const char*);
FnDrawSettings ResolveDraw(const char* dll, const char* sym) {
#ifdef _WIN32
    HMODULE h = GetModuleHandleA(dll); // already loaded by the exe
    return h ? (FnDrawSettings)GetProcAddress(h, sym) : nullptr;
#else
    return nullptr;
#endif
}
FnDrawSettings sSohDraw = nullptr; // still used by DrawSharedPanel for the engine sidebars
// Engine sidebars shown ONCE in the Shared tab (rendered by OOT, since they write the shared
// gSettings.* engine CVars) and skipped from the per-game tabs to avoid duplication.
const char* kEngineSidebars = "Settings/Graphics,Settings/General";

// Generate trigger (soh.dll export); runs synchronously on the calling thread (Inc7: thread removed).
typedef void (*FnTriggerGenerate)(const char*, ComboRando::ComboGenProgress*);
FnTriggerGenerate sTrigger = nullptr;
FnTriggerGenerate ResolveTrigger() {
#ifdef _WIN32
    HMODULE h = GetModuleHandleA("soh.dll");
    return h ? (FnTriggerGenerate)GetProcAddress(h, "SOH_TriggerComboGenerate") : nullptr;
#else
    return nullptr;
#endif
}
} // namespace

namespace ComboRando {

static std::shared_ptr<ComboMenu> sComboMenu;

void ComboMenu::Draw() {
    // Mirror Ship::Menu::Draw — skip GuiWindow::Draw's normal Begin/End wrapper so DrawElement
    // can open its own fullscreen overlay window instead of a floating one.
    if (!IsVisible()) {
        return;
    }
    DrawElement();
    SyncVisibilityConsoleVariable();
}

void ComboMenu::DrawElement() {
    // ImGui's GImGui (current-context) is a per-module global; comboui.dll has its own,
    // separate from libultraship.dll where the context actually lives. Point it at the shared
    // context before any ImGui call here (same pattern soh.dll/2ship.dll use) — otherwise
    // ImGui::BeginTabBar dereferences a null context and crashes.
    auto ctx = Ship::Context::GetInstance();
    if (ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
        ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());
    }

    // Fullscreen overlay covering the viewport work area, matching the old port menu
    // (NoDecoration/NoMove, sized to the viewport, with a translucent backdrop).
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(0, 0, 0, CVarGetFloat("gSettings.Menu.BackgroundOpacity", 0.85f)));
    bool open = ImGui::Begin("Combo Menu", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (open) {
        if (ImGui::BeginTabBar("ComboScopeTabs")) {
            if (ImGui::BeginTabItem("Shared")) { DrawSharedPanel(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("OOT"))    { DrawGamePanel("oot"); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("MM"))     { DrawGamePanel("mm");  ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Combo"))  { DrawComboPanel(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

// Shared tab: render OOT's themed engine sidebars (Graphics/General) via the only-list. They write
// the shared gSettings.* engine CVars, so this one panel controls both games — and it looks native.
void ComboMenu::DrawSharedPanel() {
    if (!sSohDraw) sSohDraw = ResolveDraw("soh.dll", "SOH_DrawSettings");
    if (sSohDraw) sSohDraw(kEngineSidebars, "");
    else ImGui::TextUnformatted("Shared settings unavailable (SOH_DrawSettings not found).");
}

// Per-game tab: render the game's menu from the combo-owned C-ABI model (CwMenu) using
// comboui's own ImGui calls (ComboMenuModel + RenderWidget); the game DLLs no longer draw their
// own tabs. Only the Shared tab (DrawSharedPanel) still delegates to OOT's SOH_DrawSettings.
void ComboMenu::DrawGamePanel(const char* gameKey) {
    auto& model = ComboMenuModel::Get();
    model.EnsureLoaded();
    const GameMenu& game = (strcmp(gameKey, "oot") == 0) ? model.Oot() : model.Mm();
    if (!game.loaded || !game.menu) {
        ImGui::TextUnformatted("Menu not available yet (game still initializing).");
        return;
    }
    const CwMenu* m = game.menu;
    // Section tab bar; within each section, each sidebar's widgets under a SeparatorText.
    // NOTE: this renders widgets linearly and ignores CwSidebar::columnCount — multi-column
    // layout is a later polish pass; a single column is fine for this milestone.
    if (ImGui::BeginTabBar("ComboGameSections")) {
        for (int s = 0; s < m->sectionCount; ++s) {
            const CwSection& sec = m->sections[s];
            if (ImGui::BeginTabItem(sec.sectionLabel && sec.sectionLabel[0] ? sec.sectionLabel : "Section")) {
                ImGui::BeginChild("##secscroll");
                for (int sb = 0; sb < sec.sidebarCount; ++sb) {
                    const CwSidebar& side = sec.sidebars[sb];
                    if (side.sidebarName && side.sidebarName[0]) ImGui::SeparatorText(side.sidebarName);
                    for (int wdx = 0; wdx < side.widgetCount; ++wdx) {
                        RenderWidget(side.widgets[wdx], game);
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
}
void ComboMenu::DrawComboPanel() {
    ImGui::TextWrapped("Generate a cross-world randomizer seed spanning OOT and MM. "
                       "You must Generate before starting a new file.");
    ImGui::Separator();
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("Seed", "(blank = random)", mSeedBuf, sizeof(mSeedBuf));
    ImGui::SameLine();

    const bool busy = mProgress.running.load();
    if (busy) ImGui::BeginDisabled();
    if (ImGui::Button("Generate")) {
        if (!sTrigger) sTrigger = ResolveTrigger();
        if (sTrigger) {
            // Arm the one-frame defer: show "Generating…" this frame so the user sees
            // feedback before the synchronous (blocking) fill runs next frame.
            mProgress.Reset();
            mProgress.done.store(false);
            mProgress.running.store(true);
            mStatusLine.clear();
            mGeneratePending = true;
        } else {
            mStatusLine = "Generate unavailable (SOH_TriggerComboGenerate not found).";
        }
    }
    if (busy) ImGui::EndDisabled();

    // One-frame defer: if a generate was requested last frame, fire it now (blocks until done).
    if (mGeneratePending) {
        mGeneratePending = false;
        sTrigger(mSeedBuf, &mProgress); // synchronous — blocks this frame; sets done on return
    }

    // Latch the result once the (synchronous) fill finishes. Branch on success, not just done —
    // a done&&!success covers both real errors and a rejected duplicate, so the UI never hangs.
    if (mProgress.done.load() && mProgress.running.load()) {
        if (mProgress.success.load()) {
            char buf[160];
            snprintf(buf, sizeof(buf), "Done: seed 0x%X - %d cross-world placements",
                     mProgress.seed.load(), mProgress.foreignCount.load());
            mStatusLine = buf;
        } else {
            mStatusLine = std::string("Error: ") + mProgress.error;
        }
        mProgress.running.store(false);
    }

    if (mProgress.running.load()) {
        int placed = mProgress.placed.load();
        int total = mProgress.total.load();
        float frac = total > 0 ? (float)placed / (float)total : 0.0f;
        ImGui::TextUnformatted(ComboGenProgress::PhaseLabel(mProgress.phase.load()));
        ImGui::ProgressBar(frac, ImVec2(360.0f, 0.0f));
        ImGui::Text("%d / %d", placed, total);
    } else if (!mStatusLine.empty()) {
        ImGui::TextUnformatted(mStatusLine.c_str());
    }
}

} // namespace ComboRando

#ifdef _WIN32
extern "C" __declspec(dllexport) void ComboUI_Register(void)
#else
extern "C" void ComboUI_Register(void)
#endif
{
    auto ctx = Ship::Context::GetInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return; // GUI not ready
    }
    auto gui = ctx->GetWindow()->GetGui();
    // Match the existing menu-visibility CVar so the in-game menu hotkey toggles us.
    ComboRando::sComboMenu = std::make_shared<ComboRando::ComboMenu>("gOpenWindows.Menu", "Combo Menu");
    gui->SetMenu(ComboRando::sComboMenu);
}
