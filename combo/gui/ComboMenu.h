// combo/gui/ComboMenu.h
// ComboShip: combo-owned unified settings menu, hosted in comboui.dll.
#pragma once

#include <libultraship/libultraship.h>
#include <string>
#include "gui/ComboGenProgress.h"

namespace ComboRando {

class ComboMenu final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    ~ComboMenu() override {}

    // Override GuiWindow::Draw so we render as a fullscreen overlay (like the port menu)
    // instead of a normal floating window. DrawElement opens its own window.
    void Draw() override;

  protected:
    void InitElement() override {}
    void DrawElement() override;       // fullscreen window: scope-tab strip + active panel
    void UpdateElement() override {}

  private:
    void DrawSharedPanel();            // delegates to OOT's themed engine sidebars
    void DrawGamePanel(const char* gameKey); // delegates to SOH_/MM_DrawSettings
    void DrawComboPanel();             // cross-world Generate (seed + button + progress)

    char             mSeedBuf[128] = { 0 };
    ComboGenProgress mProgress;        // worker (in the exe) writes; this polls. Pointer handed across DLL.
    std::string      mStatusLine;
    bool             mGeneratePending = false; // one-frame defer: show "Generating…" before blocking fill
};

} // namespace ComboRando

#ifdef _WIN32
extern "C" __declspec(dllexport) void ComboUI_Register(void);
#else
extern "C" void ComboUI_Register(void);
#endif
