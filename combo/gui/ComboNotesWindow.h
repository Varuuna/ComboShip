// combo/gui/ComboNotesWindow.h
//
// ComboShip (#165): combo-native cross-game Personal Notes window. Replaces soh's OOT-only item
// tracker notes (compiled out in combo builds). One note per save slot, stored by the launcher as
// combo.notes in the slot's .combosav (accessors pushed in via ComboUI_SetNotesStore), so the same
// text is editable from either game and survives OOT<->MM switches.
#pragma once

#include <ship/window/gui/GuiWindow.h>

namespace ComboNotes {

// Registered in ComboUI_Register alongside the tracker windows; toggled from the Shared tracker panel.
void RegisterWindow();

bool WindowShown();
void SetWindowShown(bool shown);

// Persist pending edits now (window close, slot change, debounce, game switch, shutdown).
void FlushPending();

} // namespace ComboNotes

class ComboNotesWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void Draw() override;

  protected:
    void InitElement() override {
    }
    void UpdateElement() override {
    }
    void DrawElement() override;
};
