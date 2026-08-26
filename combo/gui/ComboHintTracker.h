// combo/gui/ComboHintTracker.h
//
// ComboShip-owned unified Hint Tracker (issue #164). Replaces soh's OOT-only vendored hint tracker
// (hidden in combo builds): combo injects every OOT hint as a pre-rendered MESSAGE hint, so native's
// Journal/WotH/found views are dead, and MM never had a tracker at all.
//
// The window draws ONLY from plain strings the launcher pushes (ComboUI_SetHintTrackerData) — no
// ResourceManager or game-DLL dependency — so it renders under either foreground game without the
// dormant-draw machinery the icon trackers need. Read state is launcher-owned and per-slot
// (.combosav combo.hintsRead); the games report reveals through SOH_/MM_SetComboHintRevealCb.
#pragma once

#include <ship/window/gui/GuiWindow.h>

namespace ComboRando {

// Registered in ComboUI_Register alongside the other combo-owned windows.
void RegisterHintTrackerWindow();

// Settings > Hint Tracker panel (combo-owned; no per-game proxy block).
void DrawHintTrackerSharedPanel();

class ComboHintTrackerWindow final : public Ship::GuiWindow {
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
