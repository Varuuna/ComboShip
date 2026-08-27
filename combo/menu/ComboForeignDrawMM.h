/* combo/menu/ComboForeignDrawMM.h — ComboShip: cross-game foreign-item rendering, MM (host) side.
 * The exact mirror of the foreign block in soh/soh/Enhancements/randomizer/draw.cpp
 * (ComboResolveForeignDrawInfo + Randomizer_DrawComboForeign), in the opposite direction: an MM check
 * holding RI_COMBO_FOREIGN actually holds an OOT item, so we render the REAL OOT model by asking
 * soh.dll (OOT_GetItemDrawInfo, C ABI in combo/menu/ComboItemDrawABI.h) which display lists draw it,
 * then submitting them as "__OTR__@oot:"-routed paths that the shared Fast3D interpreter resolves
 * against OOT's ResourceManager (CrossRMRegistry — OOT's RM stays resident while MM runs).
 *
 * Unlike OOT, MM passes the originating RandoCheckId straight into Rando::DrawItem at every world
 * draw site (freestanding, chest, grass/pot, shop), so the check identity is available directly — no
 * GetItemEntry-stamping mechanism is needed (OOT's comboForeignCheck field has no MM analog here).
 *
 * TU-GLUE HEADER (menu-extraction pattern): include ONCE from mm/2s2h/Rando/DrawItem.cpp, inside its
 * #ifdef COMBO_BUILD, AFTER the engine headers (OPEN_DISPS, Gfx_SetupDL25 Opa/Xlu, the Matrix_ and
 * gbi macros, gPlayState, gSaveContext, GetItem_Draw) and Rando headers are in scope. Not
 * standalone. Lives in
 * combo/menu/ because that directory is already on 2ship's include path (zero CMake churn).
 *
 * The animated cross-game class (combo/menu/ComboForeignAnim.h) is wired here symmetrically with
 * ComboForeignDrawOOT.h: when the static export has no DL row, OOT_GetItemAnimDrawInfo describes a
 * skeletal recipe (the boss souls' real boss skeletons, issue #86) and that header drives MM's own
 * SkelAnime on OOT's resources.
 */
#ifndef COMBO_FOREIGN_DRAW_MM_H
#define COMBO_FOREIGN_DRAW_MM_H

#ifndef OPEN_DISPS
#error "ComboForeignDrawMM.h is TU-glue: include the host engine headers before it"
#endif

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#ifdef _WIN32
#include <windows.h>
#endif

#include "ComboItemDrawABI.h"
// ComboShip: the animated class, with 2ship.dll as the host (see the shim in ComboForeignAnim.h).
#define COMBO_FOREIGN_ANIM_HOST_MM 1
#include "ComboForeignAnim.h"
#include "2s2h/Rando/MiscBehavior/MiscBehavior.h" // Rando::MiscBehavior::MM_LookupForeign
#include "rando/CrossForeign.h"                   // ComboRando::ForeignItem / GAME_OOT

namespace {

struct ComboForeignDrawInfoOOT {
    bool ok = false;
    int32_t count = 0;
    int32_t xluStart = -1; // first XLU entry in dls[] order; -1 = all OPA
    float scale = 0.0f;    // extra uniform model scale; 0 = none (OOT rupees: 0.7)
    bool hasEnvColor = false;
    uint8_t envColor[4] = { 0, 0, 0, 0 };
    int32_t drawKind = CW_DRAW_KIND_SIMPLE;   // non-SIMPLE = replicate a specific OOT draw func
    uint8_t primColorXlu[4] = { 0, 0, 0, 0 }; // JEWEL gem prim / MUSIC_NOTE tint
    uint8_t envColorXlu[4] = { 0, 0, 0, 0 };  // JEWEL gem env
    uint8_t primColorOpa[4] = { 0, 0, 0, 0 }; // JEWEL setting prim
    uint8_t envColorOpa[4] = { 0, 0, 0, 0 };  // JEWEL setting env
    // CW_DRAW_KIND_COLOR_LAYERS: per-DL prim/env colors; bit i of each mask = dls[i] sets it.
    uint8_t layerPrimColor[CW_DRAW_MAX_DLISTS][4] = {};
    uint8_t layerEnvColor[CW_DRAW_MAX_DLISTS][4] = {};
    int32_t layerPrimMask = 0;
    int32_t layerEnvMask = 0;
    const char* dls[CW_DRAW_MAX_DLISTS] = { nullptr }; // interned "__OTR__@oot:..." routed paths
    // OOT's own setup DL for each stream (raw Gfx* in soh.dll), or null for our 25 Opa/Xlu.
    const void* setupDlOpa = nullptr;
    const void* setupDlXlu = nullptr;
    // ComboShip: animated class (no static DL row — OOT boss souls' real skeletons). When animOk,
    // anim describes the item and ComboForeignAnim_Draw renders it; paths point at soh.dll statics.
    bool animOk = false;
    CwItemAnimDrawInfo anim{};
    // Recipe chosen from live save state (progressive tier, Triforce shard, junk/trap) — re-resolve
    // every frame instead of caching, or the first model drawn sticks for the whole save slot.
    bool stateDependent = false;
};

// Routed path strings must outlive the frame (the GBI wrapper emits the raw pointer into the display
// list; the interpreter dereferences it later), so intern them for the process lifetime.
inline const char* ComboInternRoutedPathOOT(const std::string& s) {
    static std::unordered_set<std::string> sPool; // node-based: c_str() stable across rehash
    return sPool.insert(s).first->c_str();
}

// One resolution attempt's outcome. NotReady = a producer/lookup that simply isn't up yet (soh.dll
// not resident, OOT's rando context null while dormant), which must NEVER be negative-cached.
enum class ComboForeignResolveOOT { Ok, Unknown, NotReady };

inline ComboForeignResolveOOT ComboFillForeignDrawInfoOOT(RandoCheckId rc, ComboForeignDrawInfoOOT& info) {
    const ComboRando::ForeignItem* fi = Rando::MiscBehavior::MM_LookupForeign(rc);
    if (fi == nullptr || fi->itemGame != ComboRando::GAME_OOT) {
        return ComboForeignResolveOOT::Unknown;
    }

#ifdef _WIN32
    static Fn_GetItemDrawInfo sGetItemDrawInfo = nullptr;
    if (sGetItemDrawInfo == nullptr) {
        HMODULE h = GetModuleHandleA("soh.dll"); // already loaded by the exe (ComboMenuModel pattern)
        sGetItemDrawInfo = h ? (Fn_GetItemDrawInfo)GetProcAddress(h, "OOT_GetItemDrawInfo") : nullptr;
    }
    if (sGetItemDrawInfo == nullptr) {
        return ComboForeignResolveOOT::NotReady; // soh.dll may simply not be resident yet
    }
    // A disguised trap must draw the item it pretends to be. Same namespace, so the itemGame dispatch
    // above is unaffected. Not state-dependent: like OOT, the disguise holds until the get-item cutscene.
    const char* drawName = fi->HasDisguise() ? fi->fakeItemName.c_str() : fi->itemName.c_str();
    CwItemDrawInfo raw{};
    int32_t rcStatic = sGetItemDrawInfo(drawName, &raw);
    if (rcStatic == CW_DRAW_NOT_READY) {
        return ComboForeignResolveOOT::NotReady; // OOT dormant / rando context null — retry next frame
    }
    if (rcStatic == 0 || raw.dlistCount <= 0) {
        // ComboShip: no static DL row — try the animated ABI (OOT boss souls' real skeletons). OOT
        // only describes the item; ComboForeignAnim_Draw loads + draws it (mirror of the OOT side).
        static Fn_GetItemAnimDrawInfo sGetItemAnimDrawInfo = nullptr;
        if (sGetItemAnimDrawInfo == nullptr) {
            HMODULE h = GetModuleHandleA("soh.dll");
            sGetItemAnimDrawInfo = h ? (Fn_GetItemAnimDrawInfo)GetProcAddress(h, "OOT_GetItemAnimDrawInfo") : nullptr;
        }
        if (sGetItemAnimDrawInfo == nullptr) {
            return ComboForeignResolveOOT::NotReady;
        }
        int32_t rcAnim = sGetItemAnimDrawInfo(drawName, &info.anim);
        if (rcAnim == CW_DRAW_NOT_READY) {
            return ComboForeignResolveOOT::NotReady;
        }
        if (rcAnim != 0) {
            info.animOk = true;
            info.stateDependent = info.anim.stateDependent != 0;
            info.ok = true;
            return ComboForeignResolveOOT::Ok;
        }
        return ComboForeignResolveOOT::Unknown; // unknown item or non-portable draw func -> sentinel
    }

    static constexpr char kOtrPrefix[] = "__OTR__";
    int32_t n = raw.dlistCount < CW_DRAW_MAX_DLISTS ? raw.dlistCount : CW_DRAW_MAX_DLISTS;
    if (n < CwMinDlistsForKind(raw.drawKind)) {
        return ComboForeignResolveOOT::Unknown; // handler would blind-index a missing slot
    }
    for (int32_t i = 0; i < n; i++) {
        const char* p = raw.dlists[i];
        if (p == nullptr || strncmp(p, kOtrPrefix, sizeof(kOtrPrefix) - 1) != 0) {
            return ComboForeignResolveOOT::Unknown; // not an OTR path literal — can't route it
        }
        info.dls[i] = ComboInternRoutedPathOOT(std::string("__OTR__@oot:") + (p + sizeof(kOtrPrefix) - 1));
    }
    info.count = n;
    info.xluStart = raw.xluStartIndex;
    info.scale = raw.scale;
    info.setupDlOpa = raw.setupDlOpa;
    info.setupDlXlu = raw.setupDlXlu;
    info.hasEnvColor = raw.hasEnvColor != 0;
    info.drawKind = raw.drawKind;
    info.stateDependent = raw.stateDependent != 0;
    info.layerPrimMask = raw.layerPrimMask;
    info.layerEnvMask = raw.layerEnvMask;
    memcpy(info.layerPrimColor, raw.layerPrimColor, sizeof(info.layerPrimColor));
    memcpy(info.layerEnvColor, raw.layerEnvColor, sizeof(info.layerEnvColor));
    for (int32_t i = 0; i < 4; i++) {
        info.envColor[i] = raw.envColor[i];
        info.primColorXlu[i] = raw.primColorXlu[i];
        info.envColorXlu[i] = raw.envColorXlu[i];
        info.primColorOpa[i] = raw.primColorOpa[i];
        info.envColorOpa[i] = raw.envColorOpa[i];
    }
    info.ok = true;
    return ComboForeignResolveOOT::Ok;
#else
    return ComboForeignResolveOOT::Unknown; // GetProcAddress resolution is Windows-only (ComboMenuModel)
#endif
}

// Full lookup chain (foreign map -> OOT export -> routed strings), cached per check per slot per
// foreign-map generation so it runs once per check instead of every frame.
inline const ComboForeignDrawInfoOOT* ComboResolveForeignDrawInfoOOT(RandoCheckId rc) {
    static std::unordered_map<int32_t, ComboForeignDrawInfoOOT> sCache;
    static int sCacheSlot = -1;
    static uint64_t sCacheGen = (uint64_t)-1;
    int slot = gSaveContext.fileNum;
    uint64_t gen = Rando::MiscBehavior::ComboRandoGen();
    if (slot != sCacheSlot || gen != sCacheGen) {
        sCache.clear();
        sCacheSlot = slot;
        sCacheGen = gen;
    }
    auto cached = sCache.find(rc);
    if (cached != sCache.end() && !cached->second.stateDependent) {
        return cached->second.ok ? &cached->second : nullptr;
    }
    // A state-dependent recipe (progressive tier, Triforce shard, junk/trap) is re-resolved every
    // frame; caching it would freeze whichever model happened to be correct on the first draw.
    ComboForeignDrawInfoOOT info{}; // built locally: a failure must not clobber a live cached recipe
    if (ComboFillForeignDrawInfoOOT(rc, info) == ComboForeignResolveOOT::NotReady) {
        sCache.erase(rc); // transient — retry next frame instead of freezing the sentinel in
        return nullptr;
    }
    ComboForeignDrawInfoOOT& entry = sCache[rc]; // Unknown caches ok=false: one lookup, then sentinel
    entry = info;
    return entry.ok ? &entry : nullptr;
}

} // namespace

// ---- Non-portable OOT draw funcs: each handler is a 1:1 port of the OOT get-item func
// (soh/src/code/z_draw.c), re-binding the segment(s) it needs in MM's frame before submitting the
// routed @oot: DLs. See docs/deviations/rando.md for the invariants.

// Pin prim+env to white right after a setup DL, before the handler's own colour commands: a layer
// the recipe doesn't tint must not inherit MM's continuously-interpolated scene material colour.
#define MM_FOREIGN_PIN_OPA()                                        \
    do {                                                            \
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255); \
        gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);        \
    } while (0)
#define MM_FOREIGN_PIN_XLU()                                        \
    do {                                                            \
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, 255); \
        gDPSetEnvColor(POLY_XLU_DISP++, 255, 255, 255, 255);        \
    } while (0)

// Restore the segments a handler bound (8 and/or 9) to a benign empty DL so later same-frame commands
// don't sample our scroll DLs. Restores on both streams (harmless where a segment wasn't bound).
// Mirrors the segment hygiene in ComboForeignAnim.h.
inline void MM_RestoreForeignSegs(const int32_t* segs, int32_t count) {
    GraphicsContext* gfxCtx = gPlayState->state.gfxCtx;
    // Array of ENDDLs: DLs may call through a bound segment at an index > 0 — see CfaEmptyDL.
    Gfx* empty = (Gfx*)GRAPH_ALLOC(gfxCtx, 8 * sizeof(Gfx));
    Gfx* e = empty;
    for (int32_t iEmpty = 0; iEmpty < 8; iEmpty++) {
        gSPEndDisplayList(e++);
    }
    OPEN_DISPS(gfxCtx);
    for (int32_t i = 0; i < count; i++) {
        gSPSegment(POLY_OPA_DISP++, segs[i], (uintptr_t)empty);
        gSPSegment(POLY_XLU_DISP++, segs[i], (uintptr_t)empty);
    }
    CLOSE_DISPS(gfxCtx);
}

// Simple path: OPA layers then XLU layers (self-contained funcs, rupees, wallets, Triforce/rod scale).
inline void MM_DrawForeignSimple(const ComboForeignDrawInfoOOT* info) {
    int32_t n = info->count;
    int32_t xs = (info->xluStart < 0 || info->xluStart > n) ? n : info->xluStart;
    GraphicsContext* gfxCtx = gPlayState->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    if (info->scale > 0.0f) {
        Matrix_Scale(info->scale, info->scale, info->scale, MTXMODE_APPLY);
    }
    if (xs > 0) {
        // OOT's own setup when the row uses one other than 25 (masks/bombchu/medallions = 26, which
        // is 1-CYCLE without fog). Under MM's 2-cycle 25 those lists' duplicated second cycle wins
        // and samples TEXEL1 — whatever tile MM last bound — instead of the item's own texture.
        if (info->setupDlOpa != nullptr) {
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->setupDlOpa);
        } else {
            Gfx_SetupDL25_Opa(gfxCtx);
        }
        MM_FOREIGN_PIN_OPA();
        MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, gfxCtx);
        if (info->hasEnvColor) {
            gDPSetEnvColor(POLY_OPA_DISP++, info->envColor[0], info->envColor[1], info->envColor[2], info->envColor[3]);
        }
        for (int32_t i = 0; i < xs; i++) {
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[i]);
        }
    }
    if (xs < n) {
        if (info->setupDlXlu != nullptr) { // sold-out sign / compass glass: setup 5, not 25
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->setupDlXlu);
        } else {
            Gfx_SetupDL25_Xlu(gfxCtx);
        }
        MM_FOREIGN_PIN_XLU();
        MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
        if (info->hasEnvColor) {
            gDPSetEnvColor(POLY_XLU_DISP++, info->envColor[0], info->envColor[1], info->envColor[2], info->envColor[3]);
        }
        for (int32_t i = xs; i < n; i++) {
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[i]);
        }
    }
    CLOSE_DISPS(gfxCtx);
}

// Biggoron's / Broken Goron's Sword: seg8 OPA scroll (z_draw.c GetItem_DrawGoronSword).
inline void MM_DrawForeignGoronSword(const ComboForeignDrawInfoOOT* info) {
    PlayState* play = gPlayState;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Opa(gfxCtx);
    MM_FOREIGN_PIN_OPA();
    gSPSegment(POLY_OPA_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(gfxCtx, G_TX_RENDERTILE, play->state.frames * 1, play->state.frames * 0,
                                             32, 32, 1, play->state.frames * 0, play->state.frames * 0, 32, 32, 1, 0, 0,
                                             0));
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[0]);
    CLOSE_DISPS(gfxCtx);
    int32_t segs[] = { 0x08 };
    MM_RestoreForeignSegs(segs, 1);
}

// Deku Nuts: seg8 OPA scroll (GetItem_DrawDekuNuts).
inline void MM_DrawForeignDekuNuts(const ComboForeignDrawInfoOOT* info) {
    PlayState* play = gPlayState;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Opa(gfxCtx);
    MM_FOREIGN_PIN_OPA();
    gSPSegment(POLY_OPA_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(gfxCtx, G_TX_RENDERTILE, play->state.frames * 6, play->state.frames * 6,
                                             32, 32, 1, play->state.frames * 6, play->state.frames * 6, 32, 32, 6, 6, 6,
                                             6));
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[0]);
    CLOSE_DISPS(gfxCtx);
    int32_t segs[] = { 0x08 };
    MM_RestoreForeignSegs(segs, 1);
}

// Recovery Heart: seg8 XLU scroll (GetItem_DrawRecoveryHeart; cosmetic grayscale recolor omitted).
inline void MM_DrawForeignRecoveryHeart(const ComboForeignDrawInfoOOT* info) {
    PlayState* play = gPlayState;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Xlu(gfxCtx);
    MM_FOREIGN_PIN_XLU();
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(gfxCtx, G_TX_RENDERTILE, play->state.frames * 0, -(play->state.frames * 3),
                                             32, 32, 1, play->state.frames * 0, -(play->state.frames * 2), 32, 32, 0,
                                             -3, 0, -2));
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[0]);
    CLOSE_DISPS(gfxCtx);
    int32_t segs[] = { 0x08 };
    MM_RestoreForeignSegs(segs, 1);
}

// Fish: seg8 XLU scroll (GetItem_DrawFish).
inline void MM_DrawForeignFish(const ComboForeignDrawInfoOOT* info) {
    PlayState* play = gPlayState;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Xlu(gfxCtx);
    MM_FOREIGN_PIN_XLU();
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(gfxCtx, G_TX_RENDERTILE, play->state.frames * 0, play->state.frames * 1,
                                             32, 32, 1, play->state.frames * 0, play->state.frames * 1, 32, 32, 0, 1, 0,
                                             1));
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[0]);
    CLOSE_DISPS(gfxCtx);
    int32_t segs[] = { 0x08 };
    MM_RestoreForeignSegs(segs, 1);
}

// Potions: seg8 OPA scroll, OPA dl[1,0,2,3] + XLU dl[4,5] (GetItem_DrawPotion).
inline void MM_DrawForeignPotion(const ComboForeignDrawInfoOOT* info) {
    PlayState* play = gPlayState;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Opa(gfxCtx);
    MM_FOREIGN_PIN_OPA();
    gSPSegment(POLY_OPA_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(gfxCtx, G_TX_RENDERTILE, -play->state.frames, play->state.frames, 32, 32,
                                             1, -play->state.frames, play->state.frames, 32, 32, -1, 1, -1, 1));
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[1]);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[0]);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[2]);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[3]);
    Gfx_SetupDL25_Xlu(gfxCtx);
    MM_FOREIGN_PIN_XLU();
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[4]);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[5]);
    CLOSE_DISPS(gfxCtx);
    int32_t segs[] = { 0x08 };
    MM_RestoreForeignSegs(segs, 1);
}

// Mirror Shield: seg8 OPA scroll, OPA dl0 + XLU dl1 (GetItem_DrawMirrorShield).
inline void MM_DrawForeignMirrorShield(const ComboForeignDrawInfoOOT* info) {
    PlayState* play = gPlayState;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Opa(gfxCtx);
    MM_FOREIGN_PIN_OPA();
    gSPSegment(POLY_OPA_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(gfxCtx, G_TX_RENDERTILE, 0, play->state.frames * 2 % 256, 64, 64, 1, 0,
                                             play->state.frames * 1 % 128, 32, 32, 0, 2, 0, 1));
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[0]);
    Gfx_SetupDL25_Xlu(gfxCtx);
    MM_FOREIGN_PIN_XLU();
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[1]);
    CLOSE_DISPS(gfxCtx);
    int32_t segs[] = { 0x08 };
    MM_RestoreForeignSegs(segs, 1);
}

// Blue Fire: OPA dl0; XLU seg8 flame scroll + billboard dl1 (GetItem_DrawBlueFire).
inline void MM_DrawForeignBlueFire(const ComboForeignDrawInfoOOT* info) {
    PlayState* play = gPlayState;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Opa(gfxCtx);
    MM_FOREIGN_PIN_OPA();
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[0]);
    Gfx_SetupDL25_Xlu(gfxCtx);
    MM_FOREIGN_PIN_XLU();
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(gfxCtx, G_TX_RENDERTILE, 0, 0, 16, 32, 1, play->state.frames * 1,
                                             -(play->state.frames * 8), 16, 32, 0, 0, 1, -8));
    Matrix_Push();
    Matrix_Translate(-8.0f, -2.0f, 0.0f, MTXMODE_APPLY);
    Matrix_ReplaceRotation(&play->billboardMtxF);
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[1]);
    Matrix_Pop();
    CLOSE_DISPS(gfxCtx);
    int32_t segs[] = { 0x08 };
    MM_RestoreForeignSegs(segs, 1);
}

// Poe / Big Poe: OPA dl0; XLU dl1; seg8 scroll; billboard dl3,dl2 (GetItem_DrawPoes).
inline void MM_DrawForeignPoes(const ComboForeignDrawInfoOOT* info) {
    PlayState* play = gPlayState;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Opa(gfxCtx);
    MM_FOREIGN_PIN_OPA();
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[0]);
    Gfx_SetupDL25_Xlu(gfxCtx);
    MM_FOREIGN_PIN_XLU();
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[1]);
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(gfxCtx, G_TX_RENDERTILE, 0, 0, 16, 32, 1, play->state.frames * 1,
                                             -(play->state.frames * 6), 16, 32, 0, 0, 1, -6));
    Matrix_Push();
    Matrix_ReplaceRotation(&play->billboardMtxF);
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[3]);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[2]);
    Matrix_Pop();
    CLOSE_DISPS(gfxCtx);
    int32_t segs[] = { 0x08 };
    MM_RestoreForeignSegs(segs, 1);
}

// Fairy (bottled): OPA dl0; XLU dl1; seg8 scroll; billboard dl2 (GetItem_DrawFairy).
inline void MM_DrawForeignFairy(const ComboForeignDrawInfoOOT* info) {
    PlayState* play = gPlayState;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Opa(gfxCtx);
    MM_FOREIGN_PIN_OPA();
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[0]);
    Gfx_SetupDL25_Xlu(gfxCtx);
    MM_FOREIGN_PIN_XLU();
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[1]);
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(gfxCtx, G_TX_RENDERTILE, 0, 0, 32, 32, 1, play->state.frames * 1,
                                             -(play->state.frames * 6), 32, 32, 0, 0, 1, -6));
    Matrix_Push();
    Matrix_ReplaceRotation(&play->billboardMtxF);
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[2]);
    Matrix_Pop();
    CLOSE_DISPS(gfxCtx);
    int32_t segs[] = { 0x08 };
    MM_RestoreForeignSegs(segs, 1);
}

// Spiritual stones: seg9 XLU + seg8 OPA (static binds), rotate, per-layer prim/env colors, gem dl0
// (XLU) + setting dl1 (OPA) (GetItem_DrawJewel + the Kokiri/Goron/Zora color wrappers).
inline void MM_DrawForeignJewel(const ComboForeignDrawInfoOOT* info) {
    PlayState* play = gPlayState;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    gSPSegment(POLY_XLU_DISP++, 9,
               (uintptr_t)Gfx_TwoTexScrollEx(gfxCtx, 0, 0 % 256, (256 - (0 % 256)) - 1, 64, 64, 1, 0 % 256,
                                             (256 - (0 % 256)) - 1, 16, 16, 0, 0, 0, 0));
    gSPSegment(POLY_OPA_DISP++, 8, (uintptr_t)Gfx_TexScrollEx(gfxCtx, 0, 0, 16, 16, 0, 0));
    Matrix_Push();
    Matrix_RotateZYX(0, -0x4000, 0x4000, MTXMODE_APPLY);
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, gfxCtx);
    Gfx_SetupDL25_Xlu(gfxCtx);
    MM_FOREIGN_PIN_XLU();
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 128, info->primColorXlu[0], info->primColorXlu[1], info->primColorXlu[2], 255);
    gDPSetEnvColor(POLY_XLU_DISP++, info->envColorXlu[0], info->envColorXlu[1], info->envColorXlu[2], 255);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[0]);
    Gfx_SetupDL25_Opa(gfxCtx);
    MM_FOREIGN_PIN_OPA();
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 128, info->primColorOpa[0], info->primColorOpa[1], info->primColorOpa[2], 255);
    gDPSetEnvColor(POLY_OPA_DISP++, info->envColorOpa[0], info->envColorOpa[1], info->envColorOpa[2], 255);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[1]);
    Matrix_Pop();
    CLOSE_DISPS(gfxCtx);
    int32_t segs[] = { 0x08, 0x09 };
    MM_RestoreForeignSegs(segs, 2);
}

// Din's Fire / Farore's Wind / Nayru's Love: XLU seg8 scroll, dl0,1,2 (GetItem_DrawMagicSpell).
inline void MM_DrawForeignMagicSpell(const ComboForeignDrawInfoOOT* info) {
    PlayState* play = gPlayState;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Xlu(gfxCtx);
    MM_FOREIGN_PIN_XLU();
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(gfxCtx, G_TX_RENDERTILE, play->state.frames * 2, -(play->state.frames * 6),
                                             32, 32, 1, play->state.frames * 1, -(play->state.frames * 2), 32, 32, 2,
                                             -6, 1, -2));
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[0]);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[1]);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[2]);
    CLOSE_DISPS(gfxCtx);
    int32_t segs[] = { 0x08 };
    MM_RestoreForeignSegs(segs, 1);
}

// Silver / Gold Scale: XLU seg8 scroll, dl2,3,1,0 (GetItem_DrawScale).
inline void MM_DrawForeignScale(const ComboForeignDrawInfoOOT* info) {
    PlayState* play = gPlayState;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Xlu(gfxCtx);
    MM_FOREIGN_PIN_XLU();
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(gfxCtx, G_TX_RENDERTILE, play->state.frames * 2, -(play->state.frames * 2),
                                             64, 64, 1, play->state.frames * 4, -(play->state.frames * 4), 32, 32, 2,
                                             -2, 4, -4));
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[2]);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[3]);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[1]);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[0]);
    CLOSE_DISPS(gfxCtx);
    int32_t segs[] = { 0x08 };
    MM_RestoreForeignSegs(segs, 1);
}

// Skulltula Token: body OPA dl0 + XLU seg8 flame dl1 (GetItem_DrawSkullToken, full flame).
inline void MM_DrawForeignSkullToken(const ComboForeignDrawInfoOOT* info) {
    PlayState* play = gPlayState;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Opa(gfxCtx);
    MM_FOREIGN_PIN_OPA();
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[0]);
    Gfx_SetupDL25_Xlu(gfxCtx);
    MM_FOREIGN_PIN_XLU();
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(gfxCtx, G_TX_RENDERTILE, play->state.frames * 0, -(play->state.frames * 5),
                                             32, 32, 1, play->state.frames * 0, play->state.frames * 0, 32, 64, 0, -5,
                                             0, 0));
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[1]);
    CLOSE_DISPS(gfxCtx);
    int32_t segs[] = { 0x08 };
    MM_RestoreForeignSegs(segs, 1);
}

// Generic rando song note: grayscale-tinted note DL (GetItem_DrawGenericMusicNote). No segments.
inline void MM_DrawForeignMusicNote(const ComboForeignDrawInfoOOT* info) {
    GraphicsContext* gfxCtx = gPlayState->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gDPSetGrayscaleColor(POLY_XLU_DISP++, info->primColorXlu[0], info->primColorXlu[1], info->primColorXlu[2], 255);
    gSPGrayscale(POLY_XLU_DISP++, true);
    Gfx_SetupDL25_Opa(gfxCtx); // OOT's func really does set up Opa state for an XLU submission
    MM_FOREIGN_PIN_XLU();      // pin the stream the DL goes out on, not the setup's
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[0]);
    gSPGrayscale(POLY_XLU_DISP++, false);
    CLOSE_DISPS(gfxCtx);
}

// OOT boss soul: seg8 flame scroll + billboard, grayscale-colored flame dl0, then generic skull dl1
// with env color (Randomizer_DrawBossSoul, SimplerBossSoulModels path — no boss skeleton cross-game).
inline void MM_DrawForeignBossSoul(const ComboForeignDrawInfoOOT* info) {
    PlayState* play = gPlayState;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Xlu(gfxCtx);
    MM_FOREIGN_PIN_XLU();
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(gfxCtx, G_TX_RENDERTILE, 0, 0, 16, 32, 1, play->state.frames * 1,
                                             -(play->state.frames * 8), 16, 32, 0, 0, 1, -8));
    Matrix_Push();
    Matrix_Translate(0.0f, -70.0f, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(5.0f, 5.0f, 5.0f, MTXMODE_APPLY);
    Matrix_ReplaceRotation(&play->billboardMtxF);
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gDPSetGrayscaleColor(POLY_XLU_DISP++, info->primColorXlu[0], info->primColorXlu[1], info->primColorXlu[2], 255);
    gSPGrayscale(POLY_XLU_DISP++, true);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[0]); // flame
    gSPGrayscale(POLY_XLU_DISP++, false);
    Matrix_Pop();
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gDPSetEnvColor(POLY_XLU_DISP++, info->envColorXlu[0], info->envColorXlu[1], info->envColorXlu[2], 255);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[1]); // generic soul skull
    CLOSE_DISPS(gfxCtx);
    int32_t segs[] = { 0x08 };
    MM_RestoreForeignSegs(segs, 1);
}

// Per-DL prim/env colored layers: the rando map/compass/small-key/boss-key/key-ring/jabber-nut/
// bombchu-bag/overworld-key funcs, which only differ in which DLs they tint and with what. Rows
// authored for another setup (26 Opa, 5 Xlu) carry it in the recipe and it is submitted below.
inline void MM_DrawForeignColorLayers(const ComboForeignDrawInfoOOT* info) {
    int32_t n = info->count;
    int32_t xs = (info->xluStart < 0 || info->xluStart > n) ? n : info->xluStart;
    GraphicsContext* gfxCtx = gPlayState->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    if (xs > 0) {
        if (info->setupDlOpa != nullptr) { // Jabber Nut / Bombchu Bag: 26 Opa, 1-cycle
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->setupDlOpa);
        } else {
            Gfx_SetupDL25_Opa(gfxCtx);
        }
        MM_FOREIGN_PIN_OPA();
        MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, gfxCtx);
        for (int32_t i = 0; i < xs; i++) {
            if (info->layerPrimMask & (1 << i)) {
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, info->layerPrimColor[i][0], info->layerPrimColor[i][1],
                                info->layerPrimColor[i][2], info->layerPrimColor[i][3]);
            }
            if (info->layerEnvMask & (1 << i)) {
                gDPSetEnvColor(POLY_OPA_DISP++, info->layerEnvColor[i][0], info->layerEnvColor[i][1],
                               info->layerEnvColor[i][2], info->layerEnvColor[i][3]);
            }
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[i]);
        }
    }
    if (xs < n) {
        if (info->setupDlXlu != nullptr) { // compass glass: 5 Xlu
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->setupDlXlu);
        } else {
            Gfx_SetupDL25_Xlu(gfxCtx);
        }
        MM_FOREIGN_PIN_XLU();
        MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
        for (int32_t i = xs; i < n; i++) {
            if (info->layerPrimMask & (1 << i)) {
                gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, info->layerPrimColor[i][0], info->layerPrimColor[i][1],
                                info->layerPrimColor[i][2], info->layerPrimColor[i][3]);
            }
            if (info->layerEnvMask & (1 << i)) {
                gDPSetEnvColor(POLY_XLU_DISP++, info->layerEnvColor[i][0], info->layerEnvColor[i][1],
                               info->layerEnvColor[i][2], info->layerEnvColor[i][3]);
            }
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[i]);
        }
    }
    CLOSE_DISPS(gfxCtx);
}

// Grayscale-tinted XLU glyph: ocarina buttons (Randomizer_DrawOcarinaButton). No segments.
inline void MM_DrawForeignGrayscaleXlu(const ComboForeignDrawInfoOOT* info) {
    GraphicsContext* gfxCtx = gPlayState->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Xlu(gfxCtx);
    MM_FOREIGN_PIN_XLU();
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gDPSetGrayscaleColor(POLY_XLU_DISP++, info->primColorXlu[0], info->primColorXlu[1], info->primColorXlu[2], 255);
    gSPGrayscale(POLY_XLU_DISP++, true);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[0]);
    gSPGrayscale(POLY_XLU_DISP++, false);
    CLOSE_DISPS(gfxCtx);
}

// Double Defense: grayscale-white heart border dl0, then the plain container dl1 (both XLU).
inline void MM_DrawForeignDoubleDefense(const ComboForeignDrawInfoOOT* info) {
    GraphicsContext* gfxCtx = gPlayState->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Xlu(gfxCtx);
    MM_FOREIGN_PIN_XLU();
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gDPSetGrayscaleColor(POLY_XLU_DISP++, 255, 255, 255, 255);
    gSPGrayscale(POLY_XLU_DISP++, true);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[0]);
    gSPGrayscale(POLY_XLU_DISP++, false);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[1]);
    CLOSE_DISPS(gfxCtx);
}

// Master Sword: seg8 OPA scroll + fixed scale/rotation (Randomizer_DrawMasterSword).
inline void MM_DrawForeignMasterSword(const ComboForeignDrawInfoOOT* info) {
    PlayState* play = gPlayState;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Opa(gfxCtx);
    MM_FOREIGN_PIN_OPA();
    gSPSegment(
        POLY_OPA_DISP++, 0x08,
        (uintptr_t)Gfx_TwoTexScrollEx(gfxCtx, 0, play->state.frames * 1, 0, 32, 32, 1, 0, 0, 32, 32, 1, 0, 0, 0));
    Matrix_Scale(0.05f, 0.05f, 0.05f, MTXMODE_APPLY);
    Matrix_RotateZF(2.1f, MTXMODE_APPLY);
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[0]);
    CLOSE_DISPS(gfxCtx);
    int32_t segs[] = { 0x08 };
    MM_RestoreForeignSegs(segs, 1);
}

// Bronze Scale: the scale model on the SCALE seg8 scroll, recolored bronze. The OOT func's two color
// DLs are inline Gfx arrays (no OTR resource), so the prim/env pairs are emitted here verbatim.
inline void MM_DrawForeignBronzeScale(const ComboForeignDrawInfoOOT* info) {
    PlayState* play = gPlayState;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL25_Xlu(gfxCtx);
    MM_FOREIGN_PIN_XLU();
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(gfxCtx, 0, play->state.frames * 2, -(play->state.frames * 2), 64, 64, 1,
                                             play->state.frames * 4, -(play->state.frames * 4), 32, 32, 2, -2, 4, -4));
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gDPPipeSync(POLY_XLU_DISP++);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 255, 255, 255, 255);
    gDPSetEnvColor(POLY_XLU_DISP++, 91, 51, 18, 255);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[0]);
    gDPPipeSync(POLY_XLU_DISP++);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x60, 255, 255, 255, 255);
    gDPSetEnvColor(POLY_XLU_DISP++, 255, 123, 0, 255);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[1]);
    CLOSE_DISPS(gfxCtx);
    int32_t segs[] = { 0x08 };
    MM_RestoreForeignSegs(segs, 1);
}

// Draw a foreign (OOT-bound) item's real OOT model at the current model matrix. Any resolution
// failure falls back to the sentinel blue rupee (the RI_COMBO_FOREIGN item's GID_RUPEE_BLUE), so we
// never draw blank. Mirrors Randomizer_DrawComboForeign (soh/.../draw.cpp).
inline void MM_DrawComboForeign(RandoCheckId randoCheckId) {
    const ComboForeignDrawInfoOOT* info =
        (randoCheckId != RC_UNKNOWN) ? ComboResolveForeignDrawInfoOOT(randoCheckId) : nullptr;
    if (info == nullptr) {
        GetItem_Draw(gPlayState, GID_RUPEE_BLUE);
        return;
    }

    // ComboShip: animated class — combo-owned skeletal draw (any failure -> sentinel, never blank).
    if (info->animOk) {
        if (!ComboForeignAnim_Draw(&info->anim, "oot", gPlayState)) {
            GetItem_Draw(gPlayState, GID_RUPEE_BLUE);
        }
        return;
    }

    switch (info->drawKind) {
        case CW_DRAW_KIND_GORON_SWORD:
            MM_DrawForeignGoronSword(info);
            break;
        case CW_DRAW_KIND_DEKU_NUTS:
            MM_DrawForeignDekuNuts(info);
            break;
        case CW_DRAW_KIND_RECOVERY_HEART:
            MM_DrawForeignRecoveryHeart(info);
            break;
        case CW_DRAW_KIND_FISH:
            MM_DrawForeignFish(info);
            break;
        case CW_DRAW_KIND_POTION:
            MM_DrawForeignPotion(info);
            break;
        case CW_DRAW_KIND_MIRROR_SHIELD:
            MM_DrawForeignMirrorShield(info);
            break;
        case CW_DRAW_KIND_BLUE_FIRE:
            MM_DrawForeignBlueFire(info);
            break;
        case CW_DRAW_KIND_POES:
            MM_DrawForeignPoes(info);
            break;
        case CW_DRAW_KIND_FAIRY:
            MM_DrawForeignFairy(info);
            break;
        case CW_DRAW_KIND_JEWEL:
            MM_DrawForeignJewel(info);
            break;
        case CW_DRAW_KIND_MAGIC_SPELL:
            MM_DrawForeignMagicSpell(info);
            break;
        case CW_DRAW_KIND_SCALE:
            MM_DrawForeignScale(info);
            break;
        case CW_DRAW_KIND_SKULL_TOKEN:
            MM_DrawForeignSkullToken(info);
            break;
        case CW_DRAW_KIND_MUSIC_NOTE:
            MM_DrawForeignMusicNote(info);
            break;
        case CW_DRAW_KIND_BOSS_SOUL:
            MM_DrawForeignBossSoul(info);
            break;
        case CW_DRAW_KIND_COLOR_LAYERS:
            MM_DrawForeignColorLayers(info);
            break;
        case CW_DRAW_KIND_GRAYSCALE_XLU:
            MM_DrawForeignGrayscaleXlu(info);
            break;
        case CW_DRAW_KIND_DOUBLE_DEFENSE:
            MM_DrawForeignDoubleDefense(info);
            break;
        case CW_DRAW_KIND_MASTER_SWORD:
            MM_DrawForeignMasterSword(info);
            break;
        case CW_DRAW_KIND_BRONZE_SCALE:
            MM_DrawForeignBronzeScale(info);
            break;
        case CW_DRAW_KIND_SIMPLE:
        default:
            MM_DrawForeignSimple(info);
            break;
    }
}

#undef MM_FOREIGN_PIN_OPA
#undef MM_FOREIGN_PIN_XLU

#endif // COMBO_FOREIGN_DRAW_MM_H
