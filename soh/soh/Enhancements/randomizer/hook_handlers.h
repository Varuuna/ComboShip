// ComboShip: combo-owned file that upstream does not have — do NOT delete on upstream merges (see
// docs/UPSTREAM_MERGES.md "hook_handlers.h re-added"). Holds only COMBO_BUILD-guarded declarations
// for cross-game randomizer consumers of hook_handlers.cpp.
#pragma once

#ifdef COMBO_BUILD
#include "rando/CrossForeign.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
// ComboShip: per-slot foreign-item lookup (defined in hook_handlers.cpp).
const ComboRando::ForeignItem* OOT_LookupForeign(int slot, const std::string& checkName);
// ComboShip: currently-queued get-item check (RC_UNKNOWN_CHECK if none) — fallback identity for
// the foreign draw func (defined in hook_handlers.cpp).
RandomizerCheck OOT_GetQueuedDrawCheck();
#endif
