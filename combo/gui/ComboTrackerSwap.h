// combo/gui/ComboTrackerSwap.h
//
// ComboShip-owned: per-frame reconcile of the both-games tracker model. Each tracker kind has a
// master enable (both games' windows show, each with its own ImGui identity/rect) and a
// HideBackground option (only the foreground game's window draws). In HideBackground mode a
// click-hold on the visible tracker's body momentarily also shows the dormant game's tracker
// (peek; reverts on release). Per-game visibility CVars are DERIVED from this model every frame —
// nothing here needs crash recovery. Reconciliation runs from the registered logic-only window
// (drawn before either tracker — name-ordered Gui map).
#pragma once

namespace ComboTracker {

// Tracker kinds (indices into ComboTrackerCommon.h kKinds).
enum SwapTrackerId { kSwapItem = 0, kSwapCheck = 1, kSwapCount = 2 };

// Register the logic-only reconcile window into the shared Gui, clear the retired swap-era CVars,
// and seed the master enables from the per-game CVars an existing config may carry.
void RegisterSwap();

// Master "is this tracker on" switch (both games) for the combo menu panels.
bool GetMasterVisible(int tracker);
void SetMasterVisible(int tracker, bool visible);

} // namespace ComboTracker
