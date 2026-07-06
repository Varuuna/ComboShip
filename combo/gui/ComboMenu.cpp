// combo/gui/ComboMenu.cpp
#include "ComboMenu.h"
#include "ComboMenuModel.h"
#include "ComboWidgetRender.h"
#include "ComboWidgetStyle.h"
#include "ComboTrackerBridge.h"
#include "ComboTrackerSwap.h"
#include <imgui.h>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <unordered_set>
#include "rando/CrossForeign.h" // ComboRando::SlotReadPath for the active seed's playthrough
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
// soh.dll exports: trigger combo generation (non-blocking; spawns the launcher worker), read the
// shared progress, and gate generation to the file-select screen.
typedef void (*FnTriggerGenerate)(void);
typedef const ComboRando::ComboGenProgress* (*FnGetProgress)(void);
typedef unsigned char (*FnIsOnFileSelect)(void);
typedef const char* (*FnGetStr)(void);
typedef int (*FnGetInt)(void);
FnTriggerGenerate sTrigger = nullptr;
FnGetProgress sGetProgress = nullptr;
FnIsOnFileSelect sIsOnFileSelect = nullptr;
FnGetInt sGetFileNum = nullptr;  // SOH_GetActiveFileNum (soh.dll)
FnGetStr sSohObtained = nullptr; // Combo_SOH_GetObtainedChecks (soh.dll)
FnGetStr sMmObtained = nullptr;  // Combo_MM_GetObtainedChecks (2ship.dll)
void ResolveComboGenSyms() {
#ifdef _WIN32
    HMODULE h = GetModuleHandleA("soh.dll");
    if (!h)
        return;
    if (!sTrigger)
        sTrigger = (FnTriggerGenerate)GetProcAddress(h, "SOH_TriggerComboGenerate");
    if (!sGetProgress)
        sGetProgress = (FnGetProgress)GetProcAddress(h, "SOH_GetComboGenProgress");
    if (!sIsOnFileSelect)
        sIsOnFileSelect = (FnIsOnFileSelect)GetProcAddress(h, "SOH_IsOnFileSelect");
    if (!sGetFileNum)
        sGetFileNum = (FnGetInt)GetProcAddress(h, "SOH_GetActiveFileNum");
    if (!sSohObtained)
        sSohObtained = (FnGetStr)GetProcAddress(h, "Combo_SOH_GetObtainedChecks");
    if (!sMmObtained) {
        HMODULE mm = GetModuleHandleA("2ship.dll");
        if (mm)
            sMmObtained = (FnGetStr)GetProcAddress(mm, "Combo_MM_GetObtainedChecks");
    }
#endif
}
} // namespace

namespace ComboRando {

static std::shared_ptr<ComboMenu> sComboMenu;

// Search normalization shared by the query and every widget haystack: lowercase + strip spaces.
static std::string NormalizeSearch(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    return s;
}
static const ImVec4 kBreadcrumbColor(0.6f, 0.6f, 0.6f, 1.0f); // muted gray for the result origin line

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
        // Global search row beneath the scope strip: a themed Clear button + a full-width input.
        // A non-empty query takes over the content area (results span both games); the scope tabs
        // stay visible but inert until the box is cleared.
        ComboMenu_PushButton(ComboMenu_ThemeColor());
        if (ImGui::Button("Clear")) {
            mSearchBuf[0] = '\0';
        }
        ComboMenu_PopButton();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##ComboSearch", "Search settings...", mSearchBuf, sizeof(mSearchBuf));
        ImGui::Separator();

        // Normalize once per frame; the scope panels render search results in their content area
        // (keeping the left nav visible) when this is non-empty.
        mSearchQuery = NormalizeSearch(mSearchBuf);
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

// Global search (issue #26): scans BOTH games' CwMenu models for a name+tooltip match against the
// pre-normalized query, rendering each hit inline via RenderWidget with an origin breadcrumb.
// Shared settings live in both models; dedupe by cvar|name (OOT wins) so they surface once.
void ComboMenu::DrawSearchResults(const std::string& query) {
    auto& model = ComboMenuModel::Get();
    model.EnsureLoaded();

    struct Source {
        const char* label;
        const GameMenu* game;
    };
    const Source sources[] = {
        { "Ship of Harkinian", &model.Oot() }, // OOT first: its copy wins the dedupe
        { "2 Ship 2 Harkinian", &model.Mm() },
    };

    // 2-3 equal-width columns (by available width) so results read as a grid, not one tall list —
    // same stretch-table pattern as RenderSidebarWidgets. Each match fills the next cell, wrapping
    // rows; the narrower cell width is what keeps the controls from spanning the whole panel.
    float avail = ImGui::GetContentRegionAvail().x;
    int cols = avail > 1100.0f ? 3 : (avail > 720.0f ? 2 : 1);

    std::unordered_set<std::string> seen; // dedupe key: normalized cvar (or per-game name for no-cvar)
    int matches = 0;
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("##searchresults", cols, flags)) {
        for (const auto& src : sources) {
            const GameMenu& game = *src.game;
            if (!game.loaded || !game.menu)
                continue;
            const CwMenu* m = game.menu;
            for (int s = 0; s < m->sectionCount; ++s) {
                const CwSection& sec = m->sections[s];
                for (int sb = 0; sb < sec.sidebarCount; ++sb) {
                    const CwSidebar& side = sec.sidebars[sb];
                    for (int w = 0; w < side.widgetCount; ++w) {
                        const CwWidget& wd = side.widgets[w];
                        // Non-interactive / opted-out kinds never appear in search.
                        if (wd.kind == CW_SEPARATOR || wd.kind == CW_SEPARATOR_TEXT || wd.kind == CW_TEXT ||
                            wd.kind == CW_CUSTOM || wd.hideInSearch)
                            continue;
                        std::string hay =
                            NormalizeSearch(std::string(wd.name ? wd.name : "") + (wd.tooltip ? wd.tooltip : ""));
                        if (hay.find(query) == std::string::npos)
                            continue;
                        // Dedupe shared settings (same backing CVar) across both games. Widgets with no
                        // CVar (buttons/window toggles) drive distinct callbacks, so scope their key per
                        // game — otherwise two same-named buttons would collapse and one would vanish.
                        std::string cvar = wd.cvar ? wd.cvar : "";
                        std::string key = cvar.empty() ? (std::string(src.label) + "|" + (wd.name ? wd.name : ""))
                                                       : NormalizeSearch(cvar);
                        if (!seen.insert(NormalizeSearch(key)).second)
                            continue; // a shared setting already shown from OOT

                        ImGui::TableNextColumn();
                        // Extra per-game ID scope: RenderWidget PushID(w.index) internally, and the two
                        // games can emit the same index — without this, their interaction state collides.
                        ImGui::PushID(src.label);
                        RenderWidget(wd, game);
                        ImGui::TextColored(kBreadcrumbColor, "[%s] %s -> %s", src.label,
                                           (sec.sectionLabel && sec.sectionLabel[0]) ? sec.sectionLabel : "Section",
                                           (side.sidebarName && side.sidebarName[0]) ? side.sidebarName : "Section");
                        ImGui::PopID();
                        ImGui::Spacing();
                        ++matches;
                    }
                }
            }
        }
        ImGui::EndTable();
    }

    if (matches == 0)
        ImGui::TextDisabled("No matching settings.");
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
    enum Kind { ENGINE, OOT_RANDO, MM_RANDO, COMBO_GEN, COMBO_TRACKER, COMBO_CHECK_TRACKER, COMBO_ENTRANCES } kind;
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
// Shared > Item Tracker: combo-owned appearance CVars (mirrored into both games, see
// ComboTrackerBridge) + the sticky game-swap selection (ComboTrackerSwap).
void DrawTrackerSharedPanel() {
    const ImVec4 theme = ComboRando::ComboMenu_ThemeColor();
    bool changed = false;

    // Same narrow-column layout as the game pages (RenderSidebarWidgets): widgets in the first
    // cell of a two-column stretch table instead of spanning the whole panel.
    const ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings;
    if (!ImGui::BeginTable("##trackercols", 2, tableFlags)) {
        return;
    }
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    bool shown = ComboTracker::GetMasterVisible(ComboTracker::kSwapItem);
    ComboRando::ComboMenu_PushCheckbox(theme);
    if (ImGui::Checkbox("Show Item Tracker", &shown)) {
        ComboTracker::SetMasterVisible(ComboTracker::kSwapItem, shown);
        changed = true;
    }
    ComboRando::ComboMenu_PopCheckbox();

    int px = CVarGetInteger("gCombo.Tracker.IconSize", ComboTracker::kDefaultIconSize);
    ComboRando::ComboMenu_PushSlider(theme);
    if (ImGui::SliderInt("Icon size (px)", &px, 16, 64)) {
        CVarSetInteger("gCombo.Tracker.IconSize", px);
        changed = true;
    }
    float op = CVarGetFloat("gCombo.Tracker.Opacity", ComboTracker::kDefaultOpacity);
    if (ImGui::SliderFloat("Background opacity", &op, 0.0f, 1.0f, "%.2f")) {
        CVarSetFloat("gCombo.Tracker.Opacity", op);
        changed = true;
    }
    ComboRando::ComboMenu_PopSlider();

    const char* kWindowTypes[] = { "Floating (overlay)", "Window" };
    int wt = CVarGetInteger("gCombo.Tracker.WindowType", ComboTracker::kDefaultWindowType);
    ComboRando::ComboMenu_PushCombobox(theme);
    if (ImGui::Combo("Window type", &wt, kWindowTypes, 2)) {
        CVarSetInteger("gCombo.Tracker.WindowType", wt);
        changed = true;
    }
    ComboRando::ComboMenu_PopCombobox();

    int drag = CVarGetInteger("gCombo.Tracker.Draggable", ComboTracker::kDefaultDraggable);
    bool dragB = drag != 0;
    ComboRando::ComboMenu_PushCheckbox(theme);
    if (ImGui::Checkbox("Draggable (floating tracker accepts the mouse)", &dragB)) {
        CVarSetInteger("gCombo.Tracker.Draggable", dragB ? 1 : 0);
        changed = true;
    }
    ComboRando::ComboMenu_PopCheckbox();
    if (changed) {
        ComboTracker::SyncAppearance();
    }

    ImGui::SeparatorText("Game");
    const char* kShown[] = { "Follow foreground game", "Ocarina of Time", "Majora's Mask" };
    int sg = CVarGetInteger("gCombo.Tracker.ShownGame", -1);
    int idx = (sg == 0 || sg == 1) ? sg + 1 : 0;
    ComboRando::ComboMenu_PushCombobox(theme);
    if (ImGui::Combo("Tracker shows", &idx, kShown, 3)) {
        CVarSetInteger("gCombo.Tracker.ShownGame", idx - 1); // swap window reconciles next frame
        changed = true;
    }
    ComboRando::ComboMenu_PopCombobox();
    int mom = CVarGetInteger("gCombo.Tracker.HoldMomentary", 0);
    bool momB = mom != 0;
    ComboRando::ComboMenu_PushCheckbox(theme);
    if (ImGui::Checkbox("Hold is a peek only (switch back on release)", &momB)) {
        CVarSetInteger("gCombo.Tracker.HoldMomentary", momB ? 1 : 0);
        changed = true;
    }
    ComboRando::ComboMenu_PopCheckbox();
    ImGui::TextDisabled("Click and hold the tracker overlay for half a second to switch game.");
    ImGui::TextDisabled("Which items each game's tracker lists is set in its own Item Tracker Settings.");

    ImGui::EndTable();

    if (changed) {
        if (auto ctx = Ship::Context::GetInstance(); ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
            ctx->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    }
}

// Shared > Check Tracker: master visibility + the sticky game-swap selection (ComboTrackerSwap).
// No shared appearance here — colors/filters stay in each game's own Check Tracker Settings.
void DrawCheckTrackerSharedPanel() {
    const ImVec4 theme = ComboRando::ComboMenu_ThemeColor();
    bool changed = false;

    const ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings;
    if (!ImGui::BeginTable("##checktrackercols", 2, tableFlags)) {
        return;
    }
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    bool shown = ComboTracker::GetMasterVisible(ComboTracker::kSwapCheck);
    ComboRando::ComboMenu_PushCheckbox(theme);
    if (ImGui::Checkbox("Show Check Tracker", &shown)) {
        ComboTracker::SetMasterVisible(ComboTracker::kSwapCheck, shown);
        changed = true;
    }
    ComboRando::ComboMenu_PopCheckbox();

    const char* kWindowTypes[] = { "Floating (overlay)", "Window" };
    int wt = CVarGetInteger("gCombo.CheckTracker.WindowType", ComboTracker::kDefaultCheckWindowType);
    ComboRando::ComboMenu_PushCombobox(theme);
    if (ImGui::Combo("Window type", &wt, kWindowTypes, 2)) {
        CVarSetInteger("gCombo.CheckTracker.WindowType", wt);
        ComboTracker::SyncAppearance(); // mirrors into OOT's CVar; MM's seam reads it directly
        changed = true;
    }
    ComboRando::ComboMenu_PopCombobox();

    ImGui::SeparatorText("Game");
    const char* kShown[] = { "Follow foreground game", "Ocarina of Time", "Majora's Mask" };
    int sg = CVarGetInteger("gCombo.CheckTracker.ShownGame", -1);
    int idx = (sg == 0 || sg == 1) ? sg + 1 : 0;
    ComboRando::ComboMenu_PushCombobox(theme);
    if (ImGui::Combo("Tracker shows", &idx, kShown, 3)) {
        CVarSetInteger("gCombo.CheckTracker.ShownGame", idx - 1); // swap window reconciles next frame
        changed = true;
    }
    ComboRando::ComboMenu_PopCombobox();
    int mom = CVarGetInteger("gCombo.CheckTracker.HoldMomentary", 0);
    bool momB = mom != 0;
    ComboRando::ComboMenu_PushCheckbox(theme);
    if (ImGui::Checkbox("Hold is a peek only (switch back on release)", &momB)) {
        CVarSetInteger("gCombo.CheckTracker.HoldMomentary", momB ? 1 : 0);
        changed = true;
    }
    ComboRando::ComboMenu_PopCheckbox();
    ImGui::TextDisabled("Click and hold the check tracker for half a second to switch game.");
    ImGui::TextDisabled("Appearance and filters are set in each game's own Check Tracker Settings.");

    ImGui::EndTable();

    if (changed) {
        if (auto ctx = Ship::Context::GetInstance(); ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
            ctx->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    }
}

// ComboShip: OOT's entrance-shuffle options (the "Entrances" column of the Logic/Access sidebar)
// are claimed by the Shared > Entrances panel and excluded from the OOT Randomizer sidebars so
// they have a single home. Matched by backing CVar (stable across upstream label changes); the
// section's own "Entrances" separator-text is claimed too so no orphaned header remains behind.
bool IsOotEntranceWidget(const CwWidget& w) {
    if (w.kind == CW_SEPARATOR_TEXT)
        return w.name && std::strcmp(w.name, "Entrances") == 0;
    if (!w.cvar || !w.cvar[0])
        return false;
    static const std::unordered_set<std::string> kEntranceCvars = {
        "gRandoSettings.ShuffleDungeonsEntrances",
        "gRandoSettings.ShuffleBossEntrances",
        "gRandoSettings.ShuffleGanonTowerEntrance",
        "gRandoSettings.ShuffleOverworldEntrances",
        "gRandoSettings.ShuffleInteriorsEntrances",
        "gRandoSettings.ShuffleThievesHideoutEntrances",
        "gRandoSettings.ShuffleGrottosEntrances",
        "gRandoSettings.ShuffleOwlDrops",
        "gRandoSettings.ShuffleWarpSongs",
        "gRandoSettings.ShuffleOverworldSpawns",
        "gRandoSettings.MixedEntrances",
        "gRandoSettings.MixDungeons",
        "gRandoSettings.MixBosses",
        "gRandoSettings.MixOverworld",
        "gRandoSettings.MixInteriors",
        "gRandoSettings.MixThievesHideout",
        "gRandoSettings.MixGrottos",
        "gRandoSettings.DecoupleEntrances",
    };
    return kEntranceCvars.count(w.cvar) != 0;
}

// Shared > Entrances: one home for entrance shuffling across both games plus ComboShip's own
// cross-game entrance features. The OOT half renders the game's own exported widgets (labels,
// tooltips, choice lists follow upstream automatically); MM joins when 2Ship gains entrance
// shuffle; Cross-Game hosts combo-owned options.
void DrawEntrancesPanel() {
    auto& model = ComboRando::ComboMenuModel::Get();

    // Same narrow-column layout as the game pages: widgets in the first cell of a two-column
    // stretch table instead of spanning the whole panel.
    const ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings;
    if (!ImGui::BeginTable("##entrancecols", 2, tableFlags)) {
        return;
    }
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    ImGui::SeparatorText("Ocarina of Time");
    const ComboRando::GameMenu& oot = model.Oot();
    bool drewOot = false;
    if (oot.loaded && oot.menu) {
        const CwMenu* m = oot.menu;
        for (int s = 0; s < m->sectionCount; ++s) {
            const CwSection& sec = m->sections[s];
            if (!sec.sectionLabel || strcmp(sec.sectionLabel, "Randomizer") != 0)
                continue;
            for (int sb = 0; sb < sec.sidebarCount; ++sb) {
                const CwSidebar& side = sec.sidebars[sb];
                for (int w = 0; w < side.widgetCount; ++w) {
                    const CwWidget& wd = side.widgets[w];
                    // Skip the group's own "Entrances" separator — this panel draws its headers.
                    if (IsOotEntranceWidget(wd) && wd.kind != CW_SEPARATOR_TEXT) {
                        ComboRando::RenderWidget(wd, oot);
                        drewOot = true;
                    }
                }
            }
            break;
        }
    }
    if (!drewOot) {
        ImGui::TextDisabled("Menu not available yet (game still initializing).");
    }

    ImGui::SeparatorText("Majora's Mask");
    // MM's entrance options (PR #1329) sit in a custom-drawn rando tab the CVar-claim filter can't
    // relocate, so draw combo-owned checkboxes on the same CVars (in-game copies compiled out under
    // COMBO_BUILD). Overworld stays hidden: upstream calls it use-at-your-own-risk, and it could
    // disturb South Clock Town (the MM->OOT portal).
    {
        const ImVec4 theme = ComboRando::ComboMenu_ThemeColor();
        static const struct {
            const char* label;
            const char* cvar;
        } kMmEntranceToggles[] = {
            { "Shuffle Interior Entrances", "gRando.Options.RO_SHUFFLE_ENTRANCES_INTERIORS" },
            { "Shuffle Dungeon Entrances", "gRando.Options.RO_SHUFFLE_ENTRANCES_DUNGEONS" },
        };
        ComboRando::ComboMenu_PushCheckbox(theme);
        for (const auto& t : kMmEntranceToggles) {
            bool v = CVarGetInteger(t.cvar, 0) != 0;
            if (ImGui::Checkbox(t.label, &v)) {
                CVarSetInteger(t.cvar, v ? 1 : 0);
                Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
        }
        ComboRando::ComboMenu_PopCheckbox();
    }

    ImGui::SeparatorText("Cross-Game");
    // Union shuffle over both games' leaf interiors (docs/ENTRANCE_RANDO_PREP.md §4): any selected
    // door can lead to any selected interior in either game. Doors in the cross pool leave the
    // native interior shuffles (partition).
    {
        const ImVec4 theme = ComboRando::ComboMenu_ThemeColor();
        bool v = CVarGetInteger("gCombo.Entrances.CrossInteriors", 0) != 0;
        ComboRando::ComboMenu_PushCheckbox(theme);
        if (ImGui::Checkbox("Cross-Game Interior Shuffle", &v)) {
            CVarSetInteger("gCombo.Entrances.CrossInteriors", v ? 1 : 0);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
        ComboRando::ComboMenu_PopCheckbox();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shuffle both games' house/shop interiors into ONE pool:\n"
                              "a Kokiri door can lead to the Milk Bar, a Clock Town door\n"
                              "to Mido's House. Walking through a cross-game door switches\n"
                              "games; exiting brings you back beside the door you entered.");
        }
    }

    ImGui::EndTable();
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
        // Combo-owned Item Tracker panel: shared appearance + game-swap selection.
        HubEntry trk;
        trk.label = "Item Tracker";
        trk.group = "Shared";
        trk.kind = HubEntry::COMBO_TRACKER;
        e.push_back(std::move(trk));
        // Combo-owned Check Tracker panel: master visibility + game-swap selection.
        HubEntry chk;
        chk.label = "Check Tracker";
        chk.group = "Shared";
        chk.kind = HubEntry::COMBO_CHECK_TRACKER;
        e.push_back(std::move(chk));
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
        // Combo-owned Entrances panel: both games' entrance-shuffle options + cross-game features.
        HubEntry ent;
        ent.label = "Entrances";
        ent.group = "Combo";
        ent.kind = HubEntry::COMBO_ENTRANCES;
        HubEntry gen;
        gen.label = "Generate";
        gen.group = "Combo";
        gen.kind = HubEntry::COMBO_GEN;
        groups.push_back({ "Combo", { std::move(ent), std::move(gen) } });
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
    if (!mSearchQuery.empty()) {
        DrawSearchResults(mSearchQuery);
    } else if (!active) {
        ImGui::TextUnformatted("Select an option.");
    } else if (active->kind == HubEntry::COMBO_GEN) {
        DrawComboPanel();
    } else if (active->kind == HubEntry::COMBO_TRACKER) {
        DrawTrackerSharedPanel();
    } else if (active->kind == HubEntry::COMBO_CHECK_TRACKER) {
        DrawCheckTrackerSharedPanel();
    } else if (active->kind == HubEntry::COMBO_ENTRANCES) {
        DrawEntrancesPanel();
    } else {
        const CwSidebar& side = active->game->menu->sections[active->sectionIndex].sidebars[active->sidebarIndex];
        // OOT's entrance options render in Combo > Entrances; strip them from the game's own
        // Randomizer sidebars so they keep a single home.
        RenderSidebarWidgets(side, *active->game,
                             active->kind == HubEntry::OOT_RANDO ? IsOotEntranceWidget : nullptr);
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
        // MM's Graphics/Audio/Controls/General are consolidated into the Shared tab — Graphics applies via
        // the single shared window, Controls via the shared gSettings.Controllers.* CVars, Audio via the
        // combo audio bridge (gSettings.Volume.* -> gSettings.Audio.*), and General is menu/engine settings
        // on shared gSettings.Menu.*/ImGuiScale CVars. Hide them from MM's per-game Settings section so they
        // live only in Shared; MM-specific sidebars (Overlay, Presets, ...) stay.
        if (!isOot && section && strcmp(section, "Settings") == 0 && sidebar &&
            (strcmp(sidebar, "Graphics") == 0 || strcmp(sidebar, "Audio") == 0 || strcmp(sidebar, "Controls") == 0 ||
             strcmp(sidebar, "General") == 0)) {
            return false;
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
    if (!mSearchQuery.empty()) {
        DrawSearchResults(mSearchQuery);
    } else if (!activeSide) {
        ImGui::TextUnformatted("Select an option.");
    } else {
        RenderSidebarWidgets(*activeSide, game);
    }
    ImGui::EndChild();
}
void ComboMenu::DrawComboPanel() {
    ResolveComboGenSyms();
    ImGui::TextWrapped("Generate a cross-world randomizer seed spanning OOT and MM. "
                       "You must Generate before starting a new file.");
    ImGui::Separator();

    // Seed field -> shared CVar the generator reads (same source the native file-select
    // "Generate a new seed" option uses).
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::InputTextWithHint("Seed", "(blank = random)", mSeedBuf, sizeof(mSeedBuf))) {
        CVarSetString("gGeneral.ComboSeed", mSeedBuf);
    }
    ImGui::SameLine();

    const ComboRando::ComboGenProgress* p = sGetProgress ? sGetProgress() : nullptr;
    const bool running = p && p->running.load();
    // Generation may only run at the OOT file-select screen, so the worker can't race a live game
    // tick (the prior off-thread crash class).
    const bool onFileSelect = sIsOnFileSelect && sIsOnFileSelect();
    const bool canGenerate = sTrigger && onFileSelect && !running;

    if (!canGenerate)
        ImGui::BeginDisabled();
    if (ImGui::Button("Generate")) {
        CVarSetString("gGeneral.ComboSeed", mSeedBuf);
        sTrigger(); // non-blocking: launcher spawns the worker; the main loop keeps running
    }
    if (!canGenerate)
        ImGui::EndDisabled();

    if (!sTrigger) {
        ImGui::TextUnformatted("Generate unavailable (soh.dll export not found).");
    } else if (!onFileSelect && !running) {
        ImGui::TextDisabled("Available on the file-select screen.");
    }

    // Live progress + result, polled from the launcher-owned progress each frame.
    if (p) {
        if (running) {
            int placed = p->placed.load();
            int total = p->total.load();
            float frac = total > 0 ? (float)placed / (float)total : 0.0f;
            ImGui::TextUnformatted(ComboGenProgress::PhaseLabel(p->phase.load()));
            ImGui::ProgressBar(frac, ImVec2(360.0f, 0.0f));
            ImGui::Text("%d / %d", placed, total);
        } else if (p->done.load()) {
            if (p->success.load()) {
                // Show the reproducible seed token (the input string, not the internal masterSeed
                // hash). Paste it into the Seed field + same settings to reproduce — shareable
                // without sharing a save file.
                ImGui::Text("Seed: %s", p->seedStr);
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy")) {
                    ImGui::SetClipboardText(p->seedStr);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Reuse")) {
                    std::strncpy(mSeedBuf, p->seedStr, sizeof(mSeedBuf) - 1);
                    mSeedBuf[sizeof(mSeedBuf) - 1] = '\0';
                    CVarSetString("gGeneral.ComboSeed", mSeedBuf);
                }
                ImGui::Text("OOT checks: %d", p->ootCheckCount.load());
                ImGui::Text("MM checks: %d", p->mmCheckCount.load());
                ImGui::Text("Cross-game placements: %d", p->foreignCount.load());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", p->error);
            }
        }
    }

    DrawHintSection();
}

// ComboShip: sphere "Get a hint" helper. When a combo save is active, loads that seed's playthrough
// from its per-slot consolidated file and, comparing against the checks obtained in BOTH games,
// reveals the next not-yet-collected step's location (one more per click). Low-spoiler: location only.
void ComboMenu::DrawHintSection() {
    int fn = sGetFileNum ? sGetFileNum() : -1;
    if (fn < 0)
        return; // no save loaded — hints are an in-game helper

    // (Re)load the active seed's playthrough when the slot changes.
    if (fn != mHintFileNum) {
        mHintFileNum = fn;
        mHintsRevealed = 0;
        mHintSteps.clear();
        try {
            auto path = ComboRando::SlotReadPath(fn);
            if (!path.empty()) {
                std::ifstream in(path);
                nlohmann::json j;
                in >> j;
                for (auto& sph : j.value("playthrough", nlohmann::json::array()))
                    for (auto& st : sph.value("steps", nlohmann::json::array()))
                        mHintSteps.emplace_back(st.value("game", std::string()), st.value("check", std::string()));
            }
        } catch (...) {}
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Sphere Hints");
    if (mHintSteps.empty()) {
        ImGui::TextDisabled("No playthrough available for this seed.");
        return;
    }

    // Obtained checks across BOTH games (active live + dormant from in-RAM save state).
    std::unordered_set<std::string> obtained;
    auto addFrom = [&](FnGetStr fn2) {
        if (!fn2)
            return;
        try {
            for (auto& n : nlohmann::json::parse(fn2()))
                obtained.insert(n.get<std::string>());
        } catch (...) {}
    };
    addFrom(sSohObtained);
    addFrom(sMmObtained);

    // Not-yet-collected steps, in playthrough (sphere) order.
    std::vector<const std::pair<std::string, std::string>*> uncollected;
    for (const auto& s : mHintSteps)
        if (!obtained.count(s.second))
            uncollected.push_back(&s);

    if (uncollected.empty()) {
        ImGui::TextDisabled("All playthrough checks collected.");
        return;
    }

    if (ImGui::Button("Get a hint") && mHintsRevealed < (int)uncollected.size())
        mHintsRevealed++;
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset hints"))
        mHintsRevealed = 0;

    int show = mHintsRevealed > (int)uncollected.size() ? (int)uncollected.size() : mHintsRevealed;
    for (int i = 0; i < show; ++i) {
        const auto& s = *uncollected[i];
        ImGui::BulletText("[%s] %s", s.first == "oot" ? "OOT" : "MM", s.second.c_str());
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

    // Item Tracker: shared appearance + game-swap manager (see ComboTrackerSwap.h).
    ComboTracker::SyncAppearance();
    ComboTracker::RegisterSwap();
}
