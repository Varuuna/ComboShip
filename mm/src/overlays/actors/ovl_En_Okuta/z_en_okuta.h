#ifndef Z_EN_OKUTA_H
#define Z_EN_OKUTA_H

#include "global.h"

struct EnOkuta;

typedef void (*EnOkutaActionFunc)(struct EnOkuta*, PlayState*);

typedef struct EnOkuta {
    /* 0x000 */ Actor actor;
    /* 0x144 */ SkelAnime skelAnime;
    /* 0x188 */ EnOkutaActionFunc actionFunc;
    /* 0x18C */ u8 unk_18C;
    /* 0x18D */ char pad_18D;
    /* 0x18E */ s16 unk_18E;
    /* 0x190 */ s16 unk_190;
    /* 0x192 */ Vec3s jointTable[16];
    /* 0x1F2 */ Vec3s morphTable[16];
    /* 0x252 */ char pad_252[0x2];
    /* 0x254 */ f32 unk_254;
    /* 0x258 */ f32 unk_258;
    /* 0x25C */ f32 unk_25C;
    /* 0x260 */ f32 unk_260;
    /* 0x264 */ Vec3f headScale;
    /* 0x270 */ Vec3f bodyPartsPos[10];
    /* 0x2E8 */ ColliderCylinder collider;
} EnOkuta; // size = 0x334

#endif // Z_EN_OKUTA_H
