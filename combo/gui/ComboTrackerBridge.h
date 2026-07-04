// combo/gui/ComboTrackerBridge.h
//
// ComboShip-owned: one-way mirror making Item Tracker appearance coherent across both games.
// gCombo.Tracker.* is canonical (edited in the Combo Menu's Shared > Item Tracker panel); the
// per-game CVars (OOT gTrackers.ItemTracker.*, MM gSettings.ItemTracker.*) are derived. Per-game
// edits made in the games' own tracker settings windows are overwritten at the next sync — same
// accepted behavior as ComboAudioBridge. First run seeds the canonical values from OOT's current
// CVars so an existing user's setup isn't reset to defaults.
#pragma once

namespace ComboTracker {

// Canonical defaults, shared by the bridge and the Shared-panel widgets (single source; these
// match vanilla OOT's tracker defaults, the reference look).
inline constexpr int kDefaultIconSize = 36;
inline constexpr float kDefaultOpacity = 0.0f;
inline constexpr int kDefaultWindowType = 0;
inline constexpr int kDefaultDraggable = 0;

// Push all canonical gCombo.Tracker.* values into both games' CVars (seeding them from OOT on
// first run). Call after any combo tracker widget changes, at ComboUI_Register, and on
// foreground transitions.
void SyncAppearance();

} // namespace ComboTracker
