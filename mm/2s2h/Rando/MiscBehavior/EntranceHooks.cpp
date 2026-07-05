#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h/Rando/Logic/EntranceShuffle.h"

extern "C" {
#include "functions.h"
#include "variables.h"
#include "z64scene.h"
}

namespace Rando {

namespace EntranceShuffle {

static RegisterShipInitFunc registerHooks(
    []() {
        // Hook into OnPlayDestroy which is called just before transitioning
        COND_HOOK(OnPlayDestroy, IsEntranceShuffleEnabled(), []() {
            // A nonzero respawnFlag means this transition is the engine reloading a previously-recorded
            // respawn point (void out, death, day-cycle reset, warp song landing, dev warp, etc.) rather
            // than the player walking through a real entrance. gSaveContext.save.entrance already holds
            // that fully-resolved "return to exactly this spot" value (set from Play_SetupRespawnPoint at
            // the time the spot was recorded, which itself already went through this same shuffle when it
            // was originally entered) - running it through GetShuffledEntrance again would send the player
            // to an unrelated shuffled destination instead of back to where they died/voided.
            // gSaveContext.respawnFlag is cleared to 0 in the next Play_Init (z_play.c), after this hook
            // runs, so it still reflects the trigger here.
            if (gSaveContext.respawnFlag != 0) {
                return;
            }

            // Grottos (and the Lone Peak Shrine, which shares the KAKUSIANA scene) always send the player
            // back to their scene's default spawn, which the game also reuses for overworld arrivals. None
            // of those entrances are shuffled, so remapping one here would fling the exit to a shuffled
            // destination and crash / drop the player through the world. Leaving that scene stays vanilla.
            if (gPlayState != NULL && gPlayState->sceneId == SCENE_KAKUSIANA) {
                return;
            }

            // Get the shuffled destination entrance
            s32 originalEntrance = gSaveContext.save.entrance;
            s32 shuffledEntrance = GetShuffledEntrance(originalEntrance);

            if (shuffledEntrance != originalEntrance) {
                gSaveContext.save.entrance = shuffledEntrance;

                // gSaveContext.nextCutsceneIndex is set independently of the entrance by dozens of call
                // sites (boss-defeat warps, credits sequences, NPC cutscenes) that pair one specific
                // vanilla entrance with one specific day/cutscene-variant index meant for that exact
                // destination. We just redirected the entrance to somewhere unrelated, so a leftover
                // index here is stale - and dangerous: Play_Init folds it into gSaveContext.sceneLayer,
                // which gets added directly into the entrance value passed to Entrance_GetTableEntry
                // (z_scene_table.c), an unchecked index into that spawn's per-layer-variant entry array.
                // Most spawns define only one such entry, so a leaked index reads garbage past the end of
                // that array as if it were a real {sceneId, spawnNum, flags}. Clearing it lets the new
                // scene's own Play_Init recompute sceneLayer fresh from its own state, which is always
                // safe (0, or a correct in-bounds value for the few scenes whose tables are sized for it).
                gSaveContext.nextCutsceneIndex = 0xFFEF;
            }
        });
    },
    { "IS_RANDO" });

} // namespace EntranceShuffle

} // namespace Rando
