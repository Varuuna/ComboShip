// mm/2s2h/Rando/MiscBehavior/AgeRequirements.h
// ComboShip (Cross-Games Age): OoTMM's MM age requirements. Each MM item (AgeReqList.h) can be usable at any age, by
// child Link only or by adult Link only (option RO_AGE_REQ_<item>, RandoOptionAgeReq); "Random" is resolved from the
// seed when the save is created (OoTMM's randomizeMmAgeRequirements, including its dependencies).
//
// They only apply while MM can have an adult Link (Combo_AdultLink_AgeSystemOn); otherwise new saves get "any age" for
// every item, so a seed can never ask for an age MM can't be. See docs/deviations/rando.md.
#ifndef COMBO_AGE_REQUIREMENTS_H
#define COMBO_AGE_REQUIREMENTS_H

#ifdef COMBO_BUILD

#ifdef __cplusplus
#include <cstdint>
extern "C" {
#endif

// The current save enforces age requirements.
int Combo_AgeReq_Active(void);
// Link, at his current MM age, may use this MM ItemId (always 1 when requirements are off).
int Combo_AgeReq_CanUseItem(int item);
// Link may use his strength upgrades at his current age (Goron's Bracelet / Silver / Golden Gauntlets).
int Combo_AgeReq_StrengthAllowed(void);

// ---- Logic (Logic.h: HAS_ITEM, HAS_TRADE_ITEM) ----
// The age MM's logic is evaluating: -1 = Link's actual age (check tracker), 0 = child, 1 = adult (the combo oracle runs
// one pass per age).
extern int gComboLogicAge;
// The save's requirement for this MM ItemId lets the logic's age use it. Reads the save's options directly (they are
// "any age" whenever MM has no adult Link).
int Combo_AgeReq_LogicAllowsItem(int item);

#ifdef __cplusplus
}

namespace Rando::AgeReq {
// New save: resolves RO_AGE_RANDOM from `seed` in `options` (an RO_MAX array), or clears every requirement when MM has
// no adult Link. The combo logic must call this with the same seed and options to agree with the save.
void Resolve(uint32_t seed, uint32_t* options, bool ageSystemOn);
// Hooks for a loaded save (from MiscBehavior::OnFileLoad).
void Init();
} // namespace Rando::AgeReq
#endif

#endif // COMBO_BUILD
#endif // COMBO_AGE_REQUIREMENTS_H
