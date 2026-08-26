#pragma once

#ifndef __cplusplus
#error This header should not be used in C files
#endif

#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/custom-message/text.h"

namespace Rando {
namespace Traps {
/// @brief A fake item name for an ice trap, with the article to use when it appears in a sentence
struct TrickName {
    Text name;
    Text article;
};
TrickName GetTrapName(uint16_t id, uint64_t* state = nullptr);
RandomizerGet GetTrapTrickModel(uint64_t* state = nullptr);
// ComboShip: true if an item id has a fake ice-trap name (i.e. is a valid disguise) — guards the dump
// export, curated-set restore, and old-seed placed-item fallback, since GetTrapName asserts on unnamed items.
bool CanBeTrapModel(uint16_t id);
bool ShouldJunkItemBeTrap();
void BuildIceTrapMessage(CustomMessage& msg, GetItemEntry getItemEntry);
#ifdef COMBO_BUILD
// ComboShip: same taunt tables, but naming an arbitrary item — a foreign trap's disguise may be an
// item of the OTHER game, which has no RandomizerGet to resolve a name from.
void BuildIceTrapMessageNamed(CustomMessage& msg, const std::string& itemName);
// ComboShip: every English trick name for an id, so the dump can hand them to the cross-world layer.
std::vector<std::string> GetTrickNamesEnglish(uint16_t id);
#endif
} // namespace Traps
} // namespace Rando