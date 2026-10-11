/* combo/menu/ComboForeignAnim.h — ComboShip: animated cross-game item rendering, host side.
 * The FOREIGN game (MM) only DESCRIBES an animated item via CwItemAnimDrawInfo
 * (ComboItemDrawABI.h: skeleton/animation/texanim resource paths + draw parameters); this header
 * loads those resources through the OWNING game's resident ResourceManager (CrossRMRegistry) and
 * drives the HOST game's own SkelAnime engine on them. Feasible because the games' skeleton and
 * animation structs are byte-identical (SkelAnime 0x44, StandardLimb, FlexSkeletonHeader,
 * AnimationHeader). Bidirectional: the host is whichever game includes this header (see the
 * COMBO_FOREIGN_ANIM_HOST_MM shim below) and `game` names the OWNING game, so limb DLs route to
 * "__OTR__@<game>:" in either direction. Classes served: MM stray fairies + enemy souls + minifrogs
 * in OOT, and OOT boss souls in MM.
 *
 * Also ports the minimal AnimatedMaterial subset the stray-fairy texanims need. OOT has no
 * AnimatedMat system; MM's lives in mm/src/code/z_scene_proc.c. All five gStrayFairy*TexAnim
 * resources were inspected (mm.o2r): 2 entries each, BOTH type 4 (ColorChangeLagrange, prim+env
 * color, segments |1|+7=8 and |-2|+7=9, negative segment = end-of-list). Ported handlers: type 4
 * (AnimatedMat_DrawColorNonLinearInterp + Scene_LagrangeInterp + AnimatedMat_SetColor), type 1
 * (DualScroll) and type 2 (AnimatedMat_DrawColor, frame-indexed color — Eyegore's eye laser);
 * any other entry type fails validation and falls back to the caller's sentinel.
 *
 * TU-GLUE HEADER (menu-extraction pattern, like combo/menu/ComboMenuDrawContent.h): include from
 * the HOST game's draw TU (soh/soh/Enhancements/randomizer/draw.cpp) AFTER the engine headers —
 * z64.h / macros.h (OPEN_DISPS, SkelAnime, PlayState, gbi macros) and functions.h
 * (SkelAnime_InitFlex/Update/DrawFlex, Matrix_*, Graph_Alloc, Gfx_SetupDL_25Xlu) must already be
 * in scope. Not standalone. Lives in combo/menu/ because that directory is already on soh's
 * include path (zero CMake churn); it is render glue, not menu glue.
 */
#ifndef COMBO_FOREIGN_ANIM_H
#define COMBO_FOREIGN_ANIM_H

#ifndef OPEN_DISPS
#error "ComboForeignAnim.h is TU-glue: include the host engine headers (z64.h/macros.h/functions.h) before it"
#endif

// ---- Host adaptation. The two engines expose the same primitives under different spellings, so the
// including TU declares which one it is: define COMBO_FOREIGN_ANIM_HOST_MM before the include when
// 2ship.dll is the host (soh.dll is the default). Everything else in this header is host-agnostic.
#ifdef COMBO_FOREIGN_ANIM_HOST_MM
#define CFA_SETUP_OPA(gfxCtx) Gfx_SetupDL25_Opa(gfxCtx)
#define CFA_SETUP_XLU(gfxCtx) Gfx_SetupDL25_Xlu(gfxCtx)
#define CFA_ALLOC(gfxCtx, size) GRAPH_ALLOC(gfxCtx, size)
#define CFA_LOAD_MTX(pkt, gfxCtx) MATRIX_FINALIZE_AND_LOAD(pkt, gfxCtx)
#define CFA_LIMB_ARG Actor*
#else
#define CFA_SETUP_OPA(gfxCtx) Gfx_SetupDL_25Opa(gfxCtx)
#define CFA_SETUP_XLU(gfxCtx) Gfx_SetupDL_25Xlu(gfxCtx)
#define CFA_ALLOC(gfxCtx, size) Graph_Alloc(gfxCtx, size)
#define CFA_LOAD_MTX(pkt, gfxCtx) \
    gSPMatrix(pkt, Matrix_NewMtx(gfxCtx, (char*)__FILE__, __LINE__), G_MTX_MODELVIEW | G_MTX_LOAD)
#define CFA_LIMB_ARG void*
#endif

#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/ResourceManagerScope.h>
#include <ship/resource/CrossRMRegistry.h>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ComboItemDrawABI.h"

// OPEN_DISPS embeds block-scope decls of these; C linkage only survives at global scope, so this
// header is namespace-free and pre-declares them extern "C". See docs/deviations/rando.md.
extern "C" {
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);
}

// ---- Local mirrors of MM's loaded TextureAnimation layout (mm/2s2h/resource/type/
// TextureAnimation.h: F3DPrimColor / F3DEnvColor / AnimatedMatColorParams / AnimatedMaterial).
// Pure POD; identical member types => identical MSVC layout as the structs MM's
// TextureAnimationFactory populates, so the loaded resource can be read through these directly.
struct CfaPrimColor {
    uint8_t r, g, b, a, lodFrac;
};
struct CfaEnvColor {
    uint8_t r, g, b, a;
};
struct CfaColorParams { // AnimatedMatColorParams
    uint16_t keyFrameLength;
    uint16_t keyFrameCount;
    CfaPrimColor* primColors;
    CfaEnvColor* envColors;
    uint16_t* keyFrames;
};
struct CfaMatEntry { // AnimatedMaterial: array terminated by a NEGATIVE segment on the last entry
    int8_t segment;  // |segment| + 7 = real segment id
    int16_t type;    // TextureAnimationParamsType; only 1 (DualScroll), 2 (ColorChange), 4 (Lagrange) ported
    void* params;
};
struct CfaTexScrollEntry { // AnimatedMatTexScrollParams (DualScroll params: two of these)
    int8_t xStep, yStep;
    uint8_t width, height;
};
constexpr int16_t kMatTypeTwoTexScroll = 1;
constexpr int16_t kMatTypeColorChange = 2;
constexpr int16_t kMatTypeColorLagrange = 4;
constexpr int32_t kMaxMatEntries = 8; // sanity bound when walking the entry array
constexpr int32_t kMaxKeyFrames = 50; // MM's handler uses fixed f32[50] tables — same bound

// ---- Ported handler subset (z_scene_proc.c:145-363, types 1/2/4), parameterized on gfxCtx+step
// instead of MM's sMatAnim* globals; alphaRatio 1, XLU-only by design.

inline float CfaLagrangeInterp(int32_t n, const float x[], const float fx[], float xp) {
    float weights[kMaxKeyFrames];
    float intp = 0.0f;
    for (int32_t i = 0; i < n; i++) {
        float m = 1.0f;
        for (int32_t j = 0; j < n; j++) {
            if (j != i) {
                m *= x[i] - x[j];
            }
        }
        weights[i] = fx[i] / m;
    }
    for (int32_t i = 0; i < n; i++) {
        float m = 1.0f;
        for (int32_t j = 0; j < n; j++) {
            if (j != i) {
                m *= xp - x[j];
            }
        }
        intp += weights[i] * m;
    }
    return intp;
}

inline uint8_t CfaLagrangeInterpColor(int32_t n, const float x[], const float fx[], float xp) {
    int32_t v = (int32_t)CfaLagrangeInterp(n, x, fx, xp);
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); // clamp like Scene_LagrangeInterpColor
}

// AnimatedMat_SetColor, XLU only: build the 3-command prim/env color DL in host frame memory and
// point the segment at it. Raw host pointer — no RM bracket needed for this one.
inline void CfaSetColorSegment(PlayState* play, int32_t segment, const CfaPrimColor* prim, const CfaEnvColor* env,
                               bool colorOpa = false) {
    Gfx* gfx = (Gfx*)CFA_ALLOC(play->state.gfxCtx, 3 * sizeof(Gfx));
    Gfx* head = gfx;

    OPEN_DISPS(play->state.gfxCtx);
    if (colorOpa) { // OPA/skeletal class: the model samples the animated colour on the OPA stream
        gSPSegment(POLY_OPA_DISP++, segment, (uintptr_t)head);
    } else {
        gSPSegment(POLY_XLU_DISP++, segment, (uintptr_t)head); // host GbiWrap fn: target is uintptr_t
    }
    CLOSE_DISPS(play->state.gfxCtx);

    gDPSetPrimColor(gfx++, 0, prim->lodFrac, prim->r, prim->g, prim->b, prim->a);
    if (env != NULL) {
        gDPSetEnvColor(gfx++, env->r, env->g, env->b, env->a);
    }
    gSPEndDisplayList(gfx++);
}

// AnimatedMat_DrawColorNonLinearInterp. The loaded resource's primColors/envColors/keyFrames are
// real host pointers (factory-built), so MM's Lib_SegmentedToVirtual calls are identity — dropped.
inline void CfaDrawColorLagrange(PlayState* play, int32_t segment, const CfaColorParams* p, uint32_t step,
                                 bool colorOpa = false) {
    float curFrame = (float)(step % p->keyFrameLength);
    float x[kMaxKeyFrames];
    float fxPrim[5][kMaxKeyFrames]; // r g b a lodFrac
    float fxEnv[4][kMaxKeyFrames];  // r g b a

    int32_t n = p->keyFrameCount;
    for (int32_t i = 0; i < n; i++) {
        x[i] = p->keyFrames[i];
        fxPrim[0][i] = p->primColors[i].r;
        fxPrim[1][i] = p->primColors[i].g;
        fxPrim[2][i] = p->primColors[i].b;
        fxPrim[3][i] = p->primColors[i].a;
        fxPrim[4][i] = p->primColors[i].lodFrac;
        if (p->envColors != NULL) {
            fxEnv[0][i] = p->envColors[i].r;
            fxEnv[1][i] = p->envColors[i].g;
            fxEnv[2][i] = p->envColors[i].b;
            fxEnv[3][i] = p->envColors[i].a;
        }
    }

    CfaPrimColor prim;
    prim.r = CfaLagrangeInterpColor(n, x, fxPrim[0], curFrame);
    prim.g = CfaLagrangeInterpColor(n, x, fxPrim[1], curFrame);
    prim.b = CfaLagrangeInterpColor(n, x, fxPrim[2], curFrame);
    prim.a = CfaLagrangeInterpColor(n, x, fxPrim[3], curFrame);
    prim.lodFrac = CfaLagrangeInterpColor(n, x, fxPrim[4], curFrame);

    CfaEnvColor env;
    if (p->envColors != NULL) {
        env.r = CfaLagrangeInterpColor(n, x, fxEnv[0], curFrame);
        env.g = CfaLagrangeInterpColor(n, x, fxEnv[1], curFrame);
        env.b = CfaLagrangeInterpColor(n, x, fxEnv[2], curFrame);
        env.a = CfaLagrangeInterpColor(n, x, fxEnv[3], curFrame);
    }
    CfaSetColorSegment(play, segment, &prim, (p->envColors != NULL) ? &env : NULL, colorOpa);
}

// AnimatedMat_DrawColor (type 2): key-frame color without interpolation — primColors is indexed
// directly by frame (keyFrames/keyFrameCount are unused and may be absent; Eyegore's eye laser).
inline void CfaDrawColorIndexed(PlayState* play, int32_t segment, const CfaColorParams* p, uint32_t step,
                                bool colorOpa = false) {
    int32_t curFrame = (int32_t)(step % p->keyFrameLength);
    const CfaPrimColor* prim = &p->primColors[curFrame];
    const CfaEnvColor* env = (p->envColors != NULL) ? &p->envColors[curFrame] : NULL;
    CfaSetColorSegment(play, segment, prim, env, colorOpa);
}

// AnimatedMat_DrawTwoTexScroll (type 1): build the two-layer scroll DL and point `segment` at it on
// the XLU stream (+ OPA when bindOpa, mirroring MM's flags=3 for get-item draws — the tear's item
// body samples the segment on the OPA layer). Formula matches AnimatedMat_TwoLayerTexScroll.
inline void CfaDrawTwoTexScroll(PlayState* play, int32_t segment, const CfaTexScrollEntry* p, uint32_t step,
                                bool bindOpa) {
    int32_t s = (int32_t)step; // signed like MM's sMatAnimStep (s32); avoids unsigned unary-minus
    Gfx* dl = Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, p[0].xStep * s, -(p[0].yStep * s), p[0].width, p[0].height, 1,
                                 p[1].xStep * s, -(p[1].yStep * s), p[1].width, p[1].height, p[0].xStep, -(p[0].yStep),
                                 p[1].xStep, -(p[1].yStep));
    OPEN_DISPS(play->state.gfxCtx);
    if (bindOpa) {
        gSPSegment(POLY_OPA_DISP++, segment, (uintptr_t)dl); // host GbiWrap fn: target is uintptr_t
    }
    gSPSegment(POLY_XLU_DISP++, segment, (uintptr_t)dl);
    CLOSE_DISPS(play->state.gfxCtx);
}

// AnimatedMat_DrawMain's entry walk for the ported subset. Records which segments were written so
// the caller can restore them. step = play->gameplayFrames (MM's AnimatedMat_Draw uses the same).
// bindOpa: also bind on the OPA stream (scroll types only; color types stay XLU-only by design).
inline void CfaDrawTexAnim(PlayState* play, const CfaMatEntry* mat, uint32_t step, int32_t* outSegs,
                           int32_t* outSegCount, bool bindOpa, bool colorOpa = false) {
    int32_t seg;
    int32_t guard = 0;
    do {
        seg = mat->segment;
        int32_t segAbs = (seg < 0 ? -seg : seg) + 7;
        if (mat->type == kMatTypeTwoTexScroll) { // type pre-validated
            CfaDrawTwoTexScroll(play, segAbs, (const CfaTexScrollEntry*)mat->params, step, bindOpa);
        } else if (mat->type == kMatTypeColorChange) {
            CfaDrawColorIndexed(play, segAbs, (const CfaColorParams*)mat->params, step, colorOpa);
        } else {
            CfaDrawColorLagrange(play, segAbs, (const CfaColorParams*)mat->params, step, colorOpa);
        }
        if (*outSegCount < kMaxMatEntries) {
            outSegs[(*outSegCount)++] = segAbs;
        }
        mat++;
    } while (seg >= 0 && ++guard < kMaxMatEntries);
}

// Validate at cache-build time that the loaded texanim only uses what we ported (types 1/2/4) and that
// its tables fit the fixed-size interpolation buffers. Anything else => load failure => sentinel.
inline bool CfaValidateTexAnim(const CfaMatEntry* mat) {
    if (mat == NULL || mat->segment == 0) {
        return false;
    }
    int32_t seg;
    int32_t guard = 0;
    do {
        seg = mat->segment;
        if (mat->type == kMatTypeTwoTexScroll) {
            if (mat->params == NULL) {
                return false;
            }
        } else if (mat->type == kMatTypeColorChange) {
            // frame-indexed: only keyFrameLength + primColors are read (keyFrames may be absent)
            const CfaColorParams* p = (const CfaColorParams*)mat->params;
            if (p == NULL || p->keyFrameLength == 0 || p->primColors == NULL) {
                return false;
            }
        } else if (mat->type == kMatTypeColorLagrange) {
            const CfaColorParams* p = (const CfaColorParams*)mat->params;
            if (p == NULL || p->keyFrameLength == 0 || p->keyFrameCount == 0 || p->keyFrameCount > kMaxKeyFrames ||
                p->primColors == NULL || p->keyFrames == NULL) {
                return false;
            }
        } else {
            return false; // unported handler type
        }
        mat++;
    } while (seg >= 0 && ++guard < kMaxMatEntries);
    return seg < 0; // must have hit the negative-segment terminator
}

// ---- Per-item caches. Resources are held as shared_ptr so they stay alive in the owning RM's
// cache; SkelAnime/jointTable storage is node-stable (unordered_map). Negative results are cached
// (ok=false) so a broken item costs one attempt, then falls back to the sentinel forever.

struct CfaSkelEntry {
    bool ok = false;
    std::shared_ptr<Ship::IResource> skelRes;
    std::shared_ptr<Ship::IResource> animRes;
    SkelAnime skelAnime{};
    std::vector<Vec3s> jointTable; // doubles as morphTable on the XLU path, mirroring DrawStrayFairy
    std::vector<Vec3s> morphTable; // OPA path only (the real funcs keep the two tables separate)
    uint32_t lastUpdate = 0;       // frame guard: one SkelAnime_Update per frame per skeleton
};

struct CfaTexAnimEntry {
    bool ok = false;
    std::shared_ptr<Ship::IResource> res;
    const CfaMatEntry* mat = nullptr;
};

struct CfaStaticTexAnim {
    bool ok = false;
    std::shared_ptr<Ship::IResource> res; // held so the resource stays alive in the RM cache
    const CfaMatEntry* mat = nullptr;
};

// Hoisted out of their functions so CfaClearCaches can clear them. The shared_ptrs point
// at FOREIGN-game resources: their control blocks and IResource vtables live in the OTHER game's
// DLL. Left populated, these maps die in THIS DLL's static destructors — after the launcher has
// already FreeLibrary'd the foreign DLL — and ~shared_ptr dispatches _Destroy() through an unmapped
// vtable. See docs/deviations/boot-shutdown.md.
inline std::unordered_map<std::string, CfaSkelEntry> sCfaSkelCache;
inline std::unordered_map<std::string, CfaTexAnimEntry> sCfaTexAnimCache;
inline std::unordered_map<std::string, CfaStaticTexAnim> sCfaStaticTexAnimCache;

// Module-local cache clear, registered as a CrossRMRegistry teardown listener the first time a
// cache is touched (CfaEnsureTeardownHook below): ANY RM unregister — both games' DeinitOTR, which
// run before every FreeLibrary — empties the caches, so the borrowed refs structurally cannot
// outlive the owning DLL. Listeners stay registered and re-fire on the second Unregister, which
// just clears empty maps.
inline void CfaClearCaches() {
    sCfaSkelCache.clear();
    sCfaTexAnimCache.clear();
    sCfaStaticTexAnimCache.clear();
}

// Register-once guard. Lazy on purpose: it runs from the cache-miss paths, so a module that never
// draws a foreign item never registers (and its empty caches need no teardown).
inline void CfaEnsureTeardownHook() {
    static bool sRegistered = (Ship::CrossRMRegistry::RegisterTeardownListener(&CfaClearCaches), true);
    (void)sRegistered;
}

// The foreign game whose limb DLs the in-flight DrawFlex is submitting (single-threaded draw).
inline const char* sCfaCurrentGame = nullptr;

// Foreign limb DLs are "__OTR__<path>" strings the host's GbiWrap would resolve against the WRONG
// RM, so rewrite each to its interned "__OTR__@<game>:" routed form. See docs/deviations/rando.md.
inline Gfx* CfaRouteLimbDList(Gfx* dList) {
    const char* s = (const char*)dList;
    if (s == nullptr || strncmp(s, "__OTR__", 7) != 0) {
        return dList; // raw pointer (or null) — covered by the RM bracket instead
    }
    if (sCfaCurrentGame == nullptr) {
        return NULL; // no direction set: drop the limb rather than submit an unrouted foreign DL
    }
    static std::unordered_map<const void*, std::string> sRouted; // node-based: values pointer-stable
    auto it = sRouted.find(s);
    if (it == sRouted.end()) {
        it = sRouted.emplace(s, std::string("__OTR__@") + sCfaCurrentGame + ":" + (s + 7)).first;
    }
    return (Gfx*)it->second.c_str();
}

// OverrideLimbDraw for the host's SkelAnime_DrawFlex: null the described hidden limb (stray fairy:
// right-facing head, mirroring MM's StrayFairyOverrideLimbDraw) and route every other limb DL.
inline s32 CfaOverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, CFA_LIMB_ARG arg,
                               Gfx** gfx) {
    if (limbIndex == (s32)(intptr_t)arg) {
        *dList = NULL;
        return false;
    }
    *dList = CfaRouteLimbDList(*dList);
    return false;
}

// ---- OPA/skeletal class (OOT boss souls, MM enemy souls + minifrogs). Unlike the XLU stray-fairy
// path above, this drives the host's SkelAnime_Draw*Opa entry points, whose limb callbacks write
// straight to POLY_OPA_DISP/POLY_XLU_DISP, and replays the recipe's segment binds, per-limb colours
// and limb-DL surgery. The in-flight recipe rides a file static because the *Opa callback signature
// has no user argument we can key on (single-threaded draw, same as sCfaCurrentGame).
inline const CwItemAnimDrawInfo* sCfaInfo = nullptr;

inline const CwAnimLimbDL* CfaFindLimbDL(s32 limbIndex) {
    if (sCfaInfo == nullptr) {
        return nullptr;
    }
    for (int32_t i = 0; i < sCfaInfo->limbDLCount; i++) {
        if (sCfaInfo->limbDLs[i].limbIndex == (int32_t)limbIndex) {
            return &sCfaInfo->limbDLs[i];
        }
    }
    return nullptr;
}

// An ARRAY of ENDDLs, not a single command: DLs may call through a bound segment at an index > 0
// (MM's Scene_SetRenderModeXlu binds 4-entry ENDDL arrays for exactly this), so every slot an
// indexed call can select must hold an ENDDL or execution runs off the allocation into whatever
// follows it. 8 entries = MM's 4 with headroom.
#define CFA_EMPTY_DL_ENTRIES 8
inline Gfx* CfaEmptyDL(PlayState* play) {
    Gfx* dl = (Gfx*)CFA_ALLOC(play->state.gfxCtx, CFA_EMPTY_DL_ENTRIES * sizeof(Gfx));
    Gfx* p = dl;
    for (int32_t i = 0; i < CFA_EMPTY_DL_ENTRIES; i++) {
        gSPEndDisplayList(p++);
    }
    return dl;
}

// MM's Scene_SetRenderModeXlu index 1 (single-cycle XLU render mode), rebuilt in host frame memory.
inline Gfx* CfaXluRenderModeDL(PlayState* play) {
    Gfx* dl = (Gfx*)CFA_ALLOC(play->state.gfxCtx, 2 * sizeof(Gfx));
    Gfx* p = dl;
    gDPSetRenderMode(p++,
                     AA_EN | Z_CMP | IM_RD | CLR_ON_CVG | CVG_DST_WRAP | ZMODE_XLU | FORCE_BL |
                         GBL_c1(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1),
                     G_RM_AA_ZB_XLU_SURF2);
    gSPEndDisplayList(p++);
    return dl;
}

inline Gfx* CfaBuildScroll(PlayState* play, const CwAnimSegBind* b) {
    int32_t t = (int32_t)play->state.frames;
    int32_t x1 = b->xBase1 + b->xStep1 * t;
    int32_t y1 = b->yBase1 + b->yStep1 * t;
    int32_t x2 = b->xBase2 + b->xStep2 * t;
    int32_t y2 = b->yBase2 + b->yStep2 * t;
    if (b->xMask1) {
        x1 &= b->xMask1;
    }
    if (b->yMask1) {
        y1 &= b->yMask1;
    }
    if (b->xMask2) {
        x2 &= b->xMask2;
    }
    if (b->yMask2) {
        y2 &= b->yMask2;
    }
    if (b->singleLayer) {
        return Gfx_TexScrollEx(play->state.gfxCtx, x1, y1, b->width1, b->height1, b->xStep1, b->yStep1);
    }
    return Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, x1, y1, b->width1, b->height1, 1, x2, y2, b->width2, b->height2,
                              b->xStep1, b->yStep1, b->xStep2, b->yStep2);
}

// Emit the segment bind on the described streams. Split out so the RM scope can wrap ONLY this.
inline void CfaEmitSeg(PlayState* play, const CwAnimSegBind* b, uintptr_t target) {
    OPEN_DISPS(play->state.gfxCtx);
    if (b->onOpa) {
        gSPSegment(POLY_OPA_DISP++, b->segment, target);
    }
    if (b->onXlu) {
        gSPSegment(POLY_XLU_DISP++, b->segment, target);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// Bind one described segment in the host frame and record it so the caller can restore it.
// Returns false when the segment could not be bound — callers must then not submit its DL.
inline bool CfaBindSeg(PlayState* play, const CwAnimSegBind* b, const char* game, int32_t* segs, int32_t* segCount) {
    uintptr_t target = 0;
    std::shared_ptr<Ship::ResourceManager> pathRm; // non-null only for CW_ANIM_SEG_PATH
    switch (b->kind) {
        case CW_ANIM_SEG_EMPTY_DL:
            target = (uintptr_t)CfaEmptyDL(play);
            break;
        case CW_ANIM_SEG_PATH:
            // The host's gSPSegment probes this path against the ACTIVE RM at record time, so the
            // playback bracket cannot help — swap to the owning RM across the emit itself.
            pathRm = Ship::CrossRMRegistry::Get(game);
            if (pathRm == nullptr) {
                return false;
            }
            target = (uintptr_t)b->path;
            break;
        case CW_ANIM_SEG_TEXSCROLL:
            target = (uintptr_t)CfaBuildScroll(play, b);
            break;
        case CW_ANIM_SEG_XLU_RENDERMODE_1CY:
            target = (uintptr_t)CfaXluRenderModeDL(play);
            break;
        default:
            return false; // pre-validated; never submit a DL against a segment we could not bind
    }
    if (pathRm != nullptr) {
        Ship::ResourceManagerScope rmScope(pathRm);
        CfaEmitSeg(play, b, target);
    } else {
        CfaEmitSeg(play, b, target); // host-built Gfx pointer: no RM swap
    }
    if (*segCount < CW_ANIM_MAX_SEGS + 1) {
        segs[(*segCount)++] = b->segment;
    }
    return true;
}

// Restore every segment the recipe bound to a benign empty DL, on BOTH streams (harmless where a
// segment was only bound on one) — same hygiene as MM_RestoreForeignSegs / OOT_RestoreForeignSegs.
inline void CfaRestoreSegs(PlayState* play, const int32_t* segs, int32_t count) {
    if (count <= 0) {
        return;
    }
    Gfx* empty = CfaEmptyDL(play);
    OPEN_DISPS(play->state.gfxCtx);
    for (int32_t i = 0; i < count; i++) {
        gSPSegment(POLY_OPA_DISP++, segs[i], (uintptr_t)empty);
        gSPSegment(POLY_XLU_DISP++, segs[i], (uintptr_t)empty);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

inline s32 CfaOverrideLimbDrawOpa(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot,
                                  CFA_LIMB_ARG arg) {
    const CwItemAnimDrawInfo* info = sCfaInfo;
    if (info == nullptr || dList == NULL) {
        return false;
    }
    bool synced = false;
    OPEN_DISPS(play->state.gfxCtx);
    for (int32_t i = 0; i < info->limbColorCount; i++) { // later ranges win, mirroring the real funcs
        const CwAnimLimbColor* c = &info->limbColors[i];
        if ((int32_t)limbIndex >= c->limbFrom && (int32_t)limbIndex <= c->limbTo) {
            if (!synced) {
                gDPPipeSync(POLY_OPA_DISP++);
                synced = true;
            }
            gDPSetEnvColor(POLY_OPA_DISP++, c->env[0], c->env[1], c->env[2], c->env[3]);
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);

    const CwAnimLimbDL* l = CfaFindLimbDL(limbIndex);
    if (l != nullptr) {
        if (pos != NULL) {
            pos->x += l->posDx;
            pos->y += l->posDy;
            pos->z += l->posDz;
        }
        if (l->hide) {
            *dList = NULL;
            return false;
        }
        if (l->dlPath != NULL) {
            *dList = CfaRouteLimbDList((Gfx*)l->dlPath);
            return false;
        }
    }
    if (info->hiddenLimb > 0 && (int32_t)limbIndex == info->hiddenLimb) {
        *dList = NULL;
        return false;
    }
    *dList = CfaRouteLimbDList(*dList);
    return false;
}

inline void CfaPostLimbDrawOpa(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, CFA_LIMB_ARG arg) {
    const CwAnimLimbDL* l = CfaFindLimbDL(limbIndex);
    if (l == nullptr || (l->postSelf == 0 && l->postDlPath == NULL)) {
        return;
    }
    Gfx* dl = (l->postDlPath != NULL) ? CfaRouteLimbDList((Gfx*)l->postDlPath)
                                      : ((dList != NULL) ? CfaRouteLimbDList(*dList) : NULL);
    if (dl == NULL) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    if (l->postBillboard) {
        Matrix_ReplaceRotation(&play->billboardMtxF);
    }
    if (l->postXlu) {
        CFA_LOAD_MTX(POLY_XLU_DISP++, play->state.gfxCtx);
        gSPDisplayList(POLY_XLU_DISP++, dl);
    } else {
        CFA_LOAD_MTX(POLY_OPA_DISP++, play->state.gfxCtx);
        gSPDisplayList(POLY_OPA_DISP++, dl);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// The secondary composite pass: the soul flame (OOT Randomizer_DrawBossSoul's grayscale-tinted blue
// fire, MM DrawEnLight's prim/env-coloured one). Always inside a Matrix_Push/Pop so it can never
// perturb the model transform.
inline void CfaDrawFlame(PlayState* play, const CwItemAnimDrawInfo* info, const char* game, int32_t* segs,
                         int32_t* segCount) {
    Gfx* dl = CfaRouteLimbDList((Gfx*)info->flameDlPath);
    if (dl == NULL) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    CFA_SETUP_XLU(play->state.gfxCtx);
    CLOSE_DISPS(play->state.gfxCtx);
    if (info->flameHasSeg && !CfaBindSeg(play, &info->flameSeg, game, segs, segCount)) {
        return; // flameSeg's only consumer is the flame DL; skip it, the model still draws
    }

    Matrix_Push();
    if (info->flameBillboardFirst) {
        Matrix_ReplaceRotation(&play->billboardMtxF);
    }
    Matrix_Translate(info->flameTranslate[0], info->flameTranslate[1], info->flameTranslate[2], MTXMODE_APPLY);
    Matrix_Scale(info->flameScale[0], info->flameScale[1], info->flameScale[2], MTXMODE_APPLY);
    if (!info->flameBillboardFirst) {
        Matrix_ReplaceRotation(&play->billboardMtxF);
    }

    OPEN_DISPS(play->state.gfxCtx);
    CFA_LOAD_MTX(POLY_XLU_DISP++, play->state.gfxCtx);
    if (info->flameGrayscale) {
        gDPSetGrayscaleColor(POLY_XLU_DISP++, info->flameColor[0], info->flameColor[1], info->flameColor[2], 255);
        gSPGrayscale(POLY_XLU_DISP++, true);
    } else {
        gDPSetPrimColor(POLY_XLU_DISP++, 0xC0, 0xC0, info->flameColor[0], info->flameColor[1], info->flameColor[2], 0);
        gDPSetEnvColor(POLY_XLU_DISP++, info->flameColor[0], info->flameColor[1], info->flameColor[2], 0);
    }
    gSPComboRMPush(POLY_XLU_DISP++, game);
    gSPDisplayList(POLY_XLU_DISP++, dl);
    gSPComboRMPop(POLY_XLU_DISP++);
    if (info->flameGrayscale) {
        gSPGrayscale(POLY_XLU_DISP++, false);
    }
    CLOSE_DISPS(play->state.gfxCtx);
    Matrix_Pop();
}

// Reject anything the OPA path cannot express BEFORE a single command is submitted: an out-of-range
// count, an unroutable path or an unbindable segment must fall back to the caller's sentinel rather
// than leave a DL sampling a segment we never bound (the documented foreign-draw garbage-DL crash).
inline bool CfaIsOtrPath(const char* p) {
    return p != nullptr && strncmp(p, "__OTR__", 7) == 0;
}

inline bool CfaValidateSegBind(const CwAnimSegBind* b) {
    if (b->segment < 8 || b->segment > 13 || (b->onOpa == 0 && b->onXlu == 0)) {
        return false;
    }
    switch (b->kind) {
        case CW_ANIM_SEG_EMPTY_DL:
        case CW_ANIM_SEG_TEXSCROLL:
        case CW_ANIM_SEG_XLU_RENDERMODE_1CY:
            return true;
        case CW_ANIM_SEG_PATH:
            return CfaIsOtrPath(b->path);
        default:
            return false;
    }
}

inline bool CfaValidateOpaInfo(const CwItemAnimDrawInfo* info) {
    if (info->segCount < 0 || info->segCount > CW_ANIM_MAX_SEGS || info->limbColorCount < 0 ||
        info->limbColorCount > CW_ANIM_MAX_LIMB_COLORS || info->limbDLCount < 0 ||
        info->limbDLCount > CW_ANIM_MAX_LIMB_DLS) {
        return false;
    }
    for (int32_t i = 0; i < info->segCount; i++) {
        if (!CfaValidateSegBind(&info->segs[i])) {
            return false;
        }
    }
    for (int32_t i = 0; i < info->limbDLCount; i++) {
        const CwAnimLimbDL* l = &info->limbDLs[i];
        if ((l->dlPath != nullptr && !CfaIsOtrPath(l->dlPath)) ||
            (l->postDlPath != nullptr && !CfaIsOtrPath(l->postDlPath))) {
            return false;
        }
    }
    if (info->flameDlPath != nullptr) {
        if (!CfaIsOtrPath(info->flameDlPath)) {
            return false;
        }
        if (info->flameHasSeg && !CfaValidateSegBind(&info->flameSeg)) {
            return false;
        }
    }
    return true;
}

/* Draw one foreign animated item described by `info` (owning game `game`, e.g. "mm") at the
 * current model matrix. Returns 1 on success; 0 on ANY load/validation failure so the caller can
 * fall back to its sentinel. Mirrors mm/2s2h/Rando/DrawItem.cpp DrawStrayFairy structure. */
inline int32_t ComboForeignAnim_Draw(const CwItemAnimDrawInfo* info, const char* game, PlayState* play) {
    if (info == NULL || game == NULL || play == NULL || info->skelPath == NULL || info->animPath == NULL ||
        (info->xlu == 0 && info->opa == 0) /* one layer must be chosen */) {
        return 0;
    }
    if (info->opa && !CfaValidateOpaInfo(info)) {
        return 0; // unexpressible recipe — sentinel beats an unbound-segment draw
    }
    if (Ship::CrossRMRegistry::Get(game) == nullptr) {
        return 0; // owning game not resident: bail before any Gfx is emitted
    }

    CfaEnsureTeardownHook();

    // -- skeleton + animation (keyed by skel+anim; all variants sharing a pair share one instance,
    //    so like MM's single-instance approach all on-screen copies animate in unison) --
    std::string skelKey = std::string(info->skelPath) + "|" + info->animPath;
    auto skelIt = sCfaSkelCache.find(skelKey);
    if (skelIt == sCfaSkelCache.end()) {
        skelIt = sCfaSkelCache.emplace(skelKey, CfaSkelEntry{}).first;
        CfaSkelEntry& e = skelIt->second;
        if (auto rm = Ship::CrossRMRegistry::Get(game)) {
            // The foreign game's Skeleton/Animation factories nested-load their limbs/tables via
            // Context::GetInstance()->GetResourceManager() — the ACTIVE RM, i.e. the HOST's while
            // the host game is running — so scope a swap to the owning RM around the loads. RAII:
            // restores on every exit path including a throwing factory; a leaked swap would leave
            // the host permanently on the foreign RM. Loads are synchronous and one-time per item.
            {
                Ship::ResourceManagerScope rmScope(rm);
                e.skelRes = rm->LoadResource(info->skelPath); // LoadResource strips "__OTR__" itself
                e.animRes = rm->LoadResource(info->animPath);
            }

            FlexSkeletonHeader* skel = e.skelRes ? (FlexSkeletonHeader*)e.skelRes->GetRawPointer() : NULL;
            AnimationHeader* anim = e.animRes ? (AnimationHeader*)e.animRes->GetRawPointer() : NULL;
            // soh's SkelAnime_Init/InitFlex assert limbCount == sh.limbCount + 1 — pre-validate instead.
            if (skel != NULL && anim != NULL && info->limbCount > 0 && (s32)skel->sh.limbCount + 1 == info->limbCount) {
                e.jointTable.resize(info->limbCount);
                if (info->opa) {
                    e.morphTable.resize(info->limbCount);
                    if (info->nonFlexSkeleton) {
                        SkelAnime_Init(play, &e.skelAnime, (SkeletonHeader*)skel, anim, e.jointTable.data(),
                                       e.morphTable.data(), info->limbCount);
                    } else {
                        SkelAnime_InitFlex(play, &e.skelAnime, skel, anim, e.jointTable.data(), e.morphTable.data(),
                                           info->limbCount);
                    }
                    if (info->playSpeed != 0.0f) {
                        e.skelAnime.playSpeed = info->playSpeed;
                    }
                } else {
                    SkelAnime_InitFlex(play, &e.skelAnime, skel, anim, e.jointTable.data(), e.jointTable.data(),
                                       info->limbCount);
                }
                e.ok = true;
            }
        }
    }
    CfaSkelEntry& skelEntry = skelIt->second;
    if (!skelEntry.ok) {
        return 0;
    }

    // -- texanim (keyed by texAnimPath; the per-area coloring lives here) --
    CfaTexAnimEntry* texAnim = nullptr;
    if (info->texAnimPath != NULL) {
        auto taIt = sCfaTexAnimCache.find(info->texAnimPath);
        if (taIt == sCfaTexAnimCache.end()) {
            taIt = sCfaTexAnimCache.emplace(info->texAnimPath, CfaTexAnimEntry{}).first;
            CfaTexAnimEntry& e = taIt->second;
            if (auto rm = Ship::CrossRMRegistry::Get(game)) {
                // Scoped like the skel/anim load: today's validated types don't nested-load, but
                // that is a property of the validated subset, not of the factory.
                Ship::ResourceManagerScope rmScope(rm);
                e.res = rm->LoadResource(info->texAnimPath);
                e.mat = e.res ? (const CfaMatEntry*)e.res->GetRawPointer() : nullptr;
                e.ok = CfaValidateTexAnim(e.mat);
            }
        }
        texAnim = &taIt->second;
        if (!texAnim->ok) {
            return 0; // item DESCRIBES a texanim we can't run — sentinel beats a miscolored model
        }
    }

    // -- OPA/skeletal path (boss souls, enemy souls, minifrogs) --
    if (info->opa) {
        sCfaCurrentGame = game;
        sCfaInfo = info;
        int32_t segs[CW_ANIM_MAX_SEGS + 1];
        int32_t segCount = 0;

        OPEN_DISPS(play->state.gfxCtx);
        CFA_SETUP_OPA(play->state.gfxCtx);
        CLOSE_DISPS(play->state.gfxCtx);

        if (info->flameDlPath != NULL && !info->flameAfter) {
            CfaDrawFlame(play, info, game, segs, &segCount); // OOT: flame first, model transform after
        }

        Matrix_Translate(info->translatePre[0], info->translatePre[1], info->translatePre[2], MTXMODE_APPLY);
        if (info->scale != 0.0f) {
            Matrix_Scale(info->scale, info->scale, info->scale, MTXMODE_APPLY);
        }
        Matrix_Translate(info->translatePost[0], info->translatePost[1], info->translatePost[2], MTXMODE_APPLY);
        if (info->billboard) {
            Matrix_ReplaceRotation(&play->billboardMtxF);
        }

        for (int32_t i = 0; i < info->segCount; i++) {
            if (!CfaBindSeg(play, &info->segs[i], game, segs, &segCount)) {
                // Unbindable segment: sentinel beats a garbage DL. Unreachable today (both false kinds
                // are pre-rejected above); the sentinel inherits this item's matrix, so it may be tiny.
                CfaRestoreSegs(play, segs, segCount);
                sCfaInfo = nullptr;
                return 0;
            }
        }
        // MM's AnimatedMat_Draw inside the enemy-soul funcs: bind the animated material on the OPA
        // stream (the skeleton samples it there), restored with the rest below.
        int32_t matSegs[kMaxMatEntries];
        int32_t matSegCount = 0;
        if (texAnim != nullptr) {
            CfaDrawTexAnim(play, texAnim->mat, play->gameplayFrames, matSegs, &matSegCount, /*bindOpa*/ true,
                           /*colorOpa*/ true);
        }
        if (skelEntry.lastUpdate != play->state.frames) {
            skelEntry.lastUpdate = play->state.frames;
            SkelAnime_Update(&skelEntry.skelAnime);
        }

        OPEN_DISPS(play->state.gfxCtx);
        if (info->hasPrimColor) {
            gDPSetPrimColor(POLY_OPA_DISP++, 0, (u8)info->primLodFrac, info->primColor[0], info->primColor[1],
                            info->primColor[2], info->primColor[3]);
        }
        if (info->hasModelEnvColor) {
            gDPSetEnvColor(POLY_OPA_DISP++, info->modelEnvColor[0], info->modelEnvColor[1], info->modelEnvColor[2],
                           info->modelEnvColor[3]);
        }
        // RM bracket on BOTH streams: the post-limb pass can submit on XLU, and any raw-pointer span
        // the skeleton drags in resolves its inner refs against the owning game's RM.
        gSPComboRMPush(POLY_OPA_DISP++, game);
        gSPComboRMPush(POLY_XLU_DISP++, game);
        CLOSE_DISPS(play->state.gfxCtx);

        if (info->nonFlexSkeleton) {
            SkelAnime_DrawOpa(play, skelEntry.skelAnime.skeleton, skelEntry.skelAnime.jointTable,
                              CfaOverrideLimbDrawOpa, CfaPostLimbDrawOpa, NULL);
        } else {
            SkelAnime_DrawFlexOpa(play, skelEntry.skelAnime.skeleton, skelEntry.skelAnime.jointTable,
                                  skelEntry.skelAnime.dListCount, CfaOverrideLimbDrawOpa, CfaPostLimbDrawOpa, NULL);
        }

        OPEN_DISPS(play->state.gfxCtx);
        gSPComboRMPop(POLY_OPA_DISP++);
        gSPComboRMPop(POLY_XLU_DISP++);
        CLOSE_DISPS(play->state.gfxCtx);

        if (info->flameDlPath != NULL && info->flameAfter) {
            CfaDrawFlame(play, info, game, segs, &segCount); // MM: flame inherits the model transform
        }
        CfaRestoreSegs(play, segs, segCount);
        CfaRestoreSegs(play, matSegs, matSegCount);
        sCfaInfo = nullptr;
        return 1;
    }

    // -- draw (mirrors DrawStrayFairy) --
    OPEN_DISPS(play->state.gfxCtx);

    CFA_SETUP_XLU(play->state.gfxCtx);

    int32_t writtenSegs[kMaxMatEntries];
    int32_t writtenSegCount = 0;
    if (texAnim != nullptr) {
        CfaDrawTexAnim(play, texAnim->mat, play->gameplayFrames, writtenSegs, &writtenSegCount, /*bindOpa*/ false);
    }

    if (info->billboard) {
        Matrix_ReplaceRotation(&play->billboardMtxF);
    }
    Matrix_Scale(info->scale, info->scale, info->scale, MTXMODE_APPLY);

    // One animation step per frame regardless of how many copies draw (MM's lastUpdate pattern;
    // its caveat about many instances accelerating the animation does not apply here).
    if (skelEntry.lastUpdate != play->state.frames) {
        skelEntry.lastUpdate = play->state.frames;
        SkelAnime_Update(&skelEntry.skelAnime);
    }

    // RM bracket: safety net so any RAW-pointer span the draw submits resolves its inner refs
    // against the owning game's RM (routed limb DLs carry their own scoped override).
    sCfaCurrentGame = game;
    gSPComboRMPush(POLY_XLU_DISP++, game);
    POLY_XLU_DISP = SkelAnime_DrawFlex(play, skelEntry.skelAnime.skeleton, skelEntry.skelAnime.jointTable,
                                       skelEntry.skelAnime.dListCount, CfaOverrideLimbDraw, NULL,
                                       (CFA_LIMB_ARG)(intptr_t)info->hiddenLimb, POLY_XLU_DISP);
    gSPComboRMPop(POLY_XLU_DISP++);

    // Segment hygiene: re-point the segments the texanim wrote at a benign empty DL (XLU only —
    // the OPA stream was never touched). See docs/deviations/rando.md.
    if (writtenSegCount > 0) {
        Gfx* empty = CfaEmptyDL(play); // array-sized: indexed seg calls must stay in bounds
        for (int32_t i = 0; i < writtenSegCount; i++) {
            gSPSegment(POLY_XLU_DISP++, writtenSegs[i], (uintptr_t)empty);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
    return 1;
}

// Re-point the segments a texanim wrote at a benign empty DL, so later same-frame commands in the
// OPA/XLU streams don't sample this item's scroll DL (segment hygiene; mirrors ComboForeignAnim_Draw's
// XLU restore). restoreOpa must match the bind's bindOpa (the segment was written on OPA too).
inline void ComboForeignTexAnim_Restore(PlayState* play, const int32_t* segs, int32_t count, bool restoreOpa) {
    if (play == NULL || count <= 0) {
        return;
    }
    Gfx* empty = CfaEmptyDL(play); // array-sized: indexed seg calls must stay in bounds
    OPEN_DISPS(play->state.gfxCtx);
    for (int32_t i = 0; i < count; i++) {
        if (restoreOpa) {
            gSPSegment(POLY_OPA_DISP++, segs[i], (uintptr_t)empty);
        }
        gSPSegment(POLY_XLU_DISP++, segs[i], (uintptr_t)empty);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

/* Static-DL foreign items (e.g. MM's Moon's Tear) whose model samples an animated-material segment:
 * load the owning game's TextureAnimation (cached) and bind it before the item's DLs are submitted.
 * Returns true when a valid texanim was bound, filling outSegs/outSegCount with the bound segments so
 * the caller can ComboForeignTexAnim_Restore them after the DLs. `bindOpa` mirrors MM's flags=3 (the
 * item body samples the segment on the OPA layer too). `texAnimPath` is the owning game's own
 * "__OTR__..." path (LoadResource strips the prefix), loaded directly from that game's RM. */
inline bool ComboForeignTexAnim_Run(PlayState* play, const char* game, const char* texAnimPath, bool bindOpa,
                                    int32_t* outSegs, int32_t* outSegCount) {
    *outSegCount = 0;
    if (play == NULL || game == NULL || texAnimPath == NULL) {
        return false;
    }
    CfaEnsureTeardownHook();
    // one attempt per path, then cached
    auto it = sCfaStaticTexAnimCache.find(texAnimPath);
    if (it == sCfaStaticTexAnimCache.end()) {
        it = sCfaStaticTexAnimCache.emplace(texAnimPath, CfaStaticTexAnim{}).first;
        CfaStaticTexAnim& e = it->second;
        if (auto rm = Ship::CrossRMRegistry::Get(game)) {
            Ship::ResourceManagerScope rmScope(rm); // as above: scope the load, not the draw
            e.res = rm->LoadResource(texAnimPath);
            e.mat = e.res ? (const CfaMatEntry*)e.res->GetRawPointer() : nullptr;
            e.ok = CfaValidateTexAnim(e.mat);
        }
    }
    if (!it->second.ok) {
        return false;
    }
    CfaDrawTexAnim(play, it->second.mat, play->gameplayFrames, outSegs, outSegCount, bindOpa);
    return true;
}

#endif /* COMBO_FOREIGN_ANIM_H */
