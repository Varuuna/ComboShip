// mm/2s2h/Rando/MiscBehavior/BombchuBag.cpp
// ComboShip: Ship of Harkinian's "Bombchu Bag" randomizer setting (soh's Context::HandleGetBombchuBag), for
// Majora's Mask, where the bomb bag normally carries the bombchus too.
//
// - None: vanilla MM, a bomb bag holds as many bombchus as bombs.
// - Single Bag: one Bombchu Bag in the pool; bombchus need it, and it holds 40.
// - Progressive Bags: three Bombchu Bags in the pool, holding 20, 30 and 40 bombchus (MM's bomb bag sizes).
//
// Either way the bomb bag stops carrying bombchus and bombchu refills need a bag. The bag is drawn as Ocarina of
// Time's Bombchu Bag (MM has none of its own). The owned tiers are RandoInf flags; a bombchu in the inventory
// without one (a starting Bombchu item) counts as the first bag.
#ifdef COMBO_BUILD

#include "SohSettings.h"
#include "2s2h/Rando/Rando.h"

extern "C" {
#include "variables.h"
#include "functions.h"
#include "macros.h"
}

namespace {

constexpr u16 kBagCapacity[] = { 0, 20, 30, 40 };

} // namespace

bool Rando::BombchuBagOn() {
    return RANDO_SAVE_OPTIONS[RO_BOMBCHU_BAG] != RO_BOMBCHU_BAG_NONE;
}

s32 Rando::BombchuBagLevel() {
    if (Flags_GetRandoInf(RANDO_INF_OBTAINED_BOMBCHU_BAG_40)) {
        return 3;
    }
    if (RANDO_SAVE_OPTIONS[RO_BOMBCHU_BAG] == RO_BOMBCHU_BAG_SINGLE) {
        return (INV_CONTENT(ITEM_BOMBCHU) == ITEM_BOMBCHU) ? 3 : 0;
    }
    if (Flags_GetRandoInf(RANDO_INF_OBTAINED_BOMBCHU_BAG_30)) {
        return 2;
    }
    if (Flags_GetRandoInf(RANDO_INF_OBTAINED_BOMBCHU_BAG_20) || INV_CONTENT(ITEM_BOMBCHU) == ITEM_BOMBCHU) {
        return 1;
    }
    return 0;
}

RandoItemId Rando::BombchuBagNext() {
    s32 level = BombchuBagLevel();
    if (RANDO_SAVE_OPTIONS[RO_BOMBCHU_BAG] == RO_BOMBCHU_BAG_SINGLE) {
        return (level == 0) ? RI_BOMBCHU_BAG_40 : RI_UNKNOWN;
    }
    switch (level) {
        case 0:
            return RI_BOMBCHU_BAG_20;
        case 1:
            return RI_BOMBCHU_BAG_30;
        case 2:
            return RI_BOMBCHU_BAG_40;
        default:
            return RI_UNKNOWN;
    }
}

bool Rando::BombchuBagGive(RandoItemId randoItemId) {
    switch (randoItemId) {
        case RI_BOMBCHU_BAG_40:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_BOMBCHU_BAG_40);
            [[fallthrough]];
        case RI_BOMBCHU_BAG_30:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_BOMBCHU_BAG_30);
            [[fallthrough]];
        case RI_BOMBCHU_BAG_20:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_BOMBCHU_BAG_20);
            INV_CONTENT(ITEM_BOMBCHU) = ITEM_BOMBCHU;
            AMMO(ITEM_BOMBCHU) = (s8)Combo_Rando_BombchuCapacity();
            return true;
        case RI_PROGRESSIVE_BOMBCHU_BAG: {
            RandoItemId next = BombchuBagNext();
            return (next != RI_UNKNOWN) && BombchuBagGive(next);
        }
        default:
            return false;
    }
}

bool Rando::BombchuBagRemove(RandoItemId randoItemId) {
    switch (randoItemId) {
        case RI_BOMBCHU_BAG_20:
        case RI_BOMBCHU_BAG_30:
        case RI_BOMBCHU_BAG_40:
        case RI_PROGRESSIVE_BOMBCHU_BAG: {
            s32 level = BombchuBagLevel();
            if (RANDO_SAVE_OPTIONS[RO_BOMBCHU_BAG] == RO_BOMBCHU_BAG_SINGLE || level <= 1) {
                Flags_ClearRandoInf(RANDO_INF_OBTAINED_BOMBCHU_BAG_20);
                Flags_ClearRandoInf(RANDO_INF_OBTAINED_BOMBCHU_BAG_30);
                Flags_ClearRandoInf(RANDO_INF_OBTAINED_BOMBCHU_BAG_40);
                INV_CONTENT(ITEM_BOMBCHU) = ITEM_NONE;
                AMMO(ITEM_BOMBCHU) = 0;
            } else {
                Flags_ClearRandoInf(level == 3 ? RANDO_INF_OBTAINED_BOMBCHU_BAG_40 : RANDO_INF_OBTAINED_BOMBCHU_BAG_30);
                if (AMMO(ITEM_BOMBCHU) > Combo_Rando_BombchuCapacity()) {
                    AMMO(ITEM_BOMBCHU) = (s8)Combo_Rando_BombchuCapacity();
                }
            }
            return true;
        }
        default:
            return false;
    }
}

extern "C" s32 Combo_Rando_BombchuBagOn(void) {
    return IS_RANDO && Rando::BombchuBagOn();
}

extern "C" s32 Combo_Rando_BombchuCapacity(void) {
    if (!Combo_Rando_BombchuBagOn()) {
        return CUR_CAPACITY(UPG_BOMB_BAG);
    }
    return kBagCapacity[Rando::BombchuBagLevel()];
}

#endif
