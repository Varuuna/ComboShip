// ComboShip: routing a foreign item into the other game's resident save (issue #3), shared by
// local collection and the Anchor receive path.
#pragma once

#include <cstdint>

void ResetCrossItemDedupForSeed(uint32_t seed);
void DeliverCrossItem(int targetGame, const char* itemName, const char* srcCheckName);
void PumpDormant();
void MarkForeignObtained(int srcGame, const char* checkName);
