// combo/gui/ComboTimersWindow.h
//
// ComboShip-owned overlay timers (issue #173). Replaces OOT's "Additional Timers" and MM's "Display
// Overlay", both hidden in combo builds: neither knew about the other game, so the play time a
// player saw only ever covered half the run.
//
// The total is the sum of each game's own play time (OOT's in-game timer + MM's filePlaytime), read
// live through SOH_/MM_ exports. Exactly one side advances at a time, so the total keeps moving as
// the player swaps. It tracks each game's live value, so a game whose in-memory save is re-read from
// the container can step back to its last saved time. The OOT-only rows (time of day, Navi,
// conditional) draw only while OOT is the foreground game.
//
// There is deliberately no real-time (RTA) row: MM's GetUnixTimestamp truncates through `long`, so
// its timestamps are not comparable with OOT's. See docs/deviations/ui-menu.md.
#pragma once

#include <ship/window/gui/GuiWindow.h>

namespace ComboRando {

// Registered in ComboUI_Register alongside the other combo-owned windows.
void RegisterTimersWindow();

// Settings > Timers panel (combo-owned; no per-game proxy block).
void DrawTimersSharedPanel();

class ComboTimersWindow final : public Ship::GuiWindow {
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
