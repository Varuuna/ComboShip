// mm/2s2h/Rando/MiscBehavior/Keyrings.cpp
// ComboShip: Ship of Harkinian's "Key Rings" randomizer setting, for Majora's Mask. A temple's key ring replaces its
// small keys in the pool (Logic/GeneratePools.cpp) and gives all of them at once: the found-key count logic reads
// (rando.foundDungeonKeys) goes to the temple's total and the keys in hand grow by the same amount, so the Song of
// Time keeps restoring them like any found key (OnCycleSave.cpp).
#ifdef COMBO_BUILD

#include "SohSettings.h"
#include "2s2h/Rando/Rando.h"

extern "C" {
#include "variables.h"
}

namespace {

struct KeyringDef {
    RandoItemId keyring;
    s32 dungeonSceneIndex;
};

constexpr KeyringDef kKeyrings[] = {
    { RI_WOODFALL_KEYRING, DUNGEON_SCENE_INDEX_WOODFALL_TEMPLE },
    { RI_SNOWHEAD_KEYRING, DUNGEON_SCENE_INDEX_SNOWHEAD_TEMPLE },
    { RI_GREAT_BAY_KEYRING, DUNGEON_SCENE_INDEX_GREAT_BAY_TEMPLE },
    { RI_STONE_TOWER_KEYRING, DUNGEON_SCENE_INDEX_STONE_TOWER_TEMPLE },
};

s8 KeyTotal(s32 dungeonSceneIndex) {
    for (const auto& k : Rando::skeletonKeyCounts) {
        if (k.dungeonSceneIndex == dungeonSceneIndex) {
            return k.count;
        }
    }
    return 0;
}

} // namespace

s32 Rando::KeyringDungeon(RandoItemId randoItemId) {
    for (const auto& k : kKeyrings) {
        if (k.keyring == randoItemId) {
            return k.dungeonSceneIndex;
        }
    }
    return -1;
}

RandoItemId Rando::KeyringForDungeon(s32 dungeonSceneIndex) {
    for (const auto& k : kKeyrings) {
        if (k.dungeonSceneIndex == dungeonSceneIndex) {
            return k.keyring;
        }
    }
    return RI_UNKNOWN;
}

bool Rando::KeyringGive(RandoItemId randoItemId) {
    s32 index = KeyringDungeon(randoItemId);
    if (index < 0) {
        return false;
    }
    s8 total = KeyTotal(index);
    s8& inventory = DUNGEON_KEY_COUNT(index);
    s8& found = gSaveContext.save.shipSaveInfo.rando.foundDungeonKeys[index];
    // Both counters are -1 when fresh (Rando::AddSmallKey).
    s8 foundNow = (found < 0) ? 0 : found;
    if (foundNow < total) {
        inventory = (s8)(((inventory < 0) ? 0 : inventory) + (total - foundNow));
        found = total;
    }
    return true;
}

bool Rando::KeyringRemove(RandoItemId randoItemId) {
    s32 index = KeyringDungeon(randoItemId);
    if (index < 0) {
        return false;
    }
    DUNGEON_KEY_COUNT(index) = 0;
    gSaveContext.save.shipSaveInfo.rando.foundDungeonKeys[index] = 0;
    return true;
}

#endif
