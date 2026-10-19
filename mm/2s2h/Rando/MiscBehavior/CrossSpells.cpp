// mm/2s2h/Rando/MiscBehavior/CrossSpells.cpp
// ComboShip (cross-game items): Ocarina of Time's magic spells cast by human Link in Majora's Mask (OoTMM's
// "Din's Fire (MM)", "Farore's Wind (MM)", "Nayru's Love (MM)"). Used from a C button through
// Combo_CrossItem_TryUse; they live in the trade-slot cycles like the other cross-game items.
//
// The effects are OOT's Magic_Fire / Magic_Dark behavior (soh/src/overlays/actors/ovl_Magic_*), run here as
// combo-owned state driven from the player's update and draw instead of spawned actors (MM's actor table has
// no slot for them), drawn with OOT's own spell models through "@oot:" routing:
// - Din's Fire: the expanding fire sphere; its cylinder hits what player attacks hit, as MM's fire arrows do
//   (OoTMM uses DMG_FIRE_ARROW too). Costs 12 magic, like OOT.
// - Nayru's Love: Link can't be hurt for 1200 frames (OOT keeps invincibilityTimer at -100); 24 magic.
// - Farore's Wind: the first cast sets a warp point where Link stands (12 magic); with a point set, casting asks
//   "Return to the Warp Point / Dispel the Warp Point / Exit", like OOT (returning uses the point up and costs no
//   magic). The point is saved (RandoSaveInfo::comboFw) and restored through RESPAWN_MODE_HUMAN, which Player_Init
//   reads for respawnFlag 8 (OoTMM's approach); refused where the Song of Soaring is. With Cross-Games Farore's Wind
//   (RO_CROSS_GAME_FW) each game keeps its own point (combo/rando/CrossFw.h) and the prompt offers Hyrule's too.
#ifdef COMBO_BUILD

#include "CrossGear.h"
#include "MiscBehavior.h"
#include "2s2h/Rando/Rando.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include <libultraship/libultraship.h>
#include "ComboExport.h"
#include "rando/CrossFw.h"
#include "2s2h/SaveManager/SaveManager.h"
#include "2s2h/CustomMessage/CustomMessage.h"

extern "C" {
#include "variables.h"
#include "functions.h"
#include "macros.h"
#include "sfx.h"
#include "z64player.h"
s32 Map_CurRoomHasMapI(PlayState* play);
void Combo_RequestCrossSwitch(int ootEntrance); // BenPort.cpp (teleport songs)
// Declared first so OPEN_DISPS' block-scope declarations keep C linkage (inside the anonymous namespace below
// they would otherwise name an undefined C++ function and lib2ship would fail to load).
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);
}

#include "align_asset_macro.h"

namespace {

// OPEN_DISPS / CLOSE_DISPS declare these at block scope; inside this namespace that names
// (anonymous)::FrameInterpolation_*, so redeclare them here with C linkage to bind the real ones.
extern "C" {
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);
}

#define COMBO_OOT_SPELL(path) "__OTR__@oot:overlays/" path
static const ALIGN_ASSET(2) char sDinsTex[] = COMBO_OOT_SPELL("ovl_Magic_Fire/sTex");
static const ALIGN_ASSET(2) char sDinsMaterialDL[] = COMBO_OOT_SPELL("ovl_Magic_Fire/sMaterialDL");
static const ALIGN_ASSET(2) char sDinsModelDL[] = COMBO_OOT_SPELL("ovl_Magic_Fire/sModelDL");
static const ALIGN_ASSET(2) char sNayrusMaterialDL[] = COMBO_OOT_SPELL("ovl_Magic_Dark/sDiamondMaterialDL");
static const ALIGN_ASSET(2) char sNayrusModelDL[] = COMBO_OOT_SPELL("ovl_Magic_Dark/sDiamondModelDL");

constexpr s16 kCostDins = 12;
constexpr s16 kCostFarores = 12;
constexpr s16 kCostNayrus = 24;
constexpr s16 kNayrusDuration = 1200;

// ---- Din's Fire (OOT MagicFire) ----
enum DinsAction { DINS_OFF, DINS_WAIT, DINS_INIT, DINS_EXPAND_SLOWLY, DINS_STOP_EXPANDING, DINS_EXPAND_QUICKLY };
enum DinsTint { TINT_NONE, TINT_FADE_IN, TINT_MAINTAIN, TINT_FADE_OUT, TINT_FINISHED };

struct DinsState {
    s16 action = DINS_OFF;
    s16 actionTimer = 0;
    s16 tint = TINT_NONE;
    s16 tintTimer = 0;
    f32 scale = 0.0f;
    f32 scalingSpeed = 0.0f;
    f32 alpha = 0.0f;
    f32 tintIntensity = 0.0f;
    Vec3f pos = { 0.0f, 0.0f, 0.0f };
    ColliderCylinder collider;
    bool colliderReady = false;
};
DinsState sDins;

ColliderCylinderInit sDinsCylinderInit = {
    {
        COL_MATERIAL_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_NONE,
        OC2_TYPE_1,
        COLSHAPE_CYLINDER,
    },
    {
        ELEM_MATERIAL_UNK0,
        { DMG_FIRE_ARROW, 0x00, 0x02 },
        { 0x00000000, 0x00, 0x00 },
        ATELEM_ON | ATELEM_SFX_NONE,
        ACELEM_NONE,
        OCELEM_NONE,
    },
    { 9, 9, 0, { 0, 0, 0 } },
};

// ---- Nayru's Love (OOT MagicDark diamond) ----
s16 sNayrusTimer = 0; // frames Nayru's Love has been up; 0 = off
s16 sNayrusAge = 0;   // frames since cast, for the opening scale/alpha ramp
s16 sNayrusYaw = 0;
Vec3f sNayrusPos = { 0.0f, 0.0f, 0.0f };

// ---- Farore's Wind ----
// The point: RANDO_SAVE.comboFwState / comboFw (z64save.h).
ComboFwPeerFn sFwPeer = nullptr; // launcher: forwards an op to OOT (-1 when OOT's save isn't resident)
bool sFwArrivalArmed = false;    // the next cross-game arrival spawns at the point

bool CrossFwOn() {
    return IS_RANDO && (RANDO_SAVE_OPTIONS[RO_CROSS_GAME_FW] == RO_GENERIC_YES) &&
           (RANDO_SAVE_OPTIONS[RO_CROSS_SPELL_WIND] == RO_GENERIC_YES);
}

int FwPeer(int op) {
    return (CrossFwOn() && (sFwPeer != nullptr)) ? sFwPeer(op) : -1;
}

bool SongPlaying(PlayState* play) {
    s32 msgMode = play->msgCtx.msgMode;
    // MSGMODE_E is OOT's MSGMODE_OCARINA_CORRECT_PLAYBACK (same slot, unnamed in MM's decomp).
    return (msgMode == MSGMODE_E) || (msgMode == MSGMODE_SONG_PLAYED);
}

// Same places MM refuses the Song of Soaring (WarpSongs.cpp uses the same test).
bool WarpForbiddenHere(PlayState* play) {
    return play->interfaceCtx.restrictions.songOfSoaring != 0 || Map_CurRoomHasMapI(play) ||
           play->sceneId == SCENE_SECOM;
}

s16 sMagicResetTimer = 0; // frames until the meter is put back to idle after a cast

bool SpendMagic(PlayState* play, s16 cost) {
    if (!gSaveContext.save.saveInfo.playerData.isMagicAcquired || !Magic_Consume(play, cost, MAGIC_CONSUME_NOW)) {
        Audio_PlaySfx(NA_SE_SY_ERROR);
        return false;
    }
    sMagicResetTimer = 20;
    return true;
}

// MM leaves the meter flashing after MAGIC_CONSUME_NOW until the caster calls Magic_Reset (OOT's Player does it when
// the spell ends), and Magic_Consume refuses while it flashes: without this the next cast in the scene failed.
void UpdateMagicReset(PlayState* play) {
    if (sMagicResetTimer == 0) {
        return;
    }
    const s16 state = gSaveContext.magicState;
    if ((state == MAGIC_STATE_CONSUME_SETUP) || (state == MAGIC_STATE_CONSUME)) {
        return; // not drained yet (the interface is paused)
    }
    if (--sMagicResetTimer == 0 && ((state == MAGIC_STATE_METER_FLASH_1) || (state == MAGIC_STATE_METER_FLASH_2) ||
                                    (state == MAGIC_STATE_METER_FLASH_3))) {
        Magic_Reset(play);
    }
}

void CastDins(PlayState* play, Player* player) {
    if (sDins.action != DINS_OFF || !SpendMagic(play, kCostDins)) {
        return;
    }
    sDins = DinsState{};
    Collider_InitCylinder(play, &sDins.collider);
    Collider_SetCylinder(play, &sDins.collider, &player->actor, &sDinsCylinderInit);
    sDins.colliderReady = true;
    sDins.action = DINS_WAIT;
    sDins.actionTimer = 20; // MagicFire_UpdateBeforeCast's wind-up
    sDins.pos = player->actor.world.pos;
}

void CastNayrus(PlayState* play, Player* player) {
    if (sNayrusTimer != 0 || !SpendMagic(play, kCostNayrus)) {
        return;
    }
    sNayrusTimer = 1;
    sNayrusAge = 0;
    sNayrusPos = player->actor.world.pos;
}

// Farore's Wind's prompt, like OOT's (Player_Action_8085063C): with a point already set, casting asks what to do.
// With Cross-Games Farore's Wind each game keeps its own point, and Hyrule's can be warped to from here.
enum FwPrompt : u8 {
    FW_PROMPT_NONE,
    FW_PROMPT_HERE,  // "Return / Dispel / Exit" (Termina's point only)
    FW_PROMPT_THERE, // "Warp to Hyrule / Set one here / Exit" (Hyrule's point only)
    FW_PROMPT_BOTH,  // "Return / Dispel / Exit", then "which point?" for Return
    FW_PROMPT_WHICH, // the second box of FW_PROMPT_BOTH
};
FwPrompt sFwPrompt = FW_PROMPT_NONE;

bool FwHere() {
    // A "lives in Hyrule" marker from before ComboShip kept a point per game counts as no point.
    return gSaveContext.save.shipSaveInfo.rando.comboFwState == CFW_HERE;
}

bool FwThere() {
    return FwPeer(CFW_STATE) == CFW_HERE;
}

void FwSetHere(PlayState* play, Player* player) {
    // Set the warp point here (OOT's "Warp Point set" behavior; PLAYER_PARAMS(0xFF, START_MODE_D)).
    RandoSaveInfo& rando = gSaveContext.save.shipSaveInfo.rando;
    Play_SetRespawnData(play, RESPAWN_MODE_HUMAN, gSaveContext.save.entrance, play->roomCtx.curRoom.num, 0xDFF,
                        &player->actor.world.pos, player->actor.shape.rot.y);
    rando.comboFw = gSaveContext.respawn[RESPAWN_MODE_HUMAN];
    rando.comboFwState = CFW_HERE;
    Player_PlaySfx(player, NA_SE_PL_MAGIC_WIND_WARP);
}

void FwReturnHere(PlayState* play, Player* player) {
    // Player_Init reads respawn[respawnFlag - 1] for a positive flag; the point is used up, like OOT's.
    RandoSaveInfo& rando = gSaveContext.save.shipSaveInfo.rando;
    gSaveContext.respawn[RESPAWN_MODE_HUMAN] = rando.comboFw;
    rando.comboFwState = CFW_NONE;
    gSaveContext.respawnFlag = RESPAWN_MODE_HUMAN + 1;
    play->nextEntrance = rando.comboFw.entrance;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_WHITE_FAST;
    Player_PlaySfx(player, NA_SE_PL_MAGIC_WIND_VANISH);
}

void FwWarpThere(Player* player) {
    // Hyrule's point: OOT spawns there on arrival and uses it up. Termina's point stays.
    const int entrance = FwThere() ? FwPeer(CFW_ENTRANCE) : -1;
    if ((entrance >= 0) && (FwPeer(CFW_ARM_ARRIVAL) >= 0)) {
        Player_PlaySfx(player, NA_SE_PL_MAGIC_WIND_VANISH);
        Combo_RequestCrossSwitch(entrance); // persist + switch on the next clean frame
    } else {
        Audio_PlaySfx(NA_SE_SY_ERROR); // Hyrule's point is gone
    }
}

void FwStartPrompt(Player* player, FwPrompt prompt) {
    // Laid out by hand (autoFormat off): '\x11' new line, '\xC2' / '\xC3' two / three choices, '\x02' green.
    CustomMessage::Entry entry;
    entry.autoFormat = false;
    std::string msg;
    switch (prompt) {
        case FW_PROMPT_THERE:
            msg = std::string("There is a warp point in Hyrule.\x11") + "\xC3" + "\x02" + "Warp to Hyrule\x11" +
                  "Set a warp point here\x11" + "Exit";
            break;
        case FW_PROMPT_WHICH:
            msg = std::string("Warp to which point?\x11") + "\xC2" + "\x02" + "Termina\x11" + "Hyrule";
            break;
        default:
            msg = std::string("Warp to the warp point?\x11") + "\xC3" + "\x02" + "Return to the warp point\x11" +
                  "Dispel the warp point\x11" + "Exit";
            break;
    }
    CustomMessage::EnsureMessageEnd(&msg);
    sFwPrompt = prompt;
    // Hold Link still while it is up (PLAYER_STATE1_20 zeroes his input; the box reads the controller itself).
    player->stateFlags1 |= PLAYER_STATE1_TALKING | PLAYER_STATE1_20;
    CustomMessage::StartTextbox(msg, entry);
}

void FwEndPrompt(Player* player) {
    sFwPrompt = FW_PROMPT_NONE;
    player->stateFlags1 &= ~(PLAYER_STATE1_TALKING | PLAYER_STATE1_20);
}

void UpdateFwPrompt(PlayState* play, Player* player) {
    if (sFwPrompt == FW_PROMPT_NONE) {
        return;
    }
    if (Message_GetState(&play->msgCtx) == TEXT_STATE_NONE) {
        FwEndPrompt(player); // closed some other way (a scene change, a cutscene)
        return;
    }
    if ((Message_GetState(&play->msgCtx) != TEXT_STATE_CHOICE) || !Message_ShouldAdvance(play)) {
        return;
    }
    const u8 choice = play->msgCtx.choiceIndex;
    const FwPrompt prompt = sFwPrompt;
    Message_CloseTextbox(play);
    FwEndPrompt(player);
    switch (prompt) {
        case FW_PROMPT_THERE:
            if (choice == 0) {
                FwWarpThere(player);
            } else if ((choice == 1) && SpendMagic(play, kCostFarores)) {
                FwSetHere(play, player);
            }
            break;
        case FW_PROMPT_WHICH:
            if (choice == 0) {
                FwReturnHere(play, player);
            } else {
                FwWarpThere(player);
            }
            break;
        default: // FW_PROMPT_HERE / FW_PROMPT_BOTH
            if (choice == 0) {
                if (prompt == FW_PROMPT_BOTH) {
                    FwStartPrompt(player, FW_PROMPT_WHICH);
                } else {
                    FwReturnHere(play, player);
                }
            } else if (choice == 1) {
                gSaveContext.save.shipSaveInfo.rando.comboFwState = CFW_NONE;
                Player_PlaySfx(player, NA_SE_PL_MAGIC_WIND_VANISH);
            }
            break;
    }
}

void CastFarores(PlayState* play, Player* player) {
    if ((sFwPrompt != FW_PROMPT_NONE) || WarpForbiddenHere(play)) {
        Audio_PlaySfx(NA_SE_SY_ERROR);
        return;
    }
    const bool here = FwHere();
    const bool there = FwThere(); // Cross-Games Farore's Wind only
    if (!here && !there) {
        if (SpendMagic(play, kCostFarores)) {
            FwSetHere(play, player);
        }
        return;
    }
    // A point to go to: ask, as OOT does (returning costs no magic there either).
    FwStartPrompt(player, here ? (there ? FW_PROMPT_BOTH : FW_PROMPT_HERE) : FW_PROMPT_THERE);
}

// MagicFire_UpdateBeforeCast + MagicFire_Update.
void UpdateDins(PlayState* play, Player* player) {
    if (sDins.action == DINS_OFF) {
        return;
    }
    sDins.pos = player->actor.world.pos;
    if (SongPlaying(play)) {
        sDins.action = DINS_OFF;
        return;
    }
    if (sDins.action == DINS_WAIT) {
        if (sDins.actionTimer > 0) {
            sDins.actionTimer--;
        } else {
            sDins.action = DINS_INIT;
            Player_PlaySfx(player, NA_SE_PL_MAGIC_FIRE);
        }
        return;
    }

    ColliderCylinder* col = &sDins.collider;
    if (sDins.action == DINS_EXPAND_SLOWLY) {
        col->elem.atDmgInfo.damage = sDins.actionTimer + 25;
    } else if (sDins.action == DINS_STOP_EXPANDING) {
        col->elem.atDmgInfo.damage = sDins.actionTimer;
    }
    col->dim.pos.x = (s16)sDins.pos.x;
    col->dim.pos.y = (s16)sDins.pos.y;
    col->dim.pos.z = (s16)sDins.pos.z;
    col->dim.radius = (s16)(sDins.scale * 325.0f);
    col->dim.height = (s16)(sDins.scale * 450.0f);
    col->dim.yShift = (s16)(sDins.scale * -225.0f);
    CollisionCheck_SetAT(play, &play->colChkCtx, &col->base);

    switch (sDins.action) {
        case DINS_INIT:
            sDins.actionTimer = 30;
            sDins.scale = 0.0f;
            sDins.alpha = 0.0f;
            sDins.scalingSpeed = 0.08f;
            sDins.action = DINS_EXPAND_SLOWLY;
            break;
        case DINS_EXPAND_SLOWLY:
            Math_StepToF(&sDins.alpha, 1.0f, 1.0f / 30.0f);
            if (sDins.actionTimer > 0) {
                Math_SmoothStepToF(&sDins.scale, 0.4f, sDins.scalingSpeed, 0.1f, 0.001f);
            } else {
                sDins.actionTimer = 25;
                sDins.action = DINS_STOP_EXPANDING;
            }
            break;
        case DINS_STOP_EXPANDING:
            if (sDins.actionTimer <= 0) {
                sDins.actionTimer = 15;
                sDins.action = DINS_EXPAND_QUICKLY;
                sDins.scalingSpeed = 0.05f;
            }
            break;
        case DINS_EXPAND_QUICKLY:
            sDins.alpha -= 8.0f / 119.00001f;
            sDins.scale += sDins.scalingSpeed;
            if (sDins.alpha <= 0.0f) {
                sDins.action = DINS_OFF;
                return;
            }
            break;
        default:
            break;
    }
    switch (sDins.tint) {
        case TINT_NONE:
            if (sDins.tintTimer <= 0) {
                sDins.tintTimer = 20;
                sDins.tint = TINT_FADE_IN;
            }
            break;
        case TINT_FADE_IN:
            sDins.tintIntensity = 1.0f - (sDins.tintTimer / 20.0f);
            if (sDins.tintTimer <= 0) {
                sDins.tintTimer = 45;
                sDins.tint = TINT_MAINTAIN;
            }
            break;
        case TINT_MAINTAIN:
            if (sDins.tintTimer <= 0) {
                sDins.tintTimer = 5;
                sDins.tint = TINT_FADE_OUT;
            }
            break;
        case TINT_FADE_OUT:
            sDins.tintIntensity = sDins.tintTimer / 5.0f;
            if (sDins.tintTimer <= 0) {
                sDins.tint = TINT_FINISHED;
            }
            break;
        default:
            break;
    }
    if (sDins.actionTimer > 0) {
        sDins.actionTimer--;
    }
    if (sDins.tintTimer > 0) {
        sDins.tintTimer--;
    }
}

// MagicDark_DiamondUpdate.
void UpdateNayrus(PlayState* play, Player* player) {
    if (sNayrusTimer == 0) {
        return;
    }
    if (SongPlaying(play) || sNayrusTimer >= kNayrusDuration) {
        player->invincibilityTimer = 0;
        sNayrusTimer = 0;
        return;
    }
    player->invincibilityTimer = -100;
    sNayrusYaw += 0x3E8;
    sNayrusAge++;
    sNayrusTimer++;
    Actor_PlaySfx_Flagged(&player->actor,
                          (sNayrusTimer < 1100) ? NA_SE_PL_MAGIC_SOUL_NORMAL : NA_SE_PL_MAGIC_SOUL_FLASH);
}

void DrawDins(PlayState* play) {
    if (sDins.action < DINS_EXPAND_SLOWLY) {
        return;
    }
    u32 frames = play->gameplayFrames;
    OPEN_DISPS(play->state.gfxCtx);
    // Screen tint.
    POLY_XLU_DISP = Gfx_SetupDL57(POLY_XLU_DISP);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, (u8)(s32)(60 * sDins.tintIntensity), (u8)(s32)(20 * sDins.tintIntensity), 0,
                    (u8)(s32)(120 * sDins.tintIntensity));
    gDPSetAlphaDither(POLY_XLU_DISP++, G_AD_DISABLE);
    gDPSetColorDither(POLY_XLU_DISP++, G_CD_DISABLE);
    gDPFillRectangle(POLY_XLU_DISP++, 0, 0, 319, 239);
    // The sphere, at Link, scaled like the actor (actor scale, then MagicFire_Draw's 0.15).
    Gfx_SetupDL25_Xlu(play->state.gfxCtx);
    u8 alpha = (u8)(sDins.alpha * 255);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 255, 200, 0, alpha);
    gDPSetEnvColor(POLY_XLU_DISP++, 255, 0, 0, alpha);
    Matrix_Translate(sDins.pos.x, sDins.pos.y, sDins.pos.z, MTXMODE_NEW);
    Matrix_Scale(sDins.scale * 0.15f, sDins.scale * 0.15f, sDins.scale * 0.15f, MTXMODE_APPLY);
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, play->state.gfxCtx);
    gDPPipeSync(POLY_XLU_DISP++);
    gSPTexture(POLY_XLU_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTextureLUT(POLY_XLU_DISP++, G_TT_NONE);
    gDPLoadTextureBlock(POLY_XLU_DISP++, sDinsTex, G_IM_FMT_I, G_IM_SIZ_8b, 64, 64, 0, G_TX_NOMIRROR | G_TX_WRAP,
                        G_TX_NOMIRROR | G_TX_WRAP, 6, 6, 15, G_TX_NOLOD);
    gDPSetTile(POLY_XLU_DISP++, G_IM_FMT_I, G_IM_SIZ_8b, 8, 0, 1, 0, G_TX_NOMIRROR | G_TX_WRAP, 6, 14,
               G_TX_NOMIRROR | G_TX_WRAP, 6, 14);
    gDPSetTileSize(POLY_XLU_DISP++, 1, 0, 0, 252, 252);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)sDinsMaterialDL);
    gSPDisplayList(POLY_XLU_DISP++,
                   Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, (frames * 2) % 512, 511 - ((frames * 5) % 512), 64, 64, 1,
                                      (frames * 2) % 256, 255 - ((frames * 20) % 256), 32, 32, 2, -5, 2, -20));
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)sDinsModelDL);
    CLOSE_DISPS(play->state.gfxCtx);
}

void DrawNayrus(PlayState* play, Player* player) {
    if (sNayrusTimer == 0) {
        return;
    }
    // MagicDark_DiamondUpdate's scale/alpha ramp (child Link's 0.4 base scale).
    const f32 base = 0.4f;
    f32 scaleXZ = base;
    f32 scaleY = base;
    if (sNayrusAge < 20) {
        scaleXZ = (1.6f - (sNayrusAge * 0.03f)) * base;
        scaleY = ((sNayrusAge * 0.01f) + 0.8f) * base;
    }
    scaleXZ *= 1.3f;
    s32 primAlpha = 255;
    if (sNayrusTimer >= 1180) {
        primAlpha = 15595 - (sNayrusTimer * 13);
        if (sNayrusTimer & 1) {
            primAlpha >>= 1;
        }
    } else if (sNayrusTimer >= 1100) {
        primAlpha = (u8)(sNayrusTimer << 7) + 127;
    }
    s32 ramp = (sNayrusAge < 20) ? (sNayrusAge * 12) : 255;
    if (primAlpha > ramp) {
        primAlpha = ramp;
    }

    // MagicDark_DiamondDraw: follow Link's waist, easing vertically.
    f32 heightDiff = player->bodyPartsPos[0].y - sNayrusPos.y;
    sNayrusPos.x = player->bodyPartsPos[0].x;
    sNayrusPos.z = player->bodyPartsPos[0].z;
    if (heightDiff < -2.0f) {
        sNayrusPos.y = player->bodyPartsPos[0].y + 2.0f;
    } else if (heightDiff > 2.0f) {
        sNayrusPos.y = player->bodyPartsPos[0].y - 2.0f;
    }
    u16 frames = play->gameplayFrames;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL25_Xlu(play->state.gfxCtx);
    Matrix_Translate(sNayrusPos.x, sNayrusPos.y, sNayrusPos.z, MTXMODE_NEW);
    Matrix_Scale(scaleXZ, scaleY, scaleXZ, MTXMODE_APPLY);
    Matrix_RotateYS((s16)(sNayrusYaw + Camera_GetCamDirYaw(GET_ACTIVE_CAM(play))), MTXMODE_APPLY);
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, play->state.gfxCtx);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 170, 255, 255, (s32)(primAlpha * 0.6f) & 0xFF);
    gDPSetEnvColor(POLY_XLU_DISP++, 0, 100, 255, 128);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)sNayrusMaterialDL);
    gSPDisplayList(POLY_XLU_DISP++, Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, frames * 2, frames * -4, 32, 32, 1, 0,
                                                       frames * -16, 64, 32, 2, -4, 0, -16));
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)sNayrusModelDL);
    CLOSE_DISPS(play->state.gfxCtx);
}

void ResetSpellEffects() {
    sDins.action = DINS_OFF;
    sNayrusTimer = 0;
    sFwPrompt = FW_PROMPT_NONE;
}

} // namespace

extern "C" int Combo_Spell_Cast(PlayState* play, Player* player, int item) {
    switch (item) {
        case ITEM_COMBO_SPELL_FIRE:
            CastDins(play, player);
            return 1;
        case ITEM_COMBO_SPELL_LOVE:
            CastNayrus(play, player);
            return 1;
        case ITEM_COMBO_SPELL_WIND:
            CastFarores(play, player);
            return 1;
        default:
            return 0;
    }
}

extern "C" void Combo_Spells_OnPlayerUpdate(PlayState* play, Player* player) {
    UpdateMagicReset(play);
    UpdateFwPrompt(play, player);
    UpdateDins(play, player);
    UpdateNayrus(play, player);
}

bool Combo_MmIsForeground(void); // BenPort.cpp

// Cross-Games Farore's Wind (combo/rando/CrossFw.h), on MM's own save; the launcher calls it while MM is dormant too.
extern "C" COMBO_EXPORT int MM_ComboFw(int op) {
    if ((gSaveContext.fileNum > 2) || !CrossFwOn()) {
        return -1;
    }
    RandoSaveInfo& rando = gSaveContext.save.shipSaveInfo.rando;
    int result = 0;
    bool changed = false;
    switch (op) {
        case CFW_STATE:
            return rando.comboFwState;
        case CFW_MARK_CROSS:
            return 0; // each game keeps its own point now: no "lives over there" marker

        case CFW_CLEAR_IF_CROSS:
            changed = (rando.comboFwState == CFW_CROSS);
            if (changed) {
                rando.comboFwState = CFW_NONE;
            }
            break;
        case CFW_ENTRANCE:
            return (rando.comboFwState == CFW_HERE) ? rando.comboFw.entrance : -1;
        case CFW_ARM_ARRIVAL:
            if (rando.comboFwState != CFW_HERE) {
                return -1;
            }
            sFwArrivalArmed = true;
            return 0;
        default:
            return -1;
    }
    if (changed && !Combo_MmIsForeground()) {
        SaveManager_SaveCurrentForCombo(); // dormant: persist now, like a dormant item grant
    }
    return result;
}

extern "C" COMBO_EXPORT void MM_SetComboFwPeer(ComboFwPeerFn peer) {
    sFwPeer = peer;
}

// title_setup.c, after a cross-game arrival entrance was applied: an armed Farore's Wind arrival spawns at the point
// instead (and uses it up), like returning to it from Termina.
extern "C" void Combo_MM_ApplyFwArrival(void) {
    if (!sFwArrivalArmed) {
        return;
    }
    sFwArrivalArmed = false;
    RandoSaveInfo& rando = gSaveContext.save.shipSaveInfo.rando;
    if (rando.comboFwState != CFW_HERE) {
        return;
    }
    gSaveContext.save.entrance = rando.comboFw.entrance;
    gSaveContext.respawn[RESPAWN_MODE_HUMAN] = rando.comboFw;
    gSaveContext.respawnFlag = RESPAWN_MODE_HUMAN + 1;
    rando.comboFwState = CFW_NONE;
}

void Rando::MiscBehavior::CrossSpells() {
    const bool on = IS_RANDO && (RANDO_SAVE_OPTIONS[RO_CROSS_SPELL_FIRE] || RANDO_SAVE_OPTIONS[RO_CROSS_SPELL_WIND] ||
                                 RANDO_SAVE_OPTIONS[RO_CROSS_SPELL_LOVE]);
    ResetSpellEffects();
    sFwArrivalArmed = false;
    COND_ID_HOOK(OnActorDraw, ACTOR_PLAYER, on, [](Actor* actor) {
        DrawDins(gPlayState);
        DrawNayrus(gPlayState, (Player*)actor);
    });
    // A new scene drops Din's Fire (its collider belongs to the old player actor); Nayru's Love carries over
    // like OOT's nayrusLoveTimer.
    COND_HOOK(OnSceneInit, on, [](s8 sceneId, s8 spawnNum) { sDins.action = DINS_OFF; });
}

#endif // COMBO_BUILD
