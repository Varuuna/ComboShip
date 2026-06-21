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
 * The animated cross-game class (combo/menu/ComboForeignAnim.h) is intentionally NOT wired here: OOT
 * exposes no skeletal-animated foreign items (OOT_GetItemAnimDrawInfo always returns 0), so there is
 * nothing for MM to draw via SkelAnime today. Add it symmetrically if that ever changes.
 */
#ifndef COMBO_FOREIGN_DRAW_MM_H
#define COMBO_FOREIGN_DRAW_MM_H

#ifndef OPEN_DISPS
#error "ComboForeignDrawMM.h is TU-glue: include the host engine headers before it"
#endif

#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#ifdef _WIN32
#include <windows.h>
#endif

#include "ComboItemDrawABI.h"
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
    const char* dls[CW_DRAW_MAX_DLISTS] = { nullptr }; // interned "__OTR__@oot:..." routed paths
};

// Routed path strings must outlive the frame (the GBI wrapper emits the raw pointer into the display
// list; the interpreter dereferences it later), so intern them for the process lifetime.
inline const char* ComboInternRoutedPathOOT(const std::string& s) {
    static std::unordered_set<std::string> sPool; // node-based: c_str() stable across rehash
    return sPool.insert(s).first->c_str();
}

// Full lookup chain (foreign map -> OOT export -> routed strings), cached per check per slot so it
// runs once per check instead of every frame.
inline const ComboForeignDrawInfoOOT* ComboResolveForeignDrawInfoOOT(RandoCheckId rc) {
    static std::unordered_map<int32_t, ComboForeignDrawInfoOOT> sCache;
    static int sCacheSlot = -1;
    int slot = gSaveContext.fileNum;
    if (slot != sCacheSlot) {
        sCache.clear();
        sCacheSlot = slot;
    }
    auto cached = sCache.find(rc);
    if (cached != sCache.end()) {
        return cached->second.ok ? &cached->second : nullptr;
    }
    ComboForeignDrawInfoOOT& info = sCache[rc]; // default ok=false caches negative results too

    const ComboRando::ForeignItem* fi = Rando::MiscBehavior::MM_LookupForeign(rc);
    if (fi == nullptr || fi->itemGame != ComboRando::GAME_OOT) {
        return nullptr;
    }

#ifdef _WIN32
    static Fn_GetItemDrawInfo sGetItemDrawInfo = nullptr;
    if (sGetItemDrawInfo == nullptr) {
        HMODULE h = GetModuleHandleA("soh.dll"); // already loaded by the exe (ComboMenuModel pattern)
        sGetItemDrawInfo = h ? (Fn_GetItemDrawInfo)GetProcAddress(h, "OOT_GetItemDrawInfo") : nullptr;
    }
    if (sGetItemDrawInfo == nullptr) {
        sCache.erase(rc); // soh.dll may simply not be resident yet — don't negative-cache, retry later
        return nullptr;
    }
    CwItemDrawInfo raw{};
    if (sGetItemDrawInfo(fi->itemName.c_str(), &raw) == 0 || raw.dlistCount <= 0) {
        return nullptr; // unknown item or non-portable draw func: cached negative -> sentinel forever
    }

    static constexpr char kOtrPrefix[] = "__OTR__";
    int32_t n = raw.dlistCount < CW_DRAW_MAX_DLISTS ? raw.dlistCount : CW_DRAW_MAX_DLISTS;
    for (int32_t i = 0; i < n; i++) {
        const char* p = raw.dlists[i];
        if (p == nullptr || strncmp(p, kOtrPrefix, sizeof(kOtrPrefix) - 1) != 0) {
            return nullptr; // not an OTR path literal — can't route it
        }
        info.dls[i] = ComboInternRoutedPathOOT(std::string("__OTR__@oot:") + (p + sizeof(kOtrPrefix) - 1));
    }
    info.count = n;
    info.xluStart = raw.xluStartIndex;
    info.scale = raw.scale;
    info.hasEnvColor = raw.hasEnvColor != 0;
    for (int32_t i = 0; i < 4; i++) {
        info.envColor[i] = raw.envColor[i];
    }
    info.ok = true;
    return &info;
#else
    return nullptr; // GetProcAddress resolution is Windows-only for now (matches ComboMenuModel)
#endif
}

} // namespace

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

    int32_t n = info->count;
    int32_t xs = (info->xluStart < 0 || info->xluStart > n) ? n : info->xluStart;

    OPEN_DISPS(gPlayState->state.gfxCtx);

    // Extra uniform model scale carried from OOT's draw func (e.g. small rupees: 0.7).
    if (info->scale > 0.0f) {
        Matrix_Scale(info->scale, info->scale, info->scale, MTXMODE_APPLY);
    }

    // Mirror OOT's GetItem_DrawOpa*/Xlu* structure: one 25Opa setup + matrix for the OPA layers,
    // then one 25Xlu setup + matrix for the XLU layers.
    if (xs > 0) {
        Gfx_SetupDL25_Opa(gPlayState->state.gfxCtx);
        MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, gPlayState->state.gfxCtx);
        if (info->hasEnvColor) {
            gDPSetEnvColor(POLY_OPA_DISP++, info->envColor[0], info->envColor[1], info->envColor[2], info->envColor[3]);
        }
        for (int32_t i = 0; i < xs; i++) {
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)info->dls[i]);
        }
    }
    if (xs < n) {
        Gfx_SetupDL25_Xlu(gPlayState->state.gfxCtx);
        MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gPlayState->state.gfxCtx);
        if (info->hasEnvColor) {
            gDPSetEnvColor(POLY_XLU_DISP++, info->envColor[0], info->envColor[1], info->envColor[2], info->envColor[3]);
        }
        for (int32_t i = xs; i < n; i++) {
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)info->dls[i]);
        }
    }

    CLOSE_DISPS(gPlayState->state.gfxCtx);
}

#endif // COMBO_FOREIGN_DRAW_MM_H
