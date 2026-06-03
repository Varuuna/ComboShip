#ifndef OBJECTS_OBJECT_CNE_H
#define OBJECTS_OBJECT_CNE_H 1

#include "align_asset_macro.h"

#define dgCneSkel "__OTR__objects/object_cne/gCneSkel"
static const ALIGN_ASSET(2) char gCneSkel[] = dgCneSkel;

#define dgCneTLUT "__OTR__objects/object_cne/gCneTLUT"
static const ALIGN_ASSET(2) char gCneTLUT[] = dgCneTLUT;

#define dgCneSkinTex "__OTR__objects/object_cne/gCneSkinTex"
static const ALIGN_ASSET(2) char gCneSkinTex[] = dgCneSkinTex;

#define dgCneBrownHairTex "__OTR__objects/object_cne/gCneBrownHairTex"
static const ALIGN_ASSET(2) char gCneBrownHairTex[] = dgCneBrownHairTex;

#define dgCneHandTex "__OTR__objects/object_cne/gCneHandTex"
static const ALIGN_ASSET(2) char gCneHandTex[] = dgCneHandTex;

#define dgCneBrownHairSkinTex "__OTR__objects/object_cne/gCneBrownHairSkinTex"
static const ALIGN_ASSET(2) char gCneBrownHairSkinTex[] = dgCneBrownHairSkinTex;

#define dgCneBrownHairFaceTex "__OTR__objects/object_cne/gCneBrownHairFaceTex"
static const ALIGN_ASSET(2) char gCneBrownHairFaceTex[] = dgCneBrownHairFaceTex;

#define dgCneDressTex "__OTR__objects/object_cne/gCneDressTex"
static const ALIGN_ASSET(2) char gCneDressTex[] = dgCneDressTex;

#define dgCneDressNeckTex "__OTR__objects/object_cne/gCneDressNeckTex"
static const ALIGN_ASSET(2) char gCneDressNeckTex[] = dgCneDressNeckTex;

#define dgCneHeadBrownHairDL "__OTR__objects/object_cne/gCneHeadBrownHairDL"
static const ALIGN_ASSET(2) char gCneHeadBrownHairDL[] = dgCneHeadBrownHairDL;

#define dgCneRightHandDL "__OTR__objects/object_cne/gCneRightHandDL"
static const ALIGN_ASSET(2) char gCneRightHandDL[] = dgCneRightHandDL;

#define dgCneRightForearmDL "__OTR__objects/object_cne/gCneRightForearmDL"
static const ALIGN_ASSET(2) char gCneRightForearmDL[] = dgCneRightForearmDL;

#define dgCneRightUpperArmDL "__OTR__objects/object_cne/gCneRightUpperArmDL"
static const ALIGN_ASSET(2) char gCneRightUpperArmDL[] = dgCneRightUpperArmDL;

#define dgCneLeftHandDL "__OTR__objects/object_cne/gCneLeftHandDL"
static const ALIGN_ASSET(2) char gCneLeftHandDL[] = dgCneLeftHandDL;

#define dgCneLeftForearmDL "__OTR__objects/object_cne/gCneLeftForearmDL"
static const ALIGN_ASSET(2) char gCneLeftForearmDL[] = dgCneLeftForearmDL;

#define dgCneLeftUpperArmDL "__OTR__objects/object_cne/gCneLeftUpperArmDL"
static const ALIGN_ASSET(2) char gCneLeftUpperArmDL[] = dgCneLeftUpperArmDL;

#define dgCneTorsoDL "__OTR__objects/object_cne/gCneTorsoDL"
static const ALIGN_ASSET(2) char gCneTorsoDL[] = dgCneTorsoDL;

#define dgCneRightFootDL "__OTR__objects/object_cne/gCneRightFootDL"
static const ALIGN_ASSET(2) char gCneRightFootDL[] = dgCneRightFootDL;

#define dgCneRightShinDL "__OTR__objects/object_cne/gCneRightShinDL"
static const ALIGN_ASSET(2) char gCneRightShinDL[] = dgCneRightShinDL;

#define dgCneRightThighDL "__OTR__objects/object_cne/gCneRightThighDL"
static const ALIGN_ASSET(2) char gCneRightThighDL[] = dgCneRightThighDL;

#define dgCneLeftFootDL "__OTR__objects/object_cne/gCneLeftFootDL"
static const ALIGN_ASSET(2) char gCneLeftFootDL[] = dgCneLeftFootDL;

#define dgCneLeftShinDL "__OTR__objects/object_cne/gCneLeftShinDL"
static const ALIGN_ASSET(2) char gCneLeftShinDL[] = dgCneLeftShinDL;

#define dgCneLeftThighDL "__OTR__objects/object_cne/gCneLeftThighDL"
static const ALIGN_ASSET(2) char gCneLeftThighDL[] = dgCneLeftThighDL;

#define dgCnePelvisDL "__OTR__objects/object_cne/gCnePelvisDL"
static const ALIGN_ASSET(2) char gCnePelvisDL[] = dgCnePelvisDL;

#define dgCneOrangeHairFaceTex "__OTR__objects/object_cne/gCneOrangeHairFaceTex"
static const ALIGN_ASSET(2) char gCneOrangeHairFaceTex[] = dgCneOrangeHairFaceTex;

#define dgCneOrangeHairSkinTex "__OTR__objects/object_cne/gCneOrangeHairSkinTex"
static const ALIGN_ASSET(2) char gCneOrangeHairSkinTex[] = dgCneOrangeHairSkinTex;

#define dgCneHeadOrangeHairDL "__OTR__objects/object_cne/gCneHeadOrangeHairDL"
static const ALIGN_ASSET(2) char gCneHeadOrangeHairDL[] = dgCneHeadOrangeHairDL;

typedef enum CneLimb {
    /* 0x00 */ CNE_LIMB_NONE,
    /* 0x01 */ CNE_LIMB_PELVIS,
    /* 0x02 */ CNE_LIMB_LEFT_THIGH,
    /* 0x03 */ CNE_LIMB_LEFT_SHIN,
    /* 0x04 */ CNE_LIMB_LEFT_FOOT,
    /* 0x05 */ CNE_LIMB_RIGHT_THIGH,
    /* 0x06 */ CNE_LIMB_RIGHT_SHIN,
    /* 0x07 */ CNE_LIMB_RIGHT_FOOT,
    /* 0x08 */ CNE_LIMB_TORSO,
    /* 0x09 */ CNE_LIMB_LEFT_UPPER_ARM,
    /* 0x0A */ CNE_LIMB_LEFT_FOREARM,
    /* 0x0B */ CNE_LIMB_LEFT_HAND,
    /* 0x0C */ CNE_LIMB_RIGHT_UPPER_ARM,
    /* 0x0D */ CNE_LIMB_RIGHT_FOREARM,
    /* 0x0E */ CNE_LIMB_RIGHT_HAND,
    /* 0x0F */ CNE_LIMB_HEAD,
    /* 0x10 */ CNE_LIMB_MAX
} CneLimb;

#endif // OBJECTS_OBJECT_CNE_H
