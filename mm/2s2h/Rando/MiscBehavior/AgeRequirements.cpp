// mm/2s2h/Rando/MiscBehavior/AgeRequirements.cpp
// ComboShip (Cross-Games Age): OoTMM's MM age requirements. See AgeRequirements.h.
//
// Ported from OoTMM (MIT License, Copyright (c) 2020-2022 OoTMM Team, https://github.com/OoTMM/OoTMM):
// packages/core/src/mm-age-requirements.ts (the item list and the Random solver) and packages/generator/src/common/
// age.c (comboCheckItemAgeReqMm). In game, OoTMM refuses to equip an item at the wrong age; ComboShip greys it out like
// MM's own form restrictions (VB_ITEM_BE_RESTRICTED: C buttons, D-pad and the pause menu), so it can still be
// assigned but not used.
#ifdef COMBO_BUILD

#include "AgeRequirements.h"
#include "AdultLink.h"
#include "CrossGear.h"
#include "MiscBehavior.h"
#include "2s2h/Rando/Rando.h"
#include "2s2h/GameInteractor/GameInteractor.h"

#include <array>
#include <vector>

extern "C" {
#include "variables.h"
#include "functions.h"
#include "z64item.h"
}

namespace {

struct AgeReqItem {
    RandoOptionId option;
    const char* label;
    const char* ootmmId;
    s32 item;
    RandoOptionId poolOption;
};

const AgeReqItem kItems[] = {
#define AGE_REQ(suffix, label, ootmmId, item, poolOption) { RO_AGE_REQ_##suffix, label, ootmmId, item, poolOption },
#include "AgeReqList.h"
#undef AGE_REQ
};
constexpr size_t kItemCount = sizeof(kItems) / sizeof(kItems[0]);

size_t IndexOf(RandoOptionId option) {
    return (size_t)(option - RO_AGE_REQ_HOOKSHOT);
}

// The requirement in effect for an option: RO_AGE_ANY when the save doesn't enforce them.
u32 Requirement(RandoOptionId option) {
    if (!Combo_AgeReq_Active()) {
        return RO_AGE_ANY;
    }
    return RANDO_SAVE_OPTIONS[option];
}

bool AgeAllows(u32 requirement) {
    switch (requirement) {
        case RO_AGE_CHILD_ONLY:
            return !Combo_AdultLink_IsAdultAge();
        case RO_AGE_ADULT_ONLY:
            return Combo_AdultLink_IsAdultAge() != 0;
        default:
            return true;
    }
}

// ---- Random (OoTMM randomizeMmAgeRequirements) ----
enum Side : u8 { SIDE_NONE, SIDE_CHILD, SIDE_ADULT };

struct Dependency {
    RandoOptionId controller;
    std::vector<RandoOptionId> dependents;
};
// MM_AGE_REQ_RESTRICTION_DEPENDENCIES: what a form (or strength) must share an age with.
const std::vector<Dependency> kDependencies = {
    { RO_AGE_REQ_MASK_DEKU, { RO_AGE_REQ_DEED_SWAMP } },
    { RO_AGE_REQ_MASK_GORON, { RO_AGE_REQ_POWDER_KEG, RO_AGE_REQ_DEED_MOUNTAIN } },
    { RO_AGE_REQ_MASK_ZORA, { RO_AGE_REQ_DEED_OCEAN } },
    { RO_AGE_REQ_STRENGTH, { RO_AGE_REQ_POWDER_KEG } },
    // ComboShip addition: elemental arrows are only usable with the Bow at the same age (OoTMM can roll a child Bow
    // with adult Light Arrows, which no age can shoot).
    { RO_AGE_REQ_BOW, { RO_AGE_REQ_ARROW_FIRE, RO_AGE_REQ_ARROW_ICE, RO_AGE_REQ_ARROW_LIGHT } },
};

bool IsController(RandoOptionId option) {
    for (const Dependency& dep : kDependencies) {
        if (dep.controller == option) {
            return true;
        }
    }
    return false;
}

using Assignments = std::array<Side, kItemCount>;

// assignmentsPotentiallyValid: a Powder Keg may differ from its controller only while the Goron Mask and Strength are
// (or may still end up) on opposite sides, since either one lets Link carry it.
bool PotentiallyValid(const Assignments& a, const std::array<bool, kItemCount>& unresolved) {
    const Side goron = a[IndexOf(RO_AGE_REQ_MASK_GORON)];
    const Side strength = a[IndexOf(RO_AGE_REQ_STRENGTH)];
    for (const Dependency& dep : kDependencies) {
        const Side controller = a[IndexOf(dep.controller)];
        if (controller == SIDE_NONE) {
            continue;
        }
        for (RandoOptionId dependentId : dep.dependents) {
            const Side dependent = a[IndexOf(dependentId)];
            if ((dependent == SIDE_NONE) || (dependent == controller)) {
                continue;
            }
            if (dependentId == RO_AGE_REQ_POWDER_KEG) {
                if ((goron != SIDE_NONE) && (strength != SIDE_NONE) && (goron != strength)) {
                    continue;
                }
                if ((goron == SIDE_NONE) && unresolved[IndexOf(RO_AGE_REQ_MASK_GORON)]) {
                    continue;
                }
                if ((strength == SIDE_NONE) && unresolved[IndexOf(RO_AGE_REQ_STRENGTH)]) {
                    continue;
                }
            }
            return false;
        }
    }
    return true;
}

// splitmix64: a private stream so resolving doesn't disturb the seed's other random rolls.
uint64_t NextRandom(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

bool InPool(const AgeReqItem& item, const uint32_t* options) {
    return (item.poolOption == RO_MAX) || (options[item.poolOption] != 0);
}

} // namespace

void Rando::AgeReq::Resolve(uint32_t seed, uint32_t* options, bool ageSystemOn) {
    if (!ageSystemOn) {
        for (const AgeReqItem& item : kItems) {
            options[item.option] = RO_AGE_ANY;
        }
        return;
    }

    Assignments assignments{};
    std::array<bool, kItemCount> unresolved{};
    std::vector<size_t> randomItems;
    for (size_t i = 0; i < kItemCount; i++) {
        const AgeReqItem& item = kItems[i];
        const uint32_t value = options[item.option];
        if (value == RO_AGE_RANDOM) {
            if (InPool(item, options)) {
                randomItems.push_back(i);
            } else {
                options[item.option] = RO_AGE_ANY; // not in this seed: nothing to restrict
            }
        } else if (InPool(item, options)) {
            assignments[i] = (value == RO_AGE_CHILD_ONLY)   ? SIDE_CHILD
                             : (value == RO_AGE_ADULT_ONLY) ? SIDE_ADULT
                                                            : SIDE_NONE;
        }
    }
    if (randomItems.empty()) {
        return;
    }

    uint64_t state = ((uint64_t)seed << 32) ^ 0x6D6D2D6167652D72ull; // "mm-age-r"
    std::array<Side, kItemCount> preferred{};
    for (size_t i : randomItems) {
        preferred[i] = (NextRandom(state) & 1) ? SIDE_ADULT : SIDE_CHILD;
    }
    std::vector<size_t> order; // controllers first, as OoTMM orders them
    for (size_t i : randomItems) {
        if (IsController(kItems[i].option)) {
            order.push_back(i);
        }
    }
    for (size_t i : randomItems) {
        if (!IsController(kItems[i].option)) {
            order.push_back(i);
        }
    }
    for (size_t i : order) {
        unresolved[i] = true;
    }

    // Depth-first, preferred side first (OoTMM's solve()).
    std::vector<int> tried(order.size(), 0);
    size_t depth = 0;
    bool solved = false;
    while (true) {
        if (depth == order.size()) {
            std::array<bool, kItemCount> none{};
            if (PotentiallyValid(assignments, none)) {
                solved = true;
                break;
            }
            if (depth == 0) {
                break;
            }
            depth--; // backtrack into the last item's other side
            continue;
        }
        const size_t i = order[depth];
        unresolved[i] = false;
        bool advanced = false;
        while (tried[depth] < 2) {
            const Side side = (tried[depth] == 0)            ? preferred[i]
                              : (preferred[i] == SIDE_CHILD) ? SIDE_ADULT
                                                             : SIDE_CHILD;
            tried[depth]++;
            assignments[i] = side;
            if (PotentiallyValid(assignments, unresolved)) {
                advanced = true;
                break;
            }
        }
        if (advanced) {
            depth++;
            continue;
        }
        assignments[i] = SIDE_NONE;
        unresolved[i] = true;
        tried[depth] = 0;
        if (depth == 0) {
            break;
        }
        depth--;
    }

    for (size_t i : randomItems) {
        if (!solved) {
            options[kItems[i].option] = RO_AGE_ANY; // OoTMM throws; ComboShip falls back to no restriction
            continue;
        }
        options[kItems[i].option] = (assignments[i] == SIDE_ADULT) ? RO_AGE_ADULT_ONLY : RO_AGE_CHILD_ONLY;
    }
    if (!solved) {
        SPDLOG_WARN("[ComboShip] random MM age requirements had no valid assignment; leaving them unrestricted");
    }
}

extern "C" int gComboLogicAge = -1;

namespace {
// The option for an MM ItemId (RO_MAX: none). ComboShip keeps OOT's Fairy Ocarina and Short Hookshot in the Ocarina of
// Time / Hookshot slots, so those follow whichever one Link has; elemental arrows show as bow-and-arrow while drawn.
RandoOptionId OptionForItem(int item) {
    if (item == ITEM_OCARINA_OF_TIME) {
        return Combo_Upgrades_OcarinaIsFairy() ? RO_AGE_REQ_OCARINA_FAIRY : RO_AGE_REQ_OCARINA_OF_TIME;
    }
    if (item == ITEM_HOOKSHOT) {
        return Combo_Upgrades_HookshotIsShort() ? RO_AGE_REQ_HOOKSHOT_SHORT : RO_AGE_REQ_HOOKSHOT;
    }
    if (item == ITEM_BOW_FIRE) {
        item = ITEM_ARROW_FIRE;
    } else if (item == ITEM_BOW_ICE) {
        item = ITEM_ARROW_ICE;
    } else if (item == ITEM_BOW_LIGHT) {
        item = ITEM_ARROW_LIGHT;
    }
    // ItemId -> option, built once (HAS_ITEM asks on every item test during the fill).
    static const auto kTable = [] {
        std::array<RandoOptionId, 256> table;
        table.fill(RO_MAX);
        for (const AgeReqItem& entry : kItems) {
            if ((entry.item != ITEM_NONE) && (entry.item >= 0) && (entry.item < 256)) {
                table[entry.item] = entry.option;
            }
        }
        return table;
    }();
    return ((item >= 0) && (item < 256)) ? kTable[item] : RO_MAX;
}

bool AllowsAge(u32 requirement, bool adult) {
    return !((requirement == RO_AGE_CHILD_ONLY) && adult) && !((requirement == RO_AGE_ADULT_ONLY) && !adult);
}
} // namespace

extern "C" int Combo_AgeReq_LogicAllowsItem(int item) {
    const RandoOptionId option = OptionForItem(item);
    if ((option == RO_MAX) || (RANDO_SAVE_OPTIONS[option] == RO_AGE_ANY)) {
        return 1; // the common case: no requirement (always so without an adult Link)
    }
    const bool adult = (gComboLogicAge >= 0) ? (gComboLogicAge != 0) : (Combo_AdultLink_IsAdultAge() != 0);
    return AllowsAge(RANDO_SAVE_OPTIONS[option], adult);
}

extern "C" int Combo_AgeReq_Active(void) {
    return IS_RANDO && Combo_AdultLink_AgeSystemOn();
}

extern "C" int Combo_AgeReq_CanUseItem(int item) {
    if ((item == ITEM_NONE) || !Combo_AgeReq_Active()) {
        return 1;
    }
    const RandoOptionId option = OptionForItem(item);
    return (option == RO_MAX) || AgeAllows(Requirement(option));
}

extern "C" int Combo_AgeReq_StrengthAllowed(void) {
    return AgeAllows(Requirement(RO_AGE_REQ_STRENGTH));
}

void Rando::AgeReq::Init() {
    // Greyed out at the wrong age, like an item the current form can't use.
    COND_VB_SHOULD(VB_ITEM_BE_RESTRICTED, IS_RANDO, {
        ItemId itemId = *va_arg(args, ItemId*);
        if (!*should && !Combo_AgeReq_CanUseItem(itemId)) {
            *should = true;
        }
    });
}

#endif // COMBO_BUILD
