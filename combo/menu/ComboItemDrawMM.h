/* combo/menu/ComboItemDrawMM.h — ComboShip: MM-side bodies of the cross-game item-draw exports
 * (combo/menu/ComboItemDrawABI.h). Combo-OWNED source compiled INTO 2ship.dll (the menu-extraction
 * pattern) so the vendored BenPort.cpp keeps only a single include — the data here mirrors MM's
 * Rando/DrawItem.cpp draw cases (song colors, stray-fairy parameters) and must track them.
 *
 * TU-GLUE HEADER: include ONCE from mm/2s2h/BenPort.cpp, inside its #ifdef COMBO_BUILD include
 * block, AFTER Rando StaticData / engine headers are in scope. Not standalone.
 */
#ifndef COMBO_ITEM_DRAW_MM_H
#define COMBO_ITEM_DRAW_MM_H

#include "ComboItemDrawABI.h"
#include "objects/gameplay_keep/gameplay_keep.h"        // stray-fairy skel/anim/texanim paths + limb enums
#include "objects/object_gi_melody/object_gi_melody.h"  // gGiSongNoteDL

// Portable slice of one sDrawItemTable row (defined in mm/src/code/z_draw.c).
extern "C" s32 GetItem_GetDrawTableEntry(s32 drawId, void** outDlists, s32 maxDlists, s32* outXluStart,
                                         f32* outScale);

// Songs have no sDrawItemTable row — MM draws them as one tinted note DL (Rando/DrawItem.cpp
// DrawSong: 25Xlu + per-song gDPSetEnvColor + gGiSongNoteDL). Fully portable as a static
// description. Returns 1 and fills env color if the item is a song. Color table mirrors DrawSong.
static int32_t MM_FillSongDrawInfo(RandoItemId id, CwItemDrawInfo* out) {
    uint8_t rgb[3];
    switch (id) {
        case RI_SONG_SUN:                                   rgb[0] = 237; rgb[1] = 231; rgb[2] = 62;  break;
        case RI_SONG_DOUBLE_TIME:
        case RI_SONG_INVERTED_TIME:
        case RI_SONG_TIME:                                  rgb[0] = 98;  rgb[1] = 177; rgb[2] = 211; break;
        case RI_SONG_HEALING:                               rgb[0] = 255; rgb[1] = 150; rgb[2] = 230; break;
        case RI_SONG_STORMS:                                rgb[0] = 146; rgb[1] = 146; rgb[2] = 146; break;
        case RI_SONG_SARIA:
        case RI_SONG_SONATA:                                rgb[0] = 98;  rgb[1] = 255; rgb[2] = 98;  break;
        case RI_SONG_SOARING:                               rgb[0] = 200; rgb[1] = 160; rgb[2] = 255; break;
        case RI_SONG_ELEGY:                                 rgb[0] = 255; rgb[1] = 98;  rgb[2] = 0;   break;
        case RI_SONG_LULLABY_INTRO:                         rgb[0] = 255; rgb[1] = 100; rgb[2] = 100; break;
        case RI_SONG_LULLABY:                               rgb[0] = 255; rgb[1] = 20;  rgb[2] = 20;  break;
        case RI_SONG_OATH:                                  rgb[0] = 98;  rgb[1] = 0;   rgb[2] = 98;  break;
        case RI_SONG_EPONA:                                 rgb[0] = 146; rgb[1] = 87;  rgb[2] = 49;  break;
        case RI_SONG_NOVA:                                  rgb[0] = 20;  rgb[1] = 20;  rgb[2] = 255; break;
        default:
            return 0;
    }
    out->dlists[0] = gGiSongNoteDL;
    out->dlistCount = 1;
    out->xluStartIndex = 0; // XLU layer, like MM's DrawSong
    out->scale = 0.0f;
    out->hasEnvColor = 1;
    out->envColor[0] = rgb[0];
    out->envColor[1] = rgb[1];
    out->envColor[2] = rgb[2];
    out->envColor[3] = 255;
    return 1;
}

// Cross-game item draw info. OOT resolves this via GetProcAddress to learn which MM display lists
// render a foreign item, then submits them through "__OTR__@mm:"-routed paths resolved against
// MM's ResourceManager (CrossRMRegistry). itemName is the MM RI_* spoilerName — the same grant key
// the foreign map carries (GetItemIdFromName keys spoilerName, see Rando/StaticData/Items.cpp).
// The returned dlists point at MM's static OTR asset-path string literals, valid for process
// lifetime. Returns 0 for unknown items / non-portable draw funcs; the caller falls back to its
// sentinel.
extern "C" __declspec(dllexport) int32_t MM_GetItemDrawInfo(const char* itemName, CwItemDrawInfo* out) {
    if (itemName == nullptr || out == nullptr) {
        return 0;
    }
    RandoItemId id = Rando::StaticData::GetItemIdFromName(itemName);
    if (id == RI_UNKNOWN) {
        return 0;
    }
    auto it = Rando::StaticData::Items.find(id);
    if (it == Rando::StaticData::Items.end()) {
        return 0;
    }
    if (MM_FillSongDrawInfo(id, out)) {
        return 1; // songs: tinted note, no table row
    }
    void* dls[CW_DRAW_MAX_DLISTS] = {};
    int32_t xluStart = -1;
    f32 scale = 0.0f;
    int32_t n = GetItem_GetDrawTableEntry((s32)it->second.drawId, dls, CW_DRAW_MAX_DLISTS, &xluStart, &scale);
    if (n <= 0) {
        return 0;
    }
    out->dlistCount = n;
    out->xluStartIndex = xluStart;
    out->scale = scale;
    out->hasEnvColor = 0;
    for (int32_t i = 0; i < n; i++) {
        out->dlists[i] = (const char*)dls[i];
    }
    return 1;
}

// Animated variant. Items in the animated class (currently the stray fairies, drawn by
// Rando/DrawItem.cpp:DrawStrayFairy in MM) have no static DL row, so the static export returns 0
// and OOT falls through to this one. MM only DESCRIBES the item — resource paths + DrawStrayFairy
// parameters — and the host's combo-owned ComboForeignAnim.h does the loading and drawing.
// Returns 0 for items outside the animated class.
extern "C" __declspec(dllexport) int32_t MM_GetItemAnimDrawInfo(const char* itemName, CwItemAnimDrawInfo* out) {
    if (itemName == nullptr || out == nullptr) {
        return 0;
    }
    const char* texAnim;
    switch (Rando::StaticData::GetItemIdFromName(itemName)) {
        case RI_WOODFALL_STRAY_FAIRY:
            texAnim = gStrayFairyWoodfallTexAnim;
            break;
        case RI_SNOWHEAD_STRAY_FAIRY:
            texAnim = gStrayFairySnowheadTexAnim;
            break;
        case RI_GREAT_BAY_STRAY_FAIRY:
            texAnim = gStrayFairyGreatBayTexAnim;
            break;
        case RI_STONE_TOWER_STRAY_FAIRY:
            texAnim = gStrayFairyStoneTowerTexAnim;
            break;
        case RI_CLOCK_TOWN_STRAY_FAIRY:
            texAnim = gStrayFairyClockTownTexAnim;
            break;
        default:
            return 0; // not in the animated class
    }
    out->skelPath = gStrayFairySkel;
    out->animPath = gStrayFairyFlyingAnim;
    out->texAnimPath = texAnim;
    out->scale = 0.03f;
    out->billboard = 1;
    out->xlu = 1;
    out->limbCount = STRAY_FAIRY_LIMB_MAX;
    out->hiddenLimb = STRAY_FAIRY_LIMB_RIGHT_FACING_HEAD;
    return 1;
}

#endif // COMBO_ITEM_DRAW_MM_H
