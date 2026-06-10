/* combo/menu/ComboMenuDrawContent.h
 * ComboShip-owned shared body for the games' Menu::DrawContent — renders one game's menu
 * (header row / sidebar / content columns, mirroring the games' upstream DrawElement layout)
 * inside comboui's already-open window, with onlyPaths/skipPaths filtering. Was previously
 * copy-pasted in soh/soh/SohGui/Menu.cpp and mm/2s2h/BenGui/Menu.cpp.
 *
 * TU-GLUE HEADER: include from the game's Menu.cpp AFTER imgui_internal.h (GImGui, ImVec2
 * math operators), libultraship (CVar bridge, Ship::Context) and the game's UIWidgets are
 * in scope. Not standalone.
 *
 * Per-game Hooks contract (static members; resolves each game's own UIWidgets/draw functions):
 *   struct Hooks {
 *       static bool HeaderEntry(const std::string& label);   // ModernMenuHeaderEntry
 *       static bool SidebarEntry(const std::string& label);  // ModernMenuSidebarEntry
 *       static void PushStyleButton(TTheme theme);           // UIWidgets::PushStyleButton
 *       static void PopStyleButton();                        // UIWidgets::PopStyleButton
 *       static void DrawItem(Widget& w, int labelWidth, TTheme theme); // MenuDrawItem
 *       static void RunUpdateFuncs(const std::string& header, const std::string& section);
 *   };
 */
#ifndef COMBO_MENU_DRAW_CONTENT_H
#define COMBO_MENU_DRAW_CONTENT_H

// struct Config below uses ImFont* at parse time, not just at template instantiation —
// fail loudly with the contract instead of a confusing "unknown type name ImFont".
#ifndef IMGUI_VERSION
#error "ComboMenuDrawContent.h is TU-glue: include imgui.h / imgui_internal.h (and libultraship) before it"
#endif

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace ComboMenuDraw {

struct Config {
    const char* idPrefix;      // "SOH" / "MM" — prefix for ImGui child-window IDs
    const char* headerCvar;    // CVar storing the active header label
    const char* defaultHeader; // fallback header label (both games: "Settings")
    ImFont* headerFont;        // pushed for the header row + sidebar (fontStandardLargest)
    ImFont* contentFont;       // pushed for the content columns (MM: fontMonoLarger); nullptr = none
};

// The caller (the game's Menu::DrawContent) has already done its per-frame setup: the
// foreground guard, the disabledMap evaluation pass, and theme/member updates.
template <typename THooks, typename TMenuEntries, typename TTheme>
inline void DrawContent(const Config& cfg, TTheme themeIndex, const std::vector<std::string>& menuOrder,
                        TMenuEntries& menuEntries, const std::set<std::string>& onlyPaths,
                        const std::set<std::string>& skipPaths) {
    // onlyPaths non-empty → allow-list mode; empty → skip-list mode.
    auto headerVisible = [&](const std::string& h) -> bool {
        if (!menuEntries.count(h)) {
            return false;
        }
        if (!onlyPaths.empty()) {
            // Header shown iff onlyPaths contains "H" or any "H/..." entry.
            if (onlyPaths.count(h)) {
                return true;
            }
            const std::string prefix = h + "/";
            for (auto& p : onlyPaths) {
                if (p.rfind(prefix, 0) == 0) {
                    return true;
                }
            }
            return false;
        }
        return skipPaths.count(h) == 0;
    };

    auto sidebarVisible = [&](const std::string& h, const std::string& s) -> bool {
        if (!onlyPaths.empty()) {
            // If onlyPaths has bare "H" with no "H/..." entries, show all sidebars.
            bool hasSidebarEntries = false;
            const std::string prefix = h + "/";
            for (auto& p : onlyPaths) {
                if (p.rfind(prefix, 0) == 0) {
                    hasSidebarEntries = true;
                    break;
                }
            }
            if (!hasSidebarEntries) {
                return true; // bare "H" in onlyPaths → all sidebars
            }
            return onlyPaths.count(h + "/" + s) > 0;
        }
        return skipPaths.count(h + "/" + s) == 0;
    };

    std::string headerIndex = CVarGetString(cfg.headerCvar, cfg.defaultHeader);

    // Build visible header list; fall back to first if stored header is filtered out.
    std::vector<std::string> visibleHeaders;
    for (auto& label : menuOrder) {
        if (headerVisible(label)) {
            visibleHeaders.push_back(label);
        }
    }
    if (visibleHeaders.empty()) {
        ImGui::TextUnformatted("No settings.");
        return;
    }
    if (std::find(visibleHeaders.begin(), visibleHeaders.end(), headerIndex) == visibleHeaders.end()) {
        headerIndex = visibleHeaders.front();
        CVarSetString(cfg.headerCvar, headerIndex.c_str());
    }

    // -----------------------------------------------------------------------
    // Themed rendering — mirrors the games' DrawElement layout without the
    // Begin/End wrapper (comboui's outer window is already open around us),
    // search bar, or quit/reset/close buttons.
    // -----------------------------------------------------------------------
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiStyle& style = ImGui::GetStyle();

    // Use available content area for sizing (DrawElement sizes off viewport WorkSize).
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float availW = avail.x > 0 ? avail.x : 800.0f;
    float availH = avail.y > 0 ? avail.y : 600.0f;

    ImGui::PushFont(cfg.headerFont);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));

    // Compute header row height from text + padding (mirrors DrawElement).
    ImVec2 firstSize = ImGui::CalcTextSize(visibleHeaders.front().c_str());
    float headerHeight = firstSize.y + style.FramePadding.y * 2;

    std::string blockId = std::string(cfg.idPrefix) + " Menu Block";
    std::string headerChildId = std::string(cfg.idPrefix) + " Header Selection";

    ImVec2 pos = window->DC.CursorPos;
    ImGui::SetNextWindowPos(pos);
    ImGui::BeginChild(blockId.c_str(), ImVec2(availW, availH),
                      ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

    // Header selection child (no close/quit buttons — comboui handles those).
    ImGui::SetNextWindowSizeConstraints({ 0, headerHeight }, { availW, headerHeight });
    ImGui::BeginChild(headerChildId.c_str(), ImVec2(availW, headerHeight),
                      ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_HorizontalScrollbar);
    for (size_t i = 0; i < visibleHeaders.size(); ++i) {
        if (i != 0) {
            ImGui::SameLine();
        }
        const std::string& label = visibleHeaders[i];
        // Defer the selection change to AFTER the Pop (mirrors DrawElement) — otherwise a click
        // flips headerIndex mid-iteration and the guarded PopStyleColor is skipped (ImGui assert).
        std::string nextHeader = "";
        THooks::PushStyleButton(themeIndex);
        if (headerIndex != label) {
            ImGui::PushStyleColor(ImGuiCol_Button, { 0, 0, 0, 0 });
        }
        if (THooks::HeaderEntry(menuEntries.at(label).label)) {
            nextHeader = label;
            CVarSetString(cfg.headerCvar, label.c_str());
            CVarSave();
        }
        if (headerIndex != label) {
            ImGui::PopStyleColor();
        }
        THooks::PopStyleButton();
        if (!nextHeader.empty()) {
            headerIndex = nextHeader;
        }
    }
    ImGui::EndChild();

    // Divider line below header row (mirrors DrawElement).
    {
        ImGuiWindow* blk = g.CurrentWindow;
        ImVec2 lp = blk->DC.CursorPos;
        blk->DrawList->AddRectFilled(lp, lp + ImVec2{ availW, 4 }, ImGui::GetColorU32({ 255, 255, 255, 255 }), true,
                                     style.WindowRounding);
        ImGui::Dummy(ImVec2(availW, 4));
    }

    // Sidebar + content area layout (mirrors DrawElement).
    float sectionHeight = availH - headerHeight - 4 - style.ItemSpacing.y * 2;
    float columnHeight = sectionHeight - style.ItemSpacing.y * 4;
    float sidebarWidth = 200 - style.ItemSpacing.x;
    if (availW > 1600) {
        sidebarWidth = availW * 0.15f;
    }

    auto& entry = menuEntries.at(headerIndex);
    const char* rawSidebarCvar = entry.sidebarCvar;
    std::string sidebarCvarStr = rawSidebarCvar ? rawSidebarCvar : "";
    std::string sectionIndex = sidebarCvarStr.empty() ? "" : CVarGetString(sidebarCvarStr.c_str(), "");

    // Build visible sidebars list for active header.
    std::vector<std::string> visibleSidebars;
    for (auto& s : entry.sidebarOrder) {
        if (sidebarVisible(headerIndex, s)) {
            visibleSidebars.push_back(s);
        }
    }
    if (visibleSidebars.empty()) {
        ImGui::PopFont();
        ImGui::PopStyleVar();
        ImGui::EndChild();
        return;
    }
    if (std::find(visibleSidebars.begin(), visibleSidebars.end(), sectionIndex) == visibleSidebars.end()) {
        sectionIndex = visibleSidebars.front();
        if (!sidebarCvarStr.empty()) {
            CVarSetString(sidebarCvarStr.c_str(), sectionIndex.c_str());
        }
    }

    ImGui::SetNextWindowSizeConstraints({ sidebarWidth, 0 }, { sidebarWidth, columnHeight });
    ImGui::BeginChild((entry.label + " Section").c_str(), { sidebarWidth, columnHeight * 3 },
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize, ImGuiWindowFlags_NoTitleBar);
    for (auto& sidebarLabel : visibleSidebars) {
        // Defer the selection change to AFTER the Pop (mirrors DrawElement) — otherwise a click
        // flips sectionIndex mid-iteration and the guarded PopStyleColor is skipped (ImGui assert).
        std::string nextSection = "";
        THooks::PushStyleButton(themeIndex);
        if (sectionIndex != sidebarLabel) {
            ImGui::PushStyleColor(ImGuiCol_Button, { 0, 0, 0, 0 });
        }
        if (THooks::SidebarEntry(sidebarLabel)) {
            nextSection = sidebarLabel;
            if (!sidebarCvarStr.empty()) {
                CVarSetString(sidebarCvarStr.c_str(), sidebarLabel.c_str());
            }
            CVarSave();
        }
        if (sectionIndex != sidebarLabel) {
            ImGui::PopStyleColor();
        }
        THooks::PopStyleButton();
        if (!nextSection.empty()) {
            sectionIndex = nextSection;
        }
    }
    ImGui::EndChild();
    ImGui::PopFont(); // pop headerFont — content area uses content/default font (matches DrawElement)

    if (cfg.contentFont) {
        ImGui::PushFont(cfg.contentFont); // MM: fontMonoLarger for content (mirrors MM DrawElement)
    }

    // Vertical divider between sidebar and content columns.
    {
        ImGuiWindow* blk = g.CurrentWindow;
        ImVec2 dvPos = blk->DC.CursorPos;
        blk->DrawList->AddRectFilled(dvPos, dvPos + ImVec2{ 4, sectionHeight - style.FramePadding.y * 2 },
                                     ImGui::GetColorU32({ 255, 255, 255, 255 }), true, style.WindowRounding);
    }

    // Content columns (mirrors DrawElement).
    float sectionWidth = availW - sidebarWidth - 4 - style.ItemSpacing.x * 4;
    std::string sectionMenuId = sectionIndex + " Settings";
    auto& sb = entry.sidebars.at(sectionIndex);
    int columns = (int)sb.columnCount;
    size_t columnFuncs = sb.columnWidgets.size();
    if (availW < 800) {
        columns = 1;
    }
    float columnWidth = (sectionWidth - style.ItemSpacing.x * columns) / columns;
    bool useColumns = columns > 1;

    // Run per-section update funcs before drawing widgets, as DrawElement does.
    THooks::RunUpdateFuncs(entry.label, sectionIndex);

    ImGui::SameLine();
    if (!useColumns) {
        ImGui::SetNextWindowSizeConstraints({ sectionWidth, 0 }, { sectionWidth, columnHeight });
        ImGui::BeginChild(sectionMenuId.c_str(), { sectionWidth, availH * 4 }, ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoTitleBar);
    }
    for (size_t i = 0; i < columnFuncs; i++) {
        std::string sectionId = sectionMenuId + " Column " + std::to_string(i);
        if (useColumns) {
            ImGui::SetNextWindowSizeConstraints({ columnWidth, 0 }, { columnWidth, columnHeight });
            ImGui::BeginChild(sectionId.c_str(), { columnWidth, availH * 4 }, ImGuiChildFlags_AutoResizeY,
                              ImGuiWindowFlags_NoTitleBar);
        }
        for (auto& w : sb.columnWidgets.at(i)) {
            THooks::DrawItem(w, 90 / std::max<int>(columns, 1), themeIndex);
        }
        if (useColumns) {
            ImGui::EndChild();
        }
        if (i < (size_t)columns - 1) {
            ImGui::SameLine();
        }
    }
    if (!useColumns) {
        ImGui::EndChild();
    }

    if (cfg.contentFont) {
        ImGui::PopFont(); // pop contentFont
    }
    ImGui::PopStyleVar(); // FramePadding
    ImGui::EndChild();    // Menu Block
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

} // namespace ComboMenuDraw

#endif // COMBO_MENU_DRAW_CONTENT_H
