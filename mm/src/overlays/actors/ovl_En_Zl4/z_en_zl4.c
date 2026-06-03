/*
 * File: z_en_zl4.c
 * Overlay: ovl_En_Zl4
 * Description: Glitched early version of Skull Kid stuck in a T-pose
 */

#include "z_en_zl4.h"
#include "objects/object_stk/object_stk.h"

#define FLAGS (ACTOR_FLAG_10 | ACTOR_FLAG_20)

#define THIS ((EnZl4*)thisx)

void EnZl4_Init(Actor* thisx, PlayState* play);
void EnZl4_Destroy(Actor* thisx, PlayState* play);
void EnZl4_Update(Actor* thisx, PlayState* play);
void EnZl4_Draw(Actor* thisx, PlayState* play);

void func_809A1BB0(SkelAnime* skelAnime, AnimationInfo* animInfo, u16 animIndex);
void func_809A1D0C(EnZl4* this, PlayState* play);
void func_809A1DA4(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, Actor* thisx);
void func_809A1DBC(PlayState* play, s32 limbIndex, Actor* thisx);
Gfx* func_809A1DD0(GraphicsContext* gfxCtx, u32 alpha);
Gfx* func_809A1E28(GraphicsContext* gfxCtx, u32 alpha);

ActorInit En_Zl4_InitVars = {
    /**/ ACTOR_EN_ZL4,
    /**/ ACTORCAT_ITEMACTION,
    /**/ FLAGS,
    /**/ OBJECT_STK,
    /**/ sizeof(EnZl4),
    /**/ EnZl4_Init,
    /**/ EnZl4_Destroy,
    /**/ EnZl4_Update,
    /**/ EnZl4_Draw,
};

AnimationInfo D_809A1F80 = { (AnimationHeader*)0x0600CC94, 1.0f, 0.0f, -1.0f, 0, 0.0f };

Vec3f D_809A1F98 = { 0.0f, 0.0f, 0.0f };

void func_809A1BB0(SkelAnime* skelAnime, AnimationInfo* animInfo, u16 animIndex) {
    f32 endFrame;

    animInfo += animIndex;

    if (animInfo->frameCount < 0.0f) {
        endFrame = Animation_GetLastFrame(animInfo->animation);
    } else {
        endFrame = animInfo->frameCount;
    }

    Animation_Change(skelAnime, animInfo->animation, animInfo->playSpeed, animInfo->startFrame, endFrame,
                     animInfo->mode, animInfo->morphFrames);
}

void EnZl4_Init(Actor* thisx, PlayState* play) {
    EnZl4* this = THIS;

    this->unk_2E0 = 0;
    this->unk_2F0 = 0xFF;
    this->actor.targetArrowOffset = 3000.0f;
    ActorShape_Init(&this->actor.shape, 0.0f, ActorShadow_DrawCircle, 24.0f);
    SkelAnime_InitFlex(play, &this->skelanime, &gSkullKidSkel, NULL, NULL, NULL, 0);
    func_809A1BB0(&this->skelanime, &D_809A1F80, 0);
    Actor_SetScale(&this->actor, 0.01f);
    this->actionFunc = func_809A1D0C;
}

void EnZl4_Destroy(Actor* thisx, PlayState* play) {
}

void func_809A1D0C(EnZl4* this, PlayState* play) {
}

void EnZl4_Update(Actor* thisx, PlayState* play) {
    EnZl4* this = THIS;
    SkelAnime_Update(&this->skelanime);
    this->unk_2F0 = this->unk_2F0;
    this->actionFunc(this, play);
}

s32 func_809A1D60(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, Actor* thisx) {
    Vec3f vec = D_809A1F98;
    return 0;
}

void func_809A1DA4(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, Actor* thisx) {
}

void func_809A1DBC(PlayState* play, s32 limbIndex, Actor* thisx) {
}

Gfx* func_809A1DD0(GraphicsContext* gfxCtx, u32 alpha) {
    Gfx* disp;
    Gfx* disp2;

    OPEN_DISPS(gfxCtx);

    // 2S2H [Port] Fixing allocation size
    disp = GRAPH_ALLOC(gfxCtx, 3 * sizeof(Gfx)); //! @bug this does not allocate enough for 3 Gfx commands;
    disp2 = disp;

    gDPSetRenderMode(disp++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2);

    gDPSetEnvColor(disp++, 0, 0, 0, alpha);

    gSPEndDisplayList(disp);

    CLOSE_DISPS(gfxCtx);
    return disp2;
}

Gfx* func_809A1E28(GraphicsContext* gfxCtx, u32 alpha) {
    Gfx* disp;
    Gfx* disp2;

    OPEN_DISPS(gfxCtx);

    disp = GRAPH_ALLOC(gfxCtx, 2 * sizeof(Gfx));
    disp2 = disp;

    gDPSetEnvColor(disp++, 0, 0, 0, alpha);

    gSPEndDisplayList(disp);

    CLOSE_DISPS(gfxCtx);

    return disp2;
}

void EnZl4_Draw(Actor* thisx, PlayState* play) {
    EnZl4* this = THIS;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL25_Opa(play->state.gfxCtx);

    if (this->unk_2F0 < 255) {
        gSPSegment(POLY_OPA_DISP++, 0x0C, func_809A1DD0(play->state.gfxCtx, this->unk_2F0));
    } else {
        gSPSegment(POLY_OPA_DISP++, 0x0C, func_809A1E28(play->state.gfxCtx, this->unk_2F0));
    }

    CLOSE_DISPS(play->state.gfxCtx);

    SkelAnime_DrawTransformFlexOpa(play, this->skelanime.skeleton, this->skelanime.jointTable,
                                   this->skelanime.dListCount, func_809A1D60, func_809A1DA4, func_809A1DBC,
                                   &this->actor);
}
