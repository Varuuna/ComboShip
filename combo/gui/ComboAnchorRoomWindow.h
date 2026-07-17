// combo/gui/ComboAnchorRoomWindow.h
//
// ComboShip: combo-native floating Anchor room window. Replaces soh's AnchorRoomWindow (hidden
// in combo builds). Unions both games' roster snapshots (SOH_Anchor_GetRoster is authoritative; MM
// supplements its own players' area names via MM_Anchor_GetRoster) so a client shows EVERY player once
// with its real scene/area name — including other-game peers, which the soh window could not name.
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
