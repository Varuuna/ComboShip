// mm/2s2h/Rando/MiscBehavior/CrossUpgrades.cpp
// ComboShip (cross-game items): Ocarina of Time items in Majora's Mask, the OoTMM "Stone of Agony in MM", "Fairy
// Ocarina in MM", "Short Hookshot in MM" and "Deku Stick & Nut Upgrades in MM" settings.
//
// - Stone of Agony (RO_CROSS_STONE_AGONY): MM already rumbles near secrets (Player_DetectSecrets); with the option on
// it
//   only does so once the Stone is owned, and every rumble flashes OOT's Stone of Agony icon above the rupee counter
//   (OoTMM's Interface_AgonyIconTick / Interface_AgonyIconDraw), for players without rumble.
// - Fairy Ocarina (RO_CROSS_OCARINA_FAIRY): a second Ocarina of Time in the pool; the first one (or the starting one)
//   is OOT's Fairy Ocarina. It is MM's own ocarina in the inventory marked RANDO_INF_COMBO_OCARINA_IS_FAIRY, so it
//   plays every song exactly like the Ocarina of Time (as in OoTMM); only its icon, pause name and the model in
//   human Link's hand are OOT's Fairy Ocarina.
// - Short Hookshot (RO_CROSS_HOOKSHOT_SHORT): a second Hookshot in the pool; the first one is MM's Hookshot marked
//   RANDO_INF_COMBO_HOOKSHOT_IS_SHORT, with OOT's Hookshot range (13 frames of flight instead of 26, OoTMM's
//   ArmsHook_SetTimerAndParent) and reticle, and OOT's Hookshot icon. MM's logic only counts the full Hookshot
//   (Logic.h HAS_ITEM).
// - Deku Stick & Nut Upgrades (RO_CROSS_STICKS_NUTS): MM's leftover UPG_DEKU_STICKS / UPG_DEKU_NUTS capacities
//   (10/20/30 sticks, 20/30/40 nuts), two progressive upgrades each.
//
// Behavior follows OoTMM (MIT License, Copyright (c) 2020-2022 OoTMM Team, https://github.com/OoTMM/OoTMM,
// packages/generator/src/mm/actors/Player.c, ovl_patch/arms_hook.c, common/interface/interface_agony.c).
// See docs/deviations/rando.md.
#ifdef COMBO_BUILD

#include "CrossGear.h"
#include "CrossItemAssets.h"
#include "MiscBehavior.h"
#include "2s2h/Rando/Rando.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"

extern "C" {
#include "variables.h"
#include "functions.h"
#include "macros.h"
#include "z64interface.h"
// Declared first so OPEN_DISPS' block-scope declarations keep C linkage (see CrossSpells.cpp).
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);
}

namespace {

// OPEN_DISPS / CLOSE_DISPS declare these at block scope; inside this namespace they would name
// (anonymous)::FrameInterpolation_* and lib2ship would fail to load, so bind them to the C ones.
extern "C" {
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);
}

// OOT's Stone of Agony icon, 24x24 RGBA32, drawn at 16x16.
constexpr int kAgonyTexSize = 24;
constexpr int kAgonyIconSize = 16;

s16 sAgonyAlpha = 0;
f32 sAgonyOffset = 0.0f;

TexturePtr sMmOcarinaIcon = nullptr; // MM's own gItemIcons entries, restored when the marker is off

void ReloadButtonIcons(u8 item) {
    if (gPlayState == nullptr) {
        return;
    }
    for (u8 btn = EQUIP_SLOT_C_LEFT; btn <= EQUIP_SLOT_C_RIGHT; btn++) {
        if (GET_CUR_FORM_BTN_ITEM(btn) == item) {
            Interface_LoadItemIconImpl(gPlayState, btn);
        }
    }
    for (u8 btn = EQUIP_SLOT_D_RIGHT; btn <= EQUIP_SLOT_D_UP; btn++) {
        if (DPAD_GET_CUR_FORM_BTN_ITEM(btn) == item) {
            Interface_Dpad_LoadItemIconImpl(gPlayState, btn);
        }
    }
}

void SwapIcon(u8 item, TexturePtr& mmIcon, bool useOot, const char* ootIcon) {
    if (mmIcon == nullptr) {
        mmIcon = gItemIcons[item];
    }
    TexturePtr want = useOot ? (TexturePtr)ootIcon : mmIcon;
    if (gItemIcons[item] != want) {
        gItemIcons[item] = want;
        ReloadButtonIcons(item);
    }
}

void UpdateIcons() {
    SwapIcon(ITEM_OCARINA_OF_TIME, sMmOcarinaIcon, Combo_Upgrades_OcarinaIsFairy() != 0, gComboIconOcarinaFairyTex);
}

// OoTMM's Interface_AgonyIconDraw: fades out from a tick, shaking more the stronger it is.
void DrawAgonyIcon() {
    if ((gPlayState == nullptr) || (sAgonyAlpha <= 0)) {
        sAgonyOffset = 0.0f;
        return;
    }
    s16 val = sAgonyAlpha;
    s16 alpha = CLAMP_MAX(val, 255);
    sAgonyAlpha = CLAMP_MIN(sAgonyAlpha - 10, 0);

    f32 amplitude = val * 0.03f;
    f32 target = Math_SinS((s16)((gPlayState->state.frames & 7) << 13)) * amplitude;
    sAgonyOffset = sAgonyOffset * 0.5f + target * 0.5f;

    s32 x = (s32)(26.0f + sAgonyOffset);
    s32 y = 220;
    s32 dsdx = (1 << 10) * kAgonyTexSize / kAgonyIconSize;

    OPEN_DISPS(gPlayState->state.gfxCtx);
    Gfx_SetupDL39_Overlay(gPlayState->state.gfxCtx);
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0x80, 255, 255, 255, alpha);
    gDPLoadTextureBlock(OVERLAY_DISP++, gComboIconStoneOfAgonyTex, G_IM_FMT_RGBA, G_IM_SIZ_32b, kAgonyTexSize,
                        kAgonyTexSize, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
                        G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPTextureRectangle(OVERLAY_DISP++, x << 2, y << 2, (x + kAgonyIconSize) << 2, (y + kAgonyIconSize) << 2,
                        G_TX_RENDERTILE, 0, 0, dsdx, dsdx);
    CLOSE_DISPS(gPlayState->state.gfxCtx);
}

void SetUpgrade(s32 upgrade, s32 value, u8 item) {
    Inventory_ChangeUpgrade(upgrade, value);
    // Like OOT's capacity upgrades: refill what is already owned.
    if (INV_CONTENT(item) == item) {
        AMMO(item) = CUR_CAPACITY(upgrade);
    }
}

} // namespace

extern "C" int Combo_Upgrades_OcarinaIsFairy(void) {
    return IS_RANDO && (INV_CONTENT(ITEM_OCARINA_OF_TIME) == ITEM_OCARINA_OF_TIME) &&
           Flags_GetRandoInf(RANDO_INF_COMBO_OCARINA_IS_FAIRY);
}

extern "C" int Combo_Upgrades_HookshotIsShort(void) {
    return IS_RANDO && (INV_CONTENT(ITEM_HOOKSHOT) == ITEM_HOOKSHOT) &&
           Flags_GetRandoInf(RANDO_INF_COMBO_HOOKSHOT_IS_SHORT);
}

extern "C" int Combo_Upgrades_SecretRumble(void) {
    if (!IS_RANDO || (RANDO_SAVE_OPTIONS[RO_CROSS_STONE_AGONY] != RO_GENERIC_YES)) {
        return 1; // vanilla MM: always rumbles, no icon
    }
    if (!Flags_GetRandoInf(RANDO_INF_OBTAINED_STONE_OF_AGONY)) {
        return 0;
    }
    sAgonyAlpha = 300;
    return 1;
}

// ---- Rando item plumbing (Rando.h) ----

bool Rando::CrossUpgradesGive(RandoItemId randoItemId) {
    switch (randoItemId) {
        case RI_STONE_OF_AGONY:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_STONE_OF_AGONY);
            return true;
        case RI_OCARINA_FAIRY:
            INV_CONTENT(ITEM_OCARINA_OF_TIME) = ITEM_OCARINA_OF_TIME;
            Flags_SetRandoInf(RANDO_INF_COMBO_OCARINA_IS_FAIRY);
            return true;
        case RI_HOOKSHOT_SHORT:
            INV_CONTENT(ITEM_HOOKSHOT) = ITEM_HOOKSHOT;
            Flags_SetRandoInf(RANDO_INF_COMBO_HOOKSHOT_IS_SHORT);
            return true;
        case RI_DEKU_STICK_UPGRADE_20:
            SetUpgrade(UPG_DEKU_STICKS, 2, ITEM_DEKU_STICK);
            return true;
        case RI_DEKU_STICK_UPGRADE_30:
            SetUpgrade(UPG_DEKU_STICKS, 3, ITEM_DEKU_STICK);
            return true;
        case RI_DEKU_NUT_UPGRADE_30:
            SetUpgrade(UPG_DEKU_NUTS, 2, ITEM_DEKU_NUT);
            return true;
        case RI_DEKU_NUT_UPGRADE_40:
            SetUpgrade(UPG_DEKU_NUTS, 3, ITEM_DEKU_NUT);
            return true;
        case RI_PROGRESSIVE_STICK_UPGRADE:
        case RI_PROGRESSIVE_NUT_UPGRADE: {
            RandoItemId concrete = Rando::ConvertItem(randoItemId);
            return (concrete != randoItemId) && CrossUpgradesGive(concrete);
        }
        default:
            return false;
    }
}

bool Rando::CrossUpgradesRemove(RandoItemId randoItemId) {
    switch (randoItemId) {
        case RI_STONE_OF_AGONY:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_STONE_OF_AGONY);
            return true;
        case RI_OCARINA_FAIRY:
            INV_CONTENT(ITEM_OCARINA_OF_TIME) = ITEM_NONE;
            Flags_ClearRandoInf(RANDO_INF_COMBO_OCARINA_IS_FAIRY);
            return true;
        case RI_HOOKSHOT_SHORT:
            INV_CONTENT(ITEM_HOOKSHOT) = ITEM_NONE;
            Flags_ClearRandoInf(RANDO_INF_COMBO_HOOKSHOT_IS_SHORT);
            return true;
        case RI_DEKU_STICK_UPGRADE_20:
        case RI_DEKU_STICK_UPGRADE_30:
        case RI_PROGRESSIVE_STICK_UPGRADE:
            if (CUR_UPG_VALUE(UPG_DEKU_STICKS) >= 2) {
                Inventory_ChangeUpgrade(UPG_DEKU_STICKS, CUR_UPG_VALUE(UPG_DEKU_STICKS) - 1);
            }
            return true;
        case RI_DEKU_NUT_UPGRADE_30:
        case RI_DEKU_NUT_UPGRADE_40:
        case RI_PROGRESSIVE_NUT_UPGRADE:
            if (CUR_UPG_VALUE(UPG_DEKU_NUTS) >= 2) {
                Inventory_ChangeUpgrade(UPG_DEKU_NUTS, CUR_UPG_VALUE(UPG_DEKU_NUTS) - 1);
            }
            return true;
        default:
            return false;
    }
}

void Rando::MiscBehavior::CrossUpgrades() {
    sAgonyAlpha = 0;
    // Always registered: outside rando (or without the markers) it puts MM's own icons back.
    COND_HOOK(OnGameStateUpdate, true, UpdateIcons);
    COND_HOOK(OnInterfaceDrawStart, IS_RANDO && (RANDO_SAVE_OPTIONS[RO_CROSS_STONE_AGONY] == RO_GENERIC_YES),
              DrawAgonyIcon);
}

#endif // COMBO_BUILD
