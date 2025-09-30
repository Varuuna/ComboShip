#ifndef Z_EN_DT_H
#define Z_EN_DT_H

#include "global.h"

struct EnDt;

typedef void (*EnDtActionFunc)(struct EnDt*, PlayState*);

typedef struct EnDt {
    /* 0x000 */ Actor actor;
    /* 0x144 */ SkelAnime skelanime;
    /* 0x188 */ Vec3s unk_188[15];
    /* 0x1E2 */ Vec3s unk_1E2[15];
    /* 0x23C */ EnDtActionFunc actionFunc;
    /* 0x240 */ s32 unk_240;
    /* 0x244 */ s16 unk_244;
    /* 0x246 */ s16 unk_246;
    /* 0x248 */ s16 unk_248;
    /* 0x24A */ s16 unk_24A;
    /* 0x24C */ s16 unk_24C;
    /* 0x250 */ f32 unk_250;
    /* 0x254 */ s16 unk_254;
    /* 0x256 */ s16 unk_256;
    /* 0x258 */ s16 csIdList[11];
    /* 0x26E */ s16 unk_26E;
    /* 0x270 */ s16 unk_270;
    /* 0x274 */ struct EnMuto* unk_274;
    /* 0x278 */ struct EnBaisen* unk_278;
    /* 0x27C */ struct Actor* targetActor;
    /* 0x280 */ s32 unk_280;
    /* 0x284 */ Vec3s unk_284;
    /* 0x28A */ Vec3s unk_28A;
    /* 0x290 */ s32 unk_290;
    /* 0x294 */ ColliderCylinder collider;
} EnDt; // size = 0x2E0

#endif // Z_EN_DT_H
