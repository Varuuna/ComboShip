// ComboShip: OoTMM's "ageless" settings (agelessSwords, agelessShields, agelessTunics, agelessBoots, agelessSticks,
// agelessBoomerang, agelessHammer, agelessHookshot, agelessSlingshot, agelessBow, agelessChildTrade,
// agelessStrength): each lets Link use that group of items at either age. RSK_COMBO_AGELESS_* (settings.cpp); the
// randomizer logic follows them in Logic::CanUse (logic.cpp).
//
// In game this is SoH's "Timeless Equipment" cheat split per group, on the same age checks:
// - VB_ITEM_MEETS_AGE_REQ / VB_SLOT_MEETS_AGE_REQ (pause menu: assigning C buttons, greying, Ask to Equip),
// - CHECK_AGE_REQ_EQUIP (equipment page), which calls Combo_Ageless_Equip under COMBO_BUILD (its VB carries only
//   the age, not which equipment),
// - Player_GetStrength (child Link's lifting strength), which calls Combo_Ageless_Strength.
// Models are the game's own per-age ones, as with the cheat: child Link holding an adult sword shows his Kokiri
// Sword, and adult-only tools have no child hand model.
// Not ported: agelessGFS (OOT has no Great Fairy's Sword) and agelessSoaring (ComboShip's Song of Soaring in OOT is
// already usable at both ages). See docs/deviations/rando.md.
#ifdef COMBO_BUILD
#include "soh/ShipInit.hpp"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/randomizer/randomizer.h"

#include <libultraship/bridge/consolevariablebridge.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "objects/object_link_boy/object_link_boy.h"
#include "objects/object_link_child/object_link_child.h"
extern PlayState* gPlayState;
Gfx* ResourceMgr_LoadGfxByName(const char* path);
}

namespace {

bool Ageless(RandomizerSettingKey key) {
    return IS_RANDO && (OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(key) != 0);
}

// The ageless setting covering an item id, or RSK_MAX for items without one.
RandomizerSettingKey ItemSetting(s32 item) {
    switch (item) {
        case ITEM_SWORD_KOKIRI:
        case ITEM_SWORD_MASTER:
        case ITEM_SWORD_BGS:
            return RSK_COMBO_AGELESS_SWORDS;
        case ITEM_SHIELD_DEKU:
        case ITEM_SHIELD_HYLIAN:
        case ITEM_SHIELD_MIRROR:
            return RSK_COMBO_AGELESS_SHIELDS;
        case ITEM_TUNIC_GORON:
        case ITEM_TUNIC_ZORA:
            return RSK_COMBO_AGELESS_TUNICS;
        case ITEM_BOOTS_IRON:
        case ITEM_BOOTS_HOVER:
            return RSK_COMBO_AGELESS_BOOTS;
        case ITEM_STICK:
            return RSK_COMBO_AGELESS_STICKS;
        case ITEM_BOOMERANG:
            return RSK_COMBO_AGELESS_BOOMERANG;
        case ITEM_HAMMER:
            return RSK_COMBO_AGELESS_HAMMER;
        case ITEM_HOOKSHOT:
        case ITEM_LONGSHOT:
            return RSK_COMBO_AGELESS_HOOKSHOT;
        case ITEM_SLINGSHOT:
        case ITEM_BULLET_BAG_30:
        case ITEM_BULLET_BAG_40:
        case ITEM_BULLET_BAG_50:
            return RSK_COMBO_AGELESS_SLINGSHOT;
        case ITEM_BOW:
        case ITEM_ARROW_FIRE:
        case ITEM_ARROW_ICE:
        case ITEM_ARROW_LIGHT:
        case ITEM_BOW_ARROW_FIRE:
        case ITEM_BOW_ARROW_ICE:
        case ITEM_BOW_ARROW_LIGHT:
        case ITEM_QUIVER_30:
        case ITEM_QUIVER_40:
        case ITEM_QUIVER_50:
            return RSK_COMBO_AGELESS_BOW;
        case ITEM_BRACELET:
        case ITEM_GAUNTLETS_SILVER:
        case ITEM_GAUNTLETS_GOLD:
            return RSK_COMBO_AGELESS_STRENGTH;
        case ITEM_COMBO_MASK_BLAST: // ComboMmMasks.cpp: MM masks worn in OOT, in the child trade slot
        case ITEM_COMBO_MASK_STONE:
        case ITEM_COMBO_MASK_KAMARO:
            return RSK_COMBO_AGELESS_CHILD_TRADE;
        default:
            if ((item >= ITEM_WEIRD_EGG) && (item <= ITEM_SOLD_OUT)) {
                return RSK_COMBO_AGELESS_CHILD_TRADE;
            }
            return RSK_MAX;
    }
}

RandomizerSettingKey SlotSetting(s32 slot) {
    switch (slot) {
        case SLOT_STICK:
            return RSK_COMBO_AGELESS_STICKS;
        case SLOT_SLINGSHOT:
            return RSK_COMBO_AGELESS_SLINGSHOT;
        case SLOT_BOOMERANG:
            return RSK_COMBO_AGELESS_BOOMERANG;
        case SLOT_HAMMER:
            return RSK_COMBO_AGELESS_HAMMER;
        case SLOT_HOOKSHOT:
            return RSK_COMBO_AGELESS_HOOKSHOT;
        case SLOT_BOW:
        case SLOT_ARROW_FIRE:
        case SLOT_ARROW_ICE:
        case SLOT_ARROW_LIGHT:
            return RSK_COMBO_AGELESS_BOW;
        case SLOT_TRADE_CHILD:
            return RSK_COMBO_AGELESS_CHILD_TRADE;
        default:
            return RSK_MAX;
    }
}

// Link's tunic color this frame (VB_APPLY_TUNIC_COLOR): OOT adult Link's sheath display lists set their own environment
// color, which OOT's tunic is shaded with, so it is put back for the limbs drawn after the sheath (the torso).
Color_RGB8 sTunicColor = { 30, 105, 27 };

// The sheath on child Link's back when he carries an adult sword (Master Sword, Giant's Knife, Biggoron's Sword) with
// no shield or the Deku Shield. OOT then draws its swordless child models (nothing, or the Deku Shield alone), so the
// sword vanished from his back. With SoH's "Show Age-Dependent Equipment" he wears adult Link's sheath (scaled down
// like SoH's other adult equipment on child Link when "Scale Adult Equipment as Child" is on), with the Deku Shield
// over it; without it, his own sheath, matching the Kokiri Sword his hand shows.
void OverrideChildAdultSwordSheath(s32 limbIndex, Gfx** dList, Player* player, PlayState* play) {
    if ((limbIndex != PLAYER_LIMB_SHEATH) || (dList == nullptr) || (play == nullptr) || (player != GET_PLAYER(play)) ||
        LINK_IS_ADULT) {
        return;
    }
    const u8 sword = gSaveContext.equips.buttonItems[0];
    const s32 shield = player->currentShield;
    if (((sword != ITEM_SWORD_MASTER) && (sword != ITEM_SWORD_BGS)) ||
        ((shield != PLAYER_SHIELD_NONE) && (shield != PLAYER_SHIELD_DEKU))) {
        return;
    }
    const bool shieldOnBack =
        (player->sheathType == PLAYER_MODELTYPE_SHEATH_18) || (player->sheathType == PLAYER_MODELTYPE_SHEATH_19);
    const bool swordInSheath =
        (player->sheathType == PLAYER_MODELTYPE_SHEATH_16) || (player->sheathType == PLAYER_MODELTYPE_SHEATH_18);
    const bool deku = (shield == PLAYER_SHIELD_DEKU);

    if (!CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0)) {
        if (shieldOnBack) { // in hand, OOT already draws the child sheath
            const char* child =
                deku ? (swordInSheath ? gLinkChildDekuShieldSwordAndSheathNearDL : gLinkChildDekuShieldAndSheathNearDL)
                     : (swordInSheath ? gLinkChildSwordAndSheathNearDL : gLinkChildSheathNearDL);
            *dList = ResourceMgr_LoadGfxByName(child);
        }
        return;
    }

    Gfx* buf = (Gfx*)Graph_Alloc(play->state.gfxCtx, 8 * sizeof(Gfx));
    Gfx* p = buf;
    // With the shield in hand, SoH already moves and scales this limb for the adult sheath.
    const bool scale = shieldOnBack && CVarGetInteger(CVAR_ENHANCEMENT("ScaleAdultEquipmentAsChild"), 0);
    if (scale) {
        Matrix_Push();
        Matrix_Translate(218.0f, -100.0f, 62.0f, MTXMODE_NEW);
        Matrix_Scale(0.8f, 0.8f, 0.8f, MTXMODE_APPLY);
        gSPMatrix(p++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_PUSH | G_MTX_MUL | G_MTX_MODELVIEW);
        Matrix_Pop();
    }
    gSPDisplayList(
        p++, ResourceMgr_LoadGfxByName(swordInSheath ? gLinkAdultMasterSwordAndSheathNearDL : gLinkAdultSheathNearDL));
    if (scale) {
        gSPPopMatrix(p++, G_MTX_MODELVIEW);
    }
    if (deku && shieldOnBack) {
        gSPDisplayList(p++, ResourceMgr_LoadGfxByName(gLinkChildDekuShieldWithMatrixDL));
    }
    gDPPipeSync(p++);
    gDPSetEnvColor(p++, sTunicColor.r, sTunicColor.g, sTunicColor.b, 0);
    gSPEndDisplayList(p++);
    *dList = buf;
}

} // namespace

extern "C" int Combo_Ageless_Equip(s32 equipType) {
    switch (equipType) {
        case EQUIP_TYPE_SWORD:
            return Ageless(RSK_COMBO_AGELESS_SWORDS);
        case EQUIP_TYPE_SHIELD:
            return Ageless(RSK_COMBO_AGELESS_SHIELDS);
        case EQUIP_TYPE_TUNIC:
            return Ageless(RSK_COMBO_AGELESS_TUNICS);
        case EQUIP_TYPE_BOOTS:
            return Ageless(RSK_COMBO_AGELESS_BOOTS);
        default:
            return 0;
    }
}

extern "C" int Combo_Ageless_Strength(void) {
    return Ageless(RSK_COMBO_AGELESS_STRENGTH);
}

static void RegisterComboAgeless() {
    const bool on = IS_RANDO;
    COND_VB_SHOULD(VB_ITEM_MEETS_AGE_REQ, on, {
        s32 item = va_arg(args, s32);
        RandomizerSettingKey key = ItemSetting(item);
        if (!*should && (key != RSK_MAX) && Ageless(key)) {
            *should = true;
        }
    });
    COND_VB_SHOULD(VB_SLOT_MEETS_AGE_REQ, on, {
        s32 slot = va_arg(args, s32);
        RandomizerSettingKey key = SlotSetting(slot);
        if (!*should && (key != RSK_MAX) && Ageless(key)) {
            *should = true;
        }
    });
    COND_VB_SHOULD(VB_PLAYER_OVERRIDE_LIMB_DRAW, on, {
        s32 limbIndex = va_arg(args, s32);
        Gfx** dList = va_arg(args, Gfx**);
        Player* player = (Player*)va_arg(args, void*);
        PlayState* play = va_arg(args, PlayState*);
        OverrideChildAdultSwordSheath(limbIndex, dList, player, play);
    });
    COND_VB_SHOULD(VB_APPLY_TUNIC_COLOR, on, {
        va_arg(args, void*); // the skeleton's draw data
        Color_RGB8* color = va_arg(args, Color_RGB8*);
        if (color != nullptr) {
            sTunicColor = *color;
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterComboAgeless, { "IS_RANDO" });
#endif
