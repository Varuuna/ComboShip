// mm/2s2h/Rando/MiscBehavior/CrossGear.h
// ComboShip (cross-game items, OoTMM-style "Iron Boots (MM)" etc.): Ocarina of Time equipment usable in
// Majora's Mask by human Link. C ABI so the vendored decomp C files (z_player.c, z_player_lib.c,
// z_parameter.c) can call it from small COMBO_BUILD-guarded hooks; the logic lives in CrossGear.cpp.
//
// Ownership and what Link wears are RandoInf flags (Types.h, RANDO_INF_OBTAINED_* / RANDO_INF_COMBO_WEARING_*),
// so the state saves with the slot and the combo oracle can set it headlessly. Boots and tunics are used
// from a C button (ITEM_COMBO_* ids, z64item.h) to toggle them on and off, like OoTMM.
//
// Gameplay ported from OoTMM's MM Player patches (MIT, https://github.com/OoTMM/OoTMM,
// packages/generator/src/mm/actors/Player.c) onto 2ship's decomp names; see docs/deviations/rando.md.
#ifndef COMBO_CROSS_GEAR_H
#define COMBO_CROSS_GEAR_H

#ifdef COMBO_BUILD

#include "color.h"

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;
struct Player;

typedef enum {
    COMBO_BOOTS_NONE,
    COMBO_BOOTS_IRON,
    COMBO_BOOTS_HOVER,
} ComboBoots;

typedef enum {
    COMBO_TUNIC_NONE,
    COMBO_TUNIC_GORON,
    COMBO_TUNIC_ZORA,
} ComboTunic;

typedef enum {
    COMBO_MASK_NONE,
    COMBO_MASK_GERUDO,
    COMBO_MASK_SKULL,
    COMBO_MASK_SPOOKY,
} ComboMask;

// OOT mask worn by human Link (exclusive with MM's own masks: wearing an MM mask takes it off).
int Combo_Gear_GetMask(struct Player* player);
// GET_PLAYER(play) wears that OOT mask.
int Combo_Gear_IsWearingMask(struct PlayState* play, int mask);
// Player_GetMask for actors where OoTMM makes the Skull Mask count as the Stone Mask (Keese, Guays, Bad
// Bats, Deku Palace lookouts): returns PLAYER_MASK_STONE while it is worn.
int Combo_Gear_GetMaskSkullAsStone(struct PlayState* play);

// Boots/tunic actually in effect: owned, worn, and Link is human (not wearing the Giant's Mask).
int Combo_Gear_GetBoots(struct Player* player);
int Combo_Gear_GetTunic(struct Player* player);
// Passive upgrades. Scale: 0 none, 1 silver, 2 golden. Strength: 0 none .. 3 golden gauntlets.
int Combo_Gear_GetScale(void);
int Combo_Gear_GetStrength(void);
// The strength upgrades owned (0 none, 1 Goron's Bracelet, 2 Silver, 3 Golden), ignoring age requirements.
int Combo_Gear_GetStrengthOwned(void);
// RO_CROSS_STRENGTH is on: human Link's strength is OOT's (none without the Goron's Bracelet), like OoTMM.
int Combo_Gear_StrengthReplacesHuman(void);

// Player_UseItem: toggles boots/tunic for an ITEM_COMBO_* id. Returns 1 when the item was a cross-game
// item (handled, including "can't use now"), 0 to let vanilla handle it.
int Combo_CrossItem_TryUse(struct PlayState* play, struct Player* player, int item);

// End of func_80123140 (Player_SetBootData): re-assert the boot index and the OOT boot physics.
void Combo_Gear_ApplyBootData(struct PlayState* play, struct Player* player);

// Hover Boots. The timer is OOT's Player::hoverBootsTimer (MM's struct has no such field).
extern unsigned char gComboHoverBootsTimer;
// Port of OOT's Player_UpdateHoverBoots preamble: ticks the timer and clears the ground flag while hovering
// over water/void. `canHoverOnGround` comes from the caller (it needs z_player.c's static floor type).
void Combo_Gear_UpdateHoverTimer(struct Player* player, int canHoverOnGround);
// Max dive depth for human Link (vanilla 120; silver scale 240; golden 360, as OoTMM).
float Combo_Gear_MaxDiveDepth(void);
// Goron Tunic: frames Link may stand on a damaging floor before taking damage (OOT's sFloorDamageDelay).
int Combo_Gear_FloorDamageDelay(int floorDamageType);
// Per-frame cosmetic refresh (tunic color); called from Player_Update.
void Combo_Gear_OnPlayerUpdate(struct PlayState* play, struct Player* player);
// CrossSpells.cpp: cast an ITEM_COMBO_SPELL_* (Din's Fire, Farore's Wind, Nayru's Love); 1 if handled.
int Combo_Spell_Cast(struct PlayState* play, struct Player* player, int item);
// CrossSpells.cpp: per-frame update of spells in progress (from Combo_Gear_OnPlayerUpdate).
void Combo_Spells_OnPlayerUpdate(struct PlayState* play, struct Player* player);
// Draws worn OOT gear (boots on the feet, masks on the head); called after the skeleton draw while segment
// 0x0D holds Link's limb matrices.
void Combo_Gear_DrawBoots(struct PlayState* play, struct Player* player);
// The Goron's Bracelet / Silver and Golden Gauntlets on human Link (after the boots, same limb matrices).
void Combo_Gear_DrawStrength(struct PlayState* play, struct Player* player);
// OOT tunic colors (Goron/Zora from the cosmetic editor's "(OoT)" options; kokiri for anything else) and gauntlet
// colors (2 silver, 3 golden).
Color_RGB8 Combo_Gear_TunicColor(int tunic, Color_RGB8 kokiri);
Color_RGB8 Combo_Gear_GauntletColor(int strength);
// CrossGearColors.cpp: MM's cosmetic colors for OOT's Link models. Recolor returns a copy of an "@oot:" display list
// with the set colors (adult hair, linen, boots, the Goron's Bracelet, gauntlet gems), or the list itself.
const void* Combo_Gear_Recolor(const void* dList);
// 3 = Goron's Bracelet, 4 = gauntlet gem (the cosmetic color, or the model's own).
Color_RGB8 Combo_Gear_CosmeticColor(int option);
// Player_Draw: OOT's glowing ring under the Hover Boots while Link hovers.
void Combo_Gear_DrawHoverGlow(struct PlayState* play, struct Player* player);

// ---- CrossTools.cpp: Megaton Hammer, Boomerang, Fairy Slingshot and Deku Shield for human Link ----
#define COMBO_SLINGSHOT_MAX_SEEDS 30 // OOT's first bullet bag
// Player_ItemToItemAction for ITEM_COMBO_* ids: the MM item action that drives a tool (Hammer: two-handed sword,
// Boomerang: PLAYER_IA_11, Slingshot: bow) for human Link, PLAYER_IA_NONE for anything else.
int Combo_Tools_ItemAction(struct Player* player, int item);
// Human Link holds that tool (heldItemId, with its item action in effect).
int Combo_Tools_IsHeld(struct Player* player, int item);
int Combo_Tools_OwnsSlingshot(void);
int Combo_Tools_GetSlingshotSeeds(void);
void Combo_Tools_ChangeSlingshotSeeds(int delta);
// An arrow drop was collected: it refills the Slingshot's seeds too while it is owned.
void Combo_Tools_OnArrowPickup(int arrows);
// Deku Shield: equipped in the Hero's Shield value (only into an empty shield slot) with a RandoInf marker.
void Combo_Tools_EquipDekuShield(void);
void Combo_Tools_LoseDekuShield(void);
// Item_Give of a Hero's/Mirror Shield: drops the Deku marker; 1 when the equipped shield was the Deku Shield.
int Combo_Tools_ReplaceDekuShield(void);
// Per-frame (from Combo_Gear_OnPlayerUpdate): the Deku Shield burns away.
void Combo_Tools_OnPlayerUpdate(struct PlayState* play, struct Player* player);
// Model overrides for human Link (Gfx*, or NULL to keep MM's): a tool drawn after MM's closed left fist, a whole OOT
// right hand, the first-person slingshot arm, the slingshot string, the Deku Shield on the back.
void* Combo_Tools_LeftHandToolDL(struct Player* player, int leftHandType);
void* Combo_Tools_RightHandDL(struct Player* player, int rightHandType);
// Cross-Games Age: what adult Link holds for a cross-game tool on that hand limb. *wholeHand = 1: a complete hand
// (OOT's adult Hammer hand); otherwise just the tool, drawn after adult Link's closed fist. NULL: nothing to change.
void* Combo_Tools_AdultHandToolDL(struct Player* player, int limbIndex, int handType, int* wholeHand);
void* Combo_Tools_FirstPersonRightHandDL(struct Player* player);
// Cross-Games Age: adult Link's first-person right hand aiming the Slingshot (a frame-allocated list), or NULL.
void* Combo_Tools_AdultFirstPersonRightHandDL(struct PlayState* play, struct Player* player);
void* Combo_Tools_SlingshotStringDL(struct Player* player);
void* Combo_Tools_BackShieldDL(struct Player* player);

// ---- CrossUpgrades.cpp: Stone of Agony, Fairy Ocarina, Short Hookshot, Deku Stick & Nut upgrades ----
// The inventory's ocarina is still OOT's Fairy Ocarina / the hookshot is still the Short Hookshot.
int Combo_Upgrades_OcarinaIsFairy(void);
int Combo_Upgrades_HookshotIsShort(void);
// Player_DetectSecrets is about to rumble: 0 to stay silent (Stone of Agony option on, not owned yet); with the Stone
// it also flashes OOT's Stone of Agony icon.
int Combo_Upgrades_SecretRumble(void);

#ifdef __cplusplus
}
#endif

#endif // COMBO_BUILD
#endif // COMBO_CROSS_GEAR_H
