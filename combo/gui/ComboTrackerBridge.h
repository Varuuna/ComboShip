// combo/gui/ComboTrackerBridge.h
//
// ComboShip-owned: one-way mirror making tracker appearance coherent across both games.
// gCombo.Tracker.* / gCombo.CheckTracker.* are canonical (edited in the Combo Menu's Shared
// panels); the per-game CVars (OOT gTrackers.*, MM gSettings.ItemTracker.*) are derived. Per-game
// edits made in the games' own tracker settings windows are overwritten at the next sync — same
// accepted behavior as ComboAudioBridge. First run seeds the canonical values from OOT's current
// CVars so an existing user's setup isn't reset to defaults. The MM Check Tracker has no native
// window-type CVar; its COMBO_BUILD seam reads gCombo.CheckTracker.WindowType directly.
#pragma once

namespace ComboTracker {

// Canonical defaults, shared by the bridge and the Shared-panel widgets (single source; these
// match vanilla OOT's tracker defaults, the reference look).
inline constexpr int kDefaultIconSize = 36;
inline constexpr float kDefaultOpacity = 0.0f;
inline constexpr int kDefaultWindowType = 0;
inline constexpr int kDefaultDraggable = 0;
inline constexpr int kDefaultCheckWindowType = 1; // TRACKER_WINDOW_WINDOW — OOT's check tracker default

// Push all canonical gCombo.Tracker.* values into both games' CVars (seeding them from OOT on
// first run). Call after any combo tracker widget changes, at ComboUI_Register, and on
// foreground transitions.
void SyncAppearance();

} // namespace ComboTracker
