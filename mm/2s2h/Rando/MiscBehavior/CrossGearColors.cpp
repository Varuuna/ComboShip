// mm/2s2h/Rando/MiscBehavior/CrossGearColors.cpp
// ComboShip: MM's cosmetic editor colors for the Ocarina of Time models Link wears in Majora's Mask: adult Link's hair,
// linen and boots (Cross-Games Age, AdultLink.cpp), the Goron's Bracelet and the gauntlet gems (CrossGear.cpp).
//
// Ship of Harkinian colors these by patching the display lists in place (CosmeticsEditor.cpp
// ApplyOrResetCustomGfxPatches, "Link.Hair" / "Link.Linen" / "Link.Boots" / "Gloves.GoronBracelet" /
// "Gloves.GauntletsGem"). MM draws the very same lists out of OOT's archive, so patching them would recolor OOT's Link
// too. Instead, while one of MM's options is set, MM draws its own copy of each list with SoH's patches applied (same
// command indices), bracketed by G_COMBO_RM_PUSH("oot") so the copied references still resolve in OOT's archive. Unset,
// MM draws OOT's list, so the colors follow OOT's cosmetic editor, as before.
#ifdef COMBO_BUILD

#include "CrossGear.h"
#include "2s2h/Rando/Rando.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <fast/resource/ResourceType.h>
#include <fast/resource/type/DisplayList.h>
#include <ship/resource/CrossRMRegistry.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/ResourceManagerScope.h>
#include <spdlog/spdlog.h>

extern "C" {
#include "variables.h"
}

extern "C" Color_RGB8 CosmeticEditor_GetComboColor(const char* id, Color_RGB8 defaultColor); // CosmeticEditor.cpp
extern "C" int CosmeticEditor_IsComboColorChanged(const char* id);

namespace {

enum Option : uint8_t { HAIR, LINEN, BOOTS, BRACELET, GEM, OPTION_COUNT };

struct OptionDef {
    const char* id;
    Color_RGB8 defaultColor;
};

// Defaults are Ship of Harkinian's (the models' own colors).
constexpr OptionDef kOptions[OPTION_COUNT] = {
    { "Player.AdultHair", { 255, 173, 27 } },       { "Player.AdultLinen", { 255, 255, 255 } },
    { "Player.AdultBoots", { 93, 44, 18 } },        { "Player.OotGoronBracelet", { 255, 255, 170 } },
    { "Player.OotGauntletsGem", { 255, 60, 100 } },
};

enum Kind : uint8_t {
    PRIM,       // gsDPSetPrimColor(option)
    GRAY_COLOR, // gsDPSetGrayscaleColor(option)
    GRAY_ON,    // gsSPGrayscale(true)
    GRAY_OFF,   // gsSPGrayscale(false)
    PRIM_WHITE, // gsDPSetPrimColor(white): keeps the white parts white once linen is tinted
};

struct Patch {
    const char* dl;
    uint16_t index;
    Kind kind;
    Option option;
};

// Ship of Harkinian's patches for adult Link and the child Goron's Bracelet, index for index.
constexpr Patch kPatches[] = {
    // Hair
    { "gLinkAdultHeadNearDL", 10, GRAY_COLOR, HAIR },
    { "gLinkAdultHeadFarDL", 10, GRAY_COLOR, HAIR },
    { "gLinkAdultHeadNearDL", 125, GRAY_ON, HAIR },
    { "gLinkAdultHeadNearDL", 159, GRAY_OFF, HAIR },
    { "gLinkAdultHeadFarDL", 102, GRAY_ON, HAIR },
    { "gLinkAdultHeadFarDL", 122, GRAY_OFF, HAIR },
    // Linen
    { "gLinkAdultLeftArmNearDL", 30, PRIM, LINEN },
    { "gLinkAdultLeftArmNearDL", 83, PRIM, LINEN },
    { "gLinkAdultLeftArmOutNearDL", 25, PRIM, LINEN },
    { "gLinkAdultLeftArmFarDL", 30, PRIM, LINEN },
    { "gLinkAdultLeftArmFarDL", 70, PRIM, LINEN },
    { "gLinkAdultRightArmFarDL", 30, PRIM, LINEN },
    { "gLinkAdultRightArmFarDL", 70, PRIM, LINEN },
    { "gLinkAdultRightArmNearDL", 30, PRIM, LINEN },
    { "gLinkAdultLeftShoulderFarDL", 55, PRIM, LINEN },
    { "gLinkAdultLeftShoulderNearDL", 57, PRIM, LINEN },
    { "gLinkAdultRightShoulderNearDL", 57, PRIM, LINEN },
    { "gLinkAdultRightShoulderFarDL", 55, PRIM, LINEN },
    { "gLinkAdultTorsoNearDL", 66, PRIM, LINEN },
    { "gLinkAdultTorsoFarDL", 57, PRIM, LINEN },
    { "gLinkAdultRightThighNearDL", 53, PRIM, LINEN },
    { "gLinkAdultLeftThighNearDL", 53, PRIM, LINEN },
    { "gLinkAdultRightThighFarDL", 54, PRIM, LINEN },
    { "gLinkAdultLeftThighFarDL", 54, PRIM, LINEN },
    { "gLinkAdultRightLegNearDL", 30, PRIM, LINEN },
    { "gLinkAdultLeftLegNearDL", 30, PRIM, LINEN },
    { "gLinkAdultRightLegFarDL", 30, PRIM, LINEN },
    { "gLinkAdultLeftLegFarDL", 30, PRIM, LINEN },
    { "gLinkAdultLeftArmFarDL", 35, PRIM_WHITE, LINEN },
    { "gLinkAdultLeftArmOutNearDL", 45, PRIM_WHITE, LINEN },
    { "gLinkAdultLeftArmNearDL", 40, PRIM_WHITE, LINEN },
    { "gLinkAdultLeftArmFarDL", 77, PRIM_WHITE, LINEN },
    { "gLinkAdultRightArmFarDL", 35, PRIM_WHITE, LINEN },
    { "gLinkAdultRightArmFarDL", 77, PRIM_WHITE, LINEN },
    { "gLinkAdultRightArmNearDL", 42, PRIM_WHITE, LINEN },
    { "gLinkAdultRightLegNearDL", 43, PRIM_WHITE, LINEN },
    { "gLinkAdultLeftLegNearDL", 43, PRIM_WHITE, LINEN },
    { "gLinkAdultRightLegFarDL", 38, PRIM_WHITE, LINEN },
    { "gLinkAdultLeftLegFarDL", 38, PRIM_WHITE, LINEN },
    // Boots
    { "gLinkAdultRightLegNearDL", 10, GRAY_COLOR, BOOTS },
    { "gLinkAdultRightLegFarDL", 10, GRAY_COLOR, BOOTS },
    { "gLinkAdultRightLegNearDL", 57, GRAY_ON, BOOTS },
    { "gLinkAdultRightLegFarDL", 52, GRAY_ON, BOOTS },
    { "gLinkAdultLeftLegNearDL", 57, GRAY_ON, BOOTS },
    { "gLinkAdultLeftLegFarDL", 52, GRAY_ON, BOOTS },
    { "gLinkAdultLeftThighNearDL", 10, GRAY_OFF, BOOTS },
    { "gLinkAdultLeftThighFarDL", 10, GRAY_OFF, BOOTS },
    { "gLinkAdultHeadNearDL", 20, GRAY_OFF, BOOTS },
    { "gLinkAdultHeadFarDL", 20, GRAY_OFF, BOOTS },
    // Goron's Bracelet (child Link's, worn by human Link in MM)
    { "gLinkChildGoronBraceletDL", 3, GRAY_COLOR, BRACELET },
    { "gLinkChildGoronBraceletDL", 11, GRAY_ON, BRACELET },
    { "gLinkChildGoronBraceletDL", 39, GRAY_OFF, BRACELET },
    // Gauntlet gems (adult Link's gauntlets)
    { "gLinkAdultLeftGauntletPlate2DL", 42, PRIM, GEM },
    { "gLinkAdultRightGauntletPlate2DL", 42, PRIM, GEM },
    { "gLinkAdultLeftGauntletPlate3DL", 42, PRIM, GEM },
    { "gLinkAdultRightGauntletPlate3DL", 42, PRIM, GEM },
};

bool IsTwoWordOpcode(uint8_t op) {
    return (op == G_SETTIMG_OTR_HASH) || (op == G_DL_OTR_HASH) || (op == G_VTX_OTR_HASH) || (op == G_BRANCH_Z_OTR) ||
           (op == G_MARKER) || (op == G_MTX_OTR) || (op == G_MOVEMEM_OTR);
}

struct Recolored {
    bool tried = false;
    std::shared_ptr<Ship::IResource> res; // keeps OOT's list alive
    std::vector<Gfx> gfx;                 // PUSH("oot"), OOT's commands, POP, END
    std::vector<const Patch*> patches;
};

std::unordered_map<std::string, Recolored> sRecolored; // by OOT path ("objects/<object>/<list>")

void ReleaseRecolored() { // CrossRMRegistry teardown: drop cross-module refs while OOT's RM is still mapped
    sRecolored.clear();
}

// OOT's list copied between an RM bracket, or false (OOT not resident yet, or a custom model whose commands don't
// match Ship of Harkinian's indices).
bool Build(Recolored& r, const std::string& path) {
    auto rm = Ship::CrossRMRegistry::Get("oot");
    if (rm == nullptr) {
        return false; // try again later
    }
    r.tried = true;
    static bool sListening = false;
    if (!sListening) {
        sListening = true;
        Ship::CrossRMRegistry::RegisterTeardownListener(ReleaseRecolored);
    }
    {
        Ship::ResourceManagerScope rmScope(rm);
        r.res = rm->LoadResource(path);
    }
    if ((r.res == nullptr) || (r.res->GetInitData()->Type != static_cast<uint32_t>(Fast::ResourceType::DisplayList)) ||
        r.res->GetInitData()->IsCustom) {
        r.res = nullptr;
        return false;
    }
    const std::vector<Gfx>& in = std::static_pointer_cast<Fast::DisplayList>(r.res)->Instructions;
    Gfx push = gsSPComboRMPush("oot");
    r.gfx.push_back(push);
    size_t i = 0;
    while (i < in.size()) {
        const uint8_t op = (uint8_t)(in[i].words.w0 >> 24);
        if (op == G_ENDDL) {
            break;
        }
        i += IsTwoWordOpcode(op) ? 2 : 1;
    }
    r.gfx.insert(r.gfx.end(), in.begin(), in.begin() + std::min(i, in.size()));
    for (const Patch* p : r.patches) {
        if (p->index >= i) {
            SPDLOG_ERROR("[ComboShip] cosmetic patch {} past the end of {}", p->index, path);
            r.gfx.clear();
            return false;
        }
    }
    Gfx pop = gsSPComboRMPop();
    Gfx end = gsSPEndDisplayList();
    r.gfx.push_back(pop);
    r.gfx.push_back(end);
    return true;
}

Gfx Command(const Patch& p, const Color_RGB8& color) {
    switch (p.kind) {
        case PRIM: {
            Gfx g = gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255);
            return g;
        }
        case GRAY_COLOR: {
            Gfx g = gsDPSetGrayscaleColor(color.r, color.g, color.b, 255);
            return g;
        }
        case GRAY_ON: {
            Gfx g = gsSPGrayscale(true);
            return g;
        }
        case GRAY_OFF: {
            Gfx g = gsSPGrayscale(false);
            return g;
        }
        default: {
            Gfx g = gsDPSetPrimColor(0, 0, 255, 255, 255, 255);
            return g;
        }
    }
}

} // namespace

extern "C" Color_RGB8 Combo_Gear_CosmeticColor(int option) {
    if ((option < 0) || (option >= OPTION_COUNT)) {
        return { 255, 255, 255 };
    }
    return CosmeticEditor_GetComboColor(kOptions[option].id, kOptions[option].defaultColor);
}

extern "C" const void* Combo_Gear_Recolor(const void* dList) {
    static constexpr char kPrefix[] = "__OTR__@oot:";
    const char* str = (const char*)dList;
    if ((str == nullptr) || (strncmp(str, kPrefix, sizeof(kPrefix) - 1) != 0)) {
        return dList;
    }
    const char* name = strrchr(str, '/');
    if (name == nullptr) {
        return dList;
    }
    name++;
    bool changed[OPTION_COUNT];
    bool any = false;
    for (int o = 0; o < OPTION_COUNT; o++) {
        changed[o] = CosmeticEditor_IsComboColorChanged(kOptions[o].id) != 0;
        any = any || changed[o];
    }
    if (!any) {
        return dList;
    }
    // Only lists with a patch for a set option get a copy.
    bool wanted = false;
    for (const Patch& p : kPatches) {
        if (changed[p.option] && (strcmp(p.dl, name) == 0)) {
            wanted = true;
            break;
        }
    }
    if (!wanted) {
        return dList;
    }
    const std::string path(str + sizeof(kPrefix) - 1);
    Recolored& r = sRecolored[path];
    if (!r.tried) {
        r.patches.clear();
        for (const Patch& p : kPatches) {
            if (strcmp(p.dl, name) == 0) {
                r.patches.push_back(&p);
            }
        }
        if (!Build(r, path)) {
            return dList;
        }
    }
    if (r.gfx.empty()) {
        return dList;
    }
    // Rewrite every patch point of this list each draw: OOT's own command for unset options, ours for set ones.
    const std::vector<Gfx>& in = std::static_pointer_cast<Fast::DisplayList>(r.res)->Instructions;
    for (const Patch* p : r.patches) {
        r.gfx[1 + p->index] =
            changed[p->option]
                ? Command(*p, CosmeticEditor_GetComboColor(kOptions[p->option].id, kOptions[p->option].defaultColor))
                : in[p->index];
    }
    return r.gfx.data();
}

#endif // COMBO_BUILD
