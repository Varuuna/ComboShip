/* combo/menu/ComboForeignAnim.h — ComboShip: animated cross-game item rendering, host side.
 * The FOREIGN game (MM) only DESCRIBES an animated item via CwItemAnimDrawInfo
 * (ComboItemDrawABI.h: skeleton/animation/texanim resource paths + draw parameters); this header
 * loads those resources through the OWNING game's resident ResourceManager (CrossRMRegistry) and
 * drives the HOST game's own SkelAnime engine on them. Feasible because the games' skeleton and
 * animation structs are byte-identical (SkelAnime 0x44, StandardLimb, FlexSkeletonHeader,
 * AnimationHeader) — verified for Increment 3b. First class served: MM stray fairies in OOT
 * (reference implementation mirrored: mm/2s2h/Rando/DrawItem.cpp DrawStrayFairy).
 *
 * Also ports the minimal AnimatedMaterial subset the stray-fairy texanims need. OOT has no
 * AnimatedMat system; MM's lives in mm/src/code/z_scene_proc.c. All five gStrayFairy*TexAnim
 * resources were inspected (mm.o2r): 2 entries each, BOTH type 4 (ColorChangeLagrange, prim+env
 * color, segments |1|+7=8 and |-2|+7=9, negative segment = end-of-list). So ONLY the type-4
 * handler (AnimatedMat_DrawColorNonLinearInterp + Scene_LagrangeInterp + AnimatedMat_SetColor) is
 * ported; any other entry type fails validation and falls back to the caller's sentinel.
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

#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/CrossRMRegistry.h>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ComboItemDrawABI.h"

// soh's OPEN_DISPS/CLOSE_DISPS macros (macros.h) embed BLOCK-SCOPE declarations of these. With a
// prior visible extern "C" declaration the block-scope redeclaration inherits C linkage — but ONLY
// at global scope; inside a namespace MSVC mangles it as a C++ symbol and the link fails
// (verified). Hence the extern "C" pre-declaration below AND no namespace around this header's
// functions (everything is Cfa-/ComboForeignAnim_-prefixed instead).
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
    int16_t type;    // TextureAnimationParamsType; only 4 (ColorChangeLagrange) is ported
    void* params;
};
constexpr int16_t kMatTypeColorLagrange = 4;
constexpr int32_t kMaxMatEntries = 8;   // sanity bound when walking the entry array
constexpr int32_t kMaxKeyFrames = 50;   // MM's handler uses fixed f32[50] tables — same bound

// ---- Ported handler subset (z_scene_proc.c:226-363, type 4 only). Parameterized on
// (play->state.gfxCtx for allocs/DISPS, step) instead of MM's sMatAnim* globals; alphaRatio is 1
// and flags are XLU-only by design: the animated foreign items draw exclusively on the XLU layer,
// so unlike MM's AnimatedMat_Draw (flags=3) we never touch the OPA stream's segment state.

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
inline void CfaSetColorSegment(PlayState* play, int32_t segment, const CfaPrimColor* prim, const CfaEnvColor* env) {
    Gfx* gfx = (Gfx*)Graph_Alloc(play->state.gfxCtx, 3 * sizeof(Gfx));
    Gfx* head = gfx;

    OPEN_DISPS(play->state.gfxCtx);
    gSPSegment(POLY_XLU_DISP++, segment, (uintptr_t)head); // host GbiWrap fn: target is uintptr_t
    CLOSE_DISPS(play->state.gfxCtx);

    gDPSetPrimColor(gfx++, 0, prim->lodFrac, prim->r, prim->g, prim->b, prim->a);
    if (env != NULL) {
        gDPSetEnvColor(gfx++, env->r, env->g, env->b, env->a);
    }
    gSPEndDisplayList(gfx++);
}

// AnimatedMat_DrawColorNonLinearInterp. The loaded resource's primColors/envColors/keyFrames are
// real host pointers (factory-built), so MM's Lib_SegmentedToVirtual calls are identity — dropped.
inline void CfaDrawColorLagrange(PlayState* play, int32_t segment, const CfaColorParams* p, uint32_t step) {
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
    CfaSetColorSegment(play, segment, &prim, (p->envColors != NULL) ? &env : NULL);
}

// AnimatedMat_DrawMain's entry walk for the ported subset. Records which segments were written so
// the caller can restore them. step = play->gameplayFrames (MM's AnimatedMat_Draw uses the same).
inline void CfaDrawTexAnim(PlayState* play, const CfaMatEntry* mat, uint32_t step, int32_t* outSegs,
                           int32_t* outSegCount) {
    int32_t seg;
    int32_t guard = 0;
    do {
        seg = mat->segment;
        int32_t segAbs = (seg < 0 ? -seg : seg) + 7;
        CfaDrawColorLagrange(play, segAbs, (const CfaColorParams*)mat->params, step); // type pre-validated
        if (*outSegCount < kMaxMatEntries) {
            outSegs[(*outSegCount)++] = segAbs;
        }
        mat++;
    } while (seg >= 0 && ++guard < kMaxMatEntries);
}

// Validate at cache-build time that the loaded texanim only uses what we ported (type 4) and that
// its tables fit the fixed-size interpolation buffers. Anything else => load failure => sentinel.
inline bool CfaValidateTexAnim(const CfaMatEntry* mat) {
    if (mat == NULL || mat->segment == 0) {
        return false;
    }
    int32_t seg;
    int32_t guard = 0;
    do {
        seg = mat->segment;
        if (mat->type != kMatTypeColorLagrange) {
            return false; // unported handler type
        }
        const CfaColorParams* p = (const CfaColorParams*)mat->params;
        if (p == NULL || p->keyFrameLength == 0 || p->keyFrameCount == 0 || p->keyFrameCount > kMaxKeyFrames ||
            p->primColors == NULL || p->keyFrames == NULL) {
            return false;
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
    std::vector<Vec3s> jointTable; // doubles as morphTable, mirroring MM's DrawStrayFairy
    uint32_t lastUpdate = 0;       // frame guard: one SkelAnime_Update per frame per skeleton
};

struct CfaTexAnimEntry {
    bool ok = false;
    std::shared_ptr<Ship::IResource> res;
    const CfaMatEntry* mat = nullptr;
};

// The foreign game whose limb DLs the in-flight DrawFlex is submitting (single-threaded draw).
inline const char* sCfaCurrentGame = nullptr;

// Limb DLs in the foreign game's loaded skeletons are "__OTR__<path>" STRING pointers (see MM's
// SkeletonLimbFactory: limbData.standardLimb.dList = path.c_str()). The host's GbiWrap resolves
// plain "__OTR__" strings at SUBMISSION time through the HOST's RM (wrong game), but routes
// "__OTR__@<game>:" strings as G_DL_OTR_FILEPATH commands the interpreter resolves against the
// named game's RM with scoped inner-reference resolution — exactly like the static foreign-DL
// path. So rewrite each limb DL string to its routed form (interned: the pointer is emitted into
// the display list and dereferenced at interpreter time).
inline Gfx* CfaRouteLimbDList(Gfx* dList) {
    const char* s = (const char*)dList;
    if (s == nullptr || strncmp(s, "__OTR__", 7) != 0) {
        return dList; // raw pointer (or null) — covered by the RM bracket instead
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
inline s32 CfaOverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* arg,
                               Gfx** gfx) {
    if (limbIndex == (s32)(intptr_t)arg) {
        *dList = NULL;
        return false;
    }
    *dList = CfaRouteLimbDList(*dList);
    return false;
}

/* Draw one foreign animated item described by `info` (owning game `game`, e.g. "mm") at the
 * current model matrix. Returns 1 on success; 0 on ANY load/validation failure so the caller can
 * fall back to its sentinel. Mirrors mm/2s2h/Rando/DrawItem.cpp DrawStrayFairy structure. */
inline int32_t ComboForeignAnim_Draw(const CwItemAnimDrawInfo* info, const char* game, PlayState* play) {
    if (info == NULL || game == NULL || play == NULL || info->skelPath == NULL || info->animPath == NULL ||
        info->xlu == 0 /* only the XLU path is implemented (whole animated class is XLU today) */) {
        return 0;
    }

    static std::unordered_map<std::string, CfaSkelEntry> sSkelCache;
    static std::unordered_map<std::string, CfaTexAnimEntry> sTexAnimCache;

    // -- skeleton + animation (keyed by skelPath; all area variants share one skeleton instance,
    //    so like MM's single-instance approach all on-screen copies animate in unison) --
    auto skelIt = sSkelCache.find(info->skelPath);
    if (skelIt == sSkelCache.end()) {
        skelIt = sSkelCache.emplace(info->skelPath, CfaSkelEntry{}).first;
        CfaSkelEntry& e = skelIt->second;
        if (auto rm = Ship::CrossRMRegistry::Get(game)) {
            // The foreign game's Skeleton/Animation factories nested-load their limbs/tables via
            // Context::GetInstance()->GetResourceManager() — the ACTIVE RM, i.e. the HOST's while
            // the host game is running — so scope a swap to the owning RM around the loads. The
            // loads are synchronous (this thread blocks on the RM's pool) and one-time per item;
            // nothing else loads resources mid-frame on other threads in practice.
            auto ctx = Ship::Context::GetInstance();
            auto prevRm = ctx->GetResourceManager();
            ctx->SetResourceManager(rm);
            e.skelRes = rm->LoadResource(info->skelPath); // LoadResource strips "__OTR__" itself
            e.animRes = rm->LoadResource(info->animPath);
            ctx->SetResourceManager(prevRm);

            FlexSkeletonHeader* skel = e.skelRes ? (FlexSkeletonHeader*)e.skelRes->GetRawPointer() : NULL;
            AnimationHeader* anim = e.animRes ? (AnimationHeader*)e.animRes->GetRawPointer() : NULL;
            // soh's SkelAnime_InitFlex asserts limbCount == sh.limbCount + 1 — pre-validate instead.
            if (skel != NULL && anim != NULL && info->limbCount > 0 &&
                (s32)skel->sh.limbCount + 1 == info->limbCount) {
                e.jointTable.resize(info->limbCount);
                SkelAnime_InitFlex(play, &e.skelAnime, skel, anim, e.jointTable.data(), e.jointTable.data(),
                                   info->limbCount);
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
        auto taIt = sTexAnimCache.find(info->texAnimPath);
        if (taIt == sTexAnimCache.end()) {
            taIt = sTexAnimCache.emplace(info->texAnimPath, CfaTexAnimEntry{}).first;
            CfaTexAnimEntry& e = taIt->second;
            if (auto rm = Ship::CrossRMRegistry::Get(game)) {
                // No RM swap needed: the TextureAnimation factory has no nested loads for the
                // color types (TexCycle would, but validation rejects it anyway).
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

    // -- draw (mirrors DrawStrayFairy) --
    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);

    int32_t writtenSegs[kMaxMatEntries];
    int32_t writtenSegCount = 0;
    if (texAnim != nullptr) {
        CfaDrawTexAnim(play, texAnim->mat, play->gameplayFrames, writtenSegs, &writtenSegCount);
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

    // RM bracket (Task 12): any RAW-pointer spans the skeletal draw submits resolve their inner
    // hash/path refs against the owning game's RM at interpreter time. The limb DLs themselves go
    // out as "__OTR__@<game>:" routed strings (CfaRouteLimbDList), which carry their own scoped
    // override — the bracket is the safety net for everything else.
    sCfaCurrentGame = game;
    gSPComboRMPush(POLY_XLU_DISP++, game);
    POLY_XLU_DISP = SkelAnime_DrawFlex(play, skelEntry.skelAnime.skeleton, skelEntry.skelAnime.jointTable,
                                       skelEntry.skelAnime.dListCount, CfaOverrideLimbDraw, NULL,
                                       (void*)(intptr_t)info->hiddenLimb, POLY_XLU_DISP);
    gSPComboRMPop(POLY_XLU_DISP++);

    // Segment hygiene: re-point the segments the texanim wrote at a benign empty DL. OOT's own
    // discipline re-establishes segments 8-D at the START of each frame's buffers (Scene_Draw ->
    // scene draw config, soh z_scene_table.c sDefaultDisplayList), so contamination is bounded to
    // commands AFTER this draw in the current XLU stream; the empty DL makes those see a no-op
    // instead of our prim/env-color DL. (The OPA stream was never touched — see CfaSetColorSegment.)
    if (writtenSegCount > 0) {
        Gfx* empty = (Gfx*)Graph_Alloc(play->state.gfxCtx, sizeof(Gfx));
        Gfx* e = empty;
        gSPEndDisplayList(e++);
        for (int32_t i = 0; i < writtenSegCount; i++) {
            gSPSegment(POLY_XLU_DISP++, writtenSegs[i], (uintptr_t)empty);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
    return 1;
}

#endif /* COMBO_FOREIGN_ANIM_H */
