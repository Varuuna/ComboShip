// mm/2s2h/Rando/MiscBehavior/CrossTools.cpp
// ComboShip (cross-game items): Ocarina of Time tools used by human Link in Majora's Mask, the OoTMM
// "Megaton Hammer (MM)", "Boomerang (MM)", "Slingshot (MM)" and "Deku Shield (MM)" settings.
//
// The held tools ride on MM item actions that already do the job, so the vanilla player code drives them:
//   - Megaton Hammer: PLAYER_IA_SWORD_TWO_HANDED (the Great Fairy's Sword swings), with Goron-punch damage.
//   - Boomerang: PLAYER_IA_11, OOT's own boomerang action left in MM (reused there for Zora Link's fins); human Link
//     gets OOT's animations and one boomerang (z_player.c), drawn with OOT's model (z_en_boom.c).
//   - Fairy Slingshot: PLAYER_IA_BOW, firing EnArrow's slingshot seed from its own Deku Seed count.
// The Deku Shield is equipped in the Hero's Shield value with a RandoInf marker; it draws as OOT's and burns away.
// Models are OOT's own, loaded at runtime from the player's extracted OOT archive: whole OOT display lists by "@oot:"
// routed path, or (Hammer, held Boomerang) OOT's hand-holding-the-tool display list cut where OOT's fist begins, the
// same cuts as OoTMM's custom objects. Nothing of OOT's is compiled in.
//
// Behavior follows OoTMM's MM patches (MIT License, Copyright (c) 2020-2022 OoTMM Team, https://github.com/OoTMM/OoTMM,
// packages/generator/src/mm/actors/Player.c, slingshot.c, boomerang.c). See docs/deviations/rando.md.
#ifdef COMBO_BUILD

#include "CrossGear.h"
#include "2s2h/Rando/Rando.h"

#include <cstring>
#include <memory>
#include <vector>
#include <fast/resource/ResourceType.h>
#include <fast/resource/type/DisplayList.h>
#include <ship/resource/CrossRMRegistry.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/ResourceManagerScope.h>
#include <ship/resource/archive/ArchiveManager.h>

extern "C" {
#include "variables.h"
#include "functions.h"
#include "macros.h"
#include "sfx.h"
#include "z64player.h"
#include "align_asset_macro.h"
}

#define COMBO_OOT_LINK_CHILD_DL(name) "__OTR__@oot:objects/object_link_child/" name
static const ALIGN_ASSET(2) char sSlingshotHandDL[] =
    COMBO_OOT_LINK_CHILD_DL("gLinkChildRightHandHoldingSlingshotNearDL");
static const ALIGN_ASSET(2) char sSlingshotFirstPersonDL[] =
    COMBO_OOT_LINK_CHILD_DL("gLinkChildRightArmStretchedSlingshotDL");
static const ALIGN_ASSET(2) char sSlingshotStringDL[] = COMBO_OOT_LINK_CHILD_DL("gLinkChildSlinghotStringDL");
static const ALIGN_ASSET(2) char sDekuShieldHandDL[] =
    COMBO_OOT_LINK_CHILD_DL("gLinkChildRightFistAndDekuShieldNearDL");
static const ALIGN_ASSET(2) char sDekuShieldBackDL[] = COMBO_OOT_LINK_CHILD_DL("gLinkChildDekuShieldWithMatrixDL");
// CrossUpgrades.cpp: OOT's Fairy Ocarina in human Link's hand while he plays it.
static const ALIGN_ASSET(2) char sFairyOcarinaHandDL[] =
    COMBO_OOT_LINK_CHILD_DL("gLinkChildRightHandHoldingFairyOcarinaNearDL");
// Cross-Games Age: OOT adult Link's hand holding the Megaton Hammer.
static const ALIGN_ASSET(2) char sHammerAdultHandDL[] =
    "__OTR__@oot:objects/object_link_boy/gLinkAdultLeftHandHoldingHammerNearDL";
// The textured half of OOT's held boomerang (OoTMM makeEqBoomerang appends it to the cut fist list).
static const ALIGN_ASSET(2) char sBoomerangTexturedDL[] = COMBO_OOT_LINK_CHILD_DL("gLinkChildDL_18580");

namespace {

bool IsHumanLink(Player* player) {
    return (player != nullptr) && (player->actor.id == ACTOR_PLAYER) && (player->transformation == PLAYER_FORM_HUMAN) &&
           (player->currentMask != PLAYER_MASK_GIANT);
}

// OOT's damage effect 1 (fire) on what hit the shield (OoTMM Player_HandleBurningDekuShield).
bool ShieldHitByFire(Collider* collider, ColliderElement* elem) {
    return (collider->acFlags & (AC_HIT | AC_BOUNCED)) && (elem->acHitElem != nullptr) &&
           (elem->acHitElem->atDmgInfo.effect == 1);
}

// ---- OOT display lists cut where OOT's fist begins ----
// LUS keeps a loaded display list as Gfx words; these opcodes take two (DisplayListFactory's "expanded" commands).
bool IsTwoWordOpcode(uint8_t op) {
    return (op == G_SETTIMG_OTR_HASH) || (op == G_DL_OTR_HASH) || (op == G_VTX_OTR_HASH) || (op == G_BRANCH_Z_OTR) ||
           (op == G_MARKER) || (op == G_MTX_OTR) || (op == G_MOVEMEM_OTR);
}

const char* TextureName(const std::shared_ptr<Ship::ResourceManager>& rm, const std::vector<Gfx>& in, size_t i) {
    const uint8_t op = (uint8_t)(in[i].words.w0 >> 24);
    if ((op == G_SETTIMG_OTR_HASH) && (i + 1 < in.size())) {
        uint64_t hash = ((uint64_t)(uint32_t)in[i + 1].words.w0 << 32) | (uint64_t)(uint32_t)in[i + 1].words.w1;
        return rm->GetArchiveManager()->HashToCString(hash);
    }
    if (op == G_SETTIMG_OTR_FILEPATH) {
        return (const char*)in[i].words.w1;
    }
    return nullptr;
}

struct CutDL {
    bool tried = false;
    std::shared_ptr<Ship::IResource> res; // keeps the OOT resource (and its path strings) alive
    std::vector<Gfx> gfx;
};
CutDL sHammerCut;
CutDL sBoomerangCut;
// Cross-Games Age: OOT child tools without child Link's hand, for adult Link's fist.
CutDL sSlingshotAdultCut;
CutDL sDekuShieldAdultCut;
CutDL sFairyOcarinaAdultCut;
CutDL sAdultAimHandCut; // OOT adult Link's first-person bow hand without the bow

void ReleaseCuts() { // CrossRMRegistry teardown: drop cross-module refs while OOT's RM is still mapped
    sHammerCut = CutDL();
    sBoomerangCut = CutDL();
    sSlingshotAdultCut = CutDL();
    sDekuShieldAdultCut = CutDL();
    sFairyOcarinaAdultCut = CutDL();
    sAdultAimHandCut = CutDL();
}

// `path`'s commands up to the material that loads a texture named like `fistTexture`, backed up over the pipe sync /
// othermode / texture commands that open that material, then `tail` (if any) and an end. Drawn inside a
// G_COMBO_RM_PUSH("oot") bracket, so the copied hash references resolve against OOT's archive.
// How CutOotDL separates the tool from the hand.
enum CutMode {
    CUT_BEFORE_TEXTURE,    // keep the commands before `texture` is loaded (the tool is drawn first)
    DROP_TRIANGLES_BEFORE, // keep everything, minus the triangles drawn before `texture` (the hand is drawn first;
                           // its texture, palette and vertex state stay, the tool may use them)
};

Gfx* CutOotDL(CutDL& cut, const char* path, const char* fistTexture, const char* tail,
              CutMode mode = CUT_BEFORE_TEXTURE) {
    if (cut.tried) {
        return cut.gfx.empty() ? nullptr : cut.gfx.data();
    }
    cut.tried = true;
    auto rm = Ship::CrossRMRegistry::Get("oot");
    if (rm == nullptr) {
        cut.tried = false; // OOT not resident yet: try again later
        return nullptr;
    }
    static bool sListening = false;
    if (!sListening) {
        sListening = true;
        Ship::CrossRMRegistry::RegisterTeardownListener(ReleaseCuts);
    }
    {
        Ship::ResourceManagerScope rmScope(rm);
        cut.res = rm->LoadResource(path);
    }
    std::shared_ptr<Fast::DisplayList> dl;
    if ((cut.res != nullptr) &&
        (cut.res->GetInitData()->Type == static_cast<uint32_t>(Fast::ResourceType::DisplayList))) {
        dl = std::static_pointer_cast<Fast::DisplayList>(cut.res);
    }
    if (dl == nullptr) {
        SPDLOG_ERROR("[ComboShip] cross-game tool model missing from the OOT archive: {}", path);
        return nullptr;
    }
    const std::vector<Gfx>& in = dl->Instructions;
    std::vector<size_t> starts; // word index of each command
    size_t cutAt = SIZE_MAX;    // index into `starts` of the first command left out
    for (size_t i = 0; i < in.size();) {
        const uint8_t op = (uint8_t)(in[i].words.w0 >> 24);
        starts.push_back(i);
        const char* tex = TextureName(rm, in, i);
        if ((tex != nullptr) && (strstr(tex, fistTexture) != nullptr)) {
            cutAt = starts.size() - 1;
            break;
        }
        if (op == G_ENDDL) {
            break;
        }
        i += IsTwoWordOpcode(op) ? 2 : 1;
    }
    if (cutAt == SIZE_MAX) {
        SPDLOG_ERROR("[ComboShip] no '{}' material in {}; not drawing that tool", fistTexture, path);
        return nullptr;
    }
    if (mode == DROP_TRIANGLES_BEFORE) {
        for (size_t k = 0; k < starts.size(); k++) {
            const size_t i = starts[k];
            const uint8_t op = (uint8_t)(in[i].words.w0 >> 24);
            const bool triangle = (op == G_TRI1) || (op == G_TRI2) || (op == G_QUAD);
            if ((k >= cutAt) || !triangle) {
                const size_t words = IsTwoWordOpcode(op) ? 2 : 1;
                cut.gfx.insert(cut.gfx.end(), in.begin() + i, in.begin() + i + words);
            }
        }
        // The rest of the list, after the texture that ended the scan.
        const size_t restAt = starts.back() + (IsTwoWordOpcode((uint8_t)(in[starts.back()].words.w0 >> 24)) ? 2 : 1);
        for (size_t i = restAt; i < in.size();) {
            const uint8_t op = (uint8_t)(in[i].words.w0 >> 24);
            if (op == G_ENDDL) {
                break;
            }
            const size_t words = IsTwoWordOpcode(op) ? 2 : 1;
            cut.gfx.insert(cut.gfx.end(), in.begin() + i, in.begin() + i + words);
            i += words;
        }
        Gfx end = gsSPEndDisplayList();
        cut.gfx.push_back(end);
        return cut.gfx.data();
    }
    while (cutAt > 0) {
        const uint8_t op = (uint8_t)(in[starts[cutAt - 1]].words.w0 >> 24);
        if ((op != G_RDPPIPESYNC) && (op != G_SETOTHERMODE_H) && (op != G_TEXTURE)) {
            break;
        }
        cutAt--;
    }
    cut.gfx.assign(in.begin(), in.begin() + starts[cutAt]);
    if (tail != nullptr) {
        // A routed path is called as G_DL_OTR_FILEPATH (what MM's runtime gSPDisplayList emits for one); a plain
        // G_DL would run the path string itself as commands.
        Gfx call = gsDma1p(G_DL_OTR_FILEPATH, tail, 0, G_DL_PUSH);
        cut.gfx.push_back(call);
    }
    Gfx end = gsSPEndDisplayList();
    cut.gfx.push_back(end);
    return cut.gfx.data();
}

} // namespace

extern "C" int Combo_Tools_ItemAction(Player* player, int item) {
    if (!IsHumanLink(player)) {
        return PLAYER_IA_NONE;
    }
    switch (item) {
        case ITEM_COMBO_HAMMER:
            return PLAYER_IA_SWORD_TWO_HANDED;
        case ITEM_COMBO_BOOMERANG:
            return PLAYER_IA_11;
        case ITEM_COMBO_SLINGSHOT:
            return PLAYER_IA_BOW;
        default:
            return PLAYER_IA_NONE;
    }
}

extern "C" int Combo_Tools_IsHeld(Player* player, int item) {
    return IsHumanLink(player) && (player->heldItemId == item) &&
           (player->heldItemAction == Combo_Tools_ItemAction(player, item));
}

// ---- Fairy Slingshot seeds (RandoSaveInfo::comboSlingshotSeeds; OOT's first bullet bag holds 30) ----

extern "C" int Combo_Tools_OwnsSlingshot(void) {
    return IS_RANDO && Flags_GetRandoInf(RANDO_INF_OBTAINED_SLINGSHOT);
}

extern "C" int Combo_Tools_GetSlingshotSeeds(void) {
    u8& seeds = gSaveContext.save.shipSaveInfo.rando.comboSlingshotSeeds;
    if (seeds > COMBO_SLINGSHOT_MAX_SEEDS) {
        seeds = COMBO_SLINGSHOT_MAX_SEEDS;
    }
    return seeds;
}

extern "C" void Combo_Tools_ChangeSlingshotSeeds(int delta) {
    int seeds = Combo_Tools_GetSlingshotSeeds() + delta;
    gSaveContext.save.shipSaveInfo.rando.comboSlingshotSeeds = (u8)CLAMP(seeds, 0, COMBO_SLINGSHOT_MAX_SEEDS);
}

extern "C" void Combo_Tools_OnArrowPickup(int arrows) {
    if (Combo_Tools_OwnsSlingshot()) {
        Combo_Tools_ChangeSlingshotSeeds(arrows);
    }
}

// ---- Deku Shield (the Hero's Shield value + RANDO_INF_COMBO_SHIELD_IS_DEKU) ----

static void RefreshShieldModel() {
    if (gPlayState != nullptr) {
        Player_SetEquipmentData(gPlayState, GET_PLAYER(gPlayState));
    }
}

extern "C" void Combo_Tools_EquipDekuShield(void) {
    // Only an empty shield slot takes it: the Hero's and Mirror Shields are better, and OOT never downgrades.
    if (GET_CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD) == EQUIP_VALUE_SHIELD_NONE) {
        SET_EQUIP_VALUE(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_HERO);
        Flags_SetRandoInf(RANDO_INF_COMBO_SHIELD_IS_DEKU);
        RefreshShieldModel();
    }
}

extern "C" void Combo_Tools_LoseDekuShield(void) {
    if (Flags_GetRandoInf(RANDO_INF_COMBO_SHIELD_IS_DEKU)) {
        Flags_ClearRandoInf(RANDO_INF_COMBO_SHIELD_IS_DEKU);
        if (GET_CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD) == EQUIP_VALUE_SHIELD_HERO) {
            SET_EQUIP_VALUE(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_NONE);
        }
        RefreshShieldModel();
    }
}

extern "C" int Combo_Tools_ReplaceDekuShield(void) {
    if (!IS_RANDO || !Flags_GetRandoInf(RANDO_INF_COMBO_SHIELD_IS_DEKU)) {
        return 0;
    }
    Flags_ClearRandoInf(RANDO_INF_COMBO_SHIELD_IS_DEKU);
    return 1;
}

static bool IsDekuShield(Player* player) {
    return IsHumanLink(player) && (player->currentShield == PLAYER_SHIELD_HEROS_SHIELD) && IS_RANDO &&
           Flags_GetRandoInf(RANDO_INF_COMBO_SHIELD_IS_DEKU);
}

extern "C" void Combo_Tools_OnPlayerUpdate(PlayState* play, Player* player) {
    if (!IsDekuShield(player)) {
        return;
    }
    // OOT's Deku Shield burns: Link on fire, or fire blocked with it.
    if (player->bodyIsBurning || ShieldHitByFire(&player->shieldQuad.base, &player->shieldQuad.elem) ||
        ShieldHitByFire(&player->shieldCylinder.base, &player->shieldCylinder.elem)) {
        Combo_Tools_LoseDekuShield();
        Actor_PlaySfx(&player->actor, NA_SE_EV_FLAME_IGNITION);
        Audio_PlaySfx(NA_SE_SY_ERROR);
    }
}

// ---- Models (z_player_lib.c hooks) ----

extern "C" void* Combo_Tools_LeftHandToolDL(Player* player, int leftHandType) {
    // OOT adult Link's hand holding the Hammer, cut before the gauntlet (OoTMM makeEqHammer).
    if ((leftHandType == PLAYER_MODELTYPE_LH_TWO_HAND_SWORD) && Combo_Tools_IsHeld(player, ITEM_COMBO_HAMMER)) {
        return CutOotDL(sHammerCut, "objects/object_link_boy/gLinkAdultLeftHandHoldingHammerNearDL", "Gauntlet",
                        nullptr);
    }
    // OOT child Link's fist holding the Boomerang, cut before the hand (OoTMM makeEqBoomerang). While thrown,
    // z_player_lib.c has already switched the hand to LH_OPEN.
    if ((leftHandType == PLAYER_MODELTYPE_LH_4) && Combo_Tools_IsHeld(player, ITEM_COMBO_BOOMERANG) &&
        !(player->stateFlags1 & PLAYER_STATE1_ZORA_BOOMERANG_THROWN)) {
        return CutOotDL(sBoomerangCut, "objects/object_link_child/gLinkChildLeftFistAndBoomerangFarDL",
                        "gLinkChildHandTex", sBoomerangTexturedDL);
    }
    return nullptr;
}

extern "C" void* Combo_Tools_RightHandDL(Player* player, int rightHandType) {
    if ((rightHandType == PLAYER_MODELTYPE_RH_BOW) && Combo_Tools_IsHeld(player, ITEM_COMBO_SLINGSHOT)) {
        return (void*)sSlingshotHandDL;
    }
    if ((rightHandType == PLAYER_MODELTYPE_RH_SHIELD) && IsDekuShield(player)) {
        return (void*)sDekuShieldHandDL;
    }
    if ((rightHandType == PLAYER_MODELTYPE_RH_INSTRUMENT) && Combo_Upgrades_OcarinaIsFairy()) {
        return (void*)sFairyOcarinaHandDL;
    }
    return nullptr;
}

extern "C" void* Combo_Tools_AdultHandToolDL(Player* player, int limbIndex, int handType, int* wholeHand) {
    *wholeHand = 0;
    if (limbIndex == PLAYER_LIMB_LEFT_HAND) {
        // OOT adult Link's own hand holding the Hammer (the whole hand: no cut needed at this age).
        if ((handType == PLAYER_MODELTYPE_LH_TWO_HAND_SWORD) && Combo_Tools_IsHeld(player, ITEM_COMBO_HAMMER)) {
            *wholeHand = 1;
            return (void*)sHammerAdultHandDL;
        }
        // The Boomerang cut is already the tool alone.
        if ((handType == PLAYER_MODELTYPE_LH_4) && Combo_Tools_IsHeld(player, ITEM_COMBO_BOOMERANG) &&
            !(player->stateFlags1 & PLAYER_STATE1_ZORA_BOOMERANG_THROWN)) {
            return CutOotDL(sBoomerangCut, "objects/object_link_child/gLinkChildLeftFistAndBoomerangFarDL",
                            "gLinkChildHandTex", sBoomerangTexturedDL);
        }
        return nullptr;
    }
    if (limbIndex != PLAYER_LIMB_RIGHT_HAND) {
        return nullptr;
    }
    if ((handType == PLAYER_MODELTYPE_RH_BOW) && Combo_Tools_IsHeld(player, ITEM_COMBO_SLINGSHOT)) {
        return CutOotDL(sSlingshotAdultCut, "objects/object_link_child/gLinkChildRightHandHoldingSlingshotNearDL",
                        "gLinkChildSlingshotTex", nullptr, DROP_TRIANGLES_BEFORE);
    }
    if ((handType == PLAYER_MODELTYPE_RH_SHIELD) && IsDekuShield(player)) {
        return CutOotDL(sDekuShieldAdultCut, "objects/object_link_child/gLinkChildRightFistAndDekuShieldNearDL",
                        "gLinkChildHandTex", nullptr);
    }
    if ((handType == PLAYER_MODELTYPE_RH_INSTRUMENT) && Combo_Upgrades_OcarinaIsFairy()) {
        return CutOotDL(sFairyOcarinaAdultCut, "objects/object_link_child/gLinkChildRightHandHoldingFairyOcarinaNearDL",
                        "gLinkChildHandTex", nullptr);
    }
    return nullptr;
}

extern "C" void* Combo_Tools_AdultFirstPersonRightHandDL(PlayState* play, Player* player) {
    if (!Combo_Tools_IsHeld(player, ITEM_COMBO_SLINGSHOT)) {
        return nullptr;
    }
    // OoTMM's adult slingshot aim: OOT adult Link's first-person bow hand minus the bow, and OOT's stretched slingshot.
    Gfx* hand = CutOotDL(sAdultAimHandCut, "objects/object_link_boy/gLinkAdultRightHandHoldingBowFirstPersonDL",
                         "gLinkAdultDefaultGauntlet1Tex", nullptr, DROP_TRIANGLES_BEFORE);
    Gfx* dl = (Gfx*)GRAPH_ALLOC(play->state.gfxCtx, 5 * sizeof(Gfx));
    Gfx* d = dl;
    gSPDisplayList(d++, (Gfx*)sSlingshotFirstPersonDL);
    if (hand != nullptr) {
        gSPComboRMPush(d++, "oot"); // the cut list carries OOT's own resource references
        gSPDisplayList(d++, hand);
        gSPComboRMPop(d++);
    }
    gSPEndDisplayList(d++);
    return dl;
}

extern "C" void* Combo_Tools_FirstPersonRightHandDL(Player* player) {
    return Combo_Tools_IsHeld(player, ITEM_COMBO_SLINGSHOT) ? (void*)sSlingshotFirstPersonDL : nullptr;
}

extern "C" void* Combo_Tools_SlingshotStringDL(Player* player) {
    return Combo_Tools_IsHeld(player, ITEM_COMBO_SLINGSHOT) ? (void*)sSlingshotStringDL : nullptr;
}

extern "C" void* Combo_Tools_BackShieldDL(Player* player) {
    return IsDekuShield(player) ? (void*)sDekuShieldBackDL : nullptr;
}

#endif // COMBO_BUILD
