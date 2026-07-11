#pragma once

#ifndef __cplusplus
#error This header should not be used in C files
#endif

#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/custom-message/text.h"

namespace Rando {
namespace Traps {
Text GetTrapName(uint16_t id, uint64_t* state = nullptr);
RandomizerGet GetTrapTrickModel(uint64_t* state = nullptr);
// ComboShip: true if an item id has a fake ice-trap name (i.e. is a valid disguise). Combo builds
// possibleIceTrapModels from placed items, so it must exclude unnamed junk that GetTrapName can't name.
bool CanBeTrapModel(uint16_t id);
bool ShouldJunkItemBeTrap();
void BuildIceTrapMessage(CustomMessage& msg, GetItemEntry getItemEntry);
} // namespace Traps
} // namespace Rando