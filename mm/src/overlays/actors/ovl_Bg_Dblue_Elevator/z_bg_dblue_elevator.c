/*
 * File: z_bg_dblue_elevator.c
 * Overlay: ovl_Bg_Dblue_Elevator
 * Description: Great Bay Temple - Elevator
 */

#include "z_bg_dblue_elevator.h"
#include "objects/object_dblue_object/object_dblue_object.h"

#define FLAGS (ACTOR_FLAG_10)

#define THIS ((BgDblueElevator*)thisx)

void BgDblueElevator_Init(Actor* thisx, PlayState* play);
void BgDblueElevator_Destroy(Actor* thisx, PlayState* play);
void BgDblueElevator_Update(Actor* thisx, PlayState* play);
void BgDblueElevator_Draw(Actor* thisx, PlayState* play);

void func_80B91F20(BgDblueElevator* this, PlayState* play);
void func_80B91F74(BgDblueElevator* this, PlayState* play);
s32 func_80B922C0(BgDblueElevator* this, PlayState* play);
s32 func_80B922FC(BgDblueElevator* this, PlayState* play);
void func_80B924DC(BgDblueElevator* this);
void func_80B924F8(BgDblueElevator* this, PlayState* play);
void func_80B9257C(BgDblueElevator* this);
void func_80B925B8(BgDblueElevator* this, PlayState* play);
void func_80B92644(BgDblueElevator* this);
void func_80B92660(BgDblueElevator* this, PlayState* play);

ActorInit Bg_Dblue_Elevator_InitVars = {
    /**/ ACTOR_BG_DBLUE_ELEVATOR,
    /**/ ACTORCAT_BG,
    /**/ FLAGS,
    /**/ OBJECT_DBLUE_OBJECT,
    /**/ sizeof(BgDblueElevator),
    /**/ BgDblueElevator_Init,
    /**/ BgDblueElevator_Destroy,
    /**/ BgDblueElevator_Update,
    /**/ BgDblueElevator_Draw,
};

static ElevatorProps D_80B92960[] = {
    {
        0,
        ((s32(*)(BgDblueElevator*, PlayState*))func_80B922C0),
        320.0f,
        0x1E,
        1,
        1.0f,
        0.1f,
        6.0f,
    },
    {
        1,
        ((s32(*)(BgDblueElevator*, PlayState*))func_80B922C0),
        195.0f,
        0x1E,
        1,
        1.0f,
        0.1f,
        5.0f,
    },
    {
        0,
        ((s32(*)(BgDblueElevator*, PlayState*))func_80B922FC),
        280.0f,
        0x1E,
        1,
        1.0f,
        0.1f,
        6.0f,
    },
    {
        0,
        ((s32(*)(BgDblueElevator*, PlayState*))func_80B922FC),
        280.0f,
        0x1E,
        -1,
        1.0f,
        0.1f,
        6.0f,
    },
};

static s16 D_80B929D0[4] = { -90, -90, 90, 90 };

static s16 D_80B929D8[4] = { -100, 90, 90, -100 };

static s8 D_80B929E0[3] = { 0, 2, 4 };

static s8 D_80B929E4[6] = { 0, 1, 2, 3, 4, 5 };

static InitChainEntry sInitChain[] = {
    ICHAIN_F32(uncullZoneForward, 4000, ICHAIN_CONTINUE),
    ICHAIN_F32(uncullZoneScale, 250, ICHAIN_CONTINUE),
    ICHAIN_F32(uncullZoneDownward, 250, ICHAIN_CONTINUE),
    ICHAIN_VEC3F_DIV1000(scale, 100, ICHAIN_STOP),
};

void func_80B91F20(BgDblueElevator* this, PlayState* play) {
    s32 pad;
    WaterBox* waterBox;
    s32 bgId;

    this->unk_16B = WaterBox_GetSurfaceImpl(play, &play->colCtx, this->dyna.actor.world.pos.x,
                                            this->dyna.actor.world.pos.z, &this->unk_16C, &waterBox, &bgId);
}

void func_80B91F74(BgDblueElevator* this, PlayState* play) {
    s32 i;
    Vec3f spB0;
    Vec3f spA4;
    s32 j;
    f32 temp1;
    f32 temp2;
    f32 temp_v0;
    f32 temp_v1;
    s32 pad;
    f32 var_fs0;

    Matrix_Push();
    Matrix_RotateYS(this->dyna.actor.shape.rot.y, MTXMODE_NEW);

    for (i = 0; i < 3; i++) {
        temp_v0 = D_80B929D0[i];
        temp_v1 = D_80B929D8[i];
        temp1 = D_80B929D0[i + 1] - D_80B929D0[i];
        temp2 = D_80B929D8[i + 1] - D_80B929D8[i];

        for (j = 0; j < 7; j++) {
            spB0.x = (j * temp1 * (1.0f / 7.0f)) + temp_v0;
            spB0.y = this->unk_16C;
            spB0.z = (j * temp2 * (1.0f / 7.0f)) + temp_v1;

            spB0.x += (Rand_ZeroOne() - 0.5f) * 20.0f;
            spB0.z += (Rand_ZeroOne() - 0.5f) * 20.0f;

            Matrix_MultVec3f(&spB0, &spA4);
            spA4.x += this->dyna.actor.world.pos.x;
            spA4.z += this->dyna.actor.world.pos.z;
            EffectSsGSplash_Spawn(play, &spA4, NULL, NULL, 0, (Rand_ZeroOne() * 400.0f) + 210.0f);
        }
    }

    for (i = 0; i < 3; i++) {
        spB0.x = ((Rand_ZeroOne() - 0.5f) * 60.0f) + this->dyna.actor.world.pos.x;
        spB0.y = this->unk_16C;
        spB0.z = ((Rand_ZeroOne() - 0.5f) * 60.0f) + this->dyna.actor.world.pos.z;
        EffectSsGRipple_Spawn(play, &spB0, 1000, 3000, D_80B929E0[i]);
    }

    for (i = 0; i < 6; i++) {
        var_fs0 = Rand_ZeroOne();
        var_fs0 = 1.0f - (var_fs0 * var_fs0);
        if ((s32)Rand_Next() > 0) {
            var_fs0 = -var_fs0;
        }
        spB0.x = (var_fs0 * 100.0f) + this->dyna.actor.world.pos.x;
        spB0.y = this->unk_16C;

        var_fs0 = Rand_ZeroOne();
        var_fs0 = 1.0f - (var_fs0 * var_fs0);

        if ((s32)Rand_Next() > 0) {
            var_fs0 = -var_fs0;
        }
        spB0.z = (var_fs0 * 100.0f) + this->dyna.actor.world.pos.z;
        EffectSsGRipple_Spawn(play, &spB0, 400, 800, D_80B929E4[i]);
    }

    Matrix_Pop();
}

s32 func_80B922C0(BgDblueElevator* this, PlayState* play) {
    s32 params = this->dyna.actor.params & 0x7F;

    if (Flags_GetSwitch(play, params) != 0) {
        return false;
    } else {
        return true;
    }
}

s32 func_80B922FC(BgDblueElevator* this, PlayState* play) {
    s32 ret = 0;

    if (Flags_GetSwitch(play, this->dyna.actor.params & 0x7F) == 0) {
        ret = 1;
    }
    if ((Flags_GetSwitch(play, (this->dyna.actor.params + 1) & 0x7F) != 0) &&
        (Flags_GetSwitch(play, (this->dyna.actor.params + 2) & 0x7F) != 0) &&
        (Flags_GetSwitch(play, (this->dyna.actor.params + 3) & 0x7F) != 0)) {
        ret += 2;
    }
    return ret;
}

void BgDblueElevator_Init(Actor* thisx, PlayState* play) {
    BgDblueElevator* this = THIS;
    s32 pad;
    s32 params = (this->dyna.actor.params >> 8) & 3;
    ElevatorProps* elevator = &D_80B92960[params];
    s32 temp_v0;

    Actor_ProcessInitChain(&this->dyna.actor, sInitChain);
    DynaPolyActor_Init(&this->dyna, 1);
    DynaPolyActor_LoadMesh(play, &this->dyna, &gGreatBayTempleObjectElevatorCol);

    temp_v0 = elevator->actionFunc(this, play);

    if (temp_v0 == 2) {
        this->unk_168 = -elevator->unk_0D;
    } else {
        this->unk_168 = elevator->unk_0D;
    }

    func_80B91F20(this, play);

    if ((temp_v0 == 0) || (temp_v0 == 3)) {
        func_80B924DC(this);
    } else {
        func_80B92644(this);
    }
}

void BgDblueElevator_Destroy(Actor* thisx, PlayState* play) {
    BgDblueElevator* this = THIS;
    DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, this->dyna.bgId);
}

void func_80B924DC(BgDblueElevator* this) {
    this->unk_169 = 60;
    this->actionFunc = func_80B924F8;
}

void func_80B924F8(BgDblueElevator* this, PlayState* play) {
    s32 params = (this->dyna.actor.params >> 8) & 3;
    ElevatorProps* elevator = &D_80B92960[params];
    s32 temp_v0 = elevator->actionFunc(this, play);

    if ((temp_v0 == 0) || (temp_v0 == 3)) {
        this->unk_169 = 60;
        return;
    }

    this->unk_169--;
    if (this->unk_169 <= 0) {
        func_80B92644(this);
    }
}

void func_80B9257C(BgDblueElevator* this) {
    s32 params = (this->dyna.actor.params >> 8) & 3;
    ElevatorProps* elevator = &D_80B92960[params];

    this->unk_16A = elevator->unk_0C;
    this->actionFunc = func_80B925B8;
}

void func_80B925B8(BgDblueElevator* this, PlayState* play) {
    s32 params = (this->dyna.actor.params >> 8) & 3;
    ElevatorProps* elevator = &D_80B92960[params];
    s32 temp_v0 = elevator->actionFunc(this, play);

    if ((temp_v0 == 0) || (temp_v0 == 3)) {
        func_80B924DC(this);
        return;
    }

    this->unk_16A -= 1;
    if (this->unk_16A <= 0) {
        func_80B92644(this);
    }
}

void func_80B92644(BgDblueElevator* this) {
    this->unk_160 = 0.0f;
    this->actionFunc = func_80B92660;
}

void func_80B92660(BgDblueElevator* this, PlayState* play) {
    s32 params = (this->dyna.actor.params >> 8) & 3;
    ElevatorProps* elevator = &D_80B92960[params];
    s32 temp_v0 = elevator->actionFunc(this, play);

    s32 pad;
    s32 sp5C;
    s32 sp58;
    f32 var_fa0;
    Vec3f sp48;
    Vec3f sp3C;
    f32 var_fv1;
    s32 pad2;

    if ((temp_v0 == 0) || (temp_v0 == 3)) {
        sp58 = Math_StepToF(&this->unk_160, 0.0f, elevator->unk_14);
    } else {
        sp58 = 0;
        Math_StepToF(&this->unk_160, elevator->unk_18, elevator->unk_10);
    }

    if (this->unk_168 > 0) {
        var_fa0 = elevator->unk_08;
    } else {
        var_fa0 = -elevator->unk_08;
    }

    if (this->unk_160 <= 1.1f) {
        var_fv1 = 1.1f;
    } else {
        var_fv1 = this->unk_160;
    }

    if (Math_SmoothStepToF(&this->unk_164, var_fa0, 0.4f, var_fv1, 1.0f) < 0.001f) {
        sp5C = 1;
    } else {
        sp5C = 0;
    }

    if (elevator->unk_00 == 0) {
        this->dyna.actor.world.pos.y = this->unk_164 + this->dyna.actor.home.pos.y;
        if (((this->dyna.actor.flags & 0x40) == 0x40) && (this->unk_16B != 0)) {
            if (this->unk_168 > 0) {
                var_fv1 = ((this->dyna.actor.world.pos.y + -10.0f) - this->unk_16C) *
                          (((&this->dyna.actor.prevPos)->y + -10.0f) - this->unk_16C);
            } else {
                var_fv1 = ((this->dyna.actor.world.pos.y + -30.0f) - this->unk_16C) *
                          ((this->dyna.actor.prevPos.y + -30.0f) - this->unk_16C);
            }
            if (var_fv1 <= 0.0f) {
                func_80B91F74(this, play);
            }
        }
    } else {
        Matrix_RotateYS(this->dyna.actor.shape.rot.y, MTXMODE_NEW);
        sp48.x = this->unk_164;
        sp48.z = sp48.y = 0.0f;
        Matrix_MultVec3f(&sp48, &sp3C);
        Math_Vec3f_Sum(&this->dyna.actor.home.pos, &sp3C, &this->dyna.actor.world.pos);
    }

    if (sp58 != 0) {
        func_80B924DC(this);
        return;
    }

    if (sp5C != 0) {
        this->unk_168 = -this->unk_168;
        func_80B9257C(this);
    }
}

void BgDblueElevator_Update(Actor* thisx, PlayState* play) {
    BgDblueElevator* this = THIS;
    this->actionFunc(this, play);
}

void BgDblueElevator_Draw(Actor* thisx, PlayState* play) {
    BgDblueElevator* this = THIS;
    Gfx_DrawDListOpa(play, gGreatBayTempleObjectElevatorDL);
}
