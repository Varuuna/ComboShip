/* combo/menu/ComboItemDrawABI.h — ComboShip: cross-game item draw info (C ABI, POD only).
 * The owning game returns its sDrawItemTable data for one item so the OTHER game can render
 * the model via "@<game>:"-routed resource paths. Strings point into the owning game's static
 * storage (path literals from asset headers — process-lifetime). */
#ifndef COMBO_ITEM_DRAW_ABI_H
#define COMBO_ITEM_DRAW_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CW_DRAW_MAX_DLISTS 8

typedef struct {
    const char* dlists[CW_DRAW_MAX_DLISTS]; /* OTR path strings, in SUBMISSION order */
    int32_t dlistCount;
    int32_t xluStartIndex; /* dlists[0..xluStart-1] are OPA layers, rest XLU; -1 = all OPA */
    float scale;           /* extra model scale; 0 = none (e.g. MM boss remains: 0.02f) */
    int32_t hasEnvColor;   /* 1 = emit envColor before the DLs (e.g. MM song notes) */
    uint8_t envColor[4];   /* RGBA */
    int32_t xluSeg8TexScroll; /* 1 = bind segment 8 to the animated flame texscroll before the XLU
                                 layer (skulltula token flame); consumer replicates the owning game's
                                 Gfx_TwoTexScrollEx so the dropped animated layer renders again */
} CwItemDrawInfo;

/* Returns 1 and fills out on success; 0 if the item is unknown/undrawable.
 * itemName is in the owning game's item namespace (MM: RI_* spoilerName; OOT: English name). */
typedef int32_t (*Fn_GetItemDrawInfo)(const char* itemName, CwItemDrawInfo* out);

/* ComboShip: animated variant — the owning game describes a skeletal/animated item (skeleton,
 * animation, texture-animation paths); the host's combo-owned code (combo/menu/ComboForeignAnim.h)
 * loads the resources via the owning game's ResourceManager (CrossRMRegistry) and drives the host's
 * own SkelAnime engine. First served: MM stray fairies. Paths are static literals (process lifetime). */
typedef struct {
    const char* skelPath;    /* FlexSkeleton resource (OTR path) */
    const char* animPath;    /* Animation resource */
    const char* texAnimPath; /* AnimatedMaterial resource, or NULL */
    float scale;             /* model scale (stray fairy: 0.03f) */
    int32_t billboard;       /* 1 = face camera (Matrix_ReplaceRotation on billboard mtx) */
    int32_t xlu;             /* 1 = draw on XLU layer with 25Xlu setup */
    int32_t limbCount;       /* skeleton limb count (jointTable sizing) */
    int32_t hiddenLimb;      /* limb index to null out (stray fairy: right-facing head), -1 none */
} CwItemAnimDrawInfo;
typedef int32_t (*Fn_GetItemAnimDrawInfo)(const char* itemName, CwItemAnimDrawInfo* out);

#ifdef __cplusplus
}
#endif
#endif /* COMBO_ITEM_DRAW_ABI_H */
