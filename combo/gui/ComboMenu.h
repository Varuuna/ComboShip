// combo/gui/ComboMenu.h
// ComboShip: combo-owned unified settings menu, hosted in comboui.dll.
#pragma once

#include <libultraship/libultraship.h>
#include "ComboExport.h"
#include <string>
#include <map>
#include <utility>
#include <vector>
#include "gui/ComboGenProgress.h"

namespace ComboRando {

class ComboMenu final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    ~ComboMenu() override {
    }

    // Override GuiWindow::Draw so we render as a fullscreen overlay (like the port menu)
    // instead of a normal floating window. DrawElement opens its own window.
    void Draw() override;

    // Open the menu with the Randomizer tab active (file-select "Open Randomizer Settings").
    void OpenAtRandomizer();

  protected:
    void InitElement() override {
    }
    void DrawElement() override; // fullscreen window: scope-tab strip + active panel
    void UpdateElement() override {
    }

  private:
    void DrawSharedPanel();                  // hub for the Settings + Randomizer tabs (by mScope)
    void DrawGamePanel(const char* gameKey); // renders from the C-ABI model (ComboMenuModel + RenderWidget)
    void DrawComboPanel();                   // cross-world Generate (seed + button + progress)
    // Global menu search: scans BOTH games' CwMenu models for widgets whose name+tooltip match the
    // (normalized) query, rendering each match inline + editable (2-3 column grid) with a breadcrumb.
    // Drawn in the active scope's content area, so the left navigation panel stays visible.
    void DrawSearchResults(const std::string& query);

    char mSearchBuf[256] = { 0 }; // raw global-search input; normalized into mSearchQuery each frame
    std::string mSearchQuery;     // normalized search text this frame ("" = not searching)
    char mSeedBuf[128] = { 0 };
    ComboGenProgress mProgress; // worker (in the exe) writes; this polls. Pointer handed across DLL.
    std::string mStatusLine;
    bool mGeneratePending = false; // one-frame defer: show "Generating…" before blocking fill
    std::string mHubActive;        // active hub entry key ("group/label"); persists across frames
    std::string mScope;            // active top-level tab: "settings" | "randomizer" | "oot" | "mm"
    std::string mLastNavSig;       // last frame's nav location; a change clears the search box
    // Per-game two-level nav selection (SOH-style top header + left sidebar): gameKey -> (header,
    // sidebar). Combo-local — deliberately NOT the games' own gSettings.Menu.*SidebarSection CVars
    // (writing those would perturb the games' native menus). Persists across frames.
    std::map<std::string, std::pair<std::string, std::string>> mGameNav;
};

} // namespace ComboRando

extern "C" COMBO_EXPORT void ComboUI_Register(void);
