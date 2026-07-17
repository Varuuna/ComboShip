// combo/gui/ComboAnchorRoomWindow.h
//
// ComboShip: combo-native floating Anchor room window. Replaces soh's AnchorRoomWindow (hidden in
// combo builds). Reads the launcher-owned always-live roster (set via ComboUI_SetAnchorRosterProvider)
// so every player shows once with a correct area name in BOTH games — even when a game is dormant.
// Area names resolved via the stateless SOH_/MM_Anchor_ResolveScene exports.
// Teleport is same-game only (button rendered only for players in the locally-active game).
#pragma once

#include <ship/window/gui/GuiWindow.h>

namespace ComboRando {

// Registered in ComboUI_Register alongside the tracker windows; toggled from the combo Anchor panel.
void RegisterAnchorRoomWindow();

class ComboAnchorRoomWindow final : public Ship::GuiWindow {
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

} // namespace ComboRando
