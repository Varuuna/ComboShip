// mm/2s2h/Rando/MiscBehavior/AdultLink.cpp
// ComboShip (Cross-Games Age): Ocarina of Time's adult Link as human Link in Majora's Mask. See AdultLink.h for the
// approach and docs/deviations/rando.md.
//
// Body measurements are OOT's adult entry of sAgeProperties (z_player.c, OOT decomp); the body, face, first-person
// arms, gauntlets and bottle follow OOT's own adult display-list tables (z_player_lib.c, OOT decomp). What he holds and
// carries is MM's own gear, like OoTMM's adult Link: MM's item meshes in OOT adult Link's fist, and MM's sheaths and
// back shields, 1.25x bigger (OoTMM's adult mask scale; adult items in OOT are about that much bigger than child ones,
// and it matches OoTMM's adult sword reach, 3000 -> 4000).
#ifdef COMBO_BUILD

#include "AdultLink.h"
#include "CrossGear.h"
#include "2s2h/Rando/Rando.h"
#include "2s2h/Rando/StaticData/StaticData.h"

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <fast/resource/ResourceType.h>
#include <fast/resource/type/DisplayList.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/utils/StrHash64.h>

extern "C" {
#include "variables.h"
#include "functions.h"
#include "macros.h"
#include "z64player.h"
#include "align_asset_macro.h"
#include "objects/object_link_child/object_link_child.h"
}

#define COMBO_ADULT(name) "__OTR__@oot:objects/object_link_boy/" name

namespace {

Player* sAdultPlayer = nullptr; // the player actor Player_Init last set up as adult
bool sLeftHandOpen = false;     // this frame's hands, for the gauntlet plates (OOT: sLeftHandType == LH_OPEN)
bool sRightHandOpen = false;
int sKeptHand = -1; // hand limb whose model a cross-game tool already set this frame

// ---- Display lists ----
// Resource paths must sit at even addresses (an odd pointer reads as a segmented address), hence the aligned arrays.
struct LodPair {
    ALIGN_ASSET(2) char nearDL[96];
    ALIGN_ASSET(2) char farDL[96];
};

#define ADULT_DL(name) \
    { COMBO_ADULT(#name "NearDL"), COMBO_ADULT(#name "FarDL") }

// [limbIndex] for the body limbs (no override in MM picks a model for these).
const LodPair kBody[PLAYER_LIMB_MAX] = {
    /* NONE           */ {},
    /* ROOT           */ {},
    /* WAIST          */ ADULT_DL(gLinkAdultWaist),
    /* LOWER_ROOT     */ {},
    /* RIGHT_THIGH    */ ADULT_DL(gLinkAdultRightThigh),
    /* RIGHT_SHIN     */ ADULT_DL(gLinkAdultRightLeg),
    /* RIGHT_FOOT     */ ADULT_DL(gLinkAdultRightFoot),
    /* LEFT_THIGH     */ ADULT_DL(gLinkAdultLeftThigh),
    /* LEFT_SHIN      */ ADULT_DL(gLinkAdultLeftLeg),
    /* LEFT_FOOT      */ ADULT_DL(gLinkAdultLeftFoot),
    /* UPPER_ROOT     */ {},
    /* HEAD           */ ADULT_DL(gLinkAdultHead),
    /* HAT            */ ADULT_DL(gLinkAdultHat),
    /* COLLAR         */ ADULT_DL(gLinkAdultCollar),
    /* LEFT_SHOULDER  */ ADULT_DL(gLinkAdultLeftShoulder),
    /* LEFT_FOREARM   */ ADULT_DL(gLinkAdultLeftArm),
    /* LEFT_HAND      */ ADULT_DL(gLinkAdultLeftHand),
    /* RIGHT_SHOULDER */ ADULT_DL(gLinkAdultRightShoulder),
    /* RIGHT_FOREARM  */ ADULT_DL(gLinkAdultRightArm),
    /* RIGHT_HAND     */ ADULT_DL(gLinkAdultRightHand),
    /* SHEATH         */ ADULT_DL(gLinkAdultSheath),
    /* TORSO          */ ADULT_DL(gLinkAdultTorso),
};

const LodPair kLeftHandClosed = ADULT_DL(gLinkAdultLeftHandClosed);
const LodPair kRightHandClosed = ADULT_DL(gLinkAdultRightHandClosed);
const LodPair kLeftHandBottle = { COMBO_ADULT("gLinkAdultHandHoldingBottleDL"),
                                  COMBO_ADULT("gLinkAdultHandHoldingBottleDL") };
const LodPair kRightHandOcarina = ADULT_DL(gLinkAdultRightHandHoldingOot); // MM's ocarina is the Ocarina of Time too
// Fallbacks if an MM item list can't be rebuilt.
const LodPair kLeftHandSword = ADULT_DL(gLinkAdultLeftHandHoldingMasterSword);

// Size of MM's gear on adult Link (see the top of this file).
constexpr float kGearScale = 1.25f;

bool IsPath(const void* dList, const char* path) {
    const char* str = (const char*)dList;
    return (strncmp(str, "__OTR__", 7) == 0) && (strcmp(str, path) == 0);
}

const char* Pick(const LodPair& pair, int lod) {
    return (lod == 0) ? pair.nearDL : pair.farDL;
}

extern "C" Color_RGB8 CosmeticEditor_GetComboColor(const char* id, Color_RGB8 defaultColor); // CosmeticEditor.cpp

// ---- MM's gear on adult Link ----
// MM's human item hands are a call to the closed fist followed by calls to the item. Rebuilt for adult Link: the fist
// call becomes OOT adult Link's fist, and the rest is drawn kGearScale bigger. Lists without the fist (sheaths) are
// drawn bigger whole.
bool IsTwoWordOpcode(uint8_t op) {
    return (op == G_SETTIMG_OTR_HASH) || (op == G_DL_OTR_HASH) || (op == G_VTX_OTR_HASH) || (op == G_BRANCH_Z_OTR) ||
           (op == G_MARKER) || (op == G_MTX_OTR) || (op == G_MOVEMEM_OTR);
}

struct BuiltList {
    bool tried = false;
    std::shared_ptr<Ship::IResource> res; // keeps MM's list (and the strings it points to) alive
    std::vector<Gfx> gfx;
};
std::map<std::pair<std::string, int>, BuiltList> sBuilt;
Mtx sGearScaleMtx;
bool sGearScaleMtxReady = false;
// Adult Link's tunic color, put back after MM gear: MM's sword, sheath and shield lists set their own environment
// color, and OOT's tunic is shaded with it, so the limbs drawn after them (the torso) came out black. Rewritten each
// frame by Combo_AdultLink_SetupDraw; the built gear lists call it last.
Gfx sTunicEnvDL[3] = { gsDPPipeSync(), gsDPSetEnvColor(30, 105, 27, 0), gsSPEndDisplayList() };

// mmPath: "__OTR__objects/..." (MM's own resource). childFist: the fist list to swap ("objects/..."), or nullptr.
Gfx* AdultGear(const char* mmPath, const char* childFist, const LodPair* adultFist, int lod) {
    BuiltList& built = sBuilt[{ mmPath, lod }];
    if (built.tried) {
        return built.gfx.empty() ? nullptr : built.gfx.data();
    }
    built.tried = true;
    if (!sGearScaleMtxReady) {
        guScale(&sGearScaleMtx, kGearScale, kGearScale, kGearScale);
        sGearScaleMtxReady = true;
    }
    built.res = Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(mmPath + 7);
    if ((built.res == nullptr) ||
        (built.res->GetInitData()->Type != static_cast<uint32_t>(Fast::ResourceType::DisplayList))) {
        SPDLOG_ERROR("[ComboShip] adult Link: MM gear list missing: {}", mmPath);
        return nullptr;
    }
    const std::vector<Gfx>& in = std::static_pointer_cast<Fast::DisplayList>(built.res)->Instructions;
    const uint64_t fistHash = (childFist != nullptr) ? CRC64(childFist) : 0;
    bool scaled = false;
    if (childFist == nullptr) {
        Gfx push = gsSPMatrix(&sGearScaleMtx, G_MTX_PUSH | G_MTX_MUL | G_MTX_MODELVIEW);
        built.gfx.push_back(push);
        scaled = true;
    }
    bool swapped = (childFist == nullptr);
    for (size_t i = 0; i < in.size();) {
        const uint8_t op = (uint8_t)(in[i].words.w0 >> 24);
        if (op == G_ENDDL) {
            break;
        }
        const size_t words = IsTwoWordOpcode(op) ? 2 : 1;
        if (!swapped && (op == G_DL_OTR_HASH) && (i + 1 < in.size()) &&
            ((((uint64_t)(uint32_t)in[i + 1].words.w0 << 32) | (uint64_t)(uint32_t)in[i + 1].words.w1) == fistHash)) {
            Gfx fist = gsDma1p(G_DL_OTR_FILEPATH, Pick(*adultFist, lod), 0, G_DL_PUSH);
            Gfx push = gsSPMatrix(&sGearScaleMtx, G_MTX_PUSH | G_MTX_MUL | G_MTX_MODELVIEW);
            built.gfx.push_back(fist);
            built.gfx.push_back(push);
            swapped = scaled = true;
        } else {
            built.gfx.insert(built.gfx.end(), in.begin() + i, in.begin() + i + words);
        }
        i += words;
    }
    if (!swapped) {
        SPDLOG_ERROR("[ComboShip] adult Link: no fist call in {}", mmPath);
        built.gfx.clear();
        return nullptr;
    }
    if (scaled) {
        Gfx pop = gsSPPopMatrix(G_MTX_MODELVIEW);
        built.gfx.push_back(pop);
    }
    Gfx tunic = gsSPDisplayList(sTunicEnvDL);
    built.gfx.push_back(tunic);
    Gfx end = gsSPEndDisplayList();
    built.gfx.push_back(end);
    return built.gfx.data();
}

#define CHILD_FIST(name) "objects/object_link_child/" name

// MM's human hand display list -> adult Link's. MM's player tables hold resource paths, so these compare by name.
struct HandMap {
    const char* human;
    const LodPair* adult;  // OOT adult Link's own model, or
    const char* childFist; // MM's item, in OOT adult Link's fist (adultFist)
    const LodPair* adultFist;
};
const HandMap kHands[] = {
    { gLinkHumanLeftHandOpenDL, &kBody[PLAYER_LIMB_LEFT_HAND] },
    { gLinkHumanLeftHandClosedDL, &kLeftHandClosed },
    { gLinkHumanLeftHandHoldBottleDL, &kLeftHandBottle },
    { gLinkHumanLeftHandHoldingKokiriSwordDL, nullptr, CHILD_FIST("gLinkHumanLeftHandClosedDL"), &kLeftHandClosed },
    { gLinkHumanLeftHandHoldingRazorSwordDL, nullptr, CHILD_FIST("gLinkHumanLeftHandClosedDL"), &kLeftHandClosed },
    { gLinkHumanLeftHandHoldingGildedSwordDL, nullptr, CHILD_FIST("gLinkHumanLeftHandClosedDL"), &kLeftHandClosed },
    { gLinkHumanLeftHandHoldingGreatFairysSwordDL, nullptr, CHILD_FIST("gLinkHumanLeftHandClosedDL"),
      &kLeftHandClosed },
    { gLinkHumanRightHandOpenDL, &kBody[PLAYER_LIMB_RIGHT_HAND] },
    { gLinkHumanRightHandClosedDL, &kRightHandClosed },
    { gLinkHumanRightHandHoldingOcarinaDL, &kRightHandOcarina },
    { gLinkHumanRightHandHoldingHerosShieldDL, nullptr, CHILD_FIST("gLinkHumanRightHandClosedDL"), &kRightHandClosed },
    { gLinkHumanRightHandHoldingMirrorShieldDL, nullptr, CHILD_FIST("gLinkHumanRightHandClosedDL"), &kRightHandClosed },
    { gLinkHumanRightHandHoldingBowDL, nullptr, CHILD_FIST("gLinkHumanRightHandClosedDL"), &kRightHandClosed },
    { gLinkHumanRightHandHoldingHookshotDL, nullptr, CHILD_FIST("gLinkHumanRightHandClosedDL"), &kRightHandClosed },
};

// Masks worn on top of the head, which adult Link's long cap would poke through.
bool MaskReplacesCap(u8 mask) {
    return (mask == PLAYER_MASK_BUNNY) || (mask == PLAYER_MASK_KAFEIS_MASK) || (mask == PLAYER_MASK_POSTMAN) ||
           (mask == PLAYER_MASK_CAPTAIN);
}

const ALIGN_ASSET(2) char kAimLeftForearm[] = COMBO_ADULT("gLinkAdultRightArmOutNearDL");
const ALIGN_ASSET(2) char kAimLeftHand[] = COMBO_ADULT("gLinkAdultRightHandOutNearDL");
const ALIGN_ASSET(2) char kAimRightShoulder[] = COMBO_ADULT("gLinkAdultRightShoulderNearDL");
const ALIGN_ASSET(2) char kAimRightForearm[] = COMBO_ADULT("gLinkAdultLeftArmOutNearDL");
const ALIGN_ASSET(2) char kAimBow[] = COMBO_ADULT("gLinkAdultRightHandHoldingBowFirstPersonDL");
const ALIGN_ASSET(2) char kAimHookshot[] = COMBO_ADULT("gLinkAdultRightHandHoldingHookshotFarDL");
const ALIGN_ASSET(2) char kBottle[] = COMBO_ADULT("gLinkAdultBottleDL");
const ALIGN_ASSET(2) char kBowString[] = COMBO_ADULT("gLinkAdultBowStringDL");
const ALIGN_ASSET(2) char kGauntletLeft1[] = COMBO_ADULT("gLinkAdultLeftGauntletPlate1DL");
const ALIGN_ASSET(2) char kGauntletRight1[] = COMBO_ADULT("gLinkAdultRightGauntletPlate1DL");
const ALIGN_ASSET(2) char kGauntletLeftOpen[] = COMBO_ADULT("gLinkAdultLeftGauntletPlate2DL");
const ALIGN_ASSET(2) char kGauntletLeftClosed[] = COMBO_ADULT("gLinkAdultLeftGauntletPlate3DL");
const ALIGN_ASSET(2) char kGauntletRightOpen[] = COMBO_ADULT("gLinkAdultRightGauntletPlate2DL");
const ALIGN_ASSET(2) char kGauntletRightClosed[] = COMBO_ADULT("gLinkAdultRightGauntletPlate3DL");

// OOT's adult eyes and mouths, in the same index order as MM's (MM kept OOT's face animation data).
const ALIGN_ASSET(2) char kEyes[PLAYER_EYES_MAX][64] = {
    COMBO_ADULT("gLinkAdultEyesOpenTex"),      COMBO_ADULT("gLinkAdultEyesHalfTex"),
    COMBO_ADULT("gLinkAdultEyesClosedfTex"),   COMBO_ADULT("gLinkAdultEyesRollLeftTex"),
    COMBO_ADULT("gLinkAdultEyesRollRightTex"), COMBO_ADULT("gLinkAdultEyesShockTex"),
    COMBO_ADULT("gLinkAdultEyesUnk1Tex"),      COMBO_ADULT("gLinkAdultEyesUnk2Tex"),
};
const ALIGN_ASSET(2) char kMouths[PLAYER_MOUTH_MAX][64] = {
    COMBO_ADULT("gLinkAdultMouth1Tex"),
    COMBO_ADULT("gLinkAdultMouth2Tex"),
    COMBO_ADULT("gLinkAdultMouth3Tex"),
    COMBO_ADULT("gLinkAdultMouth4Tex"),
};

PlayerAgeProperties sAdultAgeProperties;

} // namespace

// Developer override: Link is adult in Termina whatever the save says (testing the model without time travel).
static bool DevAdult() {
    return CVarGetInteger("gCombo.Dev.MmAdultLink", 0) != 0;
}

extern "C" int Combo_AdultLink_AgeSystemOn(void) {
    // The save's (or, in the combo fill's oracle, the seed's) Cross-Games Age option.
    return DevAdult() || (IS_RANDO && (RANDO_SAVE_OPTIONS[RO_CROSS_GAME_AGE] != RO_GENERIC_OFF));
}

extern "C" int Combo_AdultLink_AgeSystemConfigured(void) {
    // The menu's choice, for code that runs before a save exists (the menu, the generator's dump).
    const auto& option = Rando::StaticData::Options[RO_CROSS_GAME_AGE];
    return DevAdult() || (CVarGetInteger(option.cvar, option.defaultValue) != RO_GENERIC_OFF);
}

extern "C" int Combo_AdultLink_IsAdultAge(void) {
    if (DevAdult()) {
        return 1;
    }
    return Combo_AdultLink_AgeSystemOn() && (gSaveContext.save.shipSaveInfo.rando.comboLinkAdult != 0);
}

// ---- Entering Termina at OOT's age ----
namespace {
int sEntryAge = -1; // the launcher's OOT age for the next MM entry: -1 none (a resume), 0 child, 1 adult

constexpr u8 kWearIron = 1 << 0, kWearHover = 1 << 1, kWearGoron = 1 << 2, kWearZora = 1 << 3;
const std::pair<u8, RandoInf> kWearFlags[] = {
    { kWearIron, RANDO_INF_COMBO_WEARING_BOOTS_IRON },
    { kWearHover, RANDO_INF_COMBO_WEARING_BOOTS_HOVER },
    { kWearGoron, RANDO_INF_COMBO_WEARING_TUNIC_GORON },
    { kWearZora, RANDO_INF_COMBO_WEARING_TUNIC_ZORA },
};

// OoTMM's Age_SwapEquipmentMm: each age keeps its own C buttons and worn boots/tunic. A first visit at an age starts
// from the other age's buttons. Masks come off (OoTMM clears the equipped mask on an age change).
void SwapLoadout(int from, int to) {
    auto& rando = gSaveContext.save.shipSaveInfo.rando;
    auto& out = rando.comboAgeLoadout[from];
    for (int b = EQUIP_SLOT_C_LEFT; b <= EQUIP_SLOT_C_RIGHT; b++) {
        out.buttonItems[b] = BUTTON_ITEM_EQUIP(0, b);
        out.cButtonSlots[b] = C_SLOT_EQUIP(0, b);
    }
    out.wear = 0;
    for (const auto& [bit, flag] : kWearFlags) {
        if (Flags_GetRandoInf(flag)) {
            out.wear |= bit;
        }
    }
    rando.comboAgeLoadoutSet |= (u8)(1 << from);

    if (rando.comboAgeLoadoutSet & (1 << to)) {
        const auto& in = rando.comboAgeLoadout[to];
        for (int b = EQUIP_SLOT_C_LEFT; b <= EQUIP_SLOT_C_RIGHT; b++) {
            const u8 slot = in.cButtonSlots[b];
            u8 item = in.buttonItems[b];
            if ((slot != SLOT_NONE) && (slot < ARRAY_COUNT(gSaveContext.save.saveInfo.inventory.items))) {
                // Re-read the slot: bottle contents and upgrades may have changed since, or the item may be gone.
                // The trade slots cycle several items (deeds, letters, OOT's items) and a button may hold one that
                // isn't the slot's current pick; OOT's items are never used up, so those stay.
                const bool tradeSlot =
                    (slot == SLOT_TRADE_DEED) || (slot == SLOT_TRADE_KEY_MAMA) || (slot == SLOT_TRADE_COUPLE);
                if (!(tradeSlot && IS_COMBO_CROSS_ITEM(item))) {
                    item = gSaveContext.save.saveInfo.inventory.items[slot];
                }
            }
            BUTTON_ITEM_EQUIP(0, b) = item;
            C_SLOT_EQUIP(0, b) = (item == ITEM_NONE) ? SLOT_NONE : slot;
        }
        for (const auto& [bit, flag] : kWearFlags) {
            if (in.wear & bit) {
                Flags_SetRandoInf(flag);
            } else {
                Flags_ClearRandoInf(flag);
            }
        }
    }
    // A transformation is a worn mask too: Link arrives in his own form, so the new age shows.
    gSaveContext.save.playerForm = PLAYER_FORM_HUMAN;
    gSaveContext.save.equippedMask = PLAYER_MASK_NONE;
    Flags_ClearRandoInf(RANDO_INF_COMBO_WEARING_MASK_GERUDO);
    Flags_ClearRandoInf(RANDO_INF_COMBO_WEARING_MASK_SKULL);
    Flags_ClearRandoInf(RANDO_INF_COMBO_WEARING_MASK_SPOOKY);
}
} // namespace

extern "C" void Combo_AdultLink_SetEntryAge(int ootAdult) {
    sEntryAge = ootAdult;
}

extern "C" void Combo_AdultLink_ApplyEntryAge(void) {
    const int age = sEntryAge;
    sEntryAge = -1;
    if (!IS_RANDO || (age < 0) || !Combo_AdultLink_AgeSystemOn()) {
        return;
    }
    auto& rando = gSaveContext.save.shipSaveInfo.rando;
    const int current = rando.comboLinkAdult ? 1 : 0;
    if (age == current) {
        return;
    }
    SwapLoadout(current, age);
    rando.comboLinkAdult = (u8)age;
    SPDLOG_INFO("[ComboShip] Cross-Games Age: Link enters Termina as {}", age ? "adult" : "child");
}

extern "C" int Combo_AdultLink_OnPlayerInit(Player* player) {
    const bool adult = (player != nullptr) && (player->actor.id == ACTOR_PLAYER) &&
                       (player->transformation == PLAYER_FORM_HUMAN) && Combo_AdultLink_IsAdultAge();
    if (adult) {
        sAdultPlayer = player;
    } else if (sAdultPlayer == player) {
        sAdultPlayer = nullptr;
    }
    return adult ? 1 : 0;
}

extern "C" int Combo_AdultLink_Is(Player* player) {
    return (player != nullptr) && (player == sAdultPlayer) && (player->transformation == PLAYER_FORM_HUMAN);
}

extern "C" float Combo_AdultLink_RiderDrop(Player* player) {
    // MM's riding animations are human Link's: on adult Link's skeleton his hips end up this much higher than human
    // Link's (measured: waist 59.8 vs 47.1 over the same root), so he floated above Epona's saddle.
    return Combo_AdultLink_Is(player) ? 12.7f : 0.0f;
}

extern "C" PlayerAgeProperties* Combo_AdultLink_AgeProperties(const PlayerAgeProperties* fierceDeity) {
    // Fierce Deity's entry is OOT adult Link's (vectors, voice, surface sounds, animations) with the sizes x1.5;
    // take OOT's own adult values for the sizes.
    sAdultAgeProperties = *fierceDeity;
    PlayerAgeProperties& p = sAdultAgeProperties;
    p.ceilingCheckHeight = 56.0f;
    p.shadowScale = 90.0f;
    p.unk_08 = 1.0f;
    p.unk_0C = 111.0f;
    p.unk_10 = 70.0f;
    p.unk_14 = 79.4f;
    p.unk_18 = 59.0f;
    p.unk_1C = 41.0f;
    p.unk_20 = 19.0f;
    p.unk_24 = 36.0f;
    p.unk_28 = 44.8f;
    p.unk_2C = 56.0f;
    p.unk_30 = 68.0f;
    p.unk_34 = 70.0f;
    p.wallCheckRadius = 18.0f;
    p.unk_3C = 15.0f;
    p.unk_40 = 70.0f;
    p.unk_98 = 22.0f;
    p.unk_9C = 29.4343f;
    return &sAdultAgeProperties;
}

extern "C" void Combo_AdultLink_SetupDraw(PlayState* play, Player* player, int eyeIndex, int mouthIndex) {
    // The cosmetic editor's "Adult Link Tunic" and "Goron/Zora Tunic (OoT)" colors (OOT's own by default).
    const Color_RGB8 color = Combo_Gear_TunicColor(Combo_Gear_GetTunic(player),
                                                   CosmeticEditor_GetComboColor("Player.AdultTunic", { 30, 105, 27 }));
    if ((eyeIndex < 0) || (eyeIndex >= PLAYER_EYES_MAX)) {
        eyeIndex = 0;
    }
    if ((mouthIndex < 0) || (mouthIndex >= PLAYER_MOUTH_MAX)) {
        mouthIndex = 0;
    }

    sKeptHand = -1;
    OPEN_DISPS(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)kEyes[eyeIndex]);
    gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)kMouths[mouthIndex]);
    gDPSetEnvColor(POLY_OPA_DISP++, color.r, color.g, color.b, 0);
    CLOSE_DISPS(play->state.gfxCtx);
    Gfx* tunicEnv = sTunicEnvDL;
    gDPPipeSync(tunicEnv++);
    gDPSetEnvColor(tunicEnv++, color.r, color.g, color.b, 0);
    gSPEndDisplayList(tunicEnv++);
}

extern "C" void Combo_AdultLink_RestoreTunicColor(PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, sTunicEnvDL);
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void Combo_AdultLink_DrawGear(PlayState* play, Player* player) {
    // OOT's Silver and Golden Gauntlets (Player_DrawImpl, OOT decomp): plates over the forearms and the backs of the
    // hands, tinted by the environment color. Segment 0x0D still holds this skeleton's limb matrices.
    const int strength = Combo_Gear_GetStrength(); // 2 silver, 3 golden
    if (!Combo_AdultLink_Is(player) || (strength < 2)) {
        return;
    }
    const Color_RGB8 color = Combo_Gear_GauntletColor(strength); // the cosmetic editor's gauntlet colors
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetEnvColor(POLY_OPA_DISP++, color.r, color.g, color.b, 0);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)kGauntletLeft1);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)kGauntletRight1);
    // The gem color (CrossGearColors.cpp) lives in the hand plates.
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)Combo_Gear_Recolor(sLeftHandOpen ? kGauntletLeftOpen : kGauntletLeftClosed));
    gSPDisplayList(POLY_OPA_DISP++,
                   (Gfx*)Combo_Gear_Recolor(sRightHandOpen ? kGauntletRightOpen : kGauntletRightClosed));
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void Combo_AdultLink_MaskTransform(void) {
    // OoTMM's own fit for its adult Link (mm_adult_link_mask_mtx.bin): MM's masks scaled by 1.25 in head-limb space.
    // OOT's adult head is 1.44x child Link's height but only ~1.06-1.10x his width and depth, so a uniform 1.25 keeps
    // face masks on the face and full-head masks around the head.
    Matrix_Scale(kGearScale, kGearScale, kGearScale, MTXMODE_APPLY);
}

extern "C" void Combo_AdultLink_GearTransform(void) {
    Matrix_Scale(kGearScale, kGearScale, kGearScale, MTXMODE_APPLY);
}

extern "C" float Combo_AdultLink_MeleeWeaponLength(Player* player, int meleeWeapon, float length) {
    if (!Combo_AdultLink_Is(player)) {
        return length;
    }
    switch (meleeWeapon) { // OoTMM's adult meleeWeaponLengths
        case PLAYER_MELEEWEAPON_SWORD_KOKIRI:
        case PLAYER_MELEEWEAPON_SWORD_RAZOR:
            return 4000.0f;
        case PLAYER_MELEEWEAPON_SWORD_GILDED:
        case PLAYER_MELEEWEAPON_SWORD_TWO_HANDED:
            return 5500.0f;
        default:
            return length;
    }
}

extern "C" void* Combo_AdultLink_BottleDL(void) {
    return (void*)kBottle;
}

extern "C" void* Combo_AdultLink_BowStringDL(void) {
    return (void*)kBowString;
}

extern "C" void* Combo_AdultLink_ClosedFistDL(int limbIndex, int lod) {
    return (void*)Pick((limbIndex == PLAYER_LIMB_LEFT_HAND) ? kLeftHandClosed : kRightHandClosed, lod);
}

extern "C" void Combo_AdultLink_KeepHand(int limbIndex) {
    sKeptHand = limbIndex;
}

static void RemapLimb(PlayState* play, Player* player, int limbIndex, void** dList, int lod, int firstPerson);

extern "C" void Combo_AdultLink_RemapLimb(PlayState* play, Player* player, int limbIndex, void** dList, int lod,
                                          int firstPerson) {
    if ((dList == nullptr) || (limbIndex <= PLAYER_LIMB_NONE) || (limbIndex >= PLAYER_LIMB_MAX) ||
        !Combo_AdultLink_Is(player)) {
        return;
    }
    RemapLimb(play, player, limbIndex, dList, lod, firstPerson);
    // MM's own hair / linen / boots colors (CrossGearColors.cpp): a recolored copy of OOT's limb while one is set.
    *dList = (void*)Combo_Gear_Recolor(*dList);
}

static void RemapLimb(PlayState* play, Player* player, int limbIndex, void** dList, int lod, int firstPerson) {
    if (firstPerson) {
        // OOT's own first-person arms (Player_OverrideLimbDrawGameplayFirstPerson, OOT decomp; the "Right"/"Left" in
        // OOT's names are swapped relative to the limbs), including the right forearm MM leaves out.
        switch (limbIndex) {
            case PLAYER_LIMB_LEFT_FOREARM:
                *dList = (void*)kAimLeftForearm;
                return;
            case PLAYER_LIMB_LEFT_HAND:
                *dList = (void*)kAimLeftHand;
                return;
            case PLAYER_LIMB_RIGHT_SHOULDER:
                *dList = (void*)kAimRightShoulder;
                return;
            case PLAYER_LIMB_RIGHT_FOREARM:
                *dList = (void*)kAimRightForearm;
                return;
            case PLAYER_LIMB_RIGHT_HAND: {
                void* slingshot = Combo_Tools_AdultFirstPersonRightHandDL(play, player);
                if (slingshot != nullptr) {
                    *dList = slingshot;
                    return;
                }
                // MM's own Bow and Hookshot in adult Link's fist, as in third person (OOT's adult bow and hookshot
                // looked out of place in Termina).
                const bool hookshot = Player_IsHoldingHookshot(player);
                Gfx* gear = AdultGear(hookshot ? gLinkHumanRightHandHoldingHookshotDL : gLinkHumanRightHandHoldingBowDL,
                                      CHILD_FIST("gLinkHumanRightHandClosedDL"), &kRightHandClosed, 0);
                *dList = (gear != nullptr) ? (void*)gear : hookshot ? (void*)kAimHookshot : (void*)kAimBow;
                return;
            }
            default:
                return;
        }
    }
    if (*dList == nullptr) {
        return;
    }
    if (limbIndex == sKeptHand) {
        sKeptHand = -1;
        ((limbIndex == PLAYER_LIMB_LEFT_HAND) ? sLeftHandOpen : sRightHandOpen) = false;
        return;
    }
    if ((limbIndex == PLAYER_LIMB_LEFT_HAND) || (limbIndex == PLAYER_LIMB_RIGHT_HAND)) {
        bool& open = (limbIndex == PLAYER_LIMB_LEFT_HAND) ? sLeftHandOpen : sRightHandOpen;
        for (const HandMap& hand : kHands) {
            if (!IsPath(*dList, hand.human)) {
                continue;
            }
            open = (hand.adult == &kBody[limbIndex]);
            if (hand.adult != nullptr) {
                *dList = (void*)Pick(*hand.adult, lod);
                return;
            }
            Gfx* gear = AdultGear(hand.human, hand.childFist, hand.adultFist, lod);
            *dList = (gear != nullptr)
                         ? (void*)gear
                         : (void*)Pick((limbIndex == PLAYER_LIMB_LEFT_HAND) ? kLeftHandSword : kRightHandClosed, lod);
            return;
        }
        open = false;
        // Anything else (a cross-game tool's hand, a skeleton default) stays as MM/ComboShip chose it.
        if (strncmp((const char*)*dList, "__OTR__", 7) == 0) {
            return;
        }
    } else if (limbIndex == PLAYER_LIMB_SHEATH) {
        // MM's sheath (with or without its sword), bigger; the back shield is drawn after it the same way.
        if (strncmp((const char*)*dList, "__OTR__", 7) == 0) {
            Gfx* gear = AdultGear((const char*)*dList, nullptr, nullptr, lod);
            *dList = (gear != nullptr) ? (void*)gear : (void*)Pick(kBody[PLAYER_LIMB_SHEATH], lod);
        }
        return;
    } else if ((limbIndex == PLAYER_LIMB_HAT) && MaskReplacesCap(player->currentMask)) {
        *dList = nullptr; // a hat-like mask: OOT's long cap would poke through it (child Link's fits inside)
        return;
    }
    if (kBody[limbIndex].nearDL[0] != '\0') {
        *dList = (void*)Pick(kBody[limbIndex], lod);
    }
}

#endif // COMBO_BUILD
