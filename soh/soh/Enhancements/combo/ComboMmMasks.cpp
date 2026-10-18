// ComboShip (cross-game items): Majora's Mask's Blast, Stone and Kamaro's Masks worn by child Link in OOT
// (OoTMM's blastMaskOot / stoneMaskOot / kamaroMaskOot). They live in the child trade cycle
// (ShuffleTradeItems.c) as ITEM_COMBO_MASK_*, and using one from a C button toggles it on Link's face.
//
// Worn state is separate from Player::currentMask (whose tables - sMaskDlists, face reactions, mask draw - only
// cover OOT's eight masks): putting a combo mask on takes the OOT mask off and vice versa. It persists across
// scenes like OOT's mask does and is cleared on file load and age change.
//
// Effects follow OoTMM (MIT, https://github.com/OoTMM/OoTMM, packages/generator/src/oot/actors/Player.c and
// src/oot/actors.c): B with the Blast Mask sets off an explosion at Link's face (cooldown 0x136 frames; it does
// not hurt Link), the Stone Mask makes OoTMM's list of enemies and guards ignore Link (their update sees Link far
// away), and Kamaro's Mask is worn for show. Drawn with MM's own player mask display lists through "@mm:" routing.
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
#include "soh/Enhancements/combo/ComboMmAssets.h"
#include "src/overlays/actors/ovl_En_Bom/z_en_bom.h"
extern PlayState* gPlayState;
// Declared first so OPEN_DISPS' block-scope declarations keep C linkage.
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);
}

namespace {

// ITEM_COMBO_MASK_* worn by Link, or 0.
u8 sWornMask = 0;
// Frames until the Blast Mask can go off again (OoTMM's default delay).
u16 sBlastCooldown = 0;
constexpr u16 kBlastCooldownFrames = 0x136;

// Stone Mask: the actor whose player-relative fields are faked during its update, and the real values.
Actor* sStoneActor = nullptr;
s16 sStoneYaw;
f32 sStoneDistSq;
f32 sStoneDistXZ;
f32 sStoneDistY;

bool IsComboMaskItem(int32_t item) {
    return item >= ITEM_COMBO_MASK_BLAST && item <= ITEM_COMBO_MASK_KAMARO;
}

void Reset() {
    sWornMask = 0;
    sBlastCooldown = 0;
    sStoneActor = nullptr;
}

// OoTMM's shouldActorIgnorePlayer (src/oot/actors.c).
bool StoneMaskHides(Actor* actor) {
    switch (actor->id) {
        case ACTOR_EN_AM:
            return actor->params != 0; // Armos statues that are enemies, not the pushable ones
        case ACTOR_EN_NY:
        case ACTOR_EN_SB:
        case ACTOR_EN_RR:
        case ACTOR_EN_TORCH2:
        case ACTOR_EN_FZ:
        case ACTOR_EN_WEIYER:
        case ACTOR_EN_EIYER:
        case ACTOR_EN_BB:
        case ACTOR_EN_ANUBICE:
        case ACTOR_EN_TP:
        case ACTOR_EN_BA:
        case ACTOR_EN_GOMA:
        case ACTOR_EN_DODOJR:
        case ACTOR_EN_SKJ:
        case ACTOR_EN_CROW:
        case ACTOR_EN_HINTNUTS:
        case ACTOR_EN_WALLMAS:
        case ACTOR_EN_REEBA:
        case ACTOR_EN_FLOORMAS:
        case ACTOR_EN_DEKUNUTS:
        case ACTOR_EN_DNS:
        case ACTOR_EN_MB:
        case ACTOR_EN_TITE:
        case ACTOR_EN_PEEHAT:
        case ACTOR_EN_FIREFLY:
        case ACTOR_EN_DODONGO:
        case ACTOR_EN_SW:
        case ACTOR_EN_VM:
        case ACTOR_EN_ST:
        case ACTOR_EN_GE1:
        case ACTOR_EN_GE2:
        case ACTOR_EN_BILI:
        case ACTOR_EN_VALI:
        case ACTOR_EN_HEISHI2:
        case ACTOR_EN_HEISHI3:
        case ACTOR_EN_HEISHI4:
            return true;
        default:
            return false;
    }
}

void StoneRestore() {
    if (sStoneActor == nullptr) {
        return;
    }
    sStoneActor->yawTowardsPlayer = sStoneYaw;
    sStoneActor->xyzDistToPlayerSq = sStoneDistSq;
    sStoneActor->xzDistToPlayer = sStoneDistXZ;
    sStoneActor->yDistToPlayer = sStoneDistY;
    sStoneActor = nullptr;
}

bool CanActWithMask(Player* player) {
    return !(player->stateFlags1 &
             (PLAYER_STATE1_CARRYING_ACTOR | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_INPUT_DISABLED |
              PLAYER_STATE1_DEAD | PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_TALKING | PLAYER_STATE1_CLIMBING_LADDER |
              PLAYER_STATE1_HANGING_OFF_LEDGE)) &&
           !(player->stateFlags2 & PLAYER_STATE2_UNDERWATER) && player->rideActor == nullptr;
}

// OoTMM's Player_BlastMask: a bomb that goes off on the next frame at Link's face. Its explosion only hits what
// player attacks hit (enemies, bombable walls), never Link.
void BlastMaskExplode(PlayState* play, Player* player) {
    EnBom* bomb = (EnBom*)Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOM, player->actor.focus.pos.x,
                                      player->actor.focus.pos.y, player->actor.focus.pos.z, 0, 0, 0, BOMB_BODY);
    if (bomb == nullptr) {
        return;
    }
    bomb->timer = 1;
    bomb->explosionCollider.base.atFlags = AT_ON | AT_TYPE_PLAYER;
    sBlastCooldown = kBlastCooldownFrames;
}

void OnPlayerPreUpdate(Player* player, PlayState* play) {
    if (sWornMask != ITEM_COMBO_MASK_BLAST || sBlastCooldown != 0) {
        return;
    }
    Input* input = &play->state.input[0];
    if (!CHECK_BTN_ALL(input->press.button, BTN_B) || !CanActWithMask(player)) {
        return;
    }
    BlastMaskExplode(play, player);
    // Swallow the press so B doesn't also swing the sword this frame (OoTMM does the same).
    input->press.button &= ~BTN_B;
}

void OnPlayerUpdate() {
    if (gPlayState == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (sBlastCooldown != 0) {
        sBlastCooldown--;
    }
    // An OOT mask going on (or growing up) takes the MM mask off.
    if (sWornMask != 0 && (player->currentMask != PLAYER_MASK_NONE || LINK_IS_ADULT)) {
        sWornMask = 0;
    }
}

} // namespace

// z_player.c (Player_DrawGameplay): draw the worn MM mask on the head. Segment 0x0D holds the limb matrices of
// the skeleton just drawn; 0x1C0 is the head (limb 7), OoTMM's prepareMask. Bit 0 marks the address segmented
// for libultraship.
extern "C" void Combo_OotMask_Draw(PlayState* play, Player* player) {
    if (sWornMask == 0 || GameInteractor_InvisibleLinkActive()) {
        return;
    }
    static Gfx sBlastFuseOff[] = {
        gsDPSetEnvColor(0, 0, 0, 255),
        gsSPEndDisplayList(),
    };

    OPEN_DISPS(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, 0x0D0001C1, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    switch (sWornMask) {
        case ITEM_COMBO_MASK_BLAST:
            // MM's Player_DrawBlastMask with the fuse not lit: segment 9 just sets an opaque env color.
            gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)sBlastFuseOff);
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gComboMmBlastMaskDL);
            break;
        case ITEM_COMBO_MASK_STONE:
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gComboMmStoneMaskDL);
            break;
        case ITEM_COMBO_MASK_KAMARO:
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gComboMmKamaroMaskDL);
            break;
        default:
            break;
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

static void RegisterComboMmMasks() {
    const bool on = IS_RANDO;
    Reset();

    // Using a combo mask from a C button toggles it; it never becomes a held item.
    COND_VB_SHOULD(VB_CHANGE_HELD_ITEM_AND_USE_ITEM, on, {
        int32_t item = va_arg(args, int32_t);
        if (!IsComboMaskItem(item)) {
            return;
        }
        *should = false;
        Player* player = GET_PLAYER(gPlayState);
        if (LINK_IS_ADULT) {
            Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
            return;
        }
        if (sWornMask == item) {
            sWornMask = 0;
        } else {
            sWornMask = (u8)item;
            player->currentMask = PLAYER_MASK_NONE;
        }
        Player_PlaySfx(&player->actor, NA_SE_PL_CHANGE_ARMS);
    });

    // They have no item action (sItemActions stops well before these ids).
    COND_VB_SHOULD(VB_ITEM_ACTION_BE_NONE, on, {
        int32_t item = va_arg(args, int32_t);
        if (IsComboMaskItem(item)) {
            *should = true;
        }
    });

    // Pause-menu name: MM's own name texture.
    COND_VB_SHOULD(VB_DRAW_CUSTOM_ITEM_NAME, on, {
        u32 namedItem = va_arg(args, u32);
        const char* tex = nullptr;
        switch (namedItem) {
            case ITEM_COMBO_MASK_BLAST:
                tex = gComboMmNameBlastMaskTex;
                break;
            case ITEM_COMBO_MASK_STONE:
                tex = gComboMmNameStoneMaskTex;
                break;
            case ITEM_COMBO_MASK_KAMARO:
                tex = gComboMmNameKamaroMaskTex;
                break;
            default:
                return;
        }
        *should = true;
        memcpy(gPlayState->pauseCtx.nameSegment, tex, strlen(tex) + 1);
    });

    COND_HOOK(OnPlayerUpdate, on, OnPlayerUpdate);

    // Before every actor update: the Blast Mask reads B ahead of Link's own update, and the Stone Mask hides Link
    // from the actors on OoTMM's list by making them see him far away for the duration of their update.
    COND_HOOK(ShouldActorUpdate, on, [](void* actorPtr, bool* result) {
        StoneRestore(); // an update that was skipped never reached OnActorUpdate
        Actor* actor = (Actor*)actorPtr;
        if (gPlayState == nullptr) {
            return;
        }
        if (actor->id == ACTOR_PLAYER) {
            OnPlayerPreUpdate((Player*)actor, gPlayState);
            return;
        }
        if (sWornMask != ITEM_COMBO_MASK_STONE || !StoneMaskHides(actor)) {
            return;
        }
        sStoneActor = actor;
        sStoneYaw = actor->yawTowardsPlayer;
        sStoneDistSq = actor->xyzDistToPlayerSq;
        sStoneDistXZ = actor->xzDistToPlayer;
        sStoneDistY = actor->yDistToPlayer;
        actor->yawTowardsPlayer = (s16)(u16)(Rand_ZeroOne() * 0x10000);
        actor->xyzDistToPlayerSq = 10000.0f * 10000.0f;
        actor->xzDistToPlayer = 10000.0f;
        actor->yDistToPlayer = 10000.0f;
    });
    COND_HOOK(OnActorUpdate, on, [](void* actor) { StoneRestore(); });

    COND_HOOK(OnLoadGame, on, [](int32_t fileNum) { Reset(); });
}

static RegisterShipInitFunc initFunc(RegisterComboMmMasks, { "IS_RANDO" });

#endif // COMBO_BUILD
