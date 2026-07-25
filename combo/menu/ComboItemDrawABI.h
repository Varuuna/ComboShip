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

/* ComboShip: which OOT get-item draw func the consumer must replicate. SIMPLE (0) = the plain
 * OPA-then-XLU submission the consumer already does (self-contained funcs + rupees/wallets/etc). The
 * rest are the non-portable OOT funcs (segment-8/9 texture scrolls, billboard rotation, per-instance
 * prim/env color) that draw as the sentinel today; each has a 1:1 handler in ComboForeignDrawMM.h
 * that re-binds the segment(s) in the host frame before submitting the routed @oot: DLs. When drawKind
 * != SIMPLE the consumer ignores xluStartIndex and dlists[] are carried in the OOT TABLE order (the
 * handler reorders exactly as the OOT func does). */
typedef enum {
    CW_DRAW_KIND_SIMPLE = 0,
    CW_DRAW_KIND_GORON_SWORD,    /* Biggoron/Broken sword: seg8 OPA scroll, dl0 */
    CW_DRAW_KIND_DEKU_NUTS,      /* seg8 OPA scroll, dl0 */
    CW_DRAW_KIND_RECOVERY_HEART, /* seg8 XLU scroll, dl0 */
    CW_DRAW_KIND_FISH,           /* seg8 XLU scroll, dl0 */
    CW_DRAW_KIND_POTION,         /* seg8 OPA scroll, OPA dl[1,0,2,3] + XLU dl[4,5] */
    CW_DRAW_KIND_MIRROR_SHIELD,  /* seg8 OPA scroll, OPA dl0 + XLU dl1 */
    CW_DRAW_KIND_BLUE_FIRE,      /* OPA dl0; XLU seg8 scroll + billboard flame dl1 */
    CW_DRAW_KIND_POES,           /* OPA dl0; XLU dl1; seg8 scroll; billboard dl3,dl2 */
    CW_DRAW_KIND_FAIRY,          /* OPA dl0; XLU dl1; seg8 scroll; billboard dl2 */
    CW_DRAW_KIND_JEWEL,          /* spiritual stones: seg9 XLU + seg8 OPA + rotate + per-layer colors */
    CW_DRAW_KIND_MAGIC_SPELL,    /* Din/Farore/Nayru: XLU seg8 scroll, dl0,1,2 */
    CW_DRAW_KIND_SCALE,          /* silver/gold scale: XLU seg8 scroll, dl2,3,1,0 */
    CW_DRAW_KIND_SKULL_TOKEN,    /* body OPA dl0 + XLU seg8 flame dl1 (full, flame included) */
    CW_DRAW_KIND_MUSIC_NOTE,     /* generic rando song note: grayscale tint (primColorXlu), dl0 */
    CW_DRAW_KIND_BOSS_SOUL, /* OOT boss soul: seg8 flame scroll (grayscale primColorXlu) dl0 + skull dl1 (envColorXlu)
                             */
    /* Triforce Piece / Fishing Pole are plain scaled-OPA draws with no extra GPU state, so they ride
     * the SIMPLE path (dlists[0] + scale) rather than a dedicated kind. */

    /* ComboShip: the bespoke Randomizer_Draw* funcs (Item::SetCustomDrawFunc), which the gid-keyed
     * sDrawItemTable is blind to. Appended so the values above keep their ABI numbers. */
    CW_DRAW_KIND_COLOR_LAYERS,   /* per-DL prim/env color then dlists[i]; xluStartIndex splits OPA/XLU */
    CW_DRAW_KIND_GRAYSCALE_XLU,  /* XLU: grayscale tint (primColorXlu) around dl0 */
    CW_DRAW_KIND_DOUBLE_DEFENSE, /* XLU: grayscale-white heart border dl0, then plain container dl1 */
    CW_DRAW_KIND_MASTER_SWORD,   /* seg8 OPA scroll + scale 0.05 + rotate Z 2.1, dl0 */
    CW_DRAW_KIND_BRONZE_SCALE,   /* XLU seg8 scroll + constant prim/env pairs around scale dl0 / water dl1 */
} CwDrawKind;

typedef struct {
    const char* dlists[CW_DRAW_MAX_DLISTS]; /* OTR path strings, in SUBMISSION order */
    int32_t dlistCount;
    int32_t xluStartIndex;    /* dlists[0..xluStart-1] are OPA layers, rest XLU; -1 = all OPA */
    float scale;              /* extra model scale; 0 = none (e.g. MM boss remains: 0.02f) */
    int32_t hasEnvColor;      /* 1 = emit envColor before the DLs (e.g. MM song notes) */
    uint8_t envColor[4];      /* RGBA */
    int32_t xluSeg8TexScroll; /* 1 = bind segment 8 to the animated flame texscroll before the XLU
                                 layer (skulltula token flame); consumer replicates the owning game's
                                 Gfx_TwoTexScrollEx so the dropped animated layer renders again */
    /* Resource-driven animated material (generalizes xluSeg8TexScroll for items whose animation lives
     * in a TextureAnimation resource, e.g. MM's Moon's Tear). If matAnimPath != NULL the consumer
     * loads it from the owning game's RM and binds the animated segment before the DLs (see
     * ComboForeignTexAnim_Run). matAnimPath is the owning game's own "__OTR__..." path (unrouted). */
    const char* matAnimPath;  /* TextureAnimation resource, or NULL */
    int32_t matAnimBindOpa;   /* 1 = also bind the animated segment on the OPA layer (item body samples it) */
    int32_t matAnimBillboard; /* 1 = Matrix_ReplaceRotation(billboardMtxF) before the XLU layer (glow) */

    /* ComboShip: kind-tagged draw recipe for the non-portable OOT funcs (OOT->MM foreign-draw
     * portability). drawKind selects the consumer handler; when != CW_DRAW_KIND_SIMPLE, dlists[] are
     * the raw OOT table row (submission order handled by the handler) and xluStartIndex is ignored.
     * The scroll/matrix params are constant per kind and embedded in the consumer handler (1:1 port of
     * the OOT func); only the JEWEL per-layer colors and the MUSIC_NOTE tint vary, so they are carried
     * here as data. Colors are RGBA (alpha always 255). */
    int32_t drawKind;        /* CwDrawKind */
    uint8_t primColorXlu[4]; /* JEWEL gem (XLU) prim; MUSIC_NOTE grayscale tint */
    uint8_t envColorXlu[4];  /* JEWEL gem (XLU) env */
    uint8_t primColorOpa[4]; /* JEWEL setting (OPA) prim */
    uint8_t envColorOpa[4];  /* JEWEL setting (OPA) env */

    /* ComboShip: 1 = this recipe depends on live save state and must NOT be cached — the consumer
     * re-queries it every frame. Set by producers for items whose model is CHOSEN at draw time rather
     * than fixed: progressive tiers (which sword/quiver you're owed), Triforce shards
     * (collected % 3, and the completed model), and junk/trap indirection. Consumers cache recipes per
     * check per save slot, so without this the first model drawn is frozen for the whole slot. */
    int32_t stateDependent;

    /* ComboShip: CW_DRAW_KIND_COLOR_LAYERS payload — the per-DL prim/env colors the rando key/map/
     * compass/jabber-nut funcs set before each display list. Bit i of each mask = dlists[i] sets it. */
    uint8_t layerPrimColor[CW_DRAW_MAX_DLISTS][4];
    uint8_t layerEnvColor[CW_DRAW_MAX_DLISTS][4];
    int32_t layerPrimMask;
    int32_t layerEnvMask;
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
