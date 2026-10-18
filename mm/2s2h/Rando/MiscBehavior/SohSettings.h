#ifndef RANDO_MISC_BEHAVIOR_SOH_SETTINGS_H
#define RANDO_MISC_BEHAVIOR_SOH_SETTINGS_H
// ComboShip: Ship of Harkinian's randomizer settings ported to Majora's Mask: "Infinite Upgrades"
// (InfiniteUpgrades.cpp), "Bombchu Bag" (BombchuBag.cpp) and "Key Rings" (Keyrings.cpp). The shop price settings
// are generation-only (Logic/ShopPrices.cpp). See docs/deviations/rando.md.
#ifdef COMBO_BUILD

#include "2s2h/Rando/Types.h"

namespace Rando {

// Infinite Upgrades. For a progressive id, the infinite item its next copy grants, or RI_UNKNOWN when it still grants
// a normal tier (or the setting is off).
RandoItemId InfiniteUpgradeFor(RandoItemId progressive);
// True when a progressive family with an infinite tier has nothing left to give (the infinite item is owned).
// Only meaningful with the setting on; callers fall back to their tier check otherwise.
bool InfiniteUpgradesOn();
bool InfiniteUpgradeOwned(RandoItemId itemId);
// Grants / removes an infinite item: raises its upgrade to the last tier and sets the flag. Save writes only, so the
// headless logic oracle can use it too. False for any other item.
bool InfiniteUpgradesGive(RandoItemId randoItemId);
bool InfiniteUpgradesRemove(RandoItemId randoItemId);

// Bombchu Bag. On: bombchus need a bag of their own, the bomb bag no longer carries them.
bool BombchuBagOn();
// Bag tier owned (0 = none, 1-3 = 20/30/40; a Single bag is tier 3).
s32 BombchuBagLevel();
// The tier a Bombchu Bag copy grants now, or RI_UNKNOWN once every bag is owned.
RandoItemId BombchuBagNext();
bool BombchuBagGive(RandoItemId randoItemId);
bool BombchuBagRemove(RandoItemId randoItemId);

// Key Rings. The dungeon scene index a key ring opens (DUNGEON_SCENE_INDEX_*), or -1 for any other item.
s32 KeyringDungeon(RandoItemId randoItemId);
// The key ring for a dungeon scene index, or RI_UNKNOWN.
RandoItemId KeyringForDungeon(s32 dungeonSceneIndex);
bool KeyringGive(RandoItemId randoItemId);
bool KeyringRemove(RandoItemId randoItemId);

} // namespace Rando

// z_parameter.c / z_en_girla.c / z_kaleido_item.c: bombchu capacity and whether a bag is required.
extern "C" s32 Combo_Rando_BombchuBagOn(void);
extern "C" s32 Combo_Rando_BombchuCapacity(void);

#endif
#endif
