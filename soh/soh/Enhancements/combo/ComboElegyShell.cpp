// ComboShip (cross-game items): the Elegy of Emptiness played in OOT leaves a statue of Link (an "empty shell"),
// like OoTMM's Custom_Torch2 (MIT License, Copyright (c) 2020-2022 OoTMM Team, https://github.com/OoTMM/OoTMM,
// packages/generator/src/oot/actors/Custom/Custom_Torch2.c, itself MM's En_Torch2). One shell at a time: playing
// the song again moves it. It fades in where Link stood, turns solid once he steps away, then blocks him, holds
// down switches and can be hookshotted, like MM's human shell. The model is MM's own human shell
// (gameplay_keep's gElegyShellHumanDL), drawn from MM's ResourceManager through an "@mm:" routed path.
// Registered as a dynamic-id actor (ActorDB), like SoH's Ivan.
#ifdef COMBO_BUILD
#include <cmath>

#include "soh/ActorDB.h"
#include "soh/ShipInit.hpp"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/frame_interpolation.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "align_asset_macro.h"
}

#include "ComboMmAssets.h"

static const ALIGN_ASSET(2) char kElegyShellHumanDL[] = COMBO_MM_ASSET("objects/gameplay_keep/gElegyShellHumanDL");

enum ShellState : s8 {
    SHELL_STATE_INITIALIZED, // waiting to appear at its home
    SHELL_STATE_FADING_IN,   // semitransparent until Link steps away
    SHELL_STATE_SOLID,
};

struct ComboElegyShell {
    Actor actor;
    ColliderCylinder collider;
    s16 alpha;
    s8 state;
    u8 framesUntilNextState;
};

static ComboElegyShell* sShell = nullptr;
static s16 sActorId = -1;

static ColliderCylinderInit sCylinderInit = {
    {
        COLTYPE_METAL,
        AT_NONE,
        AC_ON | AC_HARD | AC_TYPE_PLAYER,
        OC1_ON | OC1_TYPE_PLAYER | OC1_TYPE_1 | OC1_TYPE_2,
        OC2_TYPE_2,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK2,
        { 0x00100000, 0, 0 },
        { 0xF7CFFFFF, 0, 0 },
        TOUCH_NONE,
        BUMP_ON | BUMP_HOOKABLE,
        OCELEM_ON,
    },
    { 20, 60, 0, { 0, 0, 0 } },
};

static void Shell_Init(Actor* thisx, PlayState* play) {
    ComboElegyShell* shell = (ComboElegyShell*)thisx;
    thisx->colChkInfo.mass = MASS_IMMOVABLE;
    Collider_InitCylinder(play, &shell->collider);
    Collider_SetCylinder(play, &shell->collider, thisx, &sCylinderInit);
    thisx->flags |= ACTOR_FLAG_CAN_PRESS_SWITCHES;
    shell->alpha = 0;
    shell->state = SHELL_STATE_INITIALIZED;
    shell->framesUntilNextState = 20;
    sShell = shell;
}

static void Shell_Destroy(Actor* thisx, PlayState* play) {
    ComboElegyShell* shell = (ComboElegyShell*)thisx;
    Collider_DestroyCylinder(play, &shell->collider);
    if (sShell == shell) {
        sShell = nullptr;
    }
}

static void Shell_Update(Actor* thisx, PlayState* play) {
    ComboElegyShell* shell = (ComboElegyShell*)thisx;
    thisx->gravity = -1.0f;
    Actor_MoveXZGravity(thisx);
    Actor_UpdateBgCheckInfo(play, thisx, 30.0f, 20.0f, 70.0f, 5); // walls + floor

    if (shell->framesUntilNextState != 0) {
        shell->framesUntilNextState--;
        return;
    }
    s16 targetAlpha;
    if (shell->state == SHELL_STATE_INITIALIZED) {
        // Appear at its home once faded out (a replay moves the shell: it fades out first).
        if (shell->alpha == 0) {
            Math_Vec3f_Copy(&thisx->world.pos, &thisx->home.pos);
            thisx->shape.rot.y = thisx->home.rot.y;
            shell->state = SHELL_STATE_FADING_IN;
        }
        targetAlpha = 0;
    } else if (shell->state == SHELL_STATE_FADING_IN) {
        if ((thisx->xzDistToPlayer > 32.0f) || (std::fabs(thisx->yDistToPlayer) > 70.0f)) {
            shell->state = SHELL_STATE_SOLID;
        }
        targetAlpha = 60;
    } else {
        Collider_UpdateCylinder(thisx, &shell->collider);
        CollisionCheck_SetOC(play, &play->colChkCtx, &shell->collider.base);
        targetAlpha = 255;
    }
    Math_StepToS(&shell->alpha, targetAlpha, 8);
}

static void Shell_Draw(Actor* thisx, PlayState* play) {
    static Gfx sRenderModeNoneDL[] = {
        gsSPEndDisplayList(),
    };
    static Gfx sRenderModeXluDL[] = {
        gsDPSetRenderMode(AA_EN | Z_CMP | IM_RD | CLR_ON_CVG | CVG_DST_WRAP | ZMODE_XLU | FORCE_BL |
                              GBL_c1(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1),
                          G_RM_AA_ZB_XLU_SURF2),
        gsSPEndDisplayList(),
    };
    ComboElegyShell* shell = (ComboElegyShell*)thisx;

    OPEN_DISPS(play->state.gfxCtx);
    if (shell->alpha == 255) {
        gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)sRenderModeNoneDL);
        gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);
        Gfx_DrawDListOpa(play, (Gfx*)kElegyShellHumanDL);
    } else if (shell->alpha > 0) {
        gSPSegment(POLY_XLU_DISP++, 0x0C, (uintptr_t)sRenderModeXluDL);
        gDPSetEnvColor(POLY_XLU_DISP++, 255, 255, 255, shell->alpha);
        Gfx_DrawDListXlu(play, (Gfx*)kElegyShellHumanDL);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

static s16 ActorId() {
    if (sActorId < 0) {
        ActorDBInit entry = {
            "Combo_Elegy_Shell",
            "Elegy of Emptiness Shell (ComboShip)",
            -1,
            ACTORCAT_PROP, // not ITEMACTION: OOT code walks that list as arrows/bombs/boomerangs
            ACTOR_FLAG_UPDATE_CULLING_DISABLED,
            OBJECT_GAMEPLAY_KEEP,
            sizeof(ComboElegyShell),
            Shell_Init,
            Shell_Destroy,
            Shell_Update,
            Shell_Draw,
            nullptr,
        };
        sActorId = (s16)ActorDB::Instance->AddEntry(entry).entry.id;
    }
    return sActorId;
}

// ComboMmSongs.cpp: the Elegy was played. The shell appears where Link stands, facing his way.
extern "C" void Combo_ElegyShell_Place(PlayState* play) {
    Player* player = GET_PLAYER(play);
    if (sShell != nullptr) {
        Math_Vec3f_Copy(&sShell->actor.home.pos, &player->actor.world.pos);
        sShell->actor.home.rot.y = player->actor.shape.rot.y;
        sShell->state = SHELL_STATE_INITIALIZED;
        sShell->framesUntilNextState = 20;
        return;
    }
    Actor_Spawn(&play->actorCtx, play, ActorId(), player->actor.world.pos.x, player->actor.world.pos.y,
                player->actor.world.pos.z, 0, player->actor.shape.rot.y, 0, 0);
}
#endif
