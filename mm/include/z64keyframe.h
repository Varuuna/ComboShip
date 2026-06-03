#ifndef Z64_KEYFRAME_H
#define Z64_KEYFRAME_H

#include "ultra64.h"
#include "z64math.h"

struct PlayState;

struct KFSkelAnimeFlex;
struct KFSkelAnime;

typedef s32 (*OverrideKeyframeDraw)(struct PlayState* play, struct KFSkelAnime* kfSkelAnime, s32 limbIndex,
                                    Gfx** dList, u8* flags, void* arg, Vec3s* rot, Vec3f* pos);
typedef s32 (*PostKeyframeDraw)(struct PlayState* play, struct KFSkelAnime* kfSkelAnime, s32 limbIndex,
                                Gfx** dList, u8* flags, void* arg, Vec3s* rot, Vec3f* pos);

typedef s32 (*OverrideKeyframeDrawScaled)(struct PlayState* play, struct KFSkelAnimeFlex* kfSkelAnime, s32 limbIndex,
                                          Gfx** dList, u8* flags, void* arg, Vec3f* scale, Vec3s* rot, Vec3f* pos);
typedef s32 (*PostKeyframeDrawScaled)(struct PlayState* play, struct KFSkelAnimeFlex* kfSkelAnime, s32 limbIndex,
                                      Gfx** dList, u8* flags, void* arg, Vec3f* scale, Vec3s* rot, Vec3f* pos);

typedef s32 (*UnkKeyframeCallback)(struct PlayState* play, struct KFSkelAnimeFlex* kfSkelAnime, s32 limbIndex,
                                   Gfx** dList, u8* flags, void* arg);

#define KEYFRAME_DRAW_OPA (0 << 0)
#define KEYFRAME_DRAW_XLU (1 << 0)

typedef enum {
    KEYFRAME_NOT_DONE,
    KEYFRAME_DONE_ONCE,
    KEYFRAME_DONE_LOOP
} KeyFrameDoneType;

#define KF_CALLBACK_INDEX_NONE 0xFF

// Keyframe limb?
typedef struct {
    /* 0x00 */ Gfx* dList;
    /* 0x04 */ u8 numChildren;
    /* 0x05 */ u8 flags;
    /* 0x06 */ Vec3s translation; // FlexLimbs have translation data in their animations instead
} KeyFrameStandardLimb; // size = 0xC

// Other limb type?
typedef struct {
    /* 0x00 */ Gfx* dList;
    /* 0x04 */ u8 numChildren;
    /* 0x05 */ u8 flags;
    /* 0x06 */ u8 callbackIndex; // transform callback index
} KeyFrameFlexLimb; // size = 0x8

typedef struct {
    /* 0x00 */ u8 limbCount;
    /* 0x01 */ u8 dListCount;   // non-zero in object files, number of non-null-dlist limbs? used to know how many matrices to alloc for drawing
    /* 0x04 */ union {
        KeyFrameStandardLimb* limbsStandard;
        KeyFrameFlexLimb* limbsFlex;
    };
} KeyFrameSkeleton; // Size = 0x8

typedef struct {
    /* 0x00 */ s16 frame;
    /* 0x02 */ s16 value;
    /* 0x04 */ s16 velocity;
} KeyFrame; // Size = 0x6

typedef struct {
    union {
    /* 0x00 */ u8* bitFlags;            // bitflags indicating whether to do keyframe interpolation or pull from preset values
    /* 0x00 */ u16* bitFlagsFlex;       // bitflags indicating whether to do keyframe interpolation or pull from preset values
    };
    /* 0x04 */ KeyFrame* keyFrames;     // keyframes array
    /* 0x08 */ s16* kfNums;             // number of keyframes for each limb
    /* 0x0C */ s16* presetValues;       // preset rotation (standard) or scale/rotation/translation (flex) values
    /* 0x0C */ char unk_10[0x2];
    /* 0x12 */ s16 duration;
} KeyFrameAnimation; // Size = 0x14

typedef enum {
    /* 0 */ KEYFRAME_ANIM_ONCE,
    /* 1 */ KEYFRAME_ANIM_LOOP
} FrameAnimMode;

typedef struct {
    /* 0x00 */ f32 start;
    /* 0x04 */ f32 end;
    /* 0x08 */ f32 duration;
    /* 0x0C */ f32 speed;
    /* 0x10 */ f32 curTime;
    /* 0x14 */ s32 animMode;
} FrameControl; // Size = 0x18

// Original name: ckf_Skeleton_Info_SV ?
typedef struct KFSkelAnimeFlex {
    /* 0x00 */ FrameControl frameCtrl;
    /* 0x18 */ KeyFrameSkeleton* skeleton;
    /* 0x1C */ KeyFrameAnimation* animation;
    /* 0x20 */ UnkKeyframeCallback* callbacks;  // pointer to array of functions, probably for transformlimbdraw purposes
    /* 0x24 */ f32 morphFrames;
    /* 0x28 */ Vec3s* jointTable; // Size 3 * limbCount
    /* 0x2C */ Vec3s* morphTable; // Size 3 * limbCount
} KFSkelAnimeFlex; // Size = 0x30

// Original name: ckf_Skeleton_Info ?
typedef struct KFSkelAnime {
    /* 0x00 */ FrameControl frameCtrl;
    /* 0x18 */ KeyFrameSkeleton* skeleton;
    /* 0x1C */ KeyFrameAnimation* animation;
    /* 0x20 */ f32 morphFrames;
    /* 0x24 */ Vec3s* jointTable;
    /* 0x28 */ Vec3s* morphTable;
    /* 0x2C */ Vec3s* rotOffsetsTable;
} KFSkelAnime; // Size = 0x30

void FrameCtrl_Reset(FrameControl* frameCtrl);
void FrameCtrl_Init(FrameControl* frameCtrl);
void FrameCtrl_SetProperties(FrameControl* frameCtrl, f32 startTime, f32 endTime, f32 duration, f32 t, f32 speed,
                             s32 animMode);
s32 FrameCtrl_PassCheck(FrameControl* frameCtrl, f32 t, f32* remainingTime);
s32 FrameCtrl_UpdateOnce(FrameControl* frameCtrl);
s32 FrameCtrl_UpdateLoop(FrameControl* frameCtrl);
s32 FrameCtrl_Update(FrameControl* frameCtrl);

void Keyframe_ResetFlex(KFSkelAnimeFlex* kfSkelAnime);
void Keyframe_InitFlex(KFSkelAnimeFlex* kfSkelAnime, KeyFrameSkeleton* skeleton, KeyFrameAnimation* animation,
                       Vec3s* jointTable, Vec3s* morphTable, UnkKeyframeCallback* callbacks);
void Keyframe_DestroyFlex(KFSkelAnimeFlex* kfSkelAnime);
void Keyframe_FlexPlayOnce(KFSkelAnimeFlex* kfSkelAnime, KeyFrameAnimation* animation);
void Keyframe_FlexPlayOnceWithSpeed(KFSkelAnimeFlex* kfSkelAnime, KeyFrameAnimation* animation, f32 speed);
void Keyframe_FlexPlayOnceWithMorph(KFSkelAnimeFlex* kfSkelAnime, KeyFrameAnimation* animation, f32 morphFrames);
void Keyframe_FlexPlayLoop(KFSkelAnimeFlex* kfSkelAnime, KeyFrameAnimation* animation);
void Keyframe_FlexPlayLoopWithSpeed(KFSkelAnimeFlex* kfSkelAnime, KeyFrameAnimation* animation, f32 speed);
void Keyframe_FlexPlayLoopWithMorph(KFSkelAnimeFlex* kfSkelAnime, KeyFrameAnimation* animation, f32 morphFrames);
void Keyframe_FlexChangeAnim(KFSkelAnimeFlex* kfSkelAnime, KeyFrameSkeleton* skeleton, KeyFrameAnimation* animation,
                             f32 startTime, f32 endTime, f32 t, f32 speed, f32 morphFrames, s32 animMode);
void Keyframe_FlexChangeAnimQuick(KFSkelAnimeFlex* kfSkelAnime, KeyFrameAnimation* animation);
f32 Keyframe_Interpolate(f32 t, f32 delta, f32 x0, f32 x1, f32 v0, f32 v1);
s16 Keyframe_KeyCalc(s16 kfStart, s16 kfNum, KeyFrame* keyFrames, f32 t);
void Keyframe_RotationInterpolate(f32 t, s16* out, s16 rot1, s16 rot2);
void Keyframe_MorphInterpolate(s16* jointData, s16* morphData, f32 t);
void Keyframe_UpdateFlexLimbs(KFSkelAnimeFlex* kfSkelAnime);
s32 Keyframe_UpdateFlex(KFSkelAnimeFlex* kfSkelAnime);
void Keyframe_DrawFlex(struct PlayState* play, KFSkelAnimeFlex* kfSkelAnime, Mtx* mtxStack,
                       OverrideKeyframeDrawScaled overrideKeyframeDraw, PostKeyframeDrawScaled postKeyframeDraw,
                       void* arg);

void Keyframe_ResetStandard(KFSkelAnime* kfSkelAnime);
void Keyframe_InitStandard(KFSkelAnime* kfSkelAnime, KeyFrameSkeleton* skeleton, KeyFrameAnimation* animation,
                           Vec3s* jointTable, Vec3s* morphTable);
void Keyframe_DestroyStandard(KFSkelAnime* kfSkelAnime);
void Keyframe_StandardPlayOnce(KFSkelAnime* kfSkelAnime, KeyFrameAnimation* animation, Vec3s* rotOffsetsTable);
void Keyframe_StandardPlayOnceWithSpeed(KFSkelAnime* kfSkelAnime, KeyFrameAnimation* animation, Vec3s* rotOffsetsTable,
                                        f32 speed);
void Keyframe_StandardPlayOnceWithMorph(KFSkelAnime* kfSkelAnime, KeyFrameAnimation* animation, Vec3s* rotOffsetsTable,
                                        f32 morphFrames);
void Keyframe_StandardPlayLoop(KFSkelAnime* kfSkelAnime, KeyFrameAnimation* animation, Vec3s* rotOffsetsTable);
void Keyframe_StandardPlayLoopWithSpeed(KFSkelAnime* kfSkelAnime, KeyFrameAnimation* animation, Vec3s* rotOffsetsTable,
                                        f32 speed);
void Keyframe_StandardPlayLoopWithMorph(KFSkelAnime* kfSkelAnime, KeyFrameAnimation* animation, Vec3s* rotOffsetsTable,
                                        f32 morphFrames);
void Keyframe_StandardChangeAnim(KFSkelAnime* kfSkelAnime, KeyFrameSkeleton* skeleton, KeyFrameAnimation* animation,
                                 f32 startTime, f32 endTime, f32 t, f32 speed, f32 morphFrames, s32 animMode,
                                 Vec3s* rotOffsetsTable);
void Keyframe_StandardChangeAnimQuick(KFSkelAnime* kfSkelAnime, KeyFrameAnimation* animation);
void Keyframe_UpdateStandardLimbs(KFSkelAnime* kfSkelAnime);
s32 Keyframe_UpdateStandard(KFSkelAnime* kfSkelAnime);
void Keyframe_DrawStandardLimb(struct PlayState* play, KFSkelAnime* kfSkelAnime, s32* limbIndex,
                               OverrideKeyframeDraw overrideKeyframeDraw, PostKeyframeDraw postKeyframeDraw, void* arg,
                               Mtx** mtxStack);
void Keyframe_DrawStandard(struct PlayState* play, KFSkelAnime* kfSkelAnime, Mtx* mtxStack,
                           OverrideKeyframeDraw overrideKeyframeDraw, PostKeyframeDraw postKeyframeDraw, void* arg);

void Keyframe_FlexGetScales(KFSkelAnime* kfSkelAnime, s32 targetLimbIndex, s16* scalesOut);

#endif
