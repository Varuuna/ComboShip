// soh/soh/Enhancements/combo/ComboMmAssets.h
// ComboShip (cross-game items): MM assets used by OOT's cross-game items, as "__OTR__@mm:" routed paths.
// libultraship resolves them against MM's ResourceManager (interpreter LoadTextureResourceRouted /
// Fast3dGui::LoadGuiTexture for textures, the DL route marker for display lists). Declared like the generated
// asset headers, ALIGN_ASSET(2): the interpreter only treats an EVEN pointer as a resource path.
// C-compatible; included from z_inventory.c and ComboMmMasks.cpp. Mirror of
// mm/2s2h/Rando/MiscBehavior/CrossItemAssets.h.
#ifndef COMBO_MM_ASSETS_H
#define COMBO_MM_ASSETS_H

#ifdef COMBO_BUILD

#include "align_asset_macro.h"

#define COMBO_MM_ASSET(path) "__OTR__@mm:" path

// Item icons (32x32 RGBA32, same format as OOT's icon_item_static).
static const ALIGN_ASSET(2) char gComboMmIconBlastMaskTex[] =
    COMBO_MM_ASSET("icon_item_static_yar/gItemIconBlastMaskTex");
static const ALIGN_ASSET(2) char gComboMmIconStoneMaskTex[] =
    COMBO_MM_ASSET("icon_item_static_yar/gItemIconStoneMaskTex");
static const ALIGN_ASSET(2) char gComboMmIconKamaroMaskTex[] =
    COMBO_MM_ASSET("icon_item_static_yar/gItemIconKamaroMaskTex");

// Pause-menu item names (128x16 IA4, the format OOT's item_name_static uses too).
static const ALIGN_ASSET(2) char gComboMmNameBlastMaskTex[] =
    COMBO_MM_ASSET("item_name_static/gItemNameBlastMaskENGTex");
static const ALIGN_ASSET(2) char gComboMmNameStoneMaskTex[] =
    COMBO_MM_ASSET("item_name_static/gItemNameStoneMaskENGTex");
static const ALIGN_ASSET(2) char gComboMmNameKamaroMaskTex[] =
    COMBO_MM_ASSET("item_name_static/gItemNameKamarosMaskENGTex");

// Masks worn on Link's head (MM's player mask DLs; they expect the head limb matrix to be loaded).
static const ALIGN_ASSET(2) char gComboMmBlastMaskDL[] =
    COMBO_MM_ASSET("objects/object_mask_bakuretu/object_mask_bakuretu_DL_0005C0");
static const ALIGN_ASSET(2) char gComboMmBlastMaskFuseDL[] =
    COMBO_MM_ASSET("objects/object_mask_bakuretu/object_mask_bakuretu_DL_000440");
static const ALIGN_ASSET(2) char gComboMmStoneMaskDL[] =
    COMBO_MM_ASSET("objects/object_mask_stone/object_mask_stone_DL_000820");
static const ALIGN_ASSET(2) char gComboMmKamaroMaskDL[] =
    COMBO_MM_ASSET("objects/object_mask_dancer/object_mask_dancer_DL_000EF0");

#endif // COMBO_BUILD
#endif // COMBO_MM_ASSETS_H
