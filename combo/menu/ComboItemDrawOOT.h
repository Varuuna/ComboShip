/* combo/menu/ComboItemDrawOOT.h — ComboShip: OOT-side bodies of the cross-game item-draw exports
 * (combo/menu/ComboItemDrawABI.h). The exact mirror of combo/menu/ComboItemDrawMM.h, in the opposite
 * direction: MM (2ship.dll) asks soh.dll which display lists render a foreign OOT item, then submits
 * them through "__OTR__@oot:"-routed paths resolved against OOT's ResourceManager (CrossRMRegistry).
 *
 * Combo-OWNED source compiled INTO soh.dll (the menu-extraction pattern) so the vendored item_list.cpp
 * keeps only a single include. The data here defers entirely to sDrawItemTable (soh/src/code/z_draw.c)
 * via GetItem_GetDrawTableEntry — there is nothing OOT-specific to mirror by hand (unlike MM, OOT
 * songs are ordinary sDrawItemTable rows, so no song special-case is needed).
 *
 * TU-GLUE HEADER: include ONCE from soh/soh/Enhancements/randomizer/item_list.cpp, inside its
 * #ifdef COMBO_BUILD, AFTER the Rando StaticData / item / ItemTableTypes headers are in scope. Not
 * standalone.
 */
#ifndef COMBO_ITEM_DRAW_OOT_H
#define COMBO_ITEM_DRAW_OOT_H

#include "ComboItemDrawABI.h"

// Portable slice of one sDrawItemTable row (defined COMBO_BUILD-guarded in soh/src/code/z_draw.c).
// outDrawKind = CwDrawKind (0 = simple OPA/XLU submission; else a non-portable func the consumer
// replicates). outColors is 16 bytes: primXlu[4], envXlu[4], primOpa[4], envOpa[4] (JEWEL/MUSIC_NOTE).
extern "C" s32 GetItem_GetDrawTableEntry(s32 drawId, void** outDlists, s32 maxDlists, s32* outXluStart, f32* outScale,
                                         s32* outDrawKind, uint8_t* outColors);

// Cross-game item draw info. MM resolves this via GetProcAddress to learn which OOT display lists
// render a foreign item, then submits them through "__OTR__@oot:"-routed paths resolved against OOT's
// ResourceManager (CrossRMRegistry). itemName is the OOT English item name — the same grant key the
// foreign map carries for OOT items (the combo generator writes GetName().english; itemNameToEnum is
// keyed on it, see soh/.../item_list.cpp). The returned dlists point at OOT's static OTR asset-path
// string literals, valid for process lifetime. Returns 0 for unknown items / non-portable draw funcs;
// the caller falls back to its sentinel.
extern "C" __declspec(dllexport) int32_t OOT_GetItemDrawInfo(const char* itemName, CwItemDrawInfo* out) {
    if (itemName == nullptr || out == nullptr) {
        return 0;
    }
    auto& nameMap = Rando::StaticData::itemNameToEnum;
    auto it = nameMap.find(itemName);
    if (it == nameMap.end()) {
        return 0;
    }
    RandomizerGet rg = it->second;
    if (rg == RG_NONE || rg == RG_COMBO_FOREIGN) {
        return 0; // sentinel / no item: nothing to draw
    }
    GetItemEntry gi = Rando::StaticData::RetrieveItem(rg).GetGIEntry_Copy();
    void* dls[CW_DRAW_MAX_DLISTS] = {};
    int32_t xluStart = -1;
    f32 scale = 0.0f;
    int32_t drawKind = CW_DRAW_KIND_SIMPLE;
    uint8_t colors[16] = {};
    int32_t n = GetItem_GetDrawTableEntry((s32)gi.gid, dls, CW_DRAW_MAX_DLISTS, &xluStart, &scale, &drawKind, colors);
    if (n <= 0) {
        return 0; // unsupported/non-portable draw func
    }
    out->dlistCount = n;
    out->xluStartIndex = xluStart;
    out->scale = scale;
    out->hasEnvColor = 0; // OOT's portable funcs bake color into their DLs (no separate env color)
    out->drawKind = drawKind;
    for (int32_t i = 0; i < 4; i++) {
        out->primColorXlu[i] = colors[i];
        out->envColorXlu[i] = colors[4 + i];
        out->primColorOpa[i] = colors[8 + i];
        out->envColorOpa[i] = colors[12 + i];
    }
    for (int32_t i = 0; i < n; i++) {
        out->dlists[i] = (const char*)dls[i];
    }
    return 1;
}

// Animated variant. OOT has no skeletal-animated foreign class: its only animated get-item draws
// (fairy, blue fire, poes, skull token) rely on non-portable segment-8 texture scrolls + billboard
// rotation rather than a SkelAnime skeleton, so there is nothing to describe. Always returns 0; the
// MM consumer then falls back to its sentinel. Exported for ABI symmetry with MM_GetItemAnimDrawInfo.
extern "C" __declspec(dllexport) int32_t OOT_GetItemAnimDrawInfo(const char* itemName, CwItemAnimDrawInfo* out) {
    (void)itemName;
    (void)out;
    return 0;
}

#endif // COMBO_ITEM_DRAW_OOT_H
