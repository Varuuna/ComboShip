// combo/gui/ComboTrackerSwap.h
//
// ComboShip-owned: swaps which game's Item/Check Tracker is displayed. Each tracker has a sticky
// selection CVar (gCombo.Tracker.ShownGame / gCombo.CheckTracker.ShownGame; -1 = follow
// foreground, 0 = OOT, 1 = MM), toggled by a click-hold on that tracker's body or the Combo
// Menu's "Tracker shows" combo; the matching HoldMomentary CVar makes the hold a momentary peek
// (reverts on release). The foreground game's tracker CVar stays the master on/off intent; this
// module only picks whose window renders. Reconciliation runs every frame from the registered
// swap window (drawn before either tracker — name-ordered Gui map).
#pragma once

namespace ComboTracker {

// Trackers the swap manages (indices into the internal config table).
enum SwapTrackerId { kSwapItem = 0, kSwapCheck = 1, kSwapCount = 2 };

// Register the logic-only swap window into the shared Gui and recover the true tracker CVars if a
// crash left mid-swap forced values persisted.
void RegisterSwap();

// Restore both games' true tracker CVars and deactivate. Call BEFORE the visibility manager's
// intent snapshot/restore (transitions) and before exit-time intent restore.
void SuspendSwap();

// Master "is this tracker on" switch for the Shared panel. Swap-aware: while a swap is active
// the foreground game's CVar is forced, so these go through the stashed true intent instead.
bool GetMasterVisible(int tracker);
void SetMasterVisible(int tracker, bool visible);

} // namespace ComboTracker
