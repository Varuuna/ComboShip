#include "core/ComboCrossItems.h"

#include <mutex>
#include <set>
#include <string>

#include "core/ComboAnchorNet.h"
#include "core/ComboDllApi.h"
#include "core/ComboGoal.h"
#include "rando/CrossWorldRando.h"

// Cross-game delivery dispatcher (issue #3). Registered into BOTH DLLs; invoked by the collector
// game's foreign-check detection (local) and the active game's Anchor receive handler (network).
// Grants into the TARGET game's resident save via its save-only export (target is usually the
// dormant game, so its save isn't mutating underneath us). See docs/deviations/rando.md.
static std::set<std::string> sAppliedCrossChecks; // dedup: the same wire packet can reach both DLLs
static uint32_t sCrossItemDedupSeed = 0;          // scoped per-seed (ResetCrossItemDedupForSeed)
// Reset runs on the generation worker; deliver runs on the game thread — both mutate the set.
static std::mutex sAppliedCrossChecksMutex;


// Clears the dedup set whenever the active seed changes (regen/new-file), so a check name reused
// across seeds isn't silently dropped as a stale "already delivered" duplicate.
void ResetCrossItemDedupForSeed(uint32_t seed) {
    std::lock_guard<std::mutex> lock(sAppliedCrossChecksMutex);
    if (seed != sCrossItemDedupSeed) {
        sAppliedCrossChecks.clear();
        sCrossItemDedupSeed = seed;
    }
}

void DeliverCrossItem(int targetGame, const char* itemName, const char* srcCheckName) {
    if (srcCheckName && srcCheckName[0] != '\0') {
        std::lock_guard<std::mutex> lock(sAppliedCrossChecksMutex);
        if (!sAppliedCrossChecks.insert(srcCheckName).second) {
            return; // already delivered for this check
        }
    }
    if (targetGame == 1) {
        if (MM_GrantCrossItem)
            MM_GrantCrossItem(itemName);
    } else {
        if (SOH_GrantCrossItem)
            SOH_GrantCrossItem(itemName);
    }
    // ComboShip (#136): the grant's own poke carries the TARGET game's fileNum, which is unbound (0xFF)
    // whenever that game is dormant, so it gets dropped. Re-poke here — the single choke point every
    // cross-grant (local collection in either game, Anchor receive, resync backfill) passes through —
    // with the launcher's loaded slot. Latched in Combo_OnTriforceProgress, so extra pokes are free.
    if (itemName && ComboRando::CwIsTriforcePiece((ComboRando::GameId)targetGame, itemName)) {
        Combo_OnTriforceProgress(targetGame, g_comboCompletionSlot);
    }
}

// A6: invoked each frame BY the active game (via the registered pump seam). Applies queued
// save-affecting co-op packets to the DORMANT game on the caller's (game) thread, so a teammate's
// collection registers in the dormant game's save live instead of only on next entry.
void PumpDormant() {
    // Finding 4: drain the on-connect resync here (game thread), once per connect.
    if (ComboAnchor::TakeResyncPending()) {
        if (SOH_Anchor_RequestResync)
            SOH_Anchor_RequestResync();
        if (MM_Anchor_RequestResync)
            MM_Anchor_RequestResync();
    }
    if (ComboAnchor::ActiveGame() == 1) {
        if (SOH_Anchor_PumpDormant)
            SOH_Anchor_PumpDormant(); // MM foreground -> apply to dormant OOT
    } else {
        if (MM_Anchor_PumpDormant)
            MM_Anchor_PumpDormant(); // OOT foreground -> apply to dormant MM
    }
}

// Network-receive idempotency: mark the SOURCE check obtained in the source game so this client
// won't later physically collect the same check and double-deliver. Save-only; persists.
void MarkForeignObtained(int srcGame, const char* checkName) {
    if (srcGame == 1) {
        if (MM_MarkForeignObtained)
            MM_MarkForeignObtained(checkName);
    } else {
        if (SOH_MarkForeignObtained)
            SOH_MarkForeignObtained(checkName);
    }
}
