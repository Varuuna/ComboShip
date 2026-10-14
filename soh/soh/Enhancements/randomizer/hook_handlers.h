// ComboShip: combo-owned file that upstream does not have — do NOT delete on upstream merges (see
// docs/UPSTREAM_MERGES.md "hook_handlers.h re-added"). Holds only COMBO_BUILD-guarded declarations
// for cross-game randomizer consumers of hook_handlers.cpp.
#pragma once

#ifdef COMBO_BUILD
#include <cstdint>
#include "rando/CrossForeign.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/Enhancements/item-tables/ItemTableTypes.h"
// ComboShip: per-slot foreign-item lookup (defined in hook_handlers.cpp).
const ComboRando::ForeignItem* OOT_LookupForeign(int slot, const std::string& checkName);
// ComboShip: same lookup keyed by check, for callers that have no save/location context.
const ComboRando::ForeignItem* OOT_LookupForeignByCheck(RandomizerCheck rc);
// ComboShip: the foreign item's home-game container category, so Container Matches Contents doesn't
// render every foreign check as the junk sentinel. Defined in hook_handlers.cpp.
GetItemCategory OOT_GetForeignCategory(RandomizerCheck rc);
// ComboShip: generation of the foreign map; bumped on every rebuild so draw caches can invalidate.
uint64_t OOT_ForeignMapGen();
// ComboShip: currently-queued get-item check (RC_UNKNOWN_CHECK if none) — fallback identity for
// the foreign draw func (defined in hook_handlers.cpp).
RandomizerCheck OOT_GetQueuedDrawCheck();
// ComboShip: deliver a foreign OOT check's real item to its home game + Anchor share + toast.
// Called at grant time (Randomizer_Item_Give) so the foreign item flows through the normal get-item
// presentation; the caller skips the local grant. Defined in hook_handlers.cpp.
void OOT_DeliverForeign(RandomizerCheck rc);
#endif
