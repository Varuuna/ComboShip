/* combo/menu/ComboForeignDrawOOT.h — ComboShip: cross-game foreign-item rendering, OOT (host) side.
 * The exact mirror of combo/menu/ComboForeignDrawMM.h in the opposite direction: an OOT check holding
 * RG_COMBO_FOREIGN actually holds an MM item, so we render the REAL MM model by asking 2ship.dll
 * (MM_GetItemDrawInfo, C ABI in combo/menu/ComboItemDrawABI.h) which display lists draw it, then
 * submitting them as "__OTR__@mm:"-routed paths that the shared Fast3D interpreter resolves against
 * MM's ResourceManager (CrossRMRegistry + scoped routing — see libultraship interpreter.cpp).
 *
 * All foreign checks share one sentinel entry, so the check identity rides in the entry itself:
 * Context::GetFinalGIEntry stamps comboForeignCheck (ItemTableTypes.h) into every entry it returns,
 * which covers every draw path that knows its check (shop shelves, freestanding, chests, queued
 * get-item, ...). Entries built without a check carry RC_UNKNOWN_CHECK and degrade to the sentinel.
 *
 * TU-GLUE HEADER (menu-extraction pattern): include ONCE from
 * soh/soh/Enhancements/randomizer/draw.cpp, inside its #ifdef COMBO_BUILD, AFTER the engine headers
 * (OPEN_DISPS, Gfx_SetupDL_25Opa/Xlu, Matrix_*, gbi macros, gPlayState, gSaveContext, GetItem_Draw)
 * and after ComboForeignAnim.h. Not standalone.
 */
#ifndef COMBO_FOREIGN_DRAW_OOT_H
#define COMBO_FOREIGN_DRAW_OOT_H

#ifndef OPEN_DISPS
#error "ComboForeignDrawOOT.h is TU-glue: include the host engine headers before it"
#endif

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "ComboItemDrawABI.h"
#include "soh/Enhancements/randomizer/hook_handlers.h" // OOT_LookupForeign / OOT_GetQueuedDrawCheck
#include "soh/Enhancements/randomizer/static_data.h"

namespace {
struct ComboForeignDrawInfo {
    bool ok = false;
    int32_t count = 0;
    int32_t xluStart = -1;    // first XLU entry in dls[] order; -1 = all OPA
    float scale = 0.0f;       // extra model scale; 0 = none (MM remains: 0.02)
    bool hasEnvColor = false; // emit env color before the DLs (MM song notes)
    uint8_t envColor[4] = { 0, 0, 0, 0 };
    bool xluSeg8TexScroll = false;          // bind segment 8 to the flame texscroll before the XLU layer (skull token)
    const char* matAnimPath = nullptr;      // MM TextureAnimation resource to replicate before the DLs (Moon's Tear)
    bool matAnimBindOpa = false;            // also bind the animated segment on the OPA layer (item body samples it)
    bool matAnimBillboard = false;          // Matrix_ReplaceRotation(billboardMtxF) before the XLU layer (glow)
    int32_t drawKind = CW_DRAW_KIND_SIMPLE; // non-SIMPLE = replicate a specific MM draw func
    uint8_t primColorXlu[4] = { 0, 0, 0, 0 }; // MM_SOUL_FLAME color
    int32_t opCount = 0;                      // CW_DRAW_KIND_OPS payload
    CwDrawOp ops[CW_DRAW_MAX_OPS] = {};
    const char* dls[CW_DRAW_MAX_DLISTS] = { nullptr }; // interned "__OTR__@mm:..." routed paths
    // MM's own setup DL for each stream (raw Gfx* in 2ship.dll), or null for our 25 Opa/Xlu.
    const void* setupDlOpa = nullptr;
    const void* setupDlXlu = nullptr;
    // ComboShip: animated class (no static DL row — MM stray fairies). When animOk, anim describes
    // the item and ComboForeignAnim_Draw renders it; path strings point at 2ship.dll statics.
    bool animOk = false;
    CwItemAnimDrawInfo anim{};
    // Recipe chosen from live save state (progressive tier, Triforce shard, junk/trap) — re-resolve
    // every frame instead of caching, or the first model drawn sticks for the whole save slot.
    bool stateDependent = false;
};

// Routed path strings must outlive the frame (the GBI wrapper emits the raw pointer into the
// display list; the interpreter dereferences it later), so intern them for the process lifetime.
inline const char* ComboInternRoutedPath(const std::string& s) {
    static std::unordered_set<std::string> sPool; // node-based: c_str() stable across rehash
    return sPool.insert(s).first->c_str();
}

// One resolution attempt's outcome. NotReady = a producer/lookup that simply isn't up yet (2ship.dll
// not resident, foreign map not built, MM's rando state null), which must NEVER be negative-cached.
enum class ComboForeignResolve { Ok, Unknown, NotReady };

inline ComboForeignResolve ComboFillForeignDrawInfo(RandomizerCheck rc, int slot, ComboForeignDrawInfo& info) {
    const std::string checkName = Rando::StaticData::GetLocation(rc)->GetName();
    const ComboRando::ForeignItem* fi = OOT_LookupForeign(slot, checkName);
    if (fi == nullptr || fi->itemGame != ComboRando::GAME_MM) {
        return ComboForeignResolve::Unknown;
    }

#ifdef _WIN32
    static Fn_GetItemDrawInfo sGetItemDrawInfo = nullptr;
    if (sGetItemDrawInfo == nullptr) {
        HMODULE h = GetModuleHandleA("2ship.dll"); // already loaded by the exe (ComboMenuModel pattern)
        sGetItemDrawInfo = h ? (Fn_GetItemDrawInfo)GetProcAddress(h, "MM_GetItemDrawInfo") : nullptr;
    }
    if (sGetItemDrawInfo == nullptr) {
        return ComboForeignResolve::NotReady; // 2ship.dll may simply not be resident yet
    }
    // A disguised trap must draw the item it pretends to be. Same namespace, so the itemGame dispatch
    // above is unaffected. Not state-dependent: like OOT, the disguise holds until the get-item cutscene.
    const char* drawName = fi->HasDisguise() ? fi->fakeItemName.c_str() : fi->itemName.c_str();
    CwItemDrawInfo raw{};
    int32_t rcStatic = sGetItemDrawInfo(drawName, &raw);
    if (rcStatic == CW_DRAW_NOT_READY) {
        return ComboForeignResolve::NotReady; // MM's rando state isn't up — retry, don't freeze
    }
    if (rcStatic == 0 || raw.dlistCount <= 0) {
        // ComboShip: no static DL row — try the animated ABI (MM stray fairies). MM only describes
        // the item; ComboForeignAnim_Draw (combo/menu/ComboForeignAnim.h) loads + draws it.
        static Fn_GetItemAnimDrawInfo sGetItemAnimDrawInfo = nullptr;
        if (sGetItemAnimDrawInfo == nullptr) {
            HMODULE h = GetModuleHandleA("2ship.dll");
            sGetItemAnimDrawInfo = h ? (Fn_GetItemAnimDrawInfo)GetProcAddress(h, "MM_GetItemAnimDrawInfo") : nullptr;
        }
        if (sGetItemAnimDrawInfo == nullptr) {
            return ComboForeignResolve::NotReady;
        }
        int32_t rcAnim = sGetItemAnimDrawInfo(drawName, &info.anim);
        if (rcAnim == CW_DRAW_NOT_READY) {
            return ComboForeignResolve::NotReady;
        }
        if (rcAnim != 0) {
            info.animOk = true;
            info.stateDependent = info.anim.stateDependent != 0;
            info.ok = true;
            return ComboForeignResolve::Ok;
        }
        return ComboForeignResolve::Unknown; // unknown item or non-portable draw func -> sentinel
    }

    static constexpr char kOtrPrefix[] = "__OTR__";
    int32_t n = raw.dlistCount < CW_DRAW_MAX_DLISTS ? raw.dlistCount : CW_DRAW_MAX_DLISTS;
    if (n < CwMinDlistsForKind(raw.drawKind)) {
        return ComboForeignResolve::Unknown; // handler would blind-index a missing slot
    }
    for (int32_t i = 0; i < n; i++) {
        const char* p = raw.dlists[i];
        if (p == nullptr || strncmp(p, kOtrPrefix, sizeof(kOtrPrefix) - 1) != 0) {
            return ComboForeignResolve::Unknown; // not an OTR path literal — can't route it
        }
        info.dls[i] = ComboInternRoutedPath(std::string("__OTR__@mm:") + (p + sizeof(kOtrPrefix) - 1));
    }
    info.count = n;
    info.xluStart = raw.xluStartIndex;
    info.scale = raw.scale;
    info.setupDlOpa = raw.setupDlOpa;
    info.setupDlXlu = raw.setupDlXlu;
    info.hasEnvColor = raw.hasEnvColor != 0;
    info.xluSeg8TexScroll = raw.xluSeg8TexScroll != 0;
    info.matAnimPath = raw.matAnimPath; // 2ship static literal (process-lifetime); loaded, not emitted
    info.matAnimBindOpa = raw.matAnimBindOpa != 0;
    info.matAnimBillboard = raw.matAnimBillboard != 0;
    info.stateDependent = raw.stateDependent != 0;
    info.drawKind = raw.drawKind;
    info.opCount = raw.opCount < CW_DRAW_MAX_OPS ? raw.opCount : CW_DRAW_MAX_OPS;
    memcpy(info.ops, raw.ops, sizeof(info.ops));
    for (int32_t i = 0; i < 4; i++) {
        info.envColor[i] = raw.envColor[i];
        info.primColorXlu[i] = raw.primColorXlu[i];
    }
    info.ok = true;
    return ComboForeignResolve::Ok;
#else
    return ComboForeignResolve::Unknown; // GetProcAddress resolution is Windows-only (ComboMenuModel)
#endif
}

// Full lookup chain (foreign map -> MM export -> routed strings), cached per check per slot per
// foreign-map generation so it runs once per check instead of every frame.
inline const ComboForeignDrawInfo* ComboResolveForeignDrawInfo(RandomizerCheck rc) {
    static std::unordered_map<int32_t, ComboForeignDrawInfo> sCache;
    static int sCacheSlot = -1;
    static uint64_t sCacheGen = (uint64_t)-1;
    int slot = gSaveContext.fileNum;
    uint64_t gen = OOT_ForeignMapGen();
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
    ComboForeignDrawInfo info{}; // built locally: a failure must not clobber a live cached recipe
    if (ComboFillForeignDrawInfo(rc, slot, info) == ComboForeignResolve::NotReady) {
        sCache.erase(rc); // transient — retry next frame instead of freezing the sentinel in
        return nullptr;
    }
    ComboForeignDrawInfo& entry = sCache[rc]; // Unknown caches ok=false: one lookup, then sentinel
    entry = info;
    return entry.ok ? &entry : nullptr;
}
} // namespace

// ---- Non-portable MM draw funcs. Each handler is a 1:1 port of the MM get-item func
// (mm/src/code/z_draw.c) or bespoke Rando draw func (mm/2s2h/Rando/DrawItem.cpp, DrawFuncs.cpp),
// re-binding the segment(s) it needs in OOT's frame with OOT's own gbi BEFORE submitting the routed
// @mm: DLs, then restoring them. Where MM's func is identical to its OOT twin the shared CwDrawKind
// is reused, so these are ports of MM's code that happen to read like OOT's. Free (non-namespaced)
// inline functions so the OPEN_DISPS block-scope decls keep C linkage — see ComboForeignAnim.h.

// Restore the segments a handler bound to a benign empty DL so later same-frame commands don't
// sample our scroll DLs. Mirrors MM_RestoreForeignSegs / the hygiene in ComboForeignAnim.h.
inline void OOT_RestoreForeignSegs(PlayState* play, const int32_t* segs, int32_t count) {
    // Array of ENDDLs: DLs may call through a bound segment at an index > 0 — see CfaEmptyDL.
    Gfx* empty = (Gfx*)Graph_Alloc(play->state.gfxCtx, 8 * sizeof(Gfx));
    Gfx* e = empty;
    for (int32_t iEmpty = 0; iEmpty < 8; iEmpty++) {
        gSPEndDisplayList(e++);
    }
    OPEN_DISPS(play->state.gfxCtx);
    for (int32_t i = 0; i < count; i++) {
        gSPSegment(POLY_OPA_DISP++, segs[i], (uintptr_t)empty);
        gSPSegment(POLY_XLU_DISP++, segs[i], (uintptr_t)empty);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

#define COMBO_FOREIGN_MTX(disp) \
    gSPMatrix(disp, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__), G_MTX_MODELVIEW | G_MTX_LOAD)

// Pin prim+env to white right after a setup DL, before the handler's own colour commands: a layer
// the recipe doesn't tint must not inherit whatever the host frame last set.
#define OOT_FOREIGN_PIN_OPA()                                       \
    do {                                                            \
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255); \
        gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);        \
    } while (0)
#define OOT_FOREIGN_PIN_XLU()                                       \
    do {                                                            \
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, 255); \
        gDPSetEnvColor(POLY_XLU_DISP++, 255, 255, 255, 255);        \
    } while (0)

// Biggoron's Sword: seg8 OPA scroll (GetItem_DrawGoronSword).
inline void OOT_DrawForeignGoronSword(PlayState* play, const ComboForeignDrawInfo* info) {
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    OOT_FOREIGN_PIN_OPA();
    gSPSegment(POLY_OPA_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, G_TX_RENDERTILE, play->state.frames * 1,
                                             play->state.frames * 0, 32, 32, 1, play->state.frames * 0,
                                             play->state.frames * 0, 32, 32, 1, 0, 0, 0));
    COMBO_FOREIGN_MTX(POLY_OPA_DISP++);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[0]);
    CLOSE_DISPS(play->state.gfxCtx);
    int32_t segs[] = { 0x08 };
    OOT_RestoreForeignSegs(play, segs, 1);
}

// Deku Nuts: seg8 OPA scroll (GetItem_DrawDekuNuts).
inline void OOT_DrawForeignDekuNuts(PlayState* play, const ComboForeignDrawInfo* info) {
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    OOT_FOREIGN_PIN_OPA();
    gSPSegment(POLY_OPA_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, G_TX_RENDERTILE, play->state.frames * 6,
                                             play->state.frames * 6, 32, 32, 1, play->state.frames * 6,
                                             play->state.frames * 6, 32, 32, 6, 6, 6, 6));
    COMBO_FOREIGN_MTX(POLY_OPA_DISP++);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[0]);
    CLOSE_DISPS(play->state.gfxCtx);
    int32_t segs[] = { 0x08 };
    OOT_RestoreForeignSegs(play, segs, 1);
}

// Recovery Heart: seg8 XLU scroll (GetItem_DrawRecoveryHeart).
inline void OOT_DrawForeignRecoveryHeart(PlayState* play, const ComboForeignDrawInfo* info) {
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    OOT_FOREIGN_PIN_XLU();
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, G_TX_RENDERTILE, play->state.frames * 0,
                                             -(int32_t)(play->state.frames * 3), 32, 32, 1, play->state.frames * 0,
                                             -(int32_t)(play->state.frames * 2), 32, 32, 0, -3, 0, -2));
    COMBO_FOREIGN_MTX(POLY_XLU_DISP++);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[0]);
    CLOSE_DISPS(play->state.gfxCtx);
    int32_t segs[] = { 0x08 };
    OOT_RestoreForeignSegs(play, segs, 1);
}

// Fish container: seg8 XLU scroll (GetItem_DrawFish).
inline void OOT_DrawForeignFish(PlayState* play, const ComboForeignDrawInfo* info) {
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    OOT_FOREIGN_PIN_XLU();
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, G_TX_RENDERTILE, play->state.frames * 0,
                                             play->state.frames * 1, 32, 32, 1, play->state.frames * 0,
                                             play->state.frames * 1, 32, 32, 0, 1, 0, 1));
    COMBO_FOREIGN_MTX(POLY_XLU_DISP++);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[0]);
    CLOSE_DISPS(play->state.gfxCtx);
    int32_t segs[] = { 0x08 };
    OOT_RestoreForeignSegs(play, segs, 1);
}

// Potions: seg8 OPA scroll, OPA dl[1,0,2,3] + XLU dl[4,5] (GetItem_DrawPotion).
inline void OOT_DrawForeignPotion(PlayState* play, const ComboForeignDrawInfo* info) {
    s32 f = (s32)play->state.frames; // soh's frames is unsigned; MM's func negates it as signed
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    OOT_FOREIGN_PIN_OPA();
    gSPSegment(POLY_OPA_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, G_TX_RENDERTILE, -f, f, 32, 32, 1, -f, f, 32, 32, -1,
                                             1, -1, 1));
    COMBO_FOREIGN_MTX(POLY_OPA_DISP++);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[1]);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[0]);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[2]);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[3]);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    OOT_FOREIGN_PIN_XLU();
    COMBO_FOREIGN_MTX(POLY_XLU_DISP++);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[4]);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[5]);
    CLOSE_DISPS(play->state.gfxCtx);
    int32_t segs[] = { 0x08 };
    OOT_RestoreForeignSegs(play, segs, 1);
}

// Poe / Big Poe bottle: OPA dl0; XLU dl1; seg8 scroll; billboard dl3,dl2 (GetItem_DrawPoes).
inline void OOT_DrawForeignPoes(PlayState* play, const ComboForeignDrawInfo* info) {
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    OOT_FOREIGN_PIN_OPA();
    COMBO_FOREIGN_MTX(POLY_OPA_DISP++);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[0]);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    OOT_FOREIGN_PIN_XLU();
    COMBO_FOREIGN_MTX(POLY_XLU_DISP++);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[1]);
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, G_TX_RENDERTILE, play->state.frames * 0,
                                             play->state.frames * 0, 16, 32, 1, play->state.frames,
                                             -(int32_t)(play->state.frames * 6), 16, 32, 0, 0, 1, -6));
    Matrix_Push();
    Matrix_ReplaceRotation(&play->billboardMtxF);
    COMBO_FOREIGN_MTX(POLY_XLU_DISP++);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[3]);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[2]);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
    int32_t segs[] = { 0x08 };
    OOT_RestoreForeignSegs(play, segs, 1);
}

// Bottled fairy: OPA dl0; XLU dl1; seg8 scroll; billboard dl2 (GetItem_DrawFairyBottle).
inline void OOT_DrawForeignFairyBottle(PlayState* play, const ComboForeignDrawInfo* info) {
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    OOT_FOREIGN_PIN_OPA();
    COMBO_FOREIGN_MTX(POLY_OPA_DISP++);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[0]);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    OOT_FOREIGN_PIN_XLU();
    COMBO_FOREIGN_MTX(POLY_XLU_DISP++);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[1]);
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, G_TX_RENDERTILE, play->state.frames * 0,
                                             play->state.frames * 0, 32, 32, 1, play->state.frames,
                                             -(int32_t)(play->state.frames * 6), 32, 320, 0, 0, 1, -6));
    Matrix_Push();
    Matrix_ReplaceRotation(&play->billboardMtxF);
    COMBO_FOREIGN_MTX(POLY_XLU_DISP++);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[2]);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
    int32_t segs[] = { 0x08 };
    OOT_RestoreForeignSegs(play, segs, 1);
}

// MM enemy soul: the billboarded soul flame in the soul's own color (DrawFuncs.cpp DrawEnLight).
// The enemy skeleton the real func draws around it isn't expressible cross-game, so this is the
// flame alone; the 0.1 scale is MM's 0.01 model scale x its flameSize 10.
inline void OOT_DrawForeignSoulFlame(PlayState* play, const ComboForeignDrawInfo* info) {
    int32_t t = (int32_t)play->state.frames;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    OOT_FOREIGN_PIN_XLU();
    Matrix_ReplaceRotation(&play->billboardMtxF);
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0, 0, 0x10, 0x20, 1, (t * 2) & 0x3F,
                                             (t * -6) & 0x7F, 0x10, 0x20, 0, 0, 2, -6));
    gDPSetPrimColor(POLY_XLU_DISP++, 0xC0, 0xC0, info->primColorXlu[0], info->primColorXlu[1], info->primColorXlu[2],
                    0);
    gDPSetEnvColor(POLY_XLU_DISP++, info->primColorXlu[0], info->primColorXlu[1], info->primColorXlu[2], 0);
    Matrix_Scale(0.1f, 0.1f, 0.1f, MTXMODE_APPLY);
    COMBO_FOREIGN_MTX(POLY_XLU_DISP++);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[0]);
    CLOSE_DISPS(play->state.gfxCtx);
    int32_t segs[] = { 0x08 };
    OOT_RestoreForeignSegs(play, segs, 1);
}

// CW_DRAW_KIND_OPS: replay the producer's transform/color/DL bytecode (MM's DrawClock,
// DrawOwlStatue, DrawSkeletonKey, DrawDoubleDefense, DrawTycoonWallet). Matrix pushes are balanced
// by the producer; a stray POP without a PUSH is guarded here so we never underflow OOT's stack.
inline void OOT_DrawForeignOps(PlayState* play, const ComboForeignDrawInfo* info) {
    int32_t depth = 0;
    bool xlu = false;
    OPEN_DISPS(play->state.gfxCtx);
    // Default stream + colour pin; every SETUP op below re-pins the stream it switches to, so a
    // recipe that opens with CW_OP_SETUP_XLU (MM's DrawDoubleDefense) is covered too.
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    OOT_FOREIGN_PIN_OPA();
    for (int32_t i = 0; i < info->opCount; i++) {
        const CwDrawOp* o = &info->ops[i];
        switch (o->op) {
            case CW_OP_SETUP_OPA:
                Gfx_SetupDL_25Opa(play->state.gfxCtx);
                OOT_FOREIGN_PIN_OPA();
                xlu = false;
                break;
            case CW_OP_SETUP_XLU:
                Gfx_SetupDL_25Xlu(play->state.gfxCtx);
                OOT_FOREIGN_PIN_XLU();
                xlu = true;
                break;
            case CW_OP_LOAD_MATRIX:
                if (xlu) {
                    COMBO_FOREIGN_MTX(POLY_XLU_DISP++);
                } else {
                    COMBO_FOREIGN_MTX(POLY_OPA_DISP++);
                }
                break;
            case CW_OP_PUSH:
                Matrix_Push();
                depth++;
                break;
            case CW_OP_POP:
                if (depth > 0) {
                    Matrix_Pop();
                    depth--;
                }
                break;
            case CW_OP_TRANSLATE:
                Matrix_Translate(o->a, o->b, o->c, MTXMODE_APPLY);
                break;
            case CW_OP_SCALE:
                Matrix_Scale(o->a, o->b, o->c, MTXMODE_APPLY);
                break;
            case CW_OP_ROTATE_X:
                Matrix_RotateZYX((s16)o->a, 0, 0, MTXMODE_APPLY);
                break;
            case CW_OP_ROTATE_Y:
                Matrix_RotateZYX(0, (s16)o->a, 0, MTXMODE_APPLY);
                break;
            case CW_OP_ROTATE_Z:
                Matrix_RotateZYX(0, 0, (s16)o->a, MTXMODE_APPLY);
                break;
            case CW_OP_BILLBOARD:
                Matrix_ReplaceRotation(&play->billboardMtxF);
                break;
            case CW_OP_PRIM_COLOR:
                if (xlu) {
                    gDPSetPrimColor(POLY_XLU_DISP++, 0, (u8)o->a, o->rgba[0], o->rgba[1], o->rgba[2], o->rgba[3]);
                } else {
                    gDPSetPrimColor(POLY_OPA_DISP++, 0, (u8)o->a, o->rgba[0], o->rgba[1], o->rgba[2], o->rgba[3]);
                }
                break;
            case CW_OP_ENV_COLOR:
                if (xlu) {
                    gDPSetEnvColor(POLY_XLU_DISP++, o->rgba[0], o->rgba[1], o->rgba[2], o->rgba[3]);
                } else {
                    gDPSetEnvColor(POLY_OPA_DISP++, o->rgba[0], o->rgba[1], o->rgba[2], o->rgba[3]);
                }
                break;
            case CW_OP_GRAYSCALE_COLOR:
                if (xlu) {
                    gDPSetGrayscaleColor(POLY_XLU_DISP++, o->rgba[0], o->rgba[1], o->rgba[2], o->rgba[3]);
                } else {
                    gDPSetGrayscaleColor(POLY_OPA_DISP++, o->rgba[0], o->rgba[1], o->rgba[2], o->rgba[3]);
                }
                break;
            case CW_OP_GRAYSCALE_ON:
            case CW_OP_GRAYSCALE_OFF: {
                bool on = (o->op == CW_OP_GRAYSCALE_ON);
                if (xlu) {
                    gSPGrayscale(POLY_XLU_DISP++, on);
                } else {
                    gSPGrayscale(POLY_OPA_DISP++, on);
                }
                break;
            }
            case CW_OP_DLIST: {
                int32_t idx = (int32_t)o->a;
                if (idx >= 0 && idx < info->count && info->dls[idx] != nullptr) {
                    if (xlu) {
                        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[idx]);
                    } else {
                        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[idx]);
                    }
                }
                break;
            }
            default:
                break;
        }
    }
    while (depth-- > 0) { // never leave the shared matrix stack unbalanced
        Matrix_Pop();
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// The original flat path: OPA layers then XLU layers, plus the animated-material / skull-token
// segment replication. Used for every SIMPLE recipe (the bulk of MM's table).
inline void OOT_DrawForeignSimple(PlayState* play, const ComboForeignDrawInfo* info) {
    int32_t n = info->count;
    int32_t xs = (info->xluStart < 0 || info->xluStart > n) ? n : info->xluStart;

    OPEN_DISPS(play->state.gfxCtx);

    // ComboShip: replicate MM's AnimatedMat_Draw segment bind before the DLs (Moon's Tear).
    int32_t matSegs[kMaxMatEntries];
    int32_t matSegCount = 0;
    if (info->matAnimPath != nullptr) {
        ComboForeignTexAnim_Run(play, "mm", info->matAnimPath, info->matAnimBindOpa, matSegs, &matSegCount);
    }

    // ComboShip: extra model scale carried from MM's draw func (e.g. boss remains: 0.02).
    if (info->scale > 0.0f) {
        Matrix_Scale(info->scale, info->scale, info->scale, MTXMODE_APPLY);
    }

    // Mirror MM's GetItem_DrawOpa*/Xlu* structure: one 25Opa setup + matrix for the OPA layers,
    // then one 25Xlu setup + matrix for the XLU layers.
    if (xs > 0) {
        // MM's own setup when the row uses one other than 25 (bombchu = 23, which is 1-CYCLE without
        // fog). Under OOT's 2-cycle 25 the list's duplicated second cycle wins and samples TEXEL1 —
        // whatever tile OOT last bound — instead of the item's own texture.
        if (info->setupDlOpa != nullptr) {
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->setupDlOpa);
        } else {
            Gfx_SetupDL_25Opa(play->state.gfxCtx);
        }
        OOT_FOREIGN_PIN_OPA();
        COMBO_FOREIGN_MTX(POLY_OPA_DISP++);
        if (info->hasEnvColor) {
            gDPSetEnvColor(POLY_OPA_DISP++, info->envColor[0], info->envColor[1], info->envColor[2], info->envColor[3]);
        }
        for (int32_t i = 0; i < xs; i++) {
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[i]);
        }
    }
    if (xs < n) {
        if (info->setupDlXlu != nullptr) { // compass glass: setup 5, not 25
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->setupDlXlu);
        } else {
            Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        }
        OOT_FOREIGN_PIN_XLU();
        // ComboShip: MM's GetItem_DrawSkullToken inlines this seg-8 flame scroll (no texanim
        // resource to carry), so it is hardcoded here. See docs/deviations/rando.md.
        if (info->xluSeg8TexScroll) {
            gSPSegment(POLY_XLU_DISP++, 0x08,
                       (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, G_TX_RENDERTILE, 0, play->state.frames * -5,
                                                     32, 32, 1, 0, 0, 32, 64, 0, -5, 0, 0));
        }
        // ComboShip: billboard the XLU layer toward the camera (Moon's Tear glow), mirroring MM's
        // Matrix_ReplaceRotation before the glow DL. Must precede the matrix capture below.
        if (info->matAnimBillboard) {
            Matrix_ReplaceRotation(&play->billboardMtxF);
        }
        COMBO_FOREIGN_MTX(POLY_XLU_DISP++);
        if (info->hasEnvColor) {
            gDPSetEnvColor(POLY_XLU_DISP++, info->envColor[0], info->envColor[1], info->envColor[2], info->envColor[3]);
        }
        for (int32_t i = xs; i < n; i++) {
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[i]);
        }
    }

    // ComboShip: segment hygiene — later same-frame draws must not sample this item's scroll DL.
    if (matSegCount > 0) {
        ComboForeignTexAnim_Restore(play, matSegs, matSegCount, info->matAnimBindOpa);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

// Draw a foreign (MM-bound) item's real MM model at the current model matrix. Any resolution
// failure falls back to the sentinel (the RG_COMBO_FOREIGN entry's blue rupee), so we never draw
// blank. Mirror of MM_DrawComboForeign (combo/menu/ComboForeignDrawMM.h).
inline void OOT_DrawComboForeign(PlayState* play, GetItemEntry* getItemEntry) {
    RandomizerCheck rc = (RandomizerCheck)getItemEntry->comboForeignCheck;
    if (rc == RC_UNKNOWN_CHECK) {
        // Defensive: entries not built via GetFinalGIEntry carry no check; the queued get-item
        // check is the only other identity source.
        rc = OOT_GetQueuedDrawCheck();
    }

    const ComboForeignDrawInfo* info = (rc != RC_UNKNOWN_CHECK) ? ComboResolveForeignDrawInfo(rc) : nullptr;
    if (info == nullptr) {
        GetItem_Draw(play, getItemEntry->gid);
        return;
    }

    // ComboShip: animated class — combo-owned skeletal draw (any failure -> sentinel, never blank).
    if (info->animOk) {
        if (!ComboForeignAnim_Draw(&info->anim, "mm", play)) {
            GetItem_Draw(play, getItemEntry->gid);
        }
        return;
    }

    switch (info->drawKind) {
        case CW_DRAW_KIND_GORON_SWORD:
            OOT_DrawForeignGoronSword(play, info);
            break;
        case CW_DRAW_KIND_DEKU_NUTS:
            OOT_DrawForeignDekuNuts(play, info);
            break;
        case CW_DRAW_KIND_RECOVERY_HEART:
            OOT_DrawForeignRecoveryHeart(play, info);
            break;
        case CW_DRAW_KIND_FISH:
            OOT_DrawForeignFish(play, info);
            break;
        case CW_DRAW_KIND_POTION:
            OOT_DrawForeignPotion(play, info);
            break;
        case CW_DRAW_KIND_POES:
            OOT_DrawForeignPoes(play, info);
            break;
        case CW_DRAW_KIND_MM_FAIRY_BOTTLE:
            OOT_DrawForeignFairyBottle(play, info);
            break;
        case CW_DRAW_KIND_MM_SOUL_FLAME:
            OOT_DrawForeignSoulFlame(play, info);
            break;
        case CW_DRAW_KIND_OPS:
            OOT_DrawForeignOps(play, info);
            break;
        case CW_DRAW_KIND_SIMPLE:
        default:
            OOT_DrawForeignSimple(play, info);
            break;
    }
}

#undef COMBO_FOREIGN_MTX
#undef OOT_FOREIGN_PIN_OPA
#undef OOT_FOREIGN_PIN_XLU

#endif /* COMBO_FOREIGN_DRAW_OOT_H */
