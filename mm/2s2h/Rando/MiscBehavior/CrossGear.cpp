// mm/2s2h/Rando/MiscBehavior/CrossGear.cpp
// ComboShip (cross-game items): Ocarina of Time equipment usable by human Link in Majora's Mask, the
// OoTMM "Iron Boots (MM)", "Hover Boots (MM)", "Goron/Zora Tunic (MM)", "Scales (MM)", "Strength (MM)"
// settings. See CrossGear.h for the C ABI the decomp hooks call, and docs/deviations/rando.md.
//
// Behavior is ported from OoTMM's MM Player patches (MIT License, Copyright (c) 2020-2022 OoTMM Team,
// https://github.com/OoTMM/OoTMM, packages/generator/src/mm/actors/Player.c) and OOT's own player code.
#ifdef COMBO_BUILD

#include "CrossGear.h"
#include "AdultLink.h"
#include "AgeRequirements.h"
#include "CrossItemAssets.h"
#include "2s2h/Rando/Rando.h"
#include "2s2h/Enhancements/FrameInterpolation/FrameInterpolation.h"

extern "C" {
#include "variables.h"
#include "functions.h"
#include "macros.h"
#include "sfx.h"
#include "z64player.h"
// z_player_lib.c: Player_SetBootData (recomputes the boot physics regs; ends in Combo_Gear_ApplyBootData).
void func_80123140(PlayState* play, Player* player);
// CrossGearBoots.c: OoTMM's child-sized boot meshes.
extern Gfx gComboLinkLeftIronBootDL[];
extern Gfx gComboLinkRightIronBootDL[];
extern Gfx gComboLinkLeftHoverBootDL[];
extern Gfx gComboLinkRightHoverBootDL[];
// CrossGearGauntlets.c: OoTMM's child-sized gauntlet plates.
extern Gfx gComboHumanLeftGauntletPlate1DL[];
extern Gfx gComboHumanRightGauntletPlate1DL[];
extern Gfx gComboHumanLeftGauntletPlate3DL[];
extern Gfx gComboHumanRightGauntletPlate3DL[];
extern Gfx gComboHumanGauntletGemDL[];
// BenGui/CosmeticEditor.cpp: OOT tunic color over the human tunic (nullptr restores the cosmetic setting).
void CosmeticEditor_SetHumanTunicOverride(const Color_RGB8* color);
Color_RGB8 CosmeticEditor_GetComboColor(const char* id, Color_RGB8 defaultColor);
extern "C" int CosmeticEditor_IsComboColorChanged(const char* id);
}

unsigned char gComboHoverBootsTimer = 0;

namespace {

bool HasFlag(RandoInf flag) {
    return IS_RANDO && Flags_GetRandoInf(flag);
}

// Human Link in his own body: the only form OOT gear applies to (OoTMM: MM_PLAYER_FORM_HUMAN only).
bool IsGearCapableLink(Player* player) {
    return (player != nullptr) && (player->actor.id == ACTOR_PLAYER) && (player->transformation == PLAYER_FORM_HUMAN) &&
           (player->currentMask != PLAYER_MASK_GIANT);
}

// OoTMM's child-Link boot physics rows (sBootData[0..2]): the 18 regs func_80123140 loads from D_801BFE14.
enum BootRow { BOOT_ROW_IRON, BOOT_ROW_HOVER, BOOT_ROW_IRON_UNDERWATER };
const s16 kComboBootData[3][18] = {
    { 200, 1000, 300, 700, 550, 270, 1000, 0, 120, 800, 300, -160, 600, 590, 750, 125, 200, 130 }, // Iron
    { 200, 1000, 300, 700, 550, 270, 600, 600, 120, 800, 550, -100, 600, 540, 270, 25, 0, 130 },   // Hover
    { 80, 800, 150, 700, 480, 270, 600, 50, 120, 800, 550, -40, 400, 540, 270, 25, 0, 80 },        // Iron (water)
};

void LoadBootRegs(PlayState* play, const s16* bootRegs) {
    REG(19) = bootRegs[0];
    REG(30) = bootRegs[1];
    REG(32) = bootRegs[2];
    REG(34) = bootRegs[3];
    REG(35) = bootRegs[4];
    REG(36) = bootRegs[5];
    REG(37) = bootRegs[6];
    REG(38) = bootRegs[7];
    REG(39) = bootRegs[8];
    R_DECELERATE_RATE = bootRegs[9];
    R_RUN_SPEED_LIMIT = bootRegs[10];
    REG(68) = bootRegs[11]; // gravity
    REG(69) = bootRegs[12];
    IREG(66) = bootRegs[13];
    IREG(67) = bootRegs[14];
    IREG(68) = bootRegs[15];
    IREG(69) = bootRegs[16];
    MREG(95) = bootRegs[17];
    if (play->roomCtx.curRoom.type == ROOM_TYPE_INDOORS) {
        R_RUN_SPEED_LIMIT = 500;
    }
}

// Toggle one piece of gear in a mutually exclusive pair (boots: Iron/Hover, tunics: Goron/Zora).
void ToggleWorn(RandoInf wearing, RandoInf other) {
    if (Flags_GetRandoInf(wearing)) {
        Flags_ClearRandoInf(wearing);
    } else {
        Flags_SetRandoInf(wearing);
        Flags_ClearRandoInf(other);
    }
}

// The human boot index that matches what is worn. Iron Boots reuse MM's Goron boot slot (OoTMM): every
// "currentBoots >= PLAYER_BOOTS_ZORA_UNDERWATER" path then sinks Link and lets him walk on the bottom.
void SyncBootIndex(PlayState* play, Player* player) {
    if (!IsGearCapableLink(player)) {
        return;
    }
    u8 wanted = (Combo_Gear_GetBoots(player) == COMBO_BOOTS_IRON) ? PLAYER_BOOTS_GORON : PLAYER_BOOTS_HYLIAN;
    if (player->currentBoots != wanted) {
        player->currentBoots = wanted;
        func_80123140(play, player);
    }
}

// Tunic color: reuse the cosmetic editor's human-tunic display-list patch with an override color. The colors are the
// cosmetic editor's "Goron Tunic (OoT)" / "Zora Tunic (OoT)" (OOT's own by default), re-applied when they change.
s32 sAppliedTunic = -1;
Color_RGB8 sAppliedTunicColor = { 0, 0, 0 };

void UpdateTunicColor(Player* player) {
    s32 tunic = (player != nullptr && player->transformation == PLAYER_FORM_HUMAN) ? Combo_Gear_GetTunic(player)
                                                                                   : (s32)COMBO_TUNIC_NONE;
    Color_RGB8 color = { 0, 0, 0 };
    if (tunic != COMBO_TUNIC_NONE) {
        color = Combo_Gear_TunicColor(tunic, color);
    }
    if ((tunic == sAppliedTunic) && (color.r == sAppliedTunicColor.r) && (color.g == sAppliedTunicColor.g) &&
        (color.b == sAppliedTunicColor.b)) {
        return;
    }
    sAppliedTunic = tunic;
    sAppliedTunicColor = color;
    CosmeticEditor_SetHumanTunicOverride((tunic != COMBO_TUNIC_NONE) ? &color : nullptr);
}

// OOT masks (OoTMM's custom masks): putting one on takes off any MM mask and the other OOT masks; using
// the one that is on takes it off. Returns 1 (handled) for Combo_CrossItem_TryUse.
int UseCustomMask(Player* player, int item) {
    static const RandoInf kWearing[] = { RANDO_INF_COMBO_WEARING_MASK_GERUDO, RANDO_INF_COMBO_WEARING_MASK_SKULL,
                                         RANDO_INF_COMBO_WEARING_MASK_SPOOKY };
    const s32 index = item - ITEM_COMBO_MASK_GERUDO;
    const bool wasOn = Combo_Gear_GetMask(player) == (COMBO_MASK_GERUDO + index);
    for (RandoInf flag : kWearing) {
        Flags_ClearRandoInf(flag);
    }
    if (wasOn) {
        Player_PlaySfx(player, NA_SE_PL_TAKE_OUT_SHIELD);
        return 1;
    }
    if (player->currentMask != PLAYER_MASK_NONE) {
        player->prevMask = player->currentMask;
        player->currentMask = PLAYER_MASK_NONE;
        gSaveContext.save.equippedMask = PLAYER_MASK_NONE;
    }
    Flags_SetRandoInf(kWearing[index]);
    Player_PlaySfx(player, NA_SE_PL_CHANGE_ARMS);
    return 1;
}

} // namespace

extern "C" int Combo_Gear_GetBoots(Player* player) {
    if (!IsGearCapableLink(player)) {
        return COMBO_BOOTS_NONE;
    }
    if (HasFlag(RANDO_INF_COMBO_WEARING_BOOTS_IRON) && HasFlag(RANDO_INF_OBTAINED_BOOTS_IRON)) {
        return COMBO_BOOTS_IRON;
    }
    if (HasFlag(RANDO_INF_COMBO_WEARING_BOOTS_HOVER) && HasFlag(RANDO_INF_OBTAINED_BOOTS_HOVER)) {
        return COMBO_BOOTS_HOVER;
    }
    return COMBO_BOOTS_NONE;
}

extern "C" Color_RGB8 Combo_Gear_TunicColor(int tunic, Color_RGB8 kokiri) {
    switch (tunic) {
        case COMBO_TUNIC_GORON:
            return CosmeticEditor_GetComboColor("Player.OotGoronTunic", { 100, 20, 0 });
        case COMBO_TUNIC_ZORA:
            return CosmeticEditor_GetComboColor("Player.OotZoraTunic", { 0, 60, 100 });
        default:
            return kokiri;
    }
}

extern "C" Color_RGB8 Combo_Gear_GauntletColor(int strength) {
    return (strength >= 3) ? CosmeticEditor_GetComboColor("Player.GoldenGauntlets", { 254, 207, 15 })
                           : CosmeticEditor_GetComboColor("Player.SilverGauntlets", { 255, 255, 255 });
}

extern "C" int Combo_Gear_GetTunic(Player* player) {
    // Tunics are clothes, not boots: the Giant's Mask doesn't take them off.
    if ((player == nullptr) || (player->actor.id != ACTOR_PLAYER) || (player->transformation != PLAYER_FORM_HUMAN)) {
        return COMBO_TUNIC_NONE;
    }
    if (HasFlag(RANDO_INF_COMBO_WEARING_TUNIC_GORON) && HasFlag(RANDO_INF_OBTAINED_TUNIC_GORON)) {
        return COMBO_TUNIC_GORON;
    }
    if (HasFlag(RANDO_INF_COMBO_WEARING_TUNIC_ZORA) && HasFlag(RANDO_INF_OBTAINED_TUNIC_ZORA)) {
        return COMBO_TUNIC_ZORA;
    }
    return COMBO_TUNIC_NONE;
}

extern "C" int Combo_Gear_GetMask(Player* player) {
    // Human Link, and no MM mask on (transformation masks count: they set currentMask too).
    if ((player == nullptr) || (player->actor.id != ACTOR_PLAYER) || (player->transformation != PLAYER_FORM_HUMAN) ||
        (player->currentMask != PLAYER_MASK_NONE)) {
        return COMBO_MASK_NONE;
    }
    if (HasFlag(RANDO_INF_COMBO_WEARING_MASK_GERUDO) && HasFlag(RANDO_INF_OBTAINED_MASK_GERUDO)) {
        return COMBO_MASK_GERUDO;
    }
    if (HasFlag(RANDO_INF_COMBO_WEARING_MASK_SKULL) && HasFlag(RANDO_INF_OBTAINED_MASK_SKULL)) {
        return COMBO_MASK_SKULL;
    }
    if (HasFlag(RANDO_INF_COMBO_WEARING_MASK_SPOOKY) && HasFlag(RANDO_INF_OBTAINED_MASK_SPOOKY)) {
        return COMBO_MASK_SPOOKY;
    }
    return COMBO_MASK_NONE;
}

extern "C" int Combo_Gear_IsWearingMask(PlayState* play, int mask) {
    return (play != nullptr) && (Combo_Gear_GetMask(GET_PLAYER(play)) == mask);
}

extern "C" int Combo_Gear_GetMaskSkullAsStone(PlayState* play) {
    if (Combo_Gear_IsWearingMask(play, COMBO_MASK_SKULL)) {
        return PLAYER_MASK_STONE;
    }
    return Player_GetMask(play);
}

extern "C" int Combo_Gear_GetScale(void) {
    if (HasFlag(RANDO_INF_OBTAINED_SCALE_GOLDEN)) {
        return 2;
    }
    return HasFlag(RANDO_INF_OBTAINED_SCALE_SILVER) ? 1 : 0;
}

extern "C" int Combo_Gear_StrengthReplacesHuman(void) {
    return IS_RANDO && (RANDO_SAVE_OPTIONS[RO_CROSS_STRENGTH] == RO_GENERIC_YES);
}

extern "C" int Combo_Gear_GetStrengthOwned(void) {
    if (HasFlag(RANDO_INF_OBTAINED_STRENGTH_GOLDEN_GAUNTLETS)) {
        return 3;
    }
    if (HasFlag(RANDO_INF_OBTAINED_STRENGTH_SILVER_GAUNTLETS)) {
        return 2;
    }
    return HasFlag(RANDO_INF_OBTAINED_STRENGTH_GORONS_BRACELET) ? 1 : 0;
}

extern "C" int Combo_Gear_GetStrength(void) {
    const int owned = Combo_Gear_GetStrengthOwned();
    // Cross-Games Age (OoTMM's Player_GetEffectiveHumanStrength): gauntlets restricted to the other age work as the
    // Goron's Bracelet.
    if ((owned > 1) && !Combo_AgeReq_StrengthAllowed()) {
        return 1;
    }
    return owned;
}

extern "C" int Combo_CrossItem_TryUse(PlayState* play, Player* player, int item) {
    if (!IS_COMBO_CROSS_ITEM(item)) {
        return 0;
    }
    if (!IsGearCapableLink(player)) {
        Audio_PlaySfx(NA_SE_SY_ERROR);
        return 1;
    }
    switch (item) {
        case ITEM_COMBO_BOOTS_IRON:
            ToggleWorn(RANDO_INF_COMBO_WEARING_BOOTS_IRON, RANDO_INF_COMBO_WEARING_BOOTS_HOVER);
            break;
        case ITEM_COMBO_BOOTS_HOVER:
            ToggleWorn(RANDO_INF_COMBO_WEARING_BOOTS_HOVER, RANDO_INF_COMBO_WEARING_BOOTS_IRON);
            break;
        case ITEM_COMBO_TUNIC_GORON:
            ToggleWorn(RANDO_INF_COMBO_WEARING_TUNIC_GORON, RANDO_INF_COMBO_WEARING_TUNIC_ZORA);
            break;
        case ITEM_COMBO_TUNIC_ZORA:
            ToggleWorn(RANDO_INF_COMBO_WEARING_TUNIC_ZORA, RANDO_INF_COMBO_WEARING_TUNIC_GORON);
            break;
        case ITEM_COMBO_MASK_GERUDO:
        case ITEM_COMBO_MASK_SKULL:
        case ITEM_COMBO_MASK_SPOOKY:
            return UseCustomMask(player, item);
        case ITEM_COMBO_SPELL_FIRE: // CrossSpells.cpp
        case ITEM_COMBO_SPELL_WIND:
        case ITEM_COMBO_SPELL_LOVE:
            return Combo_Spell_Cast(play, player, item);
        case ITEM_COMBO_HAMMER: // CrossTools.cpp: held items with an item action (Player_ItemToItemAction)
        case ITEM_COMBO_BOOMERANG:
        case ITEM_COMBO_SLINGSHOT:
            return 0;
        case ITEM_COMBO_ROCS_FEATHER: // CrossRocsFeather.cpp
            return Combo_Rocs_Use(play, player);
        default:
            // Cross-game items that aren't usable this way (yet): refuse cleanly, never fall into vanilla
            // item handling with an id past its tables.
            Audio_PlaySfx(NA_SE_SY_ERROR);
            return 1;
    }
    gComboHoverBootsTimer = 0;
    // Re-derive the boot index and physics now: func_80123140 ends in Combo_Gear_ApplyBootData, which sets
    // the Goron/Hylian index for what is worn and loads the OOT boot regs.
    func_80123140(play, player);
    Player_PlaySfx(player, NA_SE_PL_CHANGE_ARMS);
    return 1;
}

extern "C" void Combo_Gear_ApplyBootData(PlayState* play, Player* player) {
    if (!IsGearCapableLink(player)) {
        return;
    }
    s32 boots = Combo_Gear_GetBoots(player);

    // Switch flags (OoTMM): Iron Boots press heavy switches; Hover Boots float over light ones.
    player->actor.flags &= ~ACTOR_FLAG_CAN_PRESS_HEAVY_SWITCHES;
    player->actor.flags |= ACTOR_FLAG_CAN_PRESS_SWITCHES;

    if (boots == COMBO_BOOTS_NONE) {
        // Iron Boots taken off (or lost) while the Goron index is still set: back to Hylian boots, then
        // recompute; this re-entry sees Hylian boots and returns above.
        if (player->currentBoots == PLAYER_BOOTS_GORON) {
            player->currentBoots = PLAYER_BOOTS_HYLIAN;
            func_80123140(play, player);
        }
        return;
    }

    REG(27) = 2000;
    REG(48) = 370;
    s32 row;
    if (boots == COMBO_BOOTS_IRON) {
        player->currentBoots = PLAYER_BOOTS_GORON;
        player->actor.flags |= ACTOR_FLAG_CAN_PRESS_HEAVY_SWITCHES;
        row = (player->stateFlags1 & PLAYER_STATE1_8000000) ? BOOT_ROW_IRON_UNDERWATER : BOOT_ROW_IRON;
        REG(27) = 500;
        REG(48) = 100;
    } else {
        player->currentBoots = PLAYER_BOOTS_HYLIAN;
        player->actor.flags &= ~ACTOR_FLAG_CAN_PRESS_SWITCHES;
        row = BOOT_ROW_HOVER;
    }
    LoadBootRegs(play, kComboBootData[row]);
}

extern "C" void Combo_Gear_UpdateHoverTimer(Player* player, int canHoverOnGround) {
    if ((Combo_Gear_GetBoots(player) == COMBO_BOOTS_HOVER) && (gComboHoverBootsTimer != 0)) {
        gComboHoverBootsTimer--;
    } else {
        gComboHoverBootsTimer = 0;
    }
    if (canHoverOnGround && (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) && (gComboHoverBootsTimer != 0)) {
        player->actor.bgCheckFlags &= ~BGCHECKFLAG_GROUND;
    }
    if ((player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) && !canHoverOnGround) {
        gComboHoverBootsTimer = 19;
    }
}

// OOT's glowing ring under the Hover Boots while Link hovers (Player_Draw, OOT decomp), with OOT's own display list.
extern "C" void Combo_Gear_DrawHoverGlow(PlayState* play, Player* player) {
    static const ALIGN_ASSET(2) char kHoverBootsCircleDL[] = "__OTR__@oot:objects/gameplay_keep/gHoverBootsCircleDL";
    static Vec3s sNoRotation = { 0, 0, 0 };
    static s32 sAlpha = 255;
    if ((Combo_Gear_GetBoots(player) != COMBO_BOOTS_HOVER) || (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ||
        (player->stateFlags1 & PLAYER_STATE1_800000) || (gComboHoverBootsTimer == 0) || (gComboHoverBootsTimer >= 19)) {
        return;
    }
    const s32 timer = gComboHoverBootsTimer;
    if (timer >= 15) {
        sAlpha = (19 - timer) * 51.0f;
    } else {
        const s32 t = (timer > 9) ? 9 : timer;
        sAlpha = (-t * 4) + 36;
        sAlpha = sAlpha * sAlpha;
        sAlpha = (s32)((Math_CosS(sAlpha) * 100.0f) + 100.0f) + 55.0f;
        sAlpha = sAlpha * (t * (1.0f / 9.0f));
    }

    OPEN_DISPS(play->state.gfxCtx);
    FrameInterpolation_RecordActorPosRotMatrix();
    Matrix_Push();
    Matrix_SetTranslateRotateYXZ(player->actor.world.pos.x, player->actor.world.pos.y + 2.0f, player->actor.world.pos.z,
                                 &sNoRotation);
    Matrix_Scale(4.0f, 4.0f, 4.0f, MTXMODE_APPLY);
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, play->state.gfxCtx);
    Matrix_Pop();
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0, 0, 16, 32, 1, 0,
                                             (play->gameplayFrames * -15) % 128, 16, 32, 0, 0, 0, -15));
    gDPSetPrimColor(POLY_XLU_DISP++, 0x80, 0x80, 255, 255, 255, sAlpha);
    gDPSetEnvColor(POLY_XLU_DISP++, 120, 90, 30, 128);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)kHoverBootsCircleDL);
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" float Combo_Gear_MaxDiveDepth(void) {
    static const float kDiveDepths[] = { 120.0f, 240.0f, 360.0f };
    return kDiveDepths[Combo_Gear_GetScale()];
}

extern "C" int Combo_Gear_FloorDamageDelay(int floorDamageType) {
    static const int kDelays[] = { 120, 60 };
    if (floorDamageType < 0 || floorDamageType > 1) {
        return 0;
    }
    return kDelays[floorDamageType];
}

extern "C" void Combo_Gear_OnPlayerUpdate(PlayState* play, Player* player) {
    if ((player == nullptr) || (player->actor.id != ACTOR_PLAYER)) {
        return;
    }
    // Gear can change outside Player_UseItem (cross-game grant/removal, Shared Items reconcile, save
    // editor): keep the boot index consistent with what is worn.
    SyncBootIndex(play, player);
    UpdateTunicColor(player);
    // An MM mask (or a transformation) went on through MM's own code: the OOT mask comes off.
    if (player->currentMask != PLAYER_MASK_NONE) {
        Flags_ClearRandoInf(RANDO_INF_COMBO_WEARING_MASK_GERUDO);
        Flags_ClearRandoInf(RANDO_INF_COMBO_WEARING_MASK_SKULL);
        Flags_ClearRandoInf(RANDO_INF_COMBO_WEARING_MASK_SPOOKY);
    }
    Combo_Spells_OnPlayerUpdate(play, player); // CrossSpells.cpp: Din's Fire / Nayru's Love in progress
    Combo_Tools_OnPlayerUpdate(play, player);  // CrossTools.cpp: Deku Shield burning
    Combo_Rocs_OnPlayerUpdate(play, player);   // CrossRocsFeather.cpp: the jump comes back on the ground
}

// ---- Rando item plumbing (Rando.h) ----

namespace {

// Put a cross-game item into its trade slot if the slot is empty, as Item_Give does for trade items; the
// slot's other contents stay reachable through the KaleidoItemPage cycle. Never calls Item_Give: these ids
// are outside its tables, and this also runs dormant (gPlayState == NULL).
void GrantSlot(u8 itemId) {
    if (INV_CONTENT(itemId) == ITEM_NONE) {
        INV_CONTENT(itemId) = itemId;
    }
}

void RemoveSlot(u8 itemId) {
    if (INV_CONTENT(itemId) == itemId) {
        INV_CONTENT(itemId) = ITEM_NONE;
    }
}

} // namespace

const char* Rando::CrossGearOOTItemName(RandoItemId randoItemId) {
    switch (randoItemId) {
        case RI_BOOTS_IRON:
            return "Iron Boots";
        case RI_BOOTS_HOVER:
            return "Hover Boots";
        case RI_TUNIC_GORON:
            return "Goron Tunic";
        case RI_TUNIC_ZORA:
            return "Zora Tunic";
        case RI_SCALE_SILVER:
            return "Silver Scale";
        case RI_SCALE_GOLDEN:
            return "Golden Scale";
        case RI_STRENGTH_GORONS_BRACELET:
            return "Goron's Bracelet";
        case RI_STRENGTH_SILVER_GAUNTLETS:
            return "Silver Gauntlets";
        case RI_STRENGTH_GOLDEN_GAUNTLETS:
            return "Golden Gauntlets";
        case RI_MASK_GERUDO:
            return "Gerudo Mask";
        case RI_MASK_SKULL:
            return "Skull Mask";
        case RI_MASK_SPOOKY:
            return "Spooky Mask";
        case RI_SPELL_FIRE:
            return "Din's Fire";
        case RI_SPELL_WIND:
            return "Farore's Wind";
        case RI_SPELL_LOVE:
            return "Nayru's Love";
        case RI_HAMMER:
            return "Megaton Hammer";
        case RI_BOOMERANG:
            return "Boomerang";
        case RI_SLINGSHOT:
            return "Fairy Slingshot";
        case RI_SHIELD_DEKU:
            return "Deku Shield";
        case RI_ROCS_FEATHER: // CrossRocsFeather.cpp
            return "Roc's Feather";
        // CrossUpgrades.cpp
        case RI_STONE_OF_AGONY:
            return "Stone of Agony";
        case RI_OCARINA_FAIRY:
            return "Fairy Ocarina";
        case RI_HOOKSHOT_SHORT:
            return "Hookshot";
        case RI_DEKU_STICK_UPGRADE_20:
            return "Deku Stick Capacity (20)";
        case RI_DEKU_STICK_UPGRADE_30:
            return "Deku Stick Capacity (30)";
        case RI_DEKU_NUT_UPGRADE_30:
            return "Deku Nut Capacity (30)";
        case RI_DEKU_NUT_UPGRADE_40:
            return "Deku Nut Capacity (40)";
        default:
            return nullptr;
    }
}

bool Rando::CrossGearGive(RandoItemId randoItemId) {
    switch (randoItemId) {
        case RI_BOOTS_IRON:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_BOOTS_IRON);
            GrantSlot(ITEM_COMBO_BOOTS_IRON);
            return true;
        case RI_BOOTS_HOVER:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_BOOTS_HOVER);
            GrantSlot(ITEM_COMBO_BOOTS_HOVER);
            return true;
        case RI_TUNIC_GORON:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_TUNIC_GORON);
            GrantSlot(ITEM_COMBO_TUNIC_GORON);
            return true;
        case RI_TUNIC_ZORA:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_TUNIC_ZORA);
            GrantSlot(ITEM_COMBO_TUNIC_ZORA);
            return true;
        // Tiers are cumulative, like OOT's upgrade value: a higher tier implies the lower ones.
        case RI_SCALE_GOLDEN:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_SCALE_GOLDEN);
            [[fallthrough]];
        case RI_SCALE_SILVER:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_SCALE_SILVER);
            return true;
        case RI_PROGRESSIVE_SCALE:
            return CrossGearGive(Combo_Gear_GetScale() == 0 ? RI_SCALE_SILVER : RI_SCALE_GOLDEN);
        case RI_STRENGTH_GOLDEN_GAUNTLETS:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_STRENGTH_GOLDEN_GAUNTLETS);
            [[fallthrough]];
        case RI_STRENGTH_SILVER_GAUNTLETS:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_STRENGTH_SILVER_GAUNTLETS);
            [[fallthrough]];
        case RI_STRENGTH_GORONS_BRACELET:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_STRENGTH_GORONS_BRACELET);
            return true;
        case RI_MASK_GERUDO:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_MASK_GERUDO);
            GrantSlot(ITEM_COMBO_MASK_GERUDO);
            return true;
        case RI_MASK_SKULL:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_MASK_SKULL);
            GrantSlot(ITEM_COMBO_MASK_SKULL);
            return true;
        case RI_MASK_SPOOKY:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_MASK_SPOOKY);
            GrantSlot(ITEM_COMBO_MASK_SPOOKY);
            return true;
        case RI_SPELL_FIRE:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_SPELL_FIRE);
            GrantSlot(ITEM_COMBO_SPELL_FIRE);
            return true;
        case RI_SPELL_WIND:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_SPELL_WIND);
            GrantSlot(ITEM_COMBO_SPELL_WIND);
            return true;
        case RI_SPELL_LOVE:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_SPELL_LOVE);
            GrantSlot(ITEM_COMBO_SPELL_LOVE);
            return true;
        case RI_HAMMER:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_HAMMER);
            GrantSlot(ITEM_COMBO_HAMMER);
            return true;
        case RI_BOOMERANG:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_BOOMERANG);
            GrantSlot(ITEM_COMBO_BOOMERANG);
            return true;
        case RI_SLINGSHOT:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_SLINGSHOT);
            GrantSlot(ITEM_COMBO_SLINGSHOT);
            Combo_Tools_ChangeSlingshotSeeds(COMBO_SLINGSHOT_MAX_SEEDS); // comes with a full bag, like OOT's
            return true;
        case RI_SHIELD_DEKU:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_SHIELD_DEKU);
            Combo_Tools_EquipDekuShield();
            return true;
        case RI_ROCS_FEATHER:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_ROCS_FEATHER);
            GrantSlot(ITEM_COMBO_ROCS_FEATHER);
            return true;
        case RI_PROGRESSIVE_STRENGTH: {
            s32 tier = Combo_Gear_GetStrengthOwned();
            return CrossGearGive(tier == 0   ? RI_STRENGTH_GORONS_BRACELET
                                 : tier == 1 ? RI_STRENGTH_SILVER_GAUNTLETS
                                             : RI_STRENGTH_GOLDEN_GAUNTLETS);
        }
        default:
            return false;
    }
}

bool Rando::CrossGearRemove(RandoItemId randoItemId) {
    switch (randoItemId) {
        case RI_BOOTS_IRON:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_BOOTS_IRON);
            Flags_ClearRandoInf(RANDO_INF_COMBO_WEARING_BOOTS_IRON);
            RemoveSlot(ITEM_COMBO_BOOTS_IRON);
            return true;
        case RI_BOOTS_HOVER:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_BOOTS_HOVER);
            Flags_ClearRandoInf(RANDO_INF_COMBO_WEARING_BOOTS_HOVER);
            RemoveSlot(ITEM_COMBO_BOOTS_HOVER);
            return true;
        case RI_TUNIC_GORON:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_TUNIC_GORON);
            Flags_ClearRandoInf(RANDO_INF_COMBO_WEARING_TUNIC_GORON);
            RemoveSlot(ITEM_COMBO_TUNIC_GORON);
            return true;
        case RI_TUNIC_ZORA:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_TUNIC_ZORA);
            Flags_ClearRandoInf(RANDO_INF_COMBO_WEARING_TUNIC_ZORA);
            RemoveSlot(ITEM_COMBO_TUNIC_ZORA);
            return true;
        case RI_SCALE_SILVER:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_SCALE_SILVER);
            [[fallthrough]];
        case RI_SCALE_GOLDEN:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_SCALE_GOLDEN);
            return true;
        case RI_PROGRESSIVE_SCALE:
            return CrossGearRemove(Combo_Gear_GetScale() == 2 ? RI_SCALE_GOLDEN : RI_SCALE_SILVER);
        case RI_STRENGTH_GORONS_BRACELET:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_STRENGTH_GORONS_BRACELET);
            [[fallthrough]];
        case RI_STRENGTH_SILVER_GAUNTLETS:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_STRENGTH_SILVER_GAUNTLETS);
            [[fallthrough]];
        case RI_STRENGTH_GOLDEN_GAUNTLETS:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_STRENGTH_GOLDEN_GAUNTLETS);
            return true;
        case RI_MASK_GERUDO:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_MASK_GERUDO);
            Flags_ClearRandoInf(RANDO_INF_COMBO_WEARING_MASK_GERUDO);
            RemoveSlot(ITEM_COMBO_MASK_GERUDO);
            return true;
        case RI_MASK_SKULL:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_MASK_SKULL);
            Flags_ClearRandoInf(RANDO_INF_COMBO_WEARING_MASK_SKULL);
            RemoveSlot(ITEM_COMBO_MASK_SKULL);
            return true;
        case RI_MASK_SPOOKY:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_MASK_SPOOKY);
            Flags_ClearRandoInf(RANDO_INF_COMBO_WEARING_MASK_SPOOKY);
            RemoveSlot(ITEM_COMBO_MASK_SPOOKY);
            return true;
        case RI_SPELL_FIRE:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_SPELL_FIRE);
            RemoveSlot(ITEM_COMBO_SPELL_FIRE);
            return true;
        case RI_SPELL_WIND:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_SPELL_WIND);
            RemoveSlot(ITEM_COMBO_SPELL_WIND);
            return true;
        case RI_SPELL_LOVE:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_SPELL_LOVE);
            RemoveSlot(ITEM_COMBO_SPELL_LOVE);
            return true;
        case RI_HAMMER:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_HAMMER);
            RemoveSlot(ITEM_COMBO_HAMMER);
            return true;
        case RI_BOOMERANG:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_BOOMERANG);
            RemoveSlot(ITEM_COMBO_BOOMERANG);
            return true;
        case RI_SLINGSHOT:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_SLINGSHOT);
            RemoveSlot(ITEM_COMBO_SLINGSHOT);
            return true;
        case RI_SHIELD_DEKU:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_SHIELD_DEKU);
            Combo_Tools_LoseDekuShield();
            return true;
        case RI_ROCS_FEATHER:
            Flags_ClearRandoInf(RANDO_INF_OBTAINED_ROCS_FEATHER);
            RemoveSlot(ITEM_COMBO_ROCS_FEATHER);
            return true;
        case RI_PROGRESSIVE_STRENGTH: {
            s32 tier = Combo_Gear_GetStrengthOwned();
            return CrossGearRemove(tier >= 3   ? RI_STRENGTH_GOLDEN_GAUNTLETS
                                   : tier == 2 ? RI_STRENGTH_SILVER_GAUNTLETS
                                               : RI_STRENGTH_GORONS_BRACELET);
        }
        default:
            return false;
    }
}

extern "C" void Combo_Gear_DrawBoots(PlayState* play, Player* player) {
    s32 boots = Combo_Gear_GetBoots(player);
    s32 mask = Combo_Gear_GetMask(player);
    if ((boots == COMBO_BOOTS_NONE) && (mask == COMBO_MASK_NONE)) {
        return;
    }
    const bool adult = Combo_AdultLink_Is(player) != 0; // Cross-Games Age: OOT's own adult meshes
    OPEN_DISPS(play->state.gfxCtx);
    if (adult && (boots == COMBO_BOOTS_IRON)) {
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gComboLinkAdultLeftIronBootDL);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gComboLinkAdultRightIronBootDL);
    } else if (adult && (boots == COMBO_BOOTS_HOVER)) {
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gComboLinkAdultLeftHoverBootDL);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gComboLinkAdultRightHoverBootDL);
    } else if (boots == COMBO_BOOTS_IRON) {
        gSPDisplayList(POLY_OPA_DISP++, gComboLinkLeftIronBootDL);
        gSPDisplayList(POLY_OPA_DISP++, gComboLinkRightIronBootDL);
    } else if (boots == COMBO_BOOTS_HOVER) {
        gSPDisplayList(POLY_OPA_DISP++, gComboLinkLeftHoverBootDL);
        gSPDisplayList(POLY_OPA_DISP++, gComboLinkRightHoverBootDL);
    }
    if (mask != COMBO_MASK_NONE) {
        static const char* const kMaskDLs[] = { gComboLinkChildGerudoMaskDL, gComboLinkChildSkullMaskDL,
                                                gComboLinkChildSpookyMaskDL };
        // Head limb matrix (OoTMM's prepareMask: 0x0D0001C0), then OOT's own worn-mask mesh.
        gSPMatrix(POLY_OPA_DISP++, 0x0D0001C1, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        if (adult) {
            // Adult Link's head: the same fit as MM's masks (AdultLink.cpp).
            Matrix_Push();
            Matrix_Translate(0.0f, 0.0f, 0.0f, MTXMODE_NEW);
            Combo_AdultLink_MaskTransform();
            gSPMatrix(POLY_OPA_DISP++, Matrix_Finalize(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW);
            Matrix_Pop();
        }
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)kMaskDLs[mask - COMBO_MASK_GERUDO]);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// OoTMM's MM Player_Draw: on human Link the Goron's Bracelet is OOT child Link's own bracelet, the Silver and Golden
// Gauntlets OoTMM's child-sized plates tinted with the cosmetic editor's gauntlet colors. Adult Link (Cross-Games Age)
// wears OOT's adult gauntlets instead (AdultLink.cpp) and, as in OOT, no bracelet.
extern "C" void Combo_Gear_DrawStrength(PlayState* play, Player* player) {
    static const ALIGN_ASSET(2) char kGoronBraceletDL[] = COMBO_OOT_LINK_CHILD("gLinkChildGoronBraceletDL");
    if (!Combo_Gear_StrengthReplacesHuman() || (Combo_AdultLink_Is(player) != 0)) {
        return;
    }
    const int strength = Combo_Gear_GetStrength();
    if (strength <= 0) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    if (strength == 1) {
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)Combo_Gear_Recolor(kGoronBraceletDL)); // its cosmetic color
    } else {
        const Color_RGB8 color = Combo_Gear_GauntletColor(strength);
        // The gem: the cosmetic editor's "Gauntlets Gem (OoT)", else the plates' own red.
        const Color_RGB8 gem = CosmeticEditor_IsComboColorChanged("Player.OotGauntletsGem")
                                   ? Combo_Gear_CosmeticColor(4)
                                   : Color_RGB8{ 254, 15, 15 };
        Gfx* gemDL = gComboHumanGauntletGemDL;
        gDPSetPrimColor(gemDL++, 0, 0, gem.r, gem.g, gem.b, 255);
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetEnvColor(POLY_OPA_DISP++, color.r, color.g, color.b, 0);
        gSPDisplayList(POLY_OPA_DISP++, gComboHumanLeftGauntletPlate1DL);
        gSPDisplayList(POLY_OPA_DISP++, gComboHumanRightGauntletPlate1DL);
        gSPDisplayList(POLY_OPA_DISP++, gComboHumanLeftGauntletPlate3DL);
        gSPDisplayList(POLY_OPA_DISP++, gComboHumanRightGauntletPlate3DL);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

#endif // COMBO_BUILD
