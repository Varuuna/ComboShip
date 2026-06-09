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

  protected:
    void InitElement() override {}
    void DrawElement() override;       // draws the scope-tab strip + active panel
    void UpdateElement() override {}

  private:
    void DrawSharedPanel();            // ComboSharedPanel.cpp (later task)
    void DrawGamePanel(const char* gameKey); // delegates to SOH_/MM_DrawSettings (later task)
    void DrawComboPanel();             // Generate (later task)
};

} // namespace ComboRando

extern "C" __declspec(dllexport) void ComboUI_Register(void);
