// ComboShip (cross-game items): the Kokiri Sword upgrades to Majora's Mask's Razor and Gilded Swords, like MM's
// progressive sword (OoTMM's extraChildSwordsOot). Item side: RSK_COMBO_CHILD_SWORDS adds two Progressive Kokiri
// Swords to the pool, and the Kokiri Sword and both copies resolve to the next tier when given (item.cpp); the tier
// is RAND_INF_OBTAINED_COMBO_RAZOR_SWORD / _GILDED_SWORD. Equipment stays the Kokiri Sword (B, equip flags, logic).
//
// This file is the gameplay side, all through existing hooks:
// - Damage: a Kokiri Sword hit (slash, spin, jump) does 2x with the Razor Sword and 3x with the Gilded Sword
//   (Combo_ChildSwordDamage, from CollisionCheck_ApplyDamage). Hits the Kokiri Sword can't hurt stay harmless.
// - Models: child Link's sword hand and sheath use MM's own Razor/Gilded Sword display lists ("@mm:" routing), with
//   OOT's Deku Shield on the back where the OOT sheath carried one. A Hylian Shield on the back keeps OOT's sheath
//   (OOT has no separate child Hylian-Shield-on-back model).
// - Icons: the Kokiri Sword's icon (B button, equipment page) and pause-menu name become MM's Razor/Gilded Sword.
#ifdef COMBO_BUILD
#include "soh/ShipInit.hpp"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/randomizer/randomizer.h"

#include <cstring>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "align_asset_macro.h"
extern PlayState* gPlayState;
Gfx* ResourceMgr_LoadGfxByName(const char* path);
}

extern "C" int Combo_ChildSwordTier(void);

#define COMBO_MM_LINK(name) "__OTR__@mm:objects/object_link_child/" name
#define COMBO_OOT_LINK_CHILD(name) "__OTR__objects/object_link_child/" name

namespace {

// [tier - 1]: Razor, Gilded.
const ALIGN_ASSET(2) char kMmHandRazor[] = COMBO_MM_LINK("gLinkHumanLeftHandHoldingRazorSwordDL");
const ALIGN_ASSET(2) char kMmHandGilded[] = COMBO_MM_LINK("gLinkHumanLeftHandHoldingGildedSwordDL");
const ALIGN_ASSET(2) char kMmSheathedRazor[] = COMBO_MM_LINK("gLinkHumanSheathedRazorSwordDL");
const ALIGN_ASSET(2) char kMmSheathedGilded[] = COMBO_MM_LINK("gLinkHumanSheathedGildedSwordDL");
const ALIGN_ASSET(2) char kMmSheathRazor[] = COMBO_MM_LINK("gLinkHumanRazorSwordSheathDL");
const ALIGN_ASSET(2) char kMmSheathGilded[] = COMBO_MM_LINK("gLinkHumanGildedSwordSheathDL");
const ALIGN_ASSET(2) char kMmIconRazor[] = "__OTR__@mm:icon_item_static_yar/gItemIconRazorSwordTex";
const ALIGN_ASSET(2) char kMmIconGilded[] = "__OTR__@mm:icon_item_static_yar/gItemIconGildedSwordTex";
const ALIGN_ASSET(2) char kMmNameRazor[] = "__OTR__@mm:item_name_static/gItemNameRazorSwordENGTex";
const ALIGN_ASSET(2) char kMmNameGilded[] = "__OTR__@mm:item_name_static/gItemNameGildedSwordENGTex";
const ALIGN_ASSET(2) char kOotDekuShieldOnBack[] = COMBO_OOT_LINK_CHILD("gLinkChildDekuShieldWithMatrixDL");

const char* const kHand[2] = { kMmHandRazor, kMmHandGilded };
const char* const kSheathed[2] = { kMmSheathedRazor, kMmSheathedGilded };
const char* const kSheathEmpty[2] = { kMmSheathRazor, kMmSheathGilded };
const char* const kIcon[2] = { kMmIconRazor, kMmIconGilded };
const char* const kName[2] = { kMmNameRazor, kMmNameGilded };

// OOT child hand holding the Kokiri Sword (near and far LOD).
const char* const kOotHands[] = {
    COMBO_OOT_LINK_CHILD("gLinkChildLeftFistAndKokiriSwordNearDL"),
    COMBO_OOT_LINK_CHILD("gLinkChildLeftFistAndKokiriSwordFarDL"),
};

// OOT child sheath display lists (near and far LOD) and what each one shows.
struct OotSheath {
    const char* path;
    bool swordInSheath;
    bool dekuShield;
};
const OotSheath kOotSheaths[] = {
    { COMBO_OOT_LINK_CHILD("gLinkChildSwordAndSheathNearDL"), true, false },
    { COMBO_OOT_LINK_CHILD("gLinkChildSwordAndSheathFarDL"), true, false },
    { COMBO_OOT_LINK_CHILD("gLinkChildDekuShieldSwordAndSheathNearDL"), true, true },
    { COMBO_OOT_LINK_CHILD("gLinkChildDekuShieldSwordAndSheathFarDL"), true, true },
    { COMBO_OOT_LINK_CHILD("gLinkChildSheathNearDL"), false, false },
    { COMBO_OOT_LINK_CHILD("gLinkChildSheathFarDL"), false, false },
    { COMBO_OOT_LINK_CHILD("gLinkChildDekuShieldAndSheathNearDL"), false, true },
    { COMBO_OOT_LINK_CHILD("gLinkChildDekuShieldAndSheathFarDL"), false, true },
};

void* sKokiriIcon = nullptr; // OOT's own gItemIcons[ITEM_SWORD_KOKIRI], restored at tier 0
// Link's tunic color this frame (VB_APPLY_TUNIC_COLOR). MM's sword and sheath display lists set their own environment
// color, which OOT's tunic is shaded with: limbs drawn after them (right arm, torso) would turn black or blue.
Color_RGB8 sTunicColor = { 30, 105, 27 };

// Display lists submitted through the runtime gSPDisplayList, which turns an "@mm:" path into a routed call. OOT's
// tunic color is put back afterwards for the limbs drawn later.
Gfx* CallLists(PlayState* play, const char* first, const char* second) {
    Gfx* buf = (Gfx*)Graph_Alloc(play->state.gfxCtx, 5 * sizeof(Gfx));
    Gfx* p = buf;
    gSPDisplayList(p++, (Gfx*)first);
    if (second != nullptr) {
        gSPDisplayList(p++, (Gfx*)second);
    }
    gDPPipeSync(p++);
    gDPSetEnvColor(p++, sTunicColor.r, sTunicColor.g, sTunicColor.b, 0);
    gSPEndDisplayList(p++);
    return buf;
}

// Child Link with the Kokiri Sword on B: the models to replace.
bool SwordModelsApply(Player* player, PlayState* play) {
    return (play != nullptr) && (player == GET_PLAYER(play)) && !LINK_IS_ADULT &&
           (gSaveContext.equips.buttonItems[0] == ITEM_SWORD_KOKIRI) && (Combo_ChildSwordTier() > 0);
}

// Gameplay hands over a loaded display list; the pause-menu Link hands over the resource path itself.
bool IsOotDList(Gfx* dList, const char* path) {
    const char* str = (const char*)dList;
    if (strncmp(str, "__OTR__", 7) == 0) {
        return strcmp(str, path) == 0;
    }
    return dList == ResourceMgr_LoadGfxByName(path);
}

void OverrideLimb(s32 limbIndex, Gfx** dList, Player* player, PlayState* play) {
    if ((dList == nullptr) || (*dList == nullptr) || !SwordModelsApply(player, play)) {
        return;
    }
    const int t = Combo_ChildSwordTier() - 1;
    if (limbIndex == PLAYER_LIMB_L_HAND) {
        for (const char* hand : kOotHands) {
            if (IsOotDList(*dList, hand)) {
                *dList = CallLists(play, kHand[t], nullptr);
                return;
            }
        }
    } else if (limbIndex == PLAYER_LIMB_SHEATH) {
        for (const OotSheath& s : kOotSheaths) {
            if (IsOotDList(*dList, s.path)) {
                *dList = CallLists(play, s.swordInSheath ? kSheathed[t] : kSheathEmpty[t],
                                   s.dekuShield ? kOotDekuShieldOnBack : nullptr);
                return;
            }
        }
    }
}

void UpdateIcon() {
    if (sKokiriIcon == nullptr) {
        sKokiriIcon = gItemIcons[ITEM_SWORD_KOKIRI];
    }
    const int tier = Combo_ChildSwordTier();
    gItemIcons[ITEM_SWORD_KOKIRI] = (tier > 0) ? (void*)kIcon[tier - 1] : sKokiriIcon;
}

} // namespace

extern "C" int Combo_ChildSwordTier(void) {
    if (!IS_RANDO) {
        return 0;
    }
    if (Flags_GetRandomizerInf(RAND_INF_OBTAINED_COMBO_GILDED_SWORD)) {
        return 2;
    }
    return Flags_GetRandomizerInf(RAND_INF_OBTAINED_COMBO_RAZOR_SWORD) ? 1 : 0;
}

extern "C" f32 Combo_ChildSwordDamage(u32 dmgFlags, f32 damage) {
    if ((dmgFlags & (DMG_SLASH_KOKIRI | DMG_SPIN_KOKIRI | DMG_JUMP_KOKIRI)) == 0) {
        return damage;
    }
    return damage * (f32)(1 + Combo_ChildSwordTier());
}

static void RegisterComboChildSwords() {
    // Not gated on the seed option: the tier flags are only ever set when it was on (item.cpp), and the
    // option may not be applied yet when this registers (see ComboMmSongs.cpp).
    const bool on = IS_RANDO;
    COND_VB_SHOULD(VB_PLAYER_OVERRIDE_LIMB_DRAW, on, {
        s32 limbIndex = va_arg(args, s32);
        Gfx** dList = va_arg(args, Gfx**);
        Player* player = (Player*)va_arg(args, void*);
        PlayState* play = va_arg(args, PlayState*);
        OverrideLimb(limbIndex, dList, player, play);
    });
    COND_VB_SHOULD(VB_PLAYER_OVERRIDE_LIMB_DRAW_PAUSE, on, {
        s32 limbIndex = va_arg(args, s32);
        Gfx** dList = va_arg(args, Gfx**);
        Player* player = (Player*)va_arg(args, void*);
        PlayState* play = va_arg(args, PlayState*);
        OverrideLimb(limbIndex, dList, player, play);
    });
    // Pause-menu name: MM's own name texture.
    COND_VB_SHOULD(VB_DRAW_CUSTOM_ITEM_NAME, on, {
        u32 namedItem = va_arg(args, u32);
        const int tier = Combo_ChildSwordTier();
        if ((namedItem != ITEM_SWORD_KOKIRI) || (tier == 0) || *should || (gPlayState == nullptr)) {
            return;
        }
        *should = true;
        memcpy(gPlayState->pauseCtx.nameSegment, kName[tier - 1], strlen(kName[tier - 1]) + 1);
    });
    COND_VB_SHOULD(VB_APPLY_TUNIC_COLOR, on, {
        va_arg(args, void*); // the skeleton's draw data
        Color_RGB8* color = va_arg(args, Color_RGB8*);
        if (color != nullptr) {
            sTunicColor = *color;
        }
    });
    COND_HOOK(OnGameFrameUpdate, true, UpdateIcon); // also restores OOT's icon outside rando
}

static RegisterShipInitFunc initFunc(RegisterComboChildSwords, { "IS_RANDO" });
#endif
