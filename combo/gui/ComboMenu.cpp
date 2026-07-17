// combo/gui/ComboMenu.cpp
#include "ComboMenu.h"
#include "ComboMenuModel.h"
#include "ComboWidgetRender.h"
#include "ComboWidgetStyle.h"
#include "ComboForeground.h" // dormant-game gate for inline tracker popout buttons
#include "ComboTrackerBridge.h"
#include "ComboTrackerCommon.h" // kKinds (HideBackground CVars for the tracker panels)
#include "ComboTrackerSwap.h"
#include "ComboAnchorRoomWindow.h"  // combo-native floating Anchor room window
#include "rando/ComboPlaythrough.h" // plando: ParseSpoilerPlacements + Suffix/BuildForeignArray + slot paths
#include <imgui.h>
#include <libultraship/libultraship.h>         // CVar bridge (CVarGet/Set* incl. color) + color.h (Color_RGBA8)
#include <ship/window/gui/IconsFontAwesome4.h> // ICON_FA_* for the header action buttons
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <unordered_set>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
// soh.dll exports: trigger combo generation (non-blocking; spawns the launcher worker), read the
// shared progress, and gate generation to the file-select screen.
typedef void (*FnTriggerGenerate)(void);
typedef const ComboRando::ComboGenProgress* (*FnGetProgress)(void);
typedef unsigned char (*FnIsOnFileSelect)(void);
FnTriggerGenerate sTrigger = nullptr;
FnGetProgress sGetProgress = nullptr;
FnIsOnFileSelect sIsOnFileSelect = nullptr;
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
#endif
}

// Bug 2: Anchor resync exports, one per game DLL — resolved the same way as the combo-gen syms
// above. The button calls both so a resync pulls the peer's OOT AND MM team-state.
typedef void (*FnRequestResync)(void);
FnRequestResync sSohRequestResync = nullptr;
FnRequestResync sMmRequestResync = nullptr;
// Anchor connection panel: soh drives Enable/Disable + reports connection state (the socket
// is launcher-owned, but enable/status stays in soh's Anchor object). Resolved from soh.dll.
typedef void (*FnSetEnabled)(int);
typedef int (*FnGetConnState)(void);
FnSetEnabled sSohAnchorSetEnabled = nullptr;
FnGetConnState sSohAnchorGetConnState = nullptr;
// Room-admin: owner-gating + room-state broadcast + clear-team-state, all in soh's Anchor object.
typedef int (*FnGetOwnerInfo)(void);
typedef void (*FnSendRoomState)(void);
typedef void (*FnClearTeamState)(void);
FnGetOwnerInfo sSohAnchorGetOwnerInfo = nullptr;
FnSendRoomState sSohAnchorSendRoomState = nullptr;
FnClearTeamState sSohAnchorClearTeamState = nullptr;
void ResolveAnchorResyncSyms() {
#ifdef _WIN32
    if (HMODULE h = GetModuleHandleA("soh.dll")) {
        if (!sSohRequestResync)
            sSohRequestResync = (FnRequestResync)GetProcAddress(h, "SOH_Anchor_RequestResync");
        if (!sSohAnchorSetEnabled)
            sSohAnchorSetEnabled = (FnSetEnabled)GetProcAddress(h, "SOH_Anchor_SetEnabled");
        if (!sSohAnchorGetConnState)
            sSohAnchorGetConnState = (FnGetConnState)GetProcAddress(h, "SOH_Anchor_GetConnectionState");
        if (!sSohAnchorGetOwnerInfo)
            sSohAnchorGetOwnerInfo = (FnGetOwnerInfo)GetProcAddress(h, "SOH_Anchor_GetOwnerInfo");
        if (!sSohAnchorSendRoomState)
            sSohAnchorSendRoomState = (FnSendRoomState)GetProcAddress(h, "SOH_Anchor_SendRoomState");
        if (!sSohAnchorClearTeamState)
            sSohAnchorClearTeamState = (FnClearTeamState)GetProcAddress(h, "SOH_Anchor_ClearTeamState");
    }
    if (!sMmRequestResync) {
        if (HMODULE h = GetModuleHandleA("2ship.dll"))
            sMmRequestResync = (FnRequestResync)GetProcAddress(h, "MM_Anchor_RequestResync");
    }
#endif
}

// Shared Anchor config CVar keys (process-global libultraship store; both game DLLs read these — see
// MMAnchor.cpp). comboui has no access to soh's CVAR_REMOTE_ANCHOR macro, so the literals live here.
namespace AnchorCVar {
constexpr const char* Host = "gRemote.Anchor.Host";
constexpr const char* Port = "gRemote.Anchor.Port";
constexpr const char* Name = "gRemote.Anchor.Name";
constexpr const char* RoomId = "gRemote.Anchor.RoomId";
constexpr const char* TeamId = "gRemote.Anchor.TeamId";
constexpr const char* ColorValue = "gRemote.Anchor.Color.Value";
constexpr const char* ShowOnMinimap = "gRemote.Anchor.ShowOtherPlayersOnMinimap";
// Room-admin settings: shared store both games read; changes broadcast via SendRoomState.
constexpr const char* PvpMode = "gRemote.Anchor.RoomSettings.PvpMode";
constexpr const char* ShowLocationsMode = "gRemote.Anchor.RoomSettings.ShowLocationsMode";
constexpr const char* TeleportMode = "gRemote.Anchor.RoomSettings.TeleportMode";
constexpr const char* SyncItemsAndFlags = "gRemote.Anchor.RoomSettings.SyncItemsAndFlags";
} // namespace AnchorCVar

// Combo plandomizer exports: each game's static-data dump (friendly item names) + the reload trigger
// that plays an edited consolidated spoiler back verbatim. Resolved like the combo-gen syms above.
typedef const char* (*FnDump)(void);
typedef int (*FnRequestReload)(const char*);
typedef int (*FnGetActiveFileNum)(void);
FnDump sSohDump = nullptr;
FnDump sMmDump = nullptr;
FnRequestReload sRequestReload = nullptr;
FnGetActiveFileNum sGetActiveFileNum = nullptr;
void ResolvePlandoSyms() {
#ifdef _WIN32
    if (HMODULE h = GetModuleHandleA("soh.dll")) {
        if (!sSohDump)
            sSohDump = (FnDump)GetProcAddress(h, "SOH_DumpRandoStaticData");
        if (!sRequestReload)
            sRequestReload = (FnRequestReload)GetProcAddress(h, "SOH_RequestComboReload");
        if (!sGetActiveFileNum)
            sGetActiveFileNum = (FnGetActiveFileNum)GetProcAddress(h, "SOH_GetActiveFileNum");
    }
    if (HMODULE h = GetModuleHandleA("2ship.dll")) {
        if (!sMmDump)
            sMmDump = (FnDump)GetProcAddress(h, "MM_DumpRandoStaticData");
    }
#endif
}

// Combo plandomizer editable state (single ComboMenu instance -> file-static). rows is the edited
// placement model; loadedJson is the original consolidated file, preserved on write-back so only
// placements/foreign change. items is the combined per-game picker list (bare name + game + display).
struct PlandoPickItem {
    std::string name; // bare friendly name (item's own game namespace)
    ComboRando::GameId game;
    bool advancement;
    std::string display; // "name (OOT)" / "name (MM)"
};
struct PlandoState {
    bool loaded = false;
    bool statusError = false;
    std::string status;
    std::string loadedJson; // original consolidated file text (write-back base)
    std::string sohDump, mmDump;
    std::vector<ComboRando::CwPlacedItem> rows;
    std::vector<PlandoPickItem> items;
    std::vector<std::string> spoilerNames; // selectable spoilers in the Randomizer folder (display stems)
    std::vector<std::string> spoilerPaths; // parallel full paths
    int spoilerSel = -1;
    char filter[128] = { 0 };
    char pickerFilter[128] = { 0 };
};
static PlandoState sPlando;

// List every *.json in the Randomizer folder (PendingPath's parent) as a loadable spoiler; default the
// selection to Last-Generated when present.
void PlandoRefreshSpoilerList() {
    sPlando.spoilerNames.clear();
    sPlando.spoilerPaths.clear();
    std::error_code ec;
    auto dir = ComboRando::PendingPath().parent_path();
    if (std::filesystem::exists(dir, ec)) {
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (e.is_regular_file() && e.path().extension() == ".json") {
                sPlando.spoilerPaths.push_back(e.path().string());
                sPlando.spoilerNames.push_back(e.path().stem().string());
            }
        }
    }
    sPlando.spoilerSel = sPlando.spoilerPaths.empty() ? -1 : 0;
    for (int i = 0; i < (int)sPlando.spoilerNames.size(); i++) {
        if (sPlando.spoilerNames[i].find("Last-Generated") != std::string::npos) {
            sPlando.spoilerSel = i;
            break;
        }
    }
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
        // Center an inset menu block over the full-screen backdrop (mirrors SoH DrawElement: 90% /
        // 16:9 cap, centered) instead of filling the viewport edge-to-edge.
        ImGuiViewport* mvp = ImGui::GetMainViewport();
        const float ww = mvp->WorkSize.x, wh = mvp->WorkSize.y;
        ImVec2 menuSize(ww, wh);
        if (ww > 1280.0f) {
            menuSize.x = std::min(ww * 0.9f, wh * 1.77f);
        }
        if (wh > 800.0f) {
            menuSize.y = wh * 0.9f;
        }
        ImGui::SetCursorScreenPos(
            ImVec2(mvp->WorkPos.x + (ww - menuSize.x) * 0.5f, mvp->WorkPos.y + (wh - menuSize.y) * 0.5f));
        ImGui::BeginChild("##ComboMenuBlock", menuSize, 0, ImGuiWindowFlags_NoScrollbar);

        // Stylized scope selector (rounded theme buttons) — Shared / SoH / MM. mScope persists.
        struct Scope {
            const char* id;
            const char* label;
        };
        static const Scope kScopes[] = {
            { "settings", "Settings" },
            { "randomizer", "Randomizer" },
            { "oot", "Ship of Harkinian" },
            { "mm", "2 Ship 2 Harkinian" },
        };
        if (mScope.empty()) {
            mScope = "settings";
        }
        // Own ID scope so a scope tab (e.g. "Settings"/"Randomizer") doesn't collide with a per-game
        // section header of the same label (StyledNavButton derives its ImGui ID from the label).
        ImGui::PushID("##ComboScopeTabs");
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
        ImGui::PopID();
        // Compact search box on the header row, just after the last scope tab (~200px, like SoH).
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##ComboSearch", "Search...", mSearchBuf, sizeof(mSearchBuf));

        // Quit / Reset (always red, theme-independent) + Close (gray), right-aligned (mirrors SoH).
        {
            const ImVec4 red(0.55f, 0.0f, 0.0f, 1.0f), gray(0.45f, 0.45f, 0.45f, 1.0f);
            ComboMenu_PushButton(red);
            // Reserve using the PUSHED FramePadding (ComboMenu_PushButton widens it) so all three
            // buttons fit and the last (Close) isn't clipped off the right edge.
            const ImGuiStyle& st = ImGui::GetStyle();
            auto bw = [&](const char* ic) { return ImGui::CalcTextSize(ic).x + st.FramePadding.x * 2.0f; };
            const float total =
                bw(ICON_FA_POWER_OFF) + bw(ICON_FA_UNDO) + bw(ICON_FA_TIMES_CIRCLE) + st.ItemSpacing.x * 2.0f;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - total);
            if (ImGui::Button(ICON_FA_POWER_OFF)) {
                Ship::Context::GetInstance()->GetWindow()->Close();
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_UNDO)) {
                if (auto console = std::static_pointer_cast<Ship::ConsoleWindow>(
                        Ship::Context::GetInstance()->GetWindow()->GetGui()->GetGuiWindow("Console"))) {
                    console->Dispatch("reset");
                }
            }
            ComboMenu_PopButton();
            ImGui::SameLine();
            ComboMenu_PushButton(gray);
            if (ImGui::Button(ICON_FA_TIMES_CIRCLE)) {
                Hide();
            }
            ComboMenu_PopButton();
        }

        // Thick white divider under the header row (mirrors SoH DrawElement's header line).
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + 4.0f), IM_COL32(255, 255, 255, 255));
            ImGui::Dummy(ImVec2(w, 4.0f + ImGui::GetStyle().ItemSpacing.y));
        }

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
        ImGui::EndChild();
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
                        RenderWidget(wd, game, 90 / cols);
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
    enum Kind {
        ENGINE,
        OOT_RANDO,
        MM_RANDO,
        COMBO_GEN,
        COMBO_PLANDO,
        COMBO_TRACKER,
        COMBO_CHECK_TRACKER,
        COMBO_NETWORK
    } kind;
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
// Render one game's tracker sidebar inline (linear; the tracker sidebars are single-column).
// Structural skips: the sidebar's main-window toggle (identified by its CVar — the combo master
// supersedes it), its redundant header separator, and the dormant game's window buttons (a popout
// shown while its game is dormant would draw without that game's RM scope).
void RenderGameTrackerBlock(int gameIdx, int kind, const char* wantSection, const char* wantSidebar) {
    auto& model = ComboMenuModel::Get();
    const ComboRando::GameMenu& game = (gameIdx == 0) ? model.Oot() : model.Mm();
    if (!game.loaded || !game.menu) {
        ImGui::TextDisabled("Game menu not available yet.");
        return;
    }
    const char* mainTrackerCvar = ComboTracker::kTrackers[gameIdx][ComboTracker::kKinds[kind].column].cvar;
    const bool foreground = ComboUI::GetForegroundGame() == gameIdx;
    const CwMenu* m = game.menu;
    for (int s = 0; s < m->sectionCount; ++s) {
        const CwSection& sec = m->sections[s];
        if (!sec.sectionLabel || strcmp(sec.sectionLabel, wantSection) != 0)
            continue;
        for (int sb = 0; sb < sec.sidebarCount; ++sb) {
            const CwSidebar& side = sec.sidebars[sb];
            if (!side.sidebarName || strcmp(side.sidebarName, wantSidebar) != 0)
                continue;
            for (int i = 0; i < side.widgetCount; ++i) {
                const CwWidget& w = side.widgets[i];
                if (w.kind == CW_WINDOW_BUTTON) {
                    if (w.cvar && strcmp(w.cvar, mainTrackerCvar) == 0)
                        continue; // main tracker toggle — the combo master supersedes it
                    if (!foreground)
                        continue; // dormant popout — unsafe to open, its content is inline anyway
                }
                if (w.kind == CW_SEPARATOR_TEXT && w.name && strcmp(w.name, wantSidebar) == 0)
                    continue; // redundant "<sidebar name>" header inside our own panel
                ComboRando::RenderWidget(w, game);
            }
            return;
        }
    }
    ImGui::TextDisabled("Tracker settings not found in the game menu.");
}

// Common head of both tracker panels: master enable (both games) + hide-background + peek hint.
// Returns true if a CVar changed (caller persists).
bool DrawTrackerMasterHead(int kind, const char* label, const ImVec4& theme) {
    bool changed = false;
    bool shown = ComboTracker::GetMasterVisible(kind);
    // Icon-button toggle (matches SoH's window-toggle buttons), not a checkbox.
    std::string txt = std::string(shown ? ICON_FA_WINDOW_CLOSE : ICON_FA_EXTERNAL_LINK_SQUARE) + " " + label;
    ComboRando::ComboMenu_PushButton(theme);
    if (ImGui::Button(txt.c_str())) {
        ComboTracker::SetMasterVisible(kind, !shown);
        changed = true;
    }
    ComboRando::ComboMenu_PopButton();
    ImGui::TextDisabled("On for both games at once; each game's window keeps its own position.");

    bool hideB = CVarGetInteger(ComboTracker::kKinds[kind].hideBgCvar, 0) != 0;
    ComboRando::ComboMenu_PushCheckbox(theme);
    if (ImGui::Checkbox("Only show the active game's tracker", &hideB)) {
        CVarSetInteger(ComboTracker::kKinds[kind].hideBgCvar, hideB ? 1 : 0);
        changed = true;
    }
    ComboRando::ComboMenu_PopCheckbox();
    if (hideB) {
        ImGui::TextDisabled("Click and hold the tracker for half a second to peek at the other game's.");
    }
    return changed;
}

// Shared > Item Tracker: master visibility + combo-owned appearance CVars (mirrored into both
// games, see ComboTrackerBridge), then both games' own item-tracker settings inline.
void DrawTrackerSharedPanel() {
    const ImVec4 theme = ComboRando::ComboMenu_ThemeColor();
    bool changed = false;

    // Same narrow-column layout as the game pages (RenderSidebarWidgets): widgets in the first
    // cell of a two-column stretch table instead of spanning the whole panel.
    const ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("##trackercols", 2, tableFlags)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        changed |= DrawTrackerMasterHead(ComboTracker::kSwapItem, "Toggle Item Tracker", theme);

        ImGui::SeparatorText("Appearance (both games)");
        bool appearanceChanged = false;
        int px = CVarGetInteger("gCombo.Tracker.IconSize", ComboTracker::kDefaultIconSize);
        ComboRando::ComboMenu_PushSlider(theme);
        if (ImGui::SliderInt("Icon size (px)", &px, 16, 64)) {
            CVarSetInteger("gCombo.Tracker.IconSize", px);
            appearanceChanged = true;
        }
        float op = CVarGetFloat("gCombo.Tracker.Opacity", ComboTracker::kDefaultOpacity);
        if (ImGui::SliderFloat("Background opacity", &op, 0.0f, 1.0f, "%.2f")) {
            CVarSetFloat("gCombo.Tracker.Opacity", op);
            appearanceChanged = true;
        }
        ComboRando::ComboMenu_PopSlider();

        const char* kWindowTypes[] = { "Floating (overlay)", "Window" };
        int wt = CVarGetInteger("gCombo.Tracker.WindowType", ComboTracker::kDefaultWindowType);
        ComboRando::ComboMenu_PushCombobox(theme);
        if (ImGui::Combo("Window type", &wt, kWindowTypes, 2)) {
            CVarSetInteger("gCombo.Tracker.WindowType", wt);
            appearanceChanged = true;
        }
        ComboRando::ComboMenu_PopCombobox();

        int drag = CVarGetInteger("gCombo.Tracker.Draggable", ComboTracker::kDefaultDraggable);
        bool dragB = drag != 0;
        ComboRando::ComboMenu_PushCheckbox(theme);
        if (ImGui::Checkbox("Draggable (floating tracker accepts the mouse)", &dragB)) {
            CVarSetInteger("gCombo.Tracker.Draggable", dragB ? 1 : 0);
            appearanceChanged = true;
        }
        ComboRando::ComboMenu_PopCheckbox();
        if (appearanceChanged) {
            ComboTracker::SyncAppearance();
            changed = true;
        }

        ImGui::EndTable();
    }

    // Per-game detail settings, inline (full panel width — the settings tables are wide).
    ImGui::SeparatorText("Ocarina of Time");
    RenderGameTrackerBlock(0, ComboTracker::kSwapItem, "Randomizer", "Item Tracker");
    ImGui::SeparatorText("Majora's Mask");
    RenderGameTrackerBlock(1, ComboTracker::kSwapItem, "Rando", "Item Tracker");

    if (changed) {
        if (auto ctx = Ship::Context::GetInstance(); ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
            ctx->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    }
}

// Shared > Check Tracker: master visibility + shared window type, then both games' own
// check-tracker settings inline (colors/filters stay per game).
void DrawCheckTrackerSharedPanel() {
    const ImVec4 theme = ComboRando::ComboMenu_ThemeColor();
    bool changed = false;

    const ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("##checktrackercols", 2, tableFlags)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        changed |= DrawTrackerMasterHead(ComboTracker::kSwapCheck, "Toggle Check Tracker", theme);

        ImGui::SeparatorText("Appearance (both games)");
        const char* kWindowTypes[] = { "Floating (overlay)", "Window" };
        int wt = CVarGetInteger("gCombo.CheckTracker.WindowType", ComboTracker::kDefaultCheckWindowType);
        ComboRando::ComboMenu_PushCombobox(theme);
        if (ImGui::Combo("Window type", &wt, kWindowTypes, 2)) {
            CVarSetInteger("gCombo.CheckTracker.WindowType", wt);
            ComboTracker::SyncAppearance(); // mirrors into OOT's CVar; MM's seam reads it directly
            changed = true;
        }
        ComboRando::ComboMenu_PopCombobox();

        ImGui::EndTable();
    }

    // Per-game detail settings, inline (full panel width).
    ImGui::SeparatorText("Ocarina of Time");
    RenderGameTrackerBlock(0, ComboTracker::kSwapCheck, "Randomizer", "Check Tracker");
    ImGui::SeparatorText("Majora's Mask");
    RenderGameTrackerBlock(1, ComboTracker::kSwapCheck, "Rando", "Check Tracker");

    if (changed) {
        if (auto ctx = Ship::Context::GetInstance(); ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
            ctx->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    }
}

// Helper: seed a fixed char buffer from a string CVar for ImGui::InputText.
static void AnchorLoadStr(char* buf, size_t n, const char* cvar, const char* dflt) {
    const char* cur = CVarGetString(cvar, dflt);
    std::strncpy(buf, cur ? cur : "", n - 1);
    buf[n - 1] = '\0';
}

// Persist CVar writes to disk next frame (mirrors soh's Anchor menu, which saves after every edit).
static void AnchorSaveCVars() {
    if (auto ctx = Ship::Context::GetInstance(); ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
        ctx->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
}

// Combo Anchor panel: connection settings, owner-only room admin, and a toggle for the room window.
// Writes the shared gRemote.Anchor.* CVars and drives the games via the SOH_/MM_Anchor_* exports.
void DrawNetworkSharedPanel() {
    const ImVec4 theme = ComboRando::ComboMenu_ThemeColor();
    ResolveAnchorResyncSyms();

    int state = sSohAnchorGetConnState ? sSohAnchorGetConnState() : 0;
    bool enabled = (state & 1) != 0;
    bool connected = (state & 2) != 0;

    // --- Connection settings (disabled while enabled, like soh) ---
    ImGui::SeparatorText("Connection Settings");
    ImGui::BeginDisabled(enabled);

    char host[256], name[128], roomId[128], teamId[128];
    AnchorLoadStr(host, sizeof(host), AnchorCVar::Host, "anchor.hm64.org");
    AnchorLoadStr(name, sizeof(name), AnchorCVar::Name, "");
    AnchorLoadStr(roomId, sizeof(roomId), AnchorCVar::RoomId, "");
    AnchorLoadStr(teamId, sizeof(teamId), AnchorCVar::TeamId, "default");
    int port = CVarGetInteger(AnchorCVar::Port, 43383);

    ImGui::Text("Host");
    ComboRando::ComboMenu_PushInput(theme);
    if (ImGui::InputText("##AnchorHost", host, sizeof(host))) {
        CVarSetString(AnchorCVar::Host, host);
        AnchorSaveCVars();
    }
    ImGui::Text("Port");
    if (ImGui::InputInt("##AnchorPort", &port)) {
        if (port < 1025)
            port = 1025;
        if (port > 65534)
            port = 65534;
        CVarSetInteger(AnchorCVar::Port, port);
        AnchorSaveCVars();
    }
    ImGui::Text("Name");
    if (ImGui::InputText("##AnchorName", name, sizeof(name))) {
        CVarSetString(AnchorCVar::Name, name);
        AnchorSaveCVars();
    }
    ComboRando::ComboMenu_PopInput();

    // Color: shared gRemote.Anchor.Color.Value (Color type; MM reads it as Color24). RGB only, matching
    // soh's picker default {100,255,100}.
    Color_RGBA8 col = CVarGetColor(AnchorCVar::ColorValue, Color_RGBA8{ 100, 255, 100, 255 });
    float rgb[3] = { col.r / 255.0f, col.g / 255.0f, col.b / 255.0f };
    ImGui::Text("Color");
    ImGui::SameLine();
    if (ImGui::ColorEdit3("##AnchorColor", rgb, ImGuiColorEditFlags_NoInputs)) {
        col.r = (uint8_t)(rgb[0] * 255.0f + 0.5f);
        col.g = (uint8_t)(rgb[1] * 255.0f + 0.5f);
        col.b = (uint8_t)(rgb[2] * 255.0f + 0.5f);
        col.a = 255;
        CVarSetColor(AnchorCVar::ColorValue, col);
        AnchorSaveCVars();
    }

    ComboRando::ComboMenu_PushInput(theme);
    ImGui::Text("Room ID");
    if (ImGui::InputText("##AnchorRoomId", roomId, sizeof(roomId))) {
        CVarSetString(AnchorCVar::RoomId, roomId);
        AnchorSaveCVars();
    }
    ImGui::Text("Team ID (Items & Flags Shared)");
    if (ImGui::InputText("##AnchorTeamId", teamId, sizeof(teamId))) {
        CVarSetString(AnchorCVar::TeamId, teamId);
        AnchorSaveCVars();
    }
    ComboRando::ComboMenu_PopInput();

    ImGui::Spacing();
    ComboRando::ComboMenu_PushButton(theme);
    if (ImGui::Button("Restore Defaults", ImVec2(ImGui::GetContentRegionAvail().x / 2, 0))) {
        CVarSetString(AnchorCVar::Host, "anchor.hm64.org");
        CVarSetInteger(AnchorCVar::Port, 43383);
        CVarSetString(AnchorCVar::TeamId, "default");
        CVarSetString(AnchorCVar::RoomId, "");
        CVarSetString(AnchorCVar::Name, "");
        AnchorSaveCVars();
    }
    ImGui::SameLine();
    if (ImGui::Button("Global Room", ImVec2(-1.0f, 0.0f))) {
        CVarSetString(AnchorCVar::Host, "anchor.hm64.org");
        CVarSetInteger(AnchorCVar::Port, 43383);
        CVarSetString(AnchorCVar::TeamId, "default");
        CVarSetString(AnchorCVar::RoomId, "soh-global");
        AnchorSaveCVars();
    }
    ComboRando::ComboMenu_PopButton();
    ImGui::SetItemTooltip("Always-online public room so you don't have to experience Hyrule alone. "
                          "PVP and syncing are disabled.");
    ImGui::EndDisabled();

    // --- Enable/Disable (gated by form validity, mirroring soh's isFormValid) ---
    ImGui::Spacing();
    bool formValid = host[0] != '\0' && port > 1024 && port < 65535 && roomId[0] != '\0' && name[0] != '\0';
    ImGui::BeginDisabled(!formValid || sSohAnchorSetEnabled == nullptr);
    const ImVec4 red = { 0.66f, 0.13f, 0.13f, 1.0f };
    const ImVec4 green = { 0.15f, 0.55f, 0.20f, 1.0f };
    ComboRando::ComboMenu_PushButton(enabled ? red : green);
    if (ImGui::Button(enabled ? "Disable" : "Enable", ImVec2(-1.0f, 0.0f))) {
        if (sSohAnchorSetEnabled)
            sSohAnchorSetEnabled(enabled ? 0 : 1);
    }
    ComboRando::ComboMenu_PopButton();
    ImGui::EndDisabled();

    // --- Status line ---
    if (enabled) {
        if (connected) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Connected");
        } else {
            ImGui::TextDisabled("Connecting...");
        }
    }

    // --- Team sync (both games) + minimap toggle ---
    ImGui::SeparatorText("Team Sync");
    ImGui::TextWrapped("Pull the latest team progress from your Anchor teammates for BOTH games.");
    ComboRando::ComboMenu_PushButton(theme);
    if (ImGui::Button("Resync team state", ImVec2(-1.0f, 0.0f))) {
        if (sSohRequestResync)
            sSohRequestResync();
        if (sMmRequestResync)
            sMmRequestResync();
    }
    ComboRando::ComboMenu_PopButton();
    ImGui::TextDisabled("Use this if you're missing items/flags a teammate has collected in either game.");

    bool showMinimap = CVarGetInteger(AnchorCVar::ShowOnMinimap, 1) != 0;
    ComboRando::ComboMenu_PushCheckbox(theme);
    if (ImGui::Checkbox("Show Other Players on Minimap", &showMinimap)) {
        CVarSetInteger(AnchorCVar::ShowOnMinimap, showMinimap ? 1 : 0);
        AnchorSaveCVars();
    }
    ComboRando::ComboMenu_PopCheckbox();

    // --- Room admin (owner-only, non-global) ---
    // ownerInfo bit0=isOwner, bit1=isGlobalRoom; 0 unless connected. Mirrors soh's AnchorAdminMenu gate.
    int ownerInfo = sSohAnchorGetOwnerInfo ? sSohAnchorGetOwnerInfo() : 0;
    bool isOwner = (ownerInfo & 1) != 0;
    bool isGlobalRoom = (ownerInfo & 2) != 0;
    if (connected && isOwner && !isGlobalRoom) {
        ImGui::SeparatorText("Room Settings (Admin Only)");

        ComboRando::ComboMenu_PushButton(theme);
        if (ImGui::Button("Clear All Team State", ImVec2(-1.0f, 0.0f))) {
            if (sSohAnchorClearTeamState)
                sSohAnchorClearTeamState();
        }
        ComboRando::ComboMenu_PopButton();

        auto sendRoomState = [&]() {
            if (sSohAnchorSendRoomState)
                sSohAnchorSendRoomState();
        };

        const char* kPvpModes[] = { "Off", "On", "On + Friendly Fire" };
        const char* kShowLocationsModes[] = { "None", "Team Only", "All" };
        const char* kTeleportModes[] = { "None", "Team Only", "All" };

        ComboRando::ComboMenu_PushCombobox(theme);
        int pvp = CVarGetInteger(AnchorCVar::PvpMode, 1);
        if (ImGui::Combo("PvP Mode", &pvp, kPvpModes, 3)) {
            CVarSetInteger(AnchorCVar::PvpMode, pvp);
            AnchorSaveCVars();
            sendRoomState();
        }
        int showLoc = CVarGetInteger(AnchorCVar::ShowLocationsMode, 1);
        if (ImGui::Combo("Show Locations For", &showLoc, kShowLocationsModes, 3)) {
            CVarSetInteger(AnchorCVar::ShowLocationsMode, showLoc);
            AnchorSaveCVars();
            sendRoomState();
        }
        int teleport = CVarGetInteger(AnchorCVar::TeleportMode, 1);
        if (ImGui::Combo("Allow Teleporting To", &teleport, kTeleportModes, 3)) {
            CVarSetInteger(AnchorCVar::TeleportMode, teleport);
            AnchorSaveCVars();
            sendRoomState();
        }
        ComboRando::ComboMenu_PopCombobox();

        bool syncItems = CVarGetInteger(AnchorCVar::SyncItemsAndFlags, 1) != 0;
        ComboRando::ComboMenu_PushCheckbox(theme);
        if (ImGui::Checkbox("Sync Items & Flags", &syncItems)) {
            CVarSetInteger(AnchorCVar::SyncItemsAndFlags, syncItems ? 1 : 0);
            AnchorSaveCVars();
            sendRoomState();
        }
        ComboRando::ComboMenu_PopCheckbox();
    }

    // --- Room window: toggle the combo-native floating roster window ---
    ImGui::SeparatorText("Room");
    bool roomOpen = CVarGetInteger("gCombo.Anchor.RoomWindow", 0) != 0;
    ComboRando::ComboMenu_PushButton(theme);
    if (ImGui::Button(roomOpen ? "Hide Room Window" : "Show Room Window", ImVec2(-1.0f, 0.0f))) {
        roomOpen = !roomOpen;
        CVarSetInteger("gCombo.Anchor.RoomWindow", roomOpen ? 1 : 0);
        AnchorSaveCVars();
        if (auto ctx = Ship::Context::GetInstance(); ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
            if (auto win = ctx->GetWindow()->GetGui()->GetGuiWindow("Anchor Room")) {
                if (roomOpen)
                    win->Show();
                else
                    win->Hide();
            }
        }
    }
    ComboRando::ComboMenu_PopButton();
    ImGui::TextDisabled("Shows every teammate's game and area, with same-game teleport.");
}

// Build the combined item picker list from both games' static-data dumps (items[] = full item table
// with friendly name + advancement). Each game's items get its own "(OOT)"/"(MM)" display tag.
void PlandoBuildItems() {
    sPlando.items.clear();
    auto add = [&](const std::string& dump, ComboRando::GameId g, const char* suf) {
        try {
            auto d = nlohmann::json::parse(dump);
            std::unordered_set<std::string> seen;
            for (auto& it : d.value("items", nlohmann::json::array())) {
                std::string n = it.value("name", std::string{});
                if (n.empty() || !seen.insert(n).second)
                    continue;
                sPlando.items.push_back({ n, g, it.value("advancement", true), n + suf });
            }
        } catch (...) {}
    };
    add(sPlando.sohDump, ComboRando::GAME_OOT, " (OOT)");
    add(sPlando.mmDump, ComboRando::GAME_MM, " (MM)");
}

// Load the current pending/slot consolidated spoiler into the editable model (the #73 fix — the
// vendored OOT plando can't parse the consolidated schema). Flattens oot/mm.placements + foreign[]
// into the shape ParseSpoilerPlacements expects, resolving foreign checks to their real cross-game item.
void PlandoLoad() {
    ResolvePlandoSyms();
    sPlando.loaded = false;
    sPlando.rows.clear();
    // Load the spoiler picked in the dropdown; fall back to the active slot's newest / pending file.
    std::filesystem::path path;
    if (sPlando.spoilerSel >= 0 && sPlando.spoilerSel < (int)sPlando.spoilerPaths.size()) {
        path = sPlando.spoilerPaths[sPlando.spoilerSel];
    } else {
        int slot = sGetActiveFileNum ? sGetActiveFileNum() : -1;
        path = (slot >= 0) ? ComboRando::SlotReadPath(slot) : std::filesystem::path{};
        if (path.empty())
            path = ComboRando::PendingPath();
    }
    std::ifstream in(path);
    if (!in.is_open()) {
        sPlando.status = "No generated seed found to load.";
        sPlando.statusError = true;
        return;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    sPlando.loadedJson = ss.str();
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(sPlando.loadedJson);
    } catch (...) {
        sPlando.status = "Failed to parse the spoiler file.";
        sPlando.statusError = true;
        return;
    }
    if (j.value("fileType", std::string()) != "ComboShipRandomizer") {
        sPlando.status = "Not a ComboShip seed (regenerate with this build).";
        sPlando.statusError = true;
        return;
    }
    sPlando.sohDump = sSohDump ? sSohDump() : "";
    sPlando.mmDump = sMmDump ? sMmDump() : "";
    PlandoBuildItems();

    nlohmann::json flat;
    flat["oot"] = j.value("oot", nlohmann::json::object()).value("placements", nlohmann::json::object());
    flat["mm"] = j.value("mm", nlohmann::json::object()).value("placements", nlohmann::json::object());
    flat["foreign"] = j.value("foreign", nlohmann::json::array());
    sPlando.rows = ComboRando::ParseSpoilerPlacements(flat.dump(), sPlando.sohDump, sPlando.mmDump);
    std::sort(sPlando.rows.begin(), sPlando.rows.end(), [](const auto& a, const auto& b) {
        if (a.checkGame != b.checkGame)
            return a.checkGame < b.checkGame;
        return a.check < b.check;
    });
    sPlando.loaded = true;
    sPlando.statusError = false;
    sPlando.status = "Loaded " + std::to_string(sPlando.rows.size()) + " placements from " + path.filename().string();
}

// Serialize the edited model back to the consolidated schema and play it verbatim. Preserves the
// original file and replaces only oot/mm.placements + foreign[], mirroring the generator's emit
// (SuffixCrossGameItems on native collisions + BuildForeignArray for cross-game entries).
void PlandoSavePlay() {
    ResolvePlandoSyms();
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(sPlando.loadedJson);
    } catch (...) {
        sPlando.status = "Internal parse error.";
        sPlando.statusError = true;
        return;
    }

    nlohmann::json ootPl = nlohmann::json::object();
    nlohmann::json mmPl = nlohmann::json::object();
    nlohmann::json foreignRaw = nlohmann::json::array();
    for (const auto& r : sPlando.rows) {
        // The check's own game stores the REAL foreign item's bare name; the sentinel is injected at
        // apply time by Combo_OnReloadRequest, never written here.
        (r.checkGame == ComboRando::GAME_OOT ? ootPl : mmPl)[r.check] = r.item;
        if (r.checkGame != r.itemGame) {
            foreignRaw.push_back({ { "checkGame", r.checkGame == ComboRando::GAME_OOT ? "oot" : "mm" },
                                   { "checkName", r.check },
                                   { "itemGame", r.itemGame == ComboRando::GAME_OOT ? "oot" : "mm" },
                                   { "itemName", r.item },
                                   { "displayName", r.item }, // BuildForeignArray appends the game suffix
                                   { "advancement", r.advancement } });
        }
    }
    // Native cross-game name collisions get their own-game suffix; foreign checks are skipped (their
    // real item travels in foreign[]). Exactly the generator's write path.
    ComboRando::SuffixCrossGameItems(ootPl, mmPl, foreignRaw, sPlando.sohDump, sPlando.mmDump);
    nlohmann::json foreign = ComboRando::BuildForeignArray(foreignRaw);

    j["oot"]["placements"] = ootPl;
    j["mm"]["placements"] = mmPl;
    j["foreign"] = foreign;

    std::error_code ec;
    std::filesystem::create_directories(ComboRando::ConsolidatedDir(), ec);
    auto path = ComboRando::PendingPath();
    {
        std::ofstream out(path, std::ios::trunc);
        if (!out.is_open()) {
            sPlando.status = "Failed to write the pending file.";
            sPlando.statusError = true;
            return;
        }
        out << j.dump(2);
    }
    sPlando.loadedJson = j.dump(2); // keep in sync for a follow-up edit
    if (!sRequestReload) {
        sPlando.status = "Reload export unavailable (soh.dll not loaded).";
        sPlando.statusError = true;
        return;
    }
    int ok = sRequestReload(path.string().c_str());
    if (ok) {
        sPlando.status = "Saved. Start a file to play it.";
        sPlando.statusError = false;
    } else {
        sPlando.status = "Reload rejected (be on the file-select screen; no generation running).";
        sPlando.statusError = true;
    }
}

// Combo > Plandomizer: load a generated combo seed, edit item placements (native OR cross-game),
// then Save & Play. Placements-only MVP; prices/settings/tricks/hints are preserved read-only.
void DrawComboPlandoPanel() {
    const ImVec4 theme = ComboRando::ComboMenu_ThemeColor();
    ResolvePlandoSyms();
    ResolveComboGenSyms(); // sIsOnFileSelect gates Save & Play (reload mutates save state)
    ImGui::TextWrapped("Load a spoiler from the Randomizer folder, edit item placements (assign any item to "
                       "any check in either game, including cross-game), then Save. Use on the file-select screen.");
    ImGui::Separator();

    if (sPlando.spoilerPaths.empty())
        PlandoRefreshSpoilerList(); // lazy first fill; the Refresh button re-scans on demand

    // Spoiler picker: every *.json in the Randomizer folder is loadable, not just Last-Generated.
    const char* preview = (sPlando.spoilerSel >= 0 && sPlando.spoilerSel < (int)sPlando.spoilerNames.size())
                              ? sPlando.spoilerNames[sPlando.spoilerSel].c_str()
                              : "(no spoilers found)";
    ImGui::SetNextItemWidth(360.0f);
    ComboRando::ComboMenu_PushCombobox(theme);
    if (ImGui::BeginCombo("##plandospoiler", preview)) {
        for (int i = 0; i < (int)sPlando.spoilerNames.size(); i++) {
            if (ImGui::Selectable(sPlando.spoilerNames[i].c_str(), i == sPlando.spoilerSel))
                sPlando.spoilerSel = i;
        }
        ImGui::EndCombo();
    }
    ComboRando::ComboMenu_PopCombobox();
    ImGui::SameLine();
    ComboRando::ComboMenu_PushButton(theme);
    if (ImGui::Button("Refresh"))
        PlandoRefreshSpoilerList();
    ImGui::SameLine();
    if (ImGui::Button("Load"))
        PlandoLoad();
    ComboRando::ComboMenu_PopButton();

    if (sPlando.loaded) {
        ImGui::SameLine();
        const bool onFileSelect = sIsOnFileSelect && sIsOnFileSelect();
        const bool canPlay = sRequestReload && onFileSelect;
        if (!canPlay)
            ImGui::BeginDisabled();
        ComboRando::ComboMenu_PushButton(theme);
        if (ImGui::Button("Save"))
            PlandoSavePlay();
        ComboRando::ComboMenu_PopButton();
        if (!canPlay)
            ImGui::EndDisabled();
        if (sRequestReload && !onFileSelect) {
            ImGui::SameLine();
            ImGui::TextDisabled("(available on the file-select screen)");
        }
    }

    if (!sPlando.status.empty()) {
        ImVec4 c = sPlando.statusError ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
        ImGui::TextColored(c, "%s", sPlando.status.c_str());
    }
    if (!sPlando.loaded)
        return;

    ImGui::Separator();
    ImGui::SetNextItemWidth(320.0f);
    ComboRando::ComboMenu_PushInput(theme);
    ImGui::InputTextWithHint("##plandofilter", "Filter by check or item name...", sPlando.filter,
                             sizeof(sPlando.filter));
    ComboRando::ComboMenu_PopInput();

    const std::string q = NormalizeSearch(sPlando.filter);

    ImGui::BeginChild("##plandotable", ImVec2(0, 0));
    const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##plandorows", 3, tf)) {
        ImGui::TableSetupColumn("Game", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("Check", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (int i = 0; i < (int)sPlando.rows.size(); ++i) {
            auto& r = sPlando.rows[i];
            if (!q.empty() && NormalizeSearch(r.check + r.item).find(q) == std::string::npos)
                continue;
            const bool cross = r.checkGame != r.itemGame;
            const char* itag = r.itemGame == ComboRando::GAME_OOT ? " (OOT)" : " (MM)";
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(r.checkGame == ComboRando::GAME_OOT ? "OOT" : "MM");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(r.check.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(i);
            std::string label = r.item + itag;
            if (cross) // highlight cross-game placements (the harness's whole point)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
            ComboRando::ComboMenu_PushButton(theme);
            if (ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f))) {
                sPlando.pickerFilter[0] = '\0';
                ImGui::OpenPopup("##itempicker");
            }
            ComboRando::ComboMenu_PopButton();
            if (cross)
                ImGui::PopStyleColor();
            if (ImGui::BeginPopup("##itempicker")) {
                ImGui::SetNextItemWidth(300.0f);
                ImGui::InputTextWithHint("##pickfilter", "Search items...", sPlando.pickerFilter,
                                         sizeof(sPlando.pickerFilter));
                const std::string pq = NormalizeSearch(sPlando.pickerFilter);
                ImGui::BeginChild("##pickerlist", ImVec2(340.0f, 380.0f));
                for (const auto& pit : sPlando.items) {
                    if (!pq.empty() && NormalizeSearch(pit.name).find(pq) == std::string::npos)
                        continue;
                    if (ImGui::Selectable(pit.display.c_str())) {
                        r.item = pit.name;
                        r.itemGame = pit.game;
                        r.advancement = pit.advancement;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndChild();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
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

    // Two top-level tabs share this panel: "Settings" (engine + combo trackers) and "Randomizer"
    // (OOT/MM rando settings + combo Generate). mScope selects which set to build.
    const bool randoScope = (mScope == "randomizer");
    if (!randoScope) {
        std::vector<HubEntry> e;
        // "Mod Menu" and "Presets" are OOT-specific (per-game mods/presets) — they live only in
        // the Ship of Harkinian tab, so omit them here to avoid duplicating them in Settings.
        AppendSectionEntries(e, "Settings", HubEntry::ENGINE, model.Oot(), "Settings", { "Mod Menu", "Presets" });
        // Combo-owned Item Tracker panel: shared appearance + game-swap selection.
        HubEntry trk;
        trk.label = "Item Tracker";
        trk.group = "Settings";
        trk.kind = HubEntry::COMBO_TRACKER;
        e.push_back(std::move(trk));
        // Combo-owned Check Tracker panel: master visibility + game-swap selection.
        HubEntry chk;
        chk.label = "Check Tracker";
        chk.group = "Settings";
        chk.kind = HubEntry::COMBO_CHECK_TRACKER;
        e.push_back(std::move(chk));
        if (!e.empty())
            groups.push_back({ "Settings", std::move(e) });
        // Network group: the Anchor team-sync control (covers BOTH games) plus the Ship of Harkinian
        // Network settings (Sail, Crowd Control) surfaced from the OOT menu model — moved here from the
        // OOT tab so all network settings live in one shared place.
        {
            std::vector<HubEntry> netE;
            // Anchor panel = SoH Network "Anchor" settings + the team-sync resync (both combo-owned draw).
            HubEntry anchor;
            anchor.label = "Anchor";
            anchor.group = "Network";
            anchor.kind = HubEntry::COMBO_NETWORK;
            netE.push_back(std::move(anchor));
            // The Anchor sidebar is drawn inside the panel above; surface only the other SoH Network
            // sidebars (Sail, Crowd Control) as their own entries.
            AppendSectionEntries(netE, "Network", HubEntry::ENGINE, model.Oot(), "Network", { "Anchor" });
            groups.push_back({ "Network", std::move(netE) });
        }
    } else {
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
            std::vector<HubEntry> e;
            HubEntry gen;
            gen.label = "Generate";
            gen.group = "Combo";
            gen.kind = HubEntry::COMBO_GEN;
            e.push_back(std::move(gen));
            HubEntry pl;
            pl.label = "Plandomizer";
            pl.group = "Combo";
            pl.kind = HubEntry::COMBO_PLANDO;
            e.push_back(std::move(pl));
            groups.push_back({ "Combo", std::move(e) });
        }
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

    // Left navigation panel — the whole sidebar renders at 1.2x to match SoH's fontStandardLargest
    // (~24px) header/sidebar tier (comboui has no separate bold font in the shared atlas). Width fits
    // the widest label at that scale, floored at 200px and capped at half the panel.
    float availW = ImGui::GetContentRegionAvail().x;
    const ImGuiStyle& sbSt = ImGui::GetStyle();
    float sbNeed = 0.0f;
    for (const auto& g : groups) {
        sbNeed = std::max(sbNeed, ImGui::CalcTextSize(g.label.c_str()).x);
        for (const auto& en : g.entries)
            sbNeed = std::max(sbNeed, ImGui::CalcTextSize(en.label.c_str()).x);
    }
    sbNeed = sbNeed * 1.2f + 20.0f; // 1.2x sidebar scale + button padding
    float sidebarW = std::max(availW > 1600 ? availW * 0.15f : 200.0f, sbNeed + sbSt.ScrollbarSize + 8.0f);
    sidebarW = std::min(sidebarW, availW * 0.5f);
    ImGui::BeginChild("##HubSidebar", ImVec2(sidebarW, 0), 0); // no box border (SoH doesn't box the sidebar)
    ImGui::SetWindowFontScale(1.2f);                           // match SoH's 24px header/sidebar tier
    for (const auto& g : groups) {
        // Group divider header — only meaningful with multiple groups (e.g. the Randomizer tab's
        // OOT/MM/Combo). A single-group tab (Settings) would just repeat the tab name, so skip it.
        if (groups.size() > 1) {
            ImGui::SeparatorText(g.label.c_str());
        }
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
    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndChild();

    ImGui::SameLine();
    // Thick white vertical divider between sidebar and content (mirrors SoH's second line).
    {
        ImVec2 dv = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(dv, ImVec2(dv.x + 4.0f, dv.y + ImGui::GetContentRegionAvail().y),
                                                  IM_COL32(255, 255, 255, 255));
    }
    ImGui::Dummy(ImVec2(4.0f, 0.0f));
    ImGui::SameLine();

    // Right content panel for the active entry.
    ImGui::BeginChild("##HubContent", ImVec2(0, 0));
    if (!mSearchQuery.empty()) {
        DrawSearchResults(mSearchQuery);
    } else if (!active) {
        ImGui::TextUnformatted("Select an option.");
    } else if (active->kind == HubEntry::COMBO_GEN) {
        DrawComboPanel();
    } else if (active->kind == HubEntry::COMBO_PLANDO) {
        DrawComboPlandoPanel();
    } else if (active->kind == HubEntry::COMBO_TRACKER) {
        DrawTrackerSharedPanel();
    } else if (active->kind == HubEntry::COMBO_CHECK_TRACKER) {
        DrawCheckTrackerSharedPanel();
    } else if (active->kind == HubEntry::COMBO_NETWORK) {
        DrawNetworkSharedPanel();
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
        // OOT Network (Sail, Crowd Control) is surfaced in the shared Network group next to the Anchor
        // team-sync control, so hide it from the OOT per-game tab.
        if (isOot && section && strcmp(section, "Network") == 0) {
            return false;
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
        // Rando settings live in Shared, Item/Check Tracker sidebars in the Shared tracker panels;
        // only Entrance/Hint Tracker (OOT-only, no shared panel yet) remain here.
        const char* randoSec = isOot ? "Randomizer" : "Rando";
        if (section && strcmp(section, randoSec) == 0) {
            return sidebar && (strcmp(sidebar, "Entrance Tracker") == 0 || strcmp(sidebar, "Hint Tracker") == 0);
        }
        return true;
    };

    // A section with no shown sidebars (e.g. the rando section after the tracker move) has no
    // content here — drop it from the header strip and the active-section resolution.
    auto sectionShown = [&](const CwSection& sec) -> bool {
        for (int sb = 0; sb < sec.sidebarCount; ++sb) {
            if (sidebarShown(sec.sectionLabel, sec.sidebars[sb].sidebarName))
                return true;
        }
        return false;
    };

    // (activeHeader, activeSidebar) for this game; persists across frames.
    auto& nav = mGameNav[gameKey];

    // Resolve the active section: prior pick if still present, else the first section.
    const CwSection* activeSec = nullptr;
    const CwSection* firstSec = nullptr;
    for (int s = 0; s < m->sectionCount; ++s) {
        const CwSection& sec = m->sections[s];
        if (!sectionShown(sec))
            continue;
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
        if (!sectionShown(sec))
            continue;
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
    // Fit the sidebar to its widest shown entry at the 1.2x sidebar scale (floored at 200px, capped
    // at half the panel) — matches SoH's fontStandardLargest (~24px) header/sidebar tier.
    const ImGuiStyle& sbSt = ImGui::GetStyle();
    float sbNeed = 0.0f;
    for (int sb = 0; sb < activeSec->sidebarCount; ++sb) {
        const CwSidebar& side = activeSec->sidebars[sb];
        if (!sidebarShown(activeSec->sectionLabel, side.sidebarName))
            continue;
        const char* lbl = (side.sidebarName && side.sidebarName[0]) ? side.sidebarName : "Section";
        sbNeed = std::max(sbNeed, ImGui::CalcTextSize(lbl).x);
    }
    sbNeed = sbNeed * 1.2f + 20.0f; // 1.2x sidebar scale + button padding
    float sidebarW = std::max(availW > 1600 ? availW * 0.15f : 200.0f, sbNeed + sbSt.ScrollbarSize + 8.0f);
    sidebarW = std::min(sidebarW, availW * 0.5f);
    ImGui::BeginChild("##GameSidebar", ImVec2(sidebarW, 0), 0); // no box border (SoH doesn't box the sidebar)
    ImGui::SetWindowFontScale(1.2f);                            // match SoH's 24px header/sidebar tier
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
    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndChild();

    ImGui::SameLine();
    // Thick white vertical divider between sidebar and content (mirrors SoH's second line).
    {
        ImVec2 dv = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(dv, ImVec2(dv.x + 4.0f, dv.y + ImGui::GetContentRegionAvail().y),
                                                  IM_COL32(255, 255, 255, 255));
    }
    ImGui::Dummy(ImVec2(4.0f, 0.0f));
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

    // Combo-native floating Anchor room window (toggled from the Anchor panel).
    ComboRando::RegisterAnchorRoomWindow();
}
