// mm/2s2h/Rando/MiscBehavior/AdultLink.h
// ComboShip (Cross-Games Age, part 1): adult Link in Majora's Mask. Human Link can be drawn and moved as Ocarina of
// Time's adult Link: MM's Fierce Deity skeleton is byte-for-byte OOT's adult skeleton (same 21 limbs, same joint
// offsets), so adult Link runs on that skeleton with MM's own animations, at human Link's scale (FD's 0.015 is OOT
// adult x1.5), with OOT's adult body measurements, and with every limb drawn from OOT's own adult display lists
// ("@oot:" routing to object_link_boy).
//
// Link is adult in Termina when the Cross-Games Age setting is on and he came from Hyrule as adult (comboLinkAdult in
// the MM save, read when the player actor is created); the developer CVar gCombo.Dev.MmAdultLink forces it. C ABI so
// the decomp C files can call it from small COMBO_BUILD-guarded hooks. See docs/deviations/rando.md.
#ifndef COMBO_ADULT_LINK_H
#define COMBO_ADULT_LINK_H

#ifdef COMBO_BUILD

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;
struct Player;
struct PlayerAgeProperties;

// MM can have an adult Link: the loaded save's (or the fill oracle's) Cross-Games Age option, or the developer CVar
// gCombo.Dev.MmAdultLink (which also forces adult).
int Combo_AdultLink_AgeSystemOn(void);
// The same from the menu's settings, before any save exists (menu, generator dump).
int Combo_AdultLink_AgeSystemConfigured(void);
// Link's age in MM is adult (whatever form he is in): the save's comboLinkAdult.
int Combo_AdultLink_IsAdultAge(void);
// The launcher, before entering Termina from Hyrule: OOT Link's age (1 adult, 0 child; -1 for a save resume).
void Combo_AdultLink_SetEntryAge(int ootAdult);
// title_setup.c, once the MM save is loaded: Link takes that age, swapping each age's C buttons and worn gear.
void Combo_AdultLink_ApplyEntryAge(void);
// Riding Epona: how much lower adult Link sits than MM's riding animations put him (0 for human Link).
float Combo_AdultLink_RiderDrop(struct Player* player);
// After MM gear drawn outside the limb lists (the shield on his back): put adult Link's tunic color back.
void Combo_AdultLink_RestoreTunicColor(struct PlayState* play);
// Player_Init: decides whether this player is adult (human form only). Returns 1 when adult.
int Combo_AdultLink_OnPlayerInit(struct Player* player);
// This player is the adult Link set up by the last Player_Init.
int Combo_AdultLink_Is(struct Player* player);
// OOT's adult body measurements on top of Fierce Deity's entry (whose animations are OOT adult Link's).
struct PlayerAgeProperties* Combo_AdultLink_AgeProperties(const struct PlayerAgeProperties* fierceDeity);
// Player_DrawImpl: eye/mouth textures (segments 0x08/0x09) and the tunic color for the adult head and body.
void Combo_AdultLink_SetupDraw(struct PlayState* play, struct Player* player, int eyeIndex, int mouthIndex);
// After the skeleton draw (segment 0x0D = limb matrices): OOT's Silver/Golden Gauntlet plates.
void Combo_AdultLink_DrawGear(struct PlayState* play, struct Player* player);
// Head-limb space: applies the transform that fits MM's (child-sized) masks onto adult Link's face. Push the matrix
// stack first.
void Combo_AdultLink_MaskTransform(void);
// Reach of the held melee weapon for the sword collider (MM's sMeleeWeaponLengths value passed in).
float Combo_AdultLink_MeleeWeaponLength(struct Player* player, int meleeWeapon, float length);
// OOT adult Link's bottle glass (translucent; tinted by the environment color).
void* Combo_AdultLink_BottleDL(void);
// OOT adult Link's bow string.
void* Combo_AdultLink_BowStringDL(void);
// Sheath-limb space: the transform that sizes MM's back shields for adult Link (push the matrix stack first).
void Combo_AdultLink_GearTransform(void);
// OOT adult Link's closed fist for a hand limb (near/far by lod).
void* Combo_AdultLink_ClosedFistDL(int limbIndex, int lod);
// The player's limb override already gave this hand limb an adult model (a cross-game tool): leave it this frame.
void Combo_AdultLink_KeepHand(int limbIndex);
// After the player's limb override: swap whatever MM chose for that limb with OOT's adult display list.
// firstPerson: the first-person arms are being drawn (aiming), which OOT draws with their own models.
void Combo_AdultLink_RemapLimb(struct PlayState* play, struct Player* player, int limbIndex, void** dList, int lod,
                               int firstPerson);

#ifdef __cplusplus
}
#endif

#endif // COMBO_BUILD
#endif // COMBO_ADULT_LINK_H
