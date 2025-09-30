#ifndef Z_BG_DBLUE_ELEVATOR_H
#define Z_BG_DBLUE_ELEVATOR_H

#include "global.h"

struct BgDblueElevator;

typedef void (*BgDblueElevatorActionFunc)(struct BgDblueElevator*, PlayState*);

typedef struct BgDblueElevator {
    /* 0x000 */ DynaPolyActor dyna;
    /* 0x15C */ BgDblueElevatorActionFunc actionFunc;
    /* 0x160 */ f32 unk_160;
    /* 0x164 */ f32 unk_164;
    /* 0x168 */ s8 unk_168;
    /* 0x169 */ s8 unk_169;
    /* 0x16A */ s8 unk_16A;
    /* 0x16B */ s8 unk_16B;
    /* 0x16C */ f32 unk_16C;
} BgDblueElevator; // size = 0x170

typedef s32 (*BgDblueElevatorPropsFunc)(struct BgDblueElevator*, PlayState*);

typedef struct ElevatorProps {
    /* 0x0 */ s32 unk_00;
    /* 0x4 */ BgDblueElevatorPropsFunc actionFunc;
    /* 0x8 */ f32 unk_08;
    /* 0xC */ s8 unk_0C;
    /* 0xD */ s8 unk_0D;
    /* 0x10 */ f32 unk_10;
    /* 0x14 */ f32 unk_14;
    /* 0x18 */ f32 unk_18;
} ElevatorProps; // size = 0x1C

#endif // Z_BG_DBLUE_ELEVATOR_H
