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

#include <cstring>
#include "ComboItemDrawABI.h"
#include "2s2h_assets.h"                                     // custom rando models (triforce, ocarina buttons, ...)
#include "objects/gameplay_keep/gameplay_keep.h"             // stray-fairy skel/anim + soul flame DL
#include "objects/object_gi_melody/object_gi_melody.h"       // gGiSongNoteDL
#include "objects/object_sek/object_sek.h"                   // gOwlStatueOpenedDL
#include "objects/object_gi_reserve00/object_gi_reserve00.h" // Moon's Tear item DL + texanim path
#include "objects/object_gi_bottle_04/object_gi_bottle_04.h" // gGiFairyBottleTexAnim
#include "objects/object_gi_hearts/object_gi_hearts.h"       // Double Defense heart border/container
#include "objects/object_gi_purse/object_gi_purse.h"         // Tycoon Wallet layers
#include "objects/object_obj_tokeidai/object_obj_tokeidai.h" // clock tower DLs

// Portable slice of one sDrawItemTable row (defined in mm/src/code/z_draw.c). outDrawKind is a
// CwDrawKind: 0 = plain OPA/XLU submission, else a non-portable func the consumer replicates.
extern "C" s32 GetItem_GetDrawTableEntry(s32 drawId, void** outDlists, s32 maxDlists, s32* outXluStart, f32* outScale,
                                         s32* outXluSeg8TexScroll, s32* outDrawKind);

// --- CW_DRAW_KIND_OPS emitters. Bounds-checked; an overflowing recipe is truncated, never written
// out of range (the consumer just draws fewer layers).
static CwDrawOp* MM_Op(CwItemDrawInfo* out, int32_t op) {
    if (out->opCount >= CW_DRAW_MAX_OPS) {
        return NULL;
    }
    CwDrawOp* o = &out->ops[out->opCount++];
    *o = CwDrawOp{};
    o->op = op;
    return o;
}
static void MM_OpV(CwItemDrawInfo* out, int32_t op, float a, float b, float c) {
    CwDrawOp* o = MM_Op(out, op);
    if (o != NULL) {
        o->a = a;
        o->b = b;
        o->c = c;
    }
}
static void MM_OpColor(CwItemDrawInfo* out, int32_t op, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float lodFrac) {
    CwDrawOp* o = MM_Op(out, op);
    if (o != NULL) {
        o->rgba[0] = r;
        o->rgba[1] = g;
        o->rgba[2] = b;
        o->rgba[3] = a;
        o->a = lodFrac;
    }
}
// Append a display list and the op that submits it.
static void MM_OpDL(CwItemDrawInfo* out, const char* dl) {
    if (out->dlistCount >= CW_DRAW_MAX_DLISTS) {
        return;
    }
    int32_t i = out->dlistCount++;
    out->dlists[i] = dl;
    MM_OpV(out, CW_OP_DLIST, (float)i, 0.0f, 0.0f);
}

// Songs have no sDrawItemTable row — MM draws them as one tinted note DL (Rando/DrawItem.cpp
// DrawSong: 25Xlu + per-song gDPSetEnvColor + gGiSongNoteDL). Fully portable as a static
// description. Returns 1 and fills env color if the item is a song. Color table mirrors DrawSong.
static int32_t MM_FillSongDrawInfo(RandoItemId id, CwItemDrawInfo* out) {
    uint8_t rgb[3];
    switch (id) {
        case RI_SONG_SUN:
            rgb[0] = 237;
            rgb[1] = 231;
            rgb[2] = 62;
            break;
        case RI_SONG_DOUBLE_TIME:
        case RI_SONG_INVERTED_TIME:
        case RI_SONG_TIME:
            rgb[0] = 98;
            rgb[1] = 177;
            rgb[2] = 211;
            break;
        case RI_SONG_HEALING:
            rgb[0] = 255;
            rgb[1] = 150;
            rgb[2] = 230;
            break;
        case RI_SONG_STORMS:
            rgb[0] = 146;
            rgb[1] = 146;
            rgb[2] = 146;
            break;
        case RI_SONG_SARIA:
        case RI_SONG_SONATA:
            rgb[0] = 98;
            rgb[1] = 255;
            rgb[2] = 98;
            break;
        case RI_SONG_SOARING:
            rgb[0] = 200;
            rgb[1] = 160;
            rgb[2] = 255;
            break;
        case RI_SONG_ELEGY:
            rgb[0] = 255;
            rgb[1] = 98;
            rgb[2] = 0;
            break;
        case RI_SONG_LULLABY_INTRO:
            rgb[0] = 255;
            rgb[1] = 100;
            rgb[2] = 100;
            break;
        case RI_SONG_LULLABY:
            rgb[0] = 255;
            rgb[1] = 20;
            rgb[2] = 20;
            break;
        case RI_SONG_OATH:
            rgb[0] = 98;
            rgb[1] = 0;
            rgb[2] = 98;
            break;
        case RI_SONG_EPONA:
            rgb[0] = 146;
            rgb[1] = 87;
            rgb[2] = 49;
            break;
        case RI_SONG_NOVA:
            rgb[0] = 20;
            rgb[1] = 20;
            rgb[2] = 255;
            break;
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

// Items MM draws with a bespoke Rando::DrawItem func whose shape is per-DL transforms/colors rather
// than one flat OPA/XLU submission. Each branch is a 1:1 description of the func in
// mm/2s2h/Rando/DrawItem.cpp (or DrawFuncs.cpp for the clock). Returns 1 when id is handled.
static int32_t MM_FillOpsDrawInfo(RandoItemId id, CwItemDrawInfo* out) {
    switch (id) {
        case RI_OWL_CLOCK_TOWN_SOUTH: // DrawOwlStatue: opened-owl model, scale 0.01, -3000 y-translate
        case RI_OWL_GREAT_BAY_COAST:
        case RI_OWL_IKANA_CANYON:
        case RI_OWL_MILK_ROAD:
        case RI_OWL_MOUNTAIN_VILLAGE:
        case RI_OWL_SNOWHEAD:
        case RI_OWL_SOUTHERN_SWAMP:
        case RI_OWL_STONE_TOWER:
        case RI_OWL_WOODFALL:
        case RI_OWL_ZORA_CAPE:
            MM_Op(out, CW_OP_SETUP_OPA);
            MM_OpV(out, CW_OP_SCALE, 0.01f, 0.01f, 0.01f);
            MM_OpV(out, CW_OP_TRANSLATE, 0.0f, -3000.0f, 0.0f);
            MM_Op(out, CW_OP_LOAD_MATRIX);
            MM_OpDL(out, gOwlStatueOpenedDL);
            break;

        case RI_TIME_DAY_1: // DrawClock (DrawFuncs.cpp)
        case RI_TIME_DAY_2:
        case RI_TIME_DAY_3:
        case RI_TIME_NIGHT_1:
        case RI_TIME_NIGHT_2:
        case RI_TIME_NIGHT_3:
        case RI_TIME_PROGRESSIVE: {
            bool night = (id == RI_TIME_NIGHT_1) || (id == RI_TIME_NIGHT_2) || (id == RI_TIME_NIGHT_3) ||
                         ((id == RI_TIME_PROGRESSIVE) && gSaveContext.save.isNight);
            // No ObjTokeidai actor cross-game, so DrawClock's live-actor fields stay 0 and its
            // yTranslation / +-1791 / clockFaceZTranslation steps fold to identity — dropped here.
            int16_t clockFaceRotation = night ? 0 : (int16_t)0xC000;
            int16_t sunMoonPanelRotation = night ? (int16_t)0x8000 : 0;
            MM_Op(out, CW_OP_SETUP_OPA);
            MM_OpV(out, CW_OP_SCALE, 0.015f, 0.015f, 0.015f);
            MM_Op(out, CW_OP_PUSH);
            MM_Op(out, CW_OP_LOAD_MATRIX);
            MM_OpDL(out, gClockTowerMinuteRingDL);
            MM_Op(out, CW_OP_POP);
            MM_Op(out, CW_OP_LOAD_MATRIX);
            MM_OpDL(out, gClockTowerClockCenterAndHandDL);
            MM_OpV(out, CW_OP_ROTATE_Z, (float)(int16_t)(-clockFaceRotation * 2), 0.0f, 0.0f);
            MM_Op(out, CW_OP_LOAD_MATRIX);
            MM_OpDL(out, gClockTowerClockFaceDL);
            MM_OpV(out, CW_OP_TRANSLATE, 0.0f, -1112.0f, -19.6f);
            MM_OpV(out, CW_OP_ROTATE_Y, (float)sunMoonPanelRotation, 0.0f, 0.0f);
            MM_Op(out, CW_OP_LOAD_MATRIX);
            MM_OpDL(out, gClockTowerSunAndMoonPanelDL);
            break;
        }

        case RI_SKELETON_KEY: // DrawSkeletonKey
            MM_Op(out, CW_OP_SETUP_OPA);
            MM_OpV(out, CW_OP_SCALE, 0.8f, 0.8f, 0.8f);
            MM_Op(out, CW_OP_LOAD_MATRIX);
            MM_OpColor(out, CW_OP_ENV_COLOR, 255, 255, 170, 255, 0.0f);
            MM_OpDL(out, gSkeletonKeyDL);
            break;

        case RI_DOUBLE_DEFENSE: // DrawDoubleDefense: white-grayscale border, then red-grayscale container
            MM_Op(out, CW_OP_SETUP_XLU);
            MM_Op(out, CW_OP_LOAD_MATRIX);
            MM_OpColor(out, CW_OP_GRAYSCALE_COLOR, 255, 255, 255, 255, 0.0f);
            MM_Op(out, CW_OP_GRAYSCALE_ON);
            MM_OpDL(out, gGiHeartBorderDL);
            MM_OpColor(out, CW_OP_GRAYSCALE_COLOR, 255, 0, 0, 100, 0.0f);
            MM_OpDL(out, gGiHeartContainerDL);
            MM_Op(out, CW_OP_GRAYSCALE_OFF);
            break;

        case RI_WALLET_TYCOON: // DrawTycoonWallet: Giant's Wallet layers, body overridden purple
            MM_Op(out, CW_OP_SETUP_OPA);
            MM_Op(out, CW_OP_LOAD_MATRIX);
            MM_OpDL(out, gGiGiantsWalletColorDL);
            MM_OpColor(out, CW_OP_PRIM_COLOR, 150, 0, 200, 255, 128.0f);
            MM_OpColor(out, CW_OP_ENV_COLOR, 80, 0, 120, 255, 0.0f);
            MM_OpDL(out, gGiWalletDL);
            MM_OpDL(out, gGiGiantsWalletRupeeOuterColorDL);
            MM_OpDL(out, gGiWalletRupeeOuterDL);
            MM_OpDL(out, gGiGiantsWalletStringColorDL);
            MM_OpDL(out, gGiWalletStringDL);
            MM_OpDL(out, gGiGiantsWalletRupeeInnerColorDL);
            MM_OpDL(out, gGiWalletRupeeInnerDL);
            break;

        default:
            return 0;
    }
    out->drawKind = CW_DRAW_KIND_OPS;
    out->xluStartIndex = -1; // unused by the ops path
    return 1;
}

// Items MM draws as one plain scaled OPA or XLU display list (no table row, no extra GPU state).
static int32_t MM_FillSimpleDrawInfo(RandoItemId id, CwItemDrawInfo* out) {
    const char* dl;
    bool xlu;
    float scale = 0.0f;
    switch (id) {
        case RI_ABILITY_SWIM: // DrawAbilityItem
            dl = gGiFlippersDL;
            xlu = true;
            break;
        case RI_MAX_TRAP: // DrawTrapModel (ice cube)
            dl = gTrapDL;
            xlu = true;
            scale = 0.03f;
            break;
        case RI_OCARINA_BUTTON_A: // DrawOcarinaButtonItem
        case RI_OCARINA_BUTTON_C_DOWN:
        case RI_OCARINA_BUTTON_C_RIGHT:
        case RI_OCARINA_BUTTON_C_LEFT:
        case RI_OCARINA_BUTTON_C_UP: {
            static const char* buttons[5] = {
                gOcarinaAButtonDL,     gOcarinaCDownButtonDL, gOcarinaCRightButtonDL,
                gOcarinaCLeftButtonDL, gOcarinaCUpButtonDL,
            };
            dl = buttons[id - RI_OCARINA_BUTTON_A];
            xlu = false;
            break;
        }
        case RI_TRIFORCE_PIECE: // DrawTriforcePiece: shard cycles with the collected count
        case RI_TRIFORCE_PIECE_PREVIOUS: {
            static const char* shards[3] = { gTriforcePiece0DL, gTriforcePiece1DL, gTriforcePiece2DL };
            uint16_t found = gSaveContext.save.shipSaveInfo.rando.foundTriforcePieces;
            if (found >= RANDO_SAVE_OPTIONS[RO_TRIFORCE_PIECES_REQUIRED]) {
                dl = gTriforcePieceCompletedDL;
            } else if (id == RI_TRIFORCE_PIECE_PREVIOUS) {
                dl = shards[(found > 0 ? found - 1 : 0) % 3]; // guard the 0-shard underflow MM's func has
            } else {
                dl = shards[found % 3];
            }
            xlu = true;
            scale = 0.03f;
            break;
        }
        default:
            return 0;
    }
    out->drawKind = CW_DRAW_KIND_SIMPLE;
    out->dlistCount = 1;
    out->dlists[0] = dl;
    out->xluStartIndex = xlu ? 0 : -1;
    out->scale = scale;
    return 1;
}

// Enemy souls (GID_NONE) are drawn by bespoke SkelAnime funcs — the enemy model plus DrawEnLight's
// billboarded flame. The skeletons aren't expressible cross-game, so we emit the FLAME ONLY, in the
// soul's own color (DrawFuncs.cpp DrawEnLight + the per-enemy color at each Draw* tail). Reads as a
// soul rather than a stand-in item; the enemy body is the part we drop.
static int32_t MM_FillEnemySoulDrawInfo(RandoItemId id, CwItemDrawInfo* out) {
    uint8_t rgb[3] = { 155, 155, 155 }; // the color the large majority of souls use
    switch (id) {
        case RI_SOUL_ENEMY_ALIEN:
            rgb[0] = 10;
            rgb[1] = 138;
            rgb[2] = 46;
            break;
        case RI_SOUL_ENEMY_CAPTAIN_KEETA:
            rgb[0] = 255;
            rgb[1] = 192;
            rgb[2] = 0;
            break;
        case RI_SOUL_ENEMY_DEXIHAND:
            rgb[0] = 155;
            rgb[1] = 155;
            rgb[2] = 70;
            break;
        case RI_SOUL_ENEMY_EENO:
            rgb[0] = 155;
            rgb[1] = 155;
            rgb[2] = 35;
            break;
        case RI_SOUL_ENEMY_EYEGORE:
            rgb[0] = 192;
            rgb[1] = 192;
            rgb[2] = 64;
            break;
        case RI_SOUL_ENEMY_GARO:
            rgb[0] = 150;
            rgb[1] = 255;
            rgb[2] = 150;
            break;
        case RI_SOUL_ENEMY_GEKKO:
            rgb[0] = 150;
            rgb[1] = 100;
            rgb[2] = 255;
            break;
        case RI_SOUL_ENEMY_GOMESS:
            rgb[0] = 155;
            rgb[1] = 0;
            rgb[2] = 0;
            break;
        case RI_SOUL_ENEMY_IGOS_DU_IKANA:
            rgb[0] = 0;
            rgb[1] = 0;
            rgb[2] = 0;
            break;
        // Every remaining enemy soul uses DrawEnLight's default gray flame.
        case RI_SOUL_ENEMY_ARMOS:
        case RI_SOUL_ENEMY_BAD_BAT:
        case RI_SOUL_ENEMY_BEAMOS:
        case RI_SOUL_ENEMY_BOE:
        case RI_SOUL_ENEMY_BUBBLE:
        case RI_SOUL_ENEMY_CHUCHU:
        case RI_SOUL_ENEMY_DEATH_ARMOS:
        case RI_SOUL_ENEMY_DEEP_PYTHON:
        case RI_SOUL_ENEMY_DEKU_BABA:
        case RI_SOUL_ENEMY_DINOLFOS:
        case RI_SOUL_ENEMY_DODONGO:
        case RI_SOUL_ENEMY_DRAGONFLY:
        case RI_SOUL_ENEMY_FREEZARD:
        case RI_SOUL_ENEMY_GIANT_BEE:
        case RI_SOUL_ENEMY_GUAY:
        case RI_SOUL_ENEMY_HIPLOOP:
        case RI_SOUL_ENEMY_IRON_KNUCKLE:
        case RI_SOUL_ENEMY_KEESE:
        case RI_SOUL_ENEMY_LEEVER:
        case RI_SOUL_ENEMY_LIKE_LIKE:
        case RI_SOUL_ENEMY_MAD_SCRUB:
        case RI_SOUL_ENEMY_NEJIRON:
        case RI_SOUL_ENEMY_OCTOROK:
        case RI_SOUL_ENEMY_PEAHAT:
        case RI_SOUL_ENEMY_PIRATE:
        case RI_SOUL_ENEMY_POE:
        case RI_SOUL_ENEMY_REDEAD:
        case RI_SOUL_ENEMY_SHELLBLADE:
        case RI_SOUL_ENEMY_SKULLFISH:
        case RI_SOUL_ENEMY_SKULLTULA:
        case RI_SOUL_ENEMY_SNAPPER:
        case RI_SOUL_ENEMY_STALCHILD:
        case RI_SOUL_ENEMY_TAKKURI:
        case RI_SOUL_ENEMY_TEKTITE:
        case RI_SOUL_ENEMY_WALLMASTER:
        case RI_SOUL_ENEMY_WART:
        case RI_SOUL_ENEMY_WIZROBE:
        case RI_SOUL_ENEMY_WOLFOS:
            break;
        default:
            return 0;
    }
    out->drawKind = CW_DRAW_KIND_MM_SOUL_FLAME;
    out->dlistCount = 1;
    out->dlists[0] = gameplay_keep_DL_01ACF0;
    out->xluStartIndex = 0;
    out->primColorXlu[0] = rgb[0];
    out->primColorXlu[1] = rgb[1];
    out->primColorXlu[2] = rgb[2];
    out->primColorXlu[3] = 255;
    return 1;
}

// GID aliasing: items with no table row of their own (GID_NONE) whose real draw func is a bespoke
// SkelAnime routine, mapped to a stand-in table row so they get a recognizable model instead of the
// sentinel. Boss souls -> the matching boss remains (Majora has none -> Twinmold's); the four
// minifrogs -> Don Gero's frog mask (their per-frog env color is not carried).
static int32_t MM_FillGidAliasDrawInfo(RandoItemId id, CwItemDrawInfo* out) {
    s32 gid;
    switch (id) {
        case RI_SOUL_BOSS_GOHT:
            gid = GID_REMAINS_GOHT;
            break;
        case RI_SOUL_BOSS_GYORG:
            gid = GID_REMAINS_GYORG;
            break;
        case RI_SOUL_BOSS_ODOLWA:
            gid = GID_REMAINS_ODOLWA;
            break;
        case RI_SOUL_BOSS_TWINMOLD:
        case RI_SOUL_BOSS_MAJORA: // no Majora remains model; Twinmold's stands in
            gid = GID_REMAINS_TWINMOLD;
            break;
        case RI_FROG_BLUE:
        case RI_FROG_CYAN:
        case RI_FROG_PINK:
        case RI_FROG_WHITE:
            gid = GID_MASK_DON_GERO;
            break;
        default:
            return 0;
    }
    void* dls[CW_DRAW_MAX_DLISTS] = {};
    int32_t xluStart = -1;
    f32 scale = 0.0f;
    s32 xluSeg8TexScroll = 0;
    s32 drawKind = CW_DRAW_KIND_SIMPLE;
    int32_t n =
        GetItem_GetDrawTableEntry(gid, dls, CW_DRAW_MAX_DLISTS, &xluStart, &scale, &xluSeg8TexScroll, &drawKind);
    if (n <= 0) {
        return 0;
    }
    out->dlistCount = n;
    out->xluStartIndex = xluStart;
    out->scale = scale;
    out->xluSeg8TexScroll = xluSeg8TexScroll;
    out->drawKind = drawKind;
    out->hasEnvColor = 0;
    for (int32_t k = 0; k < n; k++) {
        out->dlists[k] = (const char*)dls[k];
    }
    return 1;
}

// Cross-game item draw info. OOT resolves this via GetProcAddress to learn which MM display lists
// render a foreign item, then submits them through "__OTR__@mm:"-routed paths resolved against
// MM's ResourceManager (CrossRMRegistry). itemName is the friendly combo-spoiler name the foreign
// map carries (resolve via GetItemIdFromDisplayName; fall back to the RI_ spoilerName for the
// sentinel / any raw id). The returned dlists point at MM's static OTR asset-path string literals,
// valid for process lifetime. Returns 0 for unknown items / non-portable draw funcs; the caller
// falls back to its sentinel.
// Items whose concrete model depends on how far the player has progressed.
static bool MM_IsProgressiveItem(RandoItemId id) {
    switch (id) {
        case RI_PROGRESSIVE_SWORD:
        case RI_PROGRESSIVE_BOW:
        case RI_PROGRESSIVE_BOMB_BAG:
        case RI_PROGRESSIVE_WALLET:
        case RI_PROGRESSIVE_MAGIC:
        case RI_PROGRESSIVE_LULLABY:
        case RI_TIME_PROGRESSIVE:
            return true;
        default:
            return false;
    }
}

// Items whose concrete model is picked at draw time from live state, so the consumer must
// re-resolve them every frame: junk/trap indirection and the Triforce shard cycle (progressive
// tiers are flagged separately below).
static bool MM_IsStateDependentDraw(RandoItemId id) {
    switch (id) {
        case RI_JUNK:
        case RI_TRAP:
        case RI_TRIFORCE_PIECE:
        case RI_TRIFORCE_PIECE_PREVIOUS:
            return true;
        default:
            return false;
    }
}

static int32_t MM_FillItemDrawInfo(RandoItemId id, CwItemDrawInfo* out) {
    // ComboShip (#88): a progressive item's model is the tier the player is owed, not the static base
    // drawId (which is always tier 1 — every Progressive Sword drew a Kokiri Sword). Resolve it the way
    // MM's own drawer does. Runs before the helpers so Progressive Lullaby, which resolves to a song,
    // still reaches MM_FillSongDrawInfo.
    if (MM_IsProgressiveItem(id)) {
        RandoItemId resolved = Rando::ConvertItem(id);
        if (resolved != RI_UNKNOWN && resolved != id) {
            id = resolved;
        }
    }
    // ComboShip: junk/trap are indirections MM resolves at draw time (Rando::DrawItem). We have no
    // check id here, so the seed-only default is used — the model is stable but may differ from the
    // one MM itself would pick for this check.
    if (id == RI_JUNK) {
        id = Rando::CurrentJunkItem();
    } else if (id == RI_TRAP) {
        id = Rando::CurrentTrapItem();
    }
    auto it = Rando::StaticData::Items.find(id);
    if (it == Rando::StaticData::Items.end()) {
        return 0;
    }
    if (MM_FillSongDrawInfo(id, out)) {
        return 1; // songs: tinted note, no table row
    }
    if (MM_FillOpsDrawInfo(id, out)) {
        return 1; // clock / owl / skeleton key / double defense / tycoon wallet
    }
    if (MM_FillSimpleDrawInfo(id, out)) {
        return 1; // flippers / ice trap / ocarina buttons / triforce shards
    }
    if (MM_FillEnemySoulDrawInfo(id, out)) {
        return 1; // enemy souls: colored soul flame, no table row
    }
    if (MM_FillGidAliasDrawInfo(id, out)) {
        return 1; // boss souls / minifrogs: stand-in table row
    }
    void* dls[CW_DRAW_MAX_DLISTS] = {};
    int32_t xluStart = -1;
    f32 scale = 0.0f;
    s32 xluSeg8TexScroll = 0;
    s32 drawKind = CW_DRAW_KIND_SIMPLE;
    int32_t n = GetItem_GetDrawTableEntry((s32)it->second.drawId, dls, CW_DRAW_MAX_DLISTS, &xluStart, &scale,
                                          &xluSeg8TexScroll, &drawKind);
    if (n <= 0) {
        return 0;
    }
    out->dlistCount = n;
    out->xluStartIndex = xluStart;
    out->scale = scale;
    out->hasEnvColor = 0;
    out->xluSeg8TexScroll = xluSeg8TexScroll;
    out->drawKind = drawKind;
    for (int32_t i = 0; i < n; i++) {
        out->dlists[i] = (const char*)dls[i];
    }
    // ComboShip: some MM item bodies sample an animated segment-8 material their draw func binds via
    // AnimatedMat_Draw (Moon's Tear, fairy bottle). z_draw.c can't carry that across, so report the
    // texanim resource for the consumer to replicate (ComboForeignTexAnim_Run). Matched by DL string
    // (separate TUs hold distinct `static` copies of the path literal).
    if (n >= 1 && dls[0] != NULL && strcmp((const char*)dls[0], gGiMoonsTearItemDL) == 0) {
        out->matAnimPath = gGiMoonsTearTexAnim; // MM's own path; consumer loads via CrossRMRegistry("mm")
        out->matAnimBindOpa = 1;                // the tear body (OPA) samples the animated segment
        out->matAnimBillboard = 1;              // the glow (XLU) billboards toward the camera
    } else if (n >= 1 && dls[0] != NULL && strcmp((const char*)dls[0], gGiFairyBottleEmptyDL) == 0) {
        out->matAnimPath = gGiFairyBottleTexAnim; // GetItem_DrawFairyContainer's AnimatedMat_Draw
        out->matAnimBindOpa = 1;
    }
    return 1;
}

extern "C" __declspec(dllexport) int32_t MM_GetItemDrawInfo(const char* itemName, CwItemDrawInfo* out) {
    if (itemName == nullptr || out == nullptr) {
        return 0;
    }
    RandoItemId id = Rando::StaticData::GetItemIdFromDisplayName(itemName);
    if (id == RI_UNKNOWN) {
        id = Rando::StaticData::GetItemIdFromName(itemName); // sentinel / raw RI_
    }
    if (id == RI_UNKNOWN) {
        return 0;
    }
    *out = CwItemDrawInfo{};
    // We run on OOT's graph thread while MM is dormant; never let an exception unwind across the C
    // ABI into soh.dll.
    try {
        if (!MM_FillItemDrawInfo(id, out)) {
            return 0;
        }
    } catch (...) { return 0; }
    out->stateDependent = (MM_IsProgressiveItem(id) || MM_IsStateDependentDraw(id)) ? 1 : 0;
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
    // ComboShip: itemName is the friendly combo-spoiler name; fall back to the RI_ spoilerName.
    RandoItemId animId = Rando::StaticData::GetItemIdFromDisplayName(itemName);
    if (animId == RI_UNKNOWN) {
        animId = Rando::StaticData::GetItemIdFromName(itemName);
    }
    switch (animId) {
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
