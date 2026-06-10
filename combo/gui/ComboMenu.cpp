// combo/gui/ComboMenu.cpp
#include "ComboMenu.h"
#include "ComboMenuModel.h"
#include "ComboWidgetRender.h"
#include "ComboWidgetStyle.h"
#include <imgui.h>
#include <memory>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
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
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

// Shared tab: a left-panel navigation hub (mirrors the native menu's left sidebar). Groups of
// entries on the left; selecting one renders its content on the right. Groups: engine "Shared"
// settings (OOT-rendered, write shared gSettings.* CVars), "OOT Randomizer", "MM Rando", and a
// single "Combo" → Generate entry. The HubEntry list is rebuilt each frame from the cached model
// (cheap: just pointers/indices into the process-stable CwMenu); nothing is cached across frames.
namespace {
struct HubEntry {
    std::string                    label;     // shown in the left panel
    std::string                    group;     // owning group label (for the unique key)
    enum Kind { ENGINE, OOT_RANDO, MM_RANDO, COMBO_GEN } kind;
    const ComboRando::GameMenu*    game = nullptr; // ENGINE/OOT_RANDO/MM_RANDO
    int                            sectionIndex = -1;
    int                            sidebarIndex = -1;
    std::string key() const { return group + "/" + label; }
};

// Append one entry per sidebar of the game's section whose sectionLabel == wantSection.
// Skips (no-op) if the game isn't loaded or the section isn't present — defensive.
void AppendSectionEntries(std::vector<HubEntry>& out, const char* groupLabel,
                          HubEntry::Kind kind, const ComboRando::GameMenu& game,
                          const char* wantSection) {
    if (!game.loaded || !game.menu) return;
    const CwMenu* m = game.menu;
    for (int s = 0; s < m->sectionCount; ++s) {
        const CwSection& sec = m->sections[s];
        if (!sec.sectionLabel || strcmp(sec.sectionLabel, wantSection) != 0) continue;
        for (int sb = 0; sb < sec.sidebarCount; ++sb) {
            const CwSidebar& side = sec.sidebars[sb];
            HubEntry e;
            e.label = (side.sidebarName && side.sidebarName[0]) ? side.sidebarName : "Section";
            e.group = groupLabel;
            e.kind = kind;
            e.game = &game;
            e.sectionIndex = s;
            e.sidebarIndex = sb;
            out.push_back(std::move(e));
        }
        break; // first matching section only
    }
}
} // namespace

void ComboMenu::DrawSharedPanel() {
    auto& model = ComboMenuModel::Get();
    model.EnsureLoaded();

    // Build the navigation model fresh each frame (group order is the display order).
    struct Group { std::string label; std::vector<HubEntry> entries; };
    std::vector<Group> groups;

    {
        std::vector<HubEntry> e;
        AppendSectionEntries(e, "Shared", HubEntry::ENGINE, model.Oot(), "Settings");
        if (!e.empty()) groups.push_back({ "Shared", std::move(e) });
    }
    {
        std::vector<HubEntry> e;
        AppendSectionEntries(e, "OOT Randomizer", HubEntry::OOT_RANDO, model.Oot(), "Randomizer");
        if (!e.empty()) groups.push_back({ "OOT Randomizer", std::move(e) });
    }
    {
        std::vector<HubEntry> e;
        AppendSectionEntries(e, "MM Rando", HubEntry::MM_RANDO, model.Mm(), "Rando");
        if (!e.empty()) groups.push_back({ "MM Rando", std::move(e) });
    }
    {
        HubEntry gen;
        gen.label = "Generate";
        gen.group = "Combo";
        gen.kind = HubEntry::COMBO_GEN;
        groups.push_back({ "Combo", { std::move(gen) } });
    }

    // Resolve the active entry; default to the first available, and recover if the prior
    // selection vanished (e.g. a game finished loading and reshaped the list).
    const HubEntry* active = nullptr;
    const HubEntry* first = nullptr;
    for (const auto& g : groups) {
        for (const auto& en : g.entries) {
            if (!first) first = &en;
            if (en.key() == mHubActive) active = &en;
        }
    }
    if (!active) {
        active = first;
        if (active) mHubActive = active->key();
    }

    const ImVec4 theme = ComboMenu_ThemeColor();

    // Left navigation panel.
    float availW = ImGui::GetContentRegionAvail().x;
    float sidebarW = availW > 1600 ? availW * 0.15f : 200.0f;
    ImGui::BeginChild("##HubSidebar", ImVec2(sidebarW, 0), ImGuiChildFlags_Borders);
    for (const auto& g : groups) {
        ImGui::SeparatorText(g.label.c_str());
        for (const auto& en : g.entries) {
            const bool selected = active && (active->key() == en.key());
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Header, theme);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme);
            }
            if (ImGui::Selectable(en.label.c_str(), selected)) {
                mHubActive = en.key();
            }
            if (selected) ImGui::PopStyleColor(2);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right content panel for the active entry.
    ImGui::BeginChild("##HubContent", ImVec2(0, 0));
    if (!active) {
        ImGui::TextUnformatted("Select an option.");
    } else if (active->kind == HubEntry::COMBO_GEN) {
        DrawComboPanel();
    } else {
        const CwSidebar& side =
            active->game->menu->sections[active->sectionIndex].sidebars[active->sidebarIndex];
        for (int wdx = 0; wdx < side.widgetCount; ++wdx) {
            RenderWidget(side.widgets[wdx], *active->game);
        }
    }
    ImGui::EndChild();
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
