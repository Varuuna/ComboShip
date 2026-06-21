// combo/gui/ComboMenu.h
// ComboShip: combo-owned unified settings menu, hosted in comboui.dll.
#pragma once

#include <libultraship/libultraship.h>
#include <string>
#include <map>
#include <utility>
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

  protected:
    void InitElement() override {
    }
    void DrawElement() override; // fullscreen window: scope-tab strip + active panel
    void UpdateElement() override {
    }

  private:
    void DrawSharedPanel();                  // left-panel hub: engine + OOT Rando + MM Rando + Combo
    void DrawGamePanel(const char* gameKey); // renders from the C-ABI model (ComboMenuModel + RenderWidget)
    void DrawComboPanel();                   // cross-world Generate (seed + button + progress)

    char mSeedBuf[128] = { 0 };
    ComboGenProgress mProgress; // worker (in the exe) writes; this polls. Pointer handed across DLL.
    std::string mStatusLine;
    bool mGeneratePending = false; // one-frame defer: show "Generating…" before blocking fill
    std::string mHubActive;        // active hub entry key ("group/label"); persists across frames
    std::string mScope;            // active top-level scope: "shared" | "oot" | "mm"; persists
    // Per-game two-level nav selection (SOH-style top header + left sidebar): gameKey -> (header,
    // sidebar). Combo-local — deliberately NOT the games' own gSettings.Menu.*SidebarSection CVars
    // (writing those would perturb the games' native menus). Persists across frames.
    std::map<std::string, std::pair<std::string, std::string>> mGameNav;
};

} // namespace ComboRando

#ifdef _WIN32
extern "C" __declspec(dllexport) void ComboUI_Register(void);
#else
extern "C" void ComboUI_Register(void);
#endif
