// mm/2s2h/Rando/MiscBehavior/InfiniteUpgrades.cpp
// ComboShip: Ship of Harkinian's "Infinite Upgrades" randomizer setting (soh's item.cpp, hook_handlers.cpp
// RandomizerOnGameFrameUpdateHandler / VB_DRAW_AMMO_COUNT), for Majora's Mask.
//
// - Progressive: each upgrade gets one more copy in the pool; the copy past the last tier is its infinite item.
// - Condensed Progressive: no extra copies; the copy after the first tier is already the infinite item.
//
// The families are the bomb bag, the quiver, the wallet, the magic meter, the bombchu bag (with Bombchu Bag on) and
// the Deku stick / nut capacities (with Deku Stick & Nut Upgrades on). An infinite item also raises its upgrade to the
// last tier, so a skipped tier (Condensed) never leaves logic or the capacity behind. Owned, the ammo / magic /
// rupees stay full and the ammo count is hidden, like Ship of Harkinian.
#ifdef COMBO_BUILD

#include <algorithm>

#include "SohSettings.h"
#include "MiscBehavior.h"
#include "2s2h/Rando/Rando.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"

extern "C" {
#include "variables.h"
#include "functions.h"
#include "macros.h"
extern int Combo_MM_BombchuBagShared(void);
}

namespace {

struct InfiniteFamily {
    RandoItemId progressive;
    RandoItemId infinite;
    RandoInf flag;
};

constexpr InfiniteFamily kFamilies[] = {
    { RI_PROGRESSIVE_BOMB_BAG, RI_BOMB_BAG_INF, RANDO_INF_HAS_INFINITE_BOMB_BAG },
    { RI_PROGRESSIVE_BOW, RI_QUIVER_INF, RANDO_INF_HAS_INFINITE_QUIVER },
    { RI_PROGRESSIVE_WALLET, RI_WALLET_INF, RANDO_INF_HAS_INFINITE_MONEY },
    { RI_PROGRESSIVE_MAGIC, RI_MAGIC_INF, RANDO_INF_HAS_INFINITE_MAGIC },
    { RI_PROGRESSIVE_STICK_UPGRADE, RI_STICK_UPGRADE_INF, RANDO_INF_HAS_INFINITE_STICKS },
    { RI_PROGRESSIVE_NUT_UPGRADE, RI_NUT_UPGRADE_INF, RANDO_INF_HAS_INFINITE_NUTS },
    { RI_PROGRESSIVE_BOMBCHU_BAG, RI_BOMBCHU_INF, RANDO_INF_HAS_INFINITE_BOMBCHUS },
};

const InfiniteFamily* FindFamily(RandoItemId itemId) {
    for (const auto& family : kFamilies) {
        if (family.progressive == itemId || family.infinite == itemId) {
            return &family;
        }
    }
    return nullptr;
}

u8 WalletMaxTier() {
    return (RANDO_SAVE_OPTIONS[RO_SHUFFLE_TYCOON_WALLET] == RO_GENERIC_YES) ? 3 : 2;
}

// The tier owned and the last tier, per family.
void Tiers(RandoItemId progressive, s32& owned, s32& last) {
    switch (progressive) {
        case RI_PROGRESSIVE_BOMB_BAG:
            owned = CUR_UPG_VALUE(UPG_BOMB_BAG);
            last = 3;
            break;
        case RI_PROGRESSIVE_BOW:
            owned = CUR_UPG_VALUE(UPG_QUIVER);
            last = 3;
            break;
        case RI_PROGRESSIVE_WALLET:
            owned = CUR_UPG_VALUE(UPG_WALLET);
            last = WalletMaxTier();
            break;
        case RI_PROGRESSIVE_MAGIC:
            owned = gSaveContext.save.saveInfo.playerData.isMagicAcquired +
                    gSaveContext.save.saveInfo.playerData.isDoubleMagicAcquired;
            last = 2;
            break;
        // MM's own capacity is the first stick / nut tier (10 sticks, 20 nuts).
        case RI_PROGRESSIVE_STICK_UPGRADE:
            owned = std::max(1, (s32)CUR_UPG_VALUE(UPG_DEKU_STICKS));
            last = 3;
            break;
        case RI_PROGRESSIVE_NUT_UPGRADE:
            owned = std::max(1, (s32)CUR_UPG_VALUE(UPG_DEKU_NUTS));
            last = 3;
            break;
        case RI_PROGRESSIVE_BOMBCHU_BAG:
            owned = Rando::BombchuBagLevel();
            last = 3;
            break;
        default:
            owned = 0;
            last = 0;
            break;
    }
}

void RefillWhatIsInfinite() {
    if (Flags_GetRandoInf(RANDO_INF_HAS_INFINITE_BOMB_BAG) && INV_CONTENT(ITEM_BOMB) == ITEM_BOMB) {
        AMMO(ITEM_BOMB) = CUR_CAPACITY(UPG_BOMB_BAG);
    }
    if (Flags_GetRandoInf(RANDO_INF_HAS_INFINITE_QUIVER) && INV_CONTENT(ITEM_BOW) == ITEM_BOW) {
        AMMO(ITEM_BOW) = CUR_CAPACITY(UPG_QUIVER);
    }
    if (Flags_GetRandoInf(RANDO_INF_HAS_INFINITE_STICKS) && INV_CONTENT(ITEM_DEKU_STICK) == ITEM_DEKU_STICK) {
        AMMO(ITEM_DEKU_STICK) = CUR_CAPACITY(UPG_DEKU_STICKS);
    }
    if (Flags_GetRandoInf(RANDO_INF_HAS_INFINITE_NUTS) && INV_CONTENT(ITEM_DEKU_NUT) == ITEM_DEKU_NUT) {
        AMMO(ITEM_DEKU_NUT) = CUR_CAPACITY(UPG_DEKU_NUTS);
    }
    if (Flags_GetRandoInf(RANDO_INF_HAS_INFINITE_BOMBCHUS) && INV_CONTENT(ITEM_BOMBCHU) == ITEM_BOMBCHU) {
        AMMO(ITEM_BOMBCHU) = Combo_Rando_BombchuCapacity();
    }
    if (Flags_GetRandoInf(RANDO_INF_HAS_INFINITE_MONEY)) {
        gSaveContext.save.saveInfo.playerData.rupees = CUR_CAPACITY(UPG_WALLET);
    }
    // Like the Infinite Magic cheat: only once the meter has grown to its level.
    if (Flags_GetRandoInf(RANDO_INF_HAS_INFINITE_MAGIC)) {
        u8 magicLevel = gSaveContext.save.saveInfo.playerData.magicLevel;
        if (magicLevel == 1) {
            gSaveContext.save.saveInfo.playerData.magic = MAGIC_NORMAL_METER;
        } else if (magicLevel == 2) {
            gSaveContext.save.saveInfo.playerData.magic = MAGIC_DOUBLE_METER;
        }
    }
}

bool AmmoIsInfinite(s16 item) {
    switch (item) {
        case ITEM_BOMB:
            return Flags_GetRandoInf(RANDO_INF_HAS_INFINITE_BOMB_BAG);
        case ITEM_BOW:
        case ITEM_BOW_FIRE:
        case ITEM_BOW_ICE:
        case ITEM_BOW_LIGHT:
            return Flags_GetRandoInf(RANDO_INF_HAS_INFINITE_QUIVER);
        case ITEM_DEKU_STICK:
            return Flags_GetRandoInf(RANDO_INF_HAS_INFINITE_STICKS);
        case ITEM_DEKU_NUT:
            return Flags_GetRandoInf(RANDO_INF_HAS_INFINITE_NUTS);
        case ITEM_BOMBCHU:
            return Flags_GetRandoInf(RANDO_INF_HAS_INFINITE_BOMBCHUS);
        default:
            return false;
    }
}

} // namespace

bool Rando::InfiniteUpgradesOn() {
    return RANDO_SAVE_OPTIONS[RO_INFINITE_UPGRADES] != RO_INFINITE_UPGRADES_OFF;
}

bool Rando::InfiniteUpgradeOwned(RandoItemId itemId) {
    const InfiniteFamily* family = FindFamily(itemId);
    return (family != nullptr) && Flags_GetRandoInf(family->flag);
}

RandoItemId Rando::InfiniteUpgradeFor(RandoItemId progressive) {
    if (!InfiniteUpgradesOn()) {
        return RI_UNKNOWN;
    }
    const InfiniteFamily* family = FindFamily(progressive);
    if (family == nullptr || family->progressive != progressive || Flags_GetRandoInf(family->flag)) {
        return RI_UNKNOWN;
    }
    if (progressive == RI_PROGRESSIVE_BOMBCHU_BAG && RANDO_SAVE_OPTIONS[RO_BOMBCHU_BAG] == RO_BOMBCHU_BAG_SINGLE) {
        // A Single bag has one tier: the next copy is infinite.
        return (BombchuBagLevel() > 0) ? RI_BOMBCHU_INF : RI_UNKNOWN;
    }
    s32 owned;
    s32 last;
    Tiers(progressive, owned, last);
    // Condensed: infinite right after the first tier (for sticks and nuts that is MM's own capacity, so their first
    // upgrade is already infinite, as in Ship of Harkinian).
    s32 infiniteAt = (RANDO_SAVE_OPTIONS[RO_INFINITE_UPGRADES] == RO_INFINITE_UPGRADES_CONDENSED) ? 1 : last;
    return (owned >= infiniteAt) ? family->infinite : RI_UNKNOWN;
}

bool Rando::InfiniteUpgradesGive(RandoItemId randoItemId) {
    switch (randoItemId) {
        case RI_BOMB_BAG_INF:
            Inventory_ChangeUpgrade(UPG_BOMB_BAG, 3);
            INV_CONTENT(ITEM_BOMB) = ITEM_BOMB;
            AMMO(ITEM_BOMB) = CUR_CAPACITY(UPG_BOMB_BAG);
            // Like the bomb bags: the bag carries bombchus unless they have a bag of their own.
            if (!BombchuBagOn() && !Combo_MM_BombchuBagShared()) {
                INV_CONTENT(ITEM_BOMBCHU) = ITEM_BOMBCHU;
                AMMO(ITEM_BOMBCHU) = CUR_CAPACITY(UPG_BOMB_BAG);
            }
            Flags_SetRandoInf(RANDO_INF_HAS_INFINITE_BOMB_BAG);
            return true;
        case RI_QUIVER_INF:
            Inventory_ChangeUpgrade(UPG_QUIVER, 3);
            INV_CONTENT(ITEM_BOW) = ITEM_BOW;
            AMMO(ITEM_BOW) = CUR_CAPACITY(UPG_QUIVER);
            Flags_SetRandoInf(RANDO_INF_HAS_INFINITE_QUIVER);
            return true;
        case RI_WALLET_INF:
            if (CUR_UPG_VALUE(UPG_WALLET) < WalletMaxTier()) {
                Inventory_ChangeUpgrade(UPG_WALLET, WalletMaxTier());
            }
            gSaveContext.save.saveInfo.playerData.rupees = CUR_CAPACITY(UPG_WALLET);
            Flags_SetRandoInf(RANDO_INF_HAS_INFINITE_MONEY);
            return true;
        case RI_MAGIC_INF:
            gSaveContext.save.saveInfo.playerData.isMagicAcquired = true;
            gSaveContext.save.saveInfo.playerData.isDoubleMagicAcquired = true;
            gSaveContext.save.saveInfo.playerData.magic = gSaveContext.magicFillTarget = MAGIC_DOUBLE_METER;
            gSaveContext.save.saveInfo.playerData.magicLevel = 0; // re-arms the meter's growth (GiveItem)
            SET_WEEKEVENTREG(WEEKEVENTREG_12_80);
            Flags_SetRandoInf(RANDO_INF_HAS_INFINITE_MAGIC);
            return true;
        case RI_STICK_UPGRADE_INF:
            Inventory_ChangeUpgrade(UPG_DEKU_STICKS, 3);
            if (INV_CONTENT(ITEM_DEKU_STICK) == ITEM_DEKU_STICK) {
                AMMO(ITEM_DEKU_STICK) = CUR_CAPACITY(UPG_DEKU_STICKS);
            }
            Flags_SetRandoInf(RANDO_INF_HAS_INFINITE_STICKS);
            return true;
        case RI_NUT_UPGRADE_INF:
            Inventory_ChangeUpgrade(UPG_DEKU_NUTS, 3);
            if (INV_CONTENT(ITEM_DEKU_NUT) == ITEM_DEKU_NUT) {
                AMMO(ITEM_DEKU_NUT) = CUR_CAPACITY(UPG_DEKU_NUTS);
            }
            Flags_SetRandoInf(RANDO_INF_HAS_INFINITE_NUTS);
            return true;
        case RI_BOMBCHU_INF:
            BombchuBagGive(RI_BOMBCHU_BAG_40);
            Flags_SetRandoInf(RANDO_INF_HAS_INFINITE_BOMBCHUS);
            return true;
        default:
            return false;
    }
}

bool Rando::InfiniteUpgradesRemove(RandoItemId randoItemId) {
    const InfiniteFamily* family = FindFamily(randoItemId);
    if (family == nullptr || family->infinite != randoItemId) {
        return false;
    }
    // The tier it raised stays: only the refill goes.
    Flags_ClearRandoInf(family->flag);
    return true;
}

static RegisterShipInitFunc initFunc(
    []() {
        // Registered for every rando save: the flags only exist with the setting on, so they gate the work.
        COND_HOOK(OnGameStateUpdate, IS_RANDO, []() {
            if (gPlayState != nullptr) {
                RefillWhatIsInfinite();
            }
        });
        // The ammo count of an infinite item is hidden; the B button keeps it for minigame ammo.
        COND_VB_SHOULD(VB_DRAW_HUD_AMMO_COUNT, IS_RANDO, {
            s16 button = (s16)va_arg(args, int);
            va_arg(args, int); // alpha
            bool isDpad = (bool)va_arg(args, int);
            if (!isDpad && button == EQUIP_SLOT_B) {
                return;
            }
            s16 item = isDpad ? DPAD_GET_CUR_FORM_BTN_ITEM(button) : GET_CUR_FORM_BTN_ITEM(button);
            if (AmmoIsInfinite(item)) {
                *should = false;
            }
        });
        COND_VB_SHOULD(VB_KALEIDO_DRAW_AMMO_COUNT, IS_RANDO, {
            va_arg(args, void*); // PauseContext*
            va_arg(args, void*); // GraphicsContext*
            s16 item = (s16)va_arg(args, int);
            if (AmmoIsInfinite(item)) {
                *should = false;
            }
        });
    },
    { "IS_RANDO" });

#endif
