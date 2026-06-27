// combo/gui/ComboMenu.cpp
#include "ComboMenu.h"
#include "ComboMenuModel.h"
#include "ComboWidgetRender.h"
#include "ComboWidgetStyle.h"
#include <imgui.h>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
// Generate trigger (soh.dll export); runs synchronously on the calling thread.
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
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, CVarGetFloat("gSettings.Menu.BackgroundOpacity", 0.85f)));
    bool open = ImGui::Begin("Combo Menu", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (open) {
        // ComboShip: stylized scope selector (rounded theme buttons) in place of a plain ImGui tab
        // bar, matching the per-game header strip. Tabs are named for each engine's brand, not the
        // bare game code. mScope persists the active scope across frames.
        struct Scope {
            const char* id;
            const char* label;
        };
        static const Scope kScopes[] = {
            { "shared", "Shared" },
            { "oot", "Ship of Harkinian" },
            { "mm", "2 Ship 2 Harkinian" },
        };
        if (mScope.empty()) {
            mScope = "shared";
        }
        bool firstScope = true;
        for (const auto& sc : kScopes) {
            if (!firstScope) {
                ImGui::SameLine();
            }
            firstScope = false;
            if (ComboMenu_StyledHeaderEntry(sc.label, mScope == sc.id)) {
                mScope = sc.id;
            }
        }
        ImGui::Separator();

        if (mScope == "oot") {
            DrawGamePanel("oot");
        } else if (mScope == "mm") {
            DrawGamePanel("mm");
        } else {
            DrawSharedPanel();
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
    std::string label; // shown in the left panel
    std::string group; // owning group label (for the unique key)
    enum Kind { ENGINE, OOT_RANDO, MM_RANDO, COMBO_GEN } kind;
    const ComboRando::GameMenu* game = nullptr; // ENGINE/OOT_RANDO/MM_RANDO
    int sectionIndex = -1;
    int sidebarIndex = -1;
    std::string key() const {
        return group + "/" + label;
    }
};

// ComboShip: tracker sidebars (Item/Entrance/Check Tracker) belong in the per-game Ship/2Ship
// tabs, not the Shared rando hub. Identified by name across both games (all contain "Tracker").
static bool IsTrackerSidebar(const char* name) {
    return name && std::strstr(name, "Tracker") != nullptr;
}

// Append one entry per sidebar of the game's section whose sectionLabel == wantSection.
// Skips (no-op) if the game isn't loaded or the section isn't present — defensive.
// `skip` is a deny-list of sidebar names to omit (e.g. sidebars that are OOT-specific and
// therefore live only in the Ship of Harkinian tab, not here in Shared). `skipTrackers` omits
// tracker sidebars (they live in the per-game tabs).
void AppendSectionEntries(std::vector<HubEntry>& out, const char* groupLabel, HubEntry::Kind kind,
                          const ComboRando::GameMenu& game, const char* wantSection,
                          const std::vector<std::string>& skip = {}, bool skipTrackers = false) {
    if (!game.loaded || !game.menu)
        return;
    const CwMenu* m = game.menu;
    for (int s = 0; s < m->sectionCount; ++s) {
        const CwSection& sec = m->sections[s];
        if (!sec.sectionLabel || strcmp(sec.sectionLabel, wantSection) != 0)
            continue;
        for (int sb = 0; sb < sec.sidebarCount; ++sb) {
            const CwSidebar& side = sec.sidebars[sb];
            const char* nm = (side.sidebarName && side.sidebarName[0]) ? side.sidebarName : "Section";
            if (std::find(skip.begin(), skip.end(), nm) != skip.end())
                continue;
            if (skipTrackers && IsTrackerSidebar(nm))
                continue;
            HubEntry e;
            e.label = nm;
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
    struct Group {
        std::string label;
        std::vector<HubEntry> entries;
    };
    std::vector<Group> groups;

    {
        std::vector<HubEntry> e;
        // "Mod Menu" and "Presets" are OOT-specific (per-game mods/presets) — they live only in
        // the Ship of Harkinian tab, so omit them here to avoid duplicating them in Shared.
        AppendSectionEntries(e, "Shared", HubEntry::ENGINE, model.Oot(), "Settings", { "Mod Menu", "Presets" });
        if (!e.empty())
            groups.push_back({ "Shared", std::move(e) });
    }
    {
        std::vector<HubEntry> e;
        // Trackers live in the Ship of Harkinian tab, not here.
        AppendSectionEntries(e, "OOT Randomizer", HubEntry::OOT_RANDO, model.Oot(), "Randomizer", {}, true);
        if (!e.empty())
            groups.push_back({ "OOT Randomizer", std::move(e) });
    }
    {
        std::vector<HubEntry> e;
        // Display label "MM Randomizer"; the MM menu's own section name is still "Rando".
        // Trackers live in the 2 Ship 2 Harkinian tab, not here.
        AppendSectionEntries(e, "MM Randomizer", HubEntry::MM_RANDO, model.Mm(), "Rando", {}, true);
        if (!e.empty())
            groups.push_back({ "MM Randomizer", std::move(e) });
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
            if (!first)
                first = &en;
            if (en.key() == mHubActive)
                active = &en;
        }
    }
    if (!active) {
        active = first;
        if (active)
            mHubActive = active->key();
    }

    // Left navigation panel — stylized entries matching the per-game left sidebar.
    float availW = ImGui::GetContentRegionAvail().x;
    float sidebarW = availW > 1600 ? availW * 0.15f : 200.0f;
    ImGui::BeginChild("##HubSidebar", ImVec2(sidebarW, 0), ImGuiChildFlags_Borders);
    for (const auto& g : groups) {
        // Enlarge the group divider label so the sections read as clear headers. comboui has no
        // separate bold font in the shared atlas, so scale the current font for the label only,
        // then restore so the entry buttons below render at normal size.
        ImGui::SetWindowFontScale(1.2f);
        ImGui::SeparatorText(g.label.c_str());
        ImGui::SetWindowFontScale(1.0f);
        for (const auto& en : g.entries) {
            const bool selected = active && (active->key() == en.key());
            // Unique ImGui ID per entry — multiple groups share labels like "General", so deriving
            // the ID from the (button-)label alone collides. Visible text stays en.label; the ID
            // comes from the unique group/label key via PushID.
            ImGui::PushID(en.key().c_str());
            if (ComboMenu_StyledSidebarEntry(en.label.c_str(), selected, ImGui::GetContentRegionAvail().x)) {
                mHubActive = en.key();
            }
            ImGui::PopID();
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
        const CwSidebar& side = active->game->menu->sections[active->sectionIndex].sidebars[active->sidebarIndex];
        RenderSidebarWidgets(side, *active->game);
    }
    ImGui::EndChild();
}

// Per-game tab: render the game's menu from the combo-owned C-ABI model (CwMenu) using comboui's
// own ImGui calls (ComboMenuModel + RenderWidget). Mirrors the original Ship of Harkinian layout —
// a top header strip (sections) plus a stylized left sidebar (the selected section's sidebars),
// with that sidebar's widgets on the right. The game DLLs no longer draw their own tabs.
//
// ComboShip presentation filter (combo-owned; the exported CwMenu is UNCHANGED):
//   Both games' randomizer *settings* are surfaced in the Shared tab ("OOT Randomizer" / "MM
//   Randomizer"). The per-game tabs keep the rando section ("Randomizer" / "Rando") but trim it to
//   just the tracker sidebars (Item/Entrance/Check Tracker) — the rest of the rando settings stay
//   in Shared. OOT additionally trims "Settings" to its game-specific sidebars ("Mod Menu",
//   "Presets"); the rest of Settings is shared. MM is otherwise rendered unfiltered.
void ComboMenu::DrawGamePanel(const char* gameKey) {
    auto& model = ComboMenuModel::Get();
    model.EnsureLoaded();
    const bool isOot = strcmp(gameKey, "oot") == 0;
    const GameMenu& game = isOot ? model.Oot() : model.Mm();
    if (!game.loaded || !game.menu) {
        ImGui::TextUnformatted("Menu not available yet (game still initializing).");
        return;
    }
    const CwMenu* m = game.menu;

    auto sidebarShown = [&](const char* section, const char* sidebar) -> bool {
        if (isOot && section && strcmp(section, "Settings") == 0) {
            return sidebar && (strcmp(sidebar, "Mod Menu") == 0 || strcmp(sidebar, "Presets") == 0);
        }
        // The rando section's settings live in the Shared tab; here we surface only its trackers.
        const char* randoSec = isOot ? "Randomizer" : "Rando";
        if (section && strcmp(section, randoSec) == 0)
            return IsTrackerSidebar(sidebar);
        return true;
    };

    // (activeHeader, activeSidebar) for this game; persists across frames.
    auto& nav = mGameNav[gameKey];

    // Resolve the active section: prior pick if still present, else the first section.
    const CwSection* activeSec = nullptr;
    const CwSection* firstSec = nullptr;
    for (int s = 0; s < m->sectionCount; ++s) {
        const CwSection& sec = m->sections[s];
        if (!firstSec)
            firstSec = &sec;
        if (sec.sectionLabel && nav.first == sec.sectionLabel)
            activeSec = &sec;
    }
    if (!activeSec)
        activeSec = firstSec;
    if (!activeSec) {
        ImGui::TextUnformatted("No sections available.");
        return;
    }
    nav.first = activeSec->sectionLabel ? activeSec->sectionLabel : "";

    // Top header strip (stylized buttons, laid out left-to-right).
    bool firstHdr = true;
    for (int s = 0; s < m->sectionCount; ++s) {
        const CwSection& sec = m->sections[s];
        if (!firstHdr)
            ImGui::SameLine();
        firstHdr = false;
        const char* label = (sec.sectionLabel && sec.sectionLabel[0]) ? sec.sectionLabel : "Section";
        if (ComboMenu_StyledHeaderEntry(label, &sec == activeSec)) {
            nav.first = sec.sectionLabel ? sec.sectionLabel : "";
            nav.second.clear(); // reset sidebar selection when switching headers
            activeSec = &sec;
        }
    }
    ImGui::Separator();

    // Resolve the active sidebar within the active section (respecting the allow-filter).
    const CwSidebar* activeSide = nullptr;
    const CwSidebar* firstSide = nullptr;
    for (int sb = 0; sb < activeSec->sidebarCount; ++sb) {
        const CwSidebar& side = activeSec->sidebars[sb];
        if (!sidebarShown(activeSec->sectionLabel, side.sidebarName))
            continue;
        if (!firstSide)
            firstSide = &side;
        if (side.sidebarName && nav.second == side.sidebarName)
            activeSide = &side;
    }
    if (!activeSide)
        activeSide = firstSide;
    if (activeSide)
        nav.second = activeSide->sidebarName ? activeSide->sidebarName : "";

    // Left sidebar (stylized buttons) + right content. NOTE: widgets render linearly and ignore
    // CwSidebar::columnCount — multi-column layout is a later polish pass.
    float availW = ImGui::GetContentRegionAvail().x;
    float sidebarW = availW > 1600 ? availW * 0.15f : 200.0f;
    ImGui::BeginChild("##GameSidebar", ImVec2(sidebarW, 0), ImGuiChildFlags_Borders);
    for (int sb = 0; sb < activeSec->sidebarCount; ++sb) {
        const CwSidebar& side = activeSec->sidebars[sb];
        if (!sidebarShown(activeSec->sectionLabel, side.sidebarName))
            continue;
        const char* label = (side.sidebarName && side.sidebarName[0]) ? side.sidebarName : "Section";
        ImGui::PushID(sb);
        if (ComboMenu_StyledSidebarEntry(label, &side == activeSide, ImGui::GetContentRegionAvail().x)) {
            nav.second = side.sidebarName ? side.sidebarName : "";
            activeSide = &side;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##GameContent", ImVec2(0, 0));
    if (!activeSide) {
        ImGui::TextUnformatted("Select an option.");
    } else {
        RenderSidebarWidgets(*activeSide, game);
    }
    ImGui::EndChild();
}
void ComboMenu::DrawComboPanel() {
    ImGui::TextWrapped("Generate a cross-world randomizer seed spanning OOT and MM. "
                       "You must Generate before starting a new file.");
    ImGui::Separator();
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("Seed", "(blank = random)", mSeedBuf, sizeof(mSeedBuf));
    ImGui::SameLine();

    const bool busy = mProgress.running.load();
    if (busy)
        ImGui::BeginDisabled();
    if (ImGui::Button("Generate")) {
        if (!sTrigger)
            sTrigger = ResolveTrigger();
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
    if (busy)
        ImGui::EndDisabled();

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
            snprintf(buf, sizeof(buf), "Done: seed 0x%X - %d cross-world placements", mProgress.seed.load(),
                     mProgress.foreignCount.load());
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
