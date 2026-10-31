// mm/2s2h/Rando/MiscBehavior/CrossItemAssets.h
// ComboShip (cross-game items): OOT textures used by MM's cross-game items, as "__OTR__@oot:" routed paths
// (libultraship resolves them against OOT's ResourceManager: interpreter LoadTextureResourceRouted and
// Fast3dGui::LoadGuiTexture). Declared like the generated asset headers, ALIGN_ASSET(2): the interpreter
// only treats an EVEN pointer as a resource path, so a bare string literal could be read as raw texels.
// C-compatible; included from z_inventory.c, z_kaleido_scope_NES.c and CrossGear.cpp.
#ifndef COMBO_CROSS_ITEM_ASSETS_H
#define COMBO_CROSS_ITEM_ASSETS_H

#ifdef COMBO_BUILD

#include "align_asset_macro.h"

#define COMBO_OOT_ICON(name) "__OTR__@oot:textures/icon_item_static/" name
#define COMBO_OOT_NAME(name) "__OTR__@oot:textures/item_name_static/" name
#define COMBO_OOT_LINK_BOY(name) "__OTR__@oot:objects/object_link_boy/" name

// Item icons (32x32 RGBA32, same format as MM's icon_item_static).
static const ALIGN_ASSET(2) char gComboIconBootsIronTex[] = COMBO_OOT_ICON("gItemIconBootsIronTex");
static const ALIGN_ASSET(2) char gComboIconBootsHoverTex[] = COMBO_OOT_ICON("gItemIconBootsHoverTex");
static const ALIGN_ASSET(2) char gComboIconTunicGoronTex[] = COMBO_OOT_ICON("gItemIconTunicGoronTex");
static const ALIGN_ASSET(2) char gComboIconTunicZoraTex[] = COMBO_OOT_ICON("gItemIconTunicZoraTex");
static const ALIGN_ASSET(2) char gComboIconScaleSilverTex[] = COMBO_OOT_ICON("gItemIconScaleSilverTex");
static const ALIGN_ASSET(2) char gComboIconScaleGoldenTex[] = COMBO_OOT_ICON("gItemIconScaleGoldenTex");
static const ALIGN_ASSET(2) char gComboIconGoronsBraceletTex[] = COMBO_OOT_ICON("gItemIconGoronsBraceletTex");
static const ALIGN_ASSET(2) char gComboIconSilverGauntletsTex[] = COMBO_OOT_ICON("gItemIconSilverGauntletsTex");
static const ALIGN_ASSET(2) char gComboIconGoldenGauntletsTex[] = COMBO_OOT_ICON("gItemIconGoldenGauntletsTex");
static const ALIGN_ASSET(2) char gComboIconMaskGerudoTex[] = COMBO_OOT_ICON("gItemIconMaskGerudoTex");
static const ALIGN_ASSET(2) char gComboIconMaskSkullTex[] = COMBO_OOT_ICON("gItemIconMaskSkullTex");
static const ALIGN_ASSET(2) char gComboIconMaskSpookyTex[] = COMBO_OOT_ICON("gItemIconMaskSpookyTex");
static const ALIGN_ASSET(2) char gComboIconDinsFireTex[] = COMBO_OOT_ICON("gItemIconDinsFireTex");
static const ALIGN_ASSET(2) char gComboIconFaroresWindTex[] = COMBO_OOT_ICON("gItemIconFaroresWindTex");
static const ALIGN_ASSET(2) char gComboIconNayrusLoveTex[] = COMBO_OOT_ICON("gItemIconNayrusLoveTex");
static const ALIGN_ASSET(2) char gComboIconHammerTex[] = COMBO_OOT_ICON("gItemIconHammerTex");
static const ALIGN_ASSET(2) char gComboIconBoomerangTex[] = COMBO_OOT_ICON("gItemIconBoomerangTex");
static const ALIGN_ASSET(2) char gComboIconSlingshotTex[] = COMBO_OOT_ICON("gItemIconSlingshotTex");
static const ALIGN_ASSET(2) char gComboIconShieldDekuTex[] = COMBO_OOT_ICON("gItemIconShieldDekuTex");

// Pause-screen item names (128x16 IA4, same format as MM's item_name_static).
static const ALIGN_ASSET(2) char gComboNameIronBootsTex[] = COMBO_OOT_NAME("gIronBootsItemNameENGTex");
static const ALIGN_ASSET(2) char gComboNameHoverBootsTex[] = COMBO_OOT_NAME("gHoverBootsItemNameENGTex");
static const ALIGN_ASSET(2) char gComboNameGoronTunicTex[] = COMBO_OOT_NAME("gGoronTunicItemNameENGTex");
static const ALIGN_ASSET(2) char gComboNameZoraTunicTex[] = COMBO_OOT_NAME("gZoraTunicItemNameENGTex");
static const ALIGN_ASSET(2) char gComboNameGerudoMaskTex[] = COMBO_OOT_NAME("gGerudoMaskItemNameENGTex");
static const ALIGN_ASSET(2) char gComboNameSkullMaskTex[] = COMBO_OOT_NAME("gSkullMaskItemNameENGTex");
static const ALIGN_ASSET(2) char gComboNameSpookyMaskTex[] = COMBO_OOT_NAME("gSpookyMaskItemNameENGTex");
static const ALIGN_ASSET(2) char gComboNameDinsFireTex[] = COMBO_OOT_NAME("gDinsFireItemNameENGTex");
static const ALIGN_ASSET(2) char gComboNameFaroresWindTex[] = COMBO_OOT_NAME("gFaroresWindItemNameENGTex");
static const ALIGN_ASSET(2) char gComboNameNayrusLoveTex[] = COMBO_OOT_NAME("gNayrusLoveItemNameENGTex");
static const ALIGN_ASSET(2) char gComboNameMegatonHammerTex[] = COMBO_OOT_NAME("gMegatonHammerItemNameENGTex");
static const ALIGN_ASSET(2) char gComboNameBoomerangTex[] = COMBO_OOT_NAME("gBoomerangItemNameENGTex");
static const ALIGN_ASSET(2) char gComboNameFairySlingshotTex[] = COMBO_OOT_NAME("gFairySlingshotItemNameENGTex");

// OOT items in MM (CrossUpgrades.cpp): the first-tier ocarina shows OOT's own icon and name (the Short Hookshot keeps
// MM's Hookshot icon), the Stone of Agony OOT's 24x24 quest icon (HUD indicator and menus).
static const ALIGN_ASSET(2) char gComboIconOcarinaFairyTex[] = COMBO_OOT_ICON("gItemIconOcarinaFairyTex");
static const ALIGN_ASSET(2) char gComboIconStoneOfAgonyTex[] =
    "__OTR__@oot:textures/icon_item_24_static/gQuestIconStoneOfAgonyTex";
static const ALIGN_ASSET(2) char gComboNameFairyOcarinaTex[] = COMBO_OOT_NAME("gFairyOcarinaItemNameENGTex");

// Boot textures for the boots drawn on Link (OoTMM's child-sized boot meshes, CrossGear.cpp).
// Cross-Games Age: OOT adult Link's own boots (they load OOT adult limb matrices from segment 0x0D, and adult Link in
// MM runs on the same skeleton).
static const ALIGN_ASSET(2) char gComboLinkAdultLeftIronBootDL[] = COMBO_OOT_LINK_BOY("gLinkAdultLeftIronBootDL");
static const ALIGN_ASSET(2) char gComboLinkAdultRightIronBootDL[] = COMBO_OOT_LINK_BOY("gLinkAdultRightIronBootDL");
static const ALIGN_ASSET(2) char gComboLinkAdultLeftHoverBootDL[] = COMBO_OOT_LINK_BOY("gLinkAdultLeftHoverBootDL");
static const ALIGN_ASSET(2) char gComboLinkAdultRightHoverBootDL[] = COMBO_OOT_LINK_BOY("gLinkAdultRightHoverBootDL");
static const ALIGN_ASSET(2) char gComboLinkIronBootTex[] = COMBO_OOT_LINK_BOY("gLinkAdultIronBootTex");
static const ALIGN_ASSET(2) char gComboLinkIronBootTLUT[] = COMBO_OOT_LINK_BOY("object_link_boyTLUT_00CB40");
static const ALIGN_ASSET(2) char gComboLinkHoverBootsHeelTex[] = COMBO_OOT_LINK_BOY("gLinkAdultHoverBootsHeelTex");
static const ALIGN_ASSET(2) char gComboLinkHoverBootsJetTex[] = COMBO_OOT_LINK_BOY("gLinkAdultHoverBootsJetTex");
static const ALIGN_ASSET(2) char gComboLinkHoverBootsFeatherTex[] =
    COMBO_OOT_LINK_BOY("gLinkAdultHoverBootsFeatherTex");

// OOT masks worn by child Link (object_link_child), drawn with MM human Link's head matrix (CrossGear.cpp).
#define COMBO_OOT_LINK_CHILD(name) "__OTR__@oot:objects/object_link_child/" name
static const ALIGN_ASSET(2) char gComboLinkChildGerudoMaskDL[] = COMBO_OOT_LINK_CHILD("gLinkChildGerudoMaskDL");
static const ALIGN_ASSET(2) char gComboLinkChildSkullMaskDL[] = COMBO_OOT_LINK_CHILD("gLinkChildSkullMaskDL");
static const ALIGN_ASSET(2) char gComboLinkChildSpookyMaskDL[] = COMBO_OOT_LINK_CHILD("gLinkChildSpookyMaskDL");

#endif // COMBO_BUILD
#endif // COMBO_CROSS_ITEM_ASSETS_H
