// combo/gui/ComboMenu.h
// ComboShip: combo-owned unified settings menu, hosted in comboui.dll.
#pragma once

#include <libultraship/libultraship.h>
#include <string>

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
    void DrawSharedPanel();            // ComboSharedPanel.cpp (later task)
    void DrawGamePanel(const char* gameKey); // delegates to SOH_/MM_DrawSettings (later task)
    void DrawComboPanel();             // Generate (later task)
};

} // namespace ComboRando

#ifdef _WIN32
extern "C" __declspec(dllexport) void ComboUI_Register(void);
#else
extern "C" void ComboUI_Register(void);
#endif
