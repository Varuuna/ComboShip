// combo/gui/ComboTrackerSwap.h
//
// ComboShip-owned: swaps which game's Item Tracker is displayed. gCombo.Tracker.ShownGame is the
// sticky selection (-1 = follow foreground, 0 = OOT, 1 = MM), toggled by a 1-second click-hold on
// the tracker body or the Combo Menu's "Tracker shows" combo; gCombo.Tracker.HoldMomentary makes
// the hold a momentary peek (reverts on release). The foreground game's tracker CVar stays the
// master on/off intent; this module only picks whose window renders. Reconciliation runs every
// frame from the registered swap window (drawn before either tracker — name-ordered Gui map).
#pragma once

namespace ComboTracker {

// Register the logic-only swap window into the shared Gui and recover the true tracker CVars if a
// crash left mid-swap forced values persisted.
void RegisterSwap();

// Restore both games' true tracker CVars and deactivate. Call BEFORE the visibility manager's
// intent snapshot/restore (transitions) and before exit-time intent restore.
void SuspendSwap();

// Master "is the Item Tracker on" switch for the Shared panel. Swap-aware: while a swap is active
// the foreground game's CVar is forced, so these go through the stashed true intent instead.
bool GetMasterVisible();
void SetMasterVisible(bool visible);

} // namespace ComboTracker
