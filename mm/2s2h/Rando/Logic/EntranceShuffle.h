#ifndef RANDO_ENTRANCE_SHUFFLE_H
#define RANDO_ENTRANCE_SHUFFLE_H

#include "Rando/Rando.h"
#include <map>
#include <vector>

extern "C" {
#include "functions.h"
#include "variables.h"
}

namespace Rando {

namespace EntranceShuffle {

// Structure to represent an entrance pair
struct EntrancePair {
    s32 entrance; // the entrance you take to enter Target area from source
    s32 exit;     // the exit you take to leave Target area to source
};

// Groups of entrances that can be shuffled together
enum EntrancePoolType {
    POOL_INTERIOR,  // Houses, shops, etc.
    POOL_GROTTO,    // Grottos and caves
    POOL_DUNGEON,   // Dungeon entrances
    POOL_OVERWORLD, // Overworld area connections
};

// Generate entrance shuffle mappings for a new seed
void ShuffleEntrances();

// Get the shuffled destination for an entrance
s32 GetShuffledEntrance(s32 originalEntrance);

// Get the original entrance from a shuffled one (for reverse lookups)
s32 GetOriginalEntrance(s32 shuffledEntrance);

// Check if entrance shuffle is enabled
bool IsEntranceShuffleEnabled();

#ifdef COMBO_BUILD
// ComboShip: true when the last ShuffleEntrances() exhausted its attempts (map may be disconnected).
bool LastShuffleFailed();
#endif

// True if the entrance belongs to the shared GROTTOS scene. Grottos are never shuffled and always
// return the player to their scene's vanilla spawn, so their exits must not be remapped.
bool IsGrottoEntrance(s32 entrance);

// Get all entrances in a specific pool
std::vector<EntrancePair> GetEntrancePool(EntrancePoolType poolType);

} // namespace EntranceShuffle

} // namespace Rando

#endif // RANDO_ENTRANCE_SHUFFLE_H
