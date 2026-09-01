// ComboShip: pushing the slot's hint state into comboui and recording reveals reported by
// either game (#164).
#pragma once

#include <nlohmann/json.hpp>

void Combo_PushHintTrackerData(int slot);
void Combo_RecordHintRead(int fileNum, const char* bucket, const nlohmann::json& value,
                          const char* matchField = nullptr);
void Combo_OnOotHintRevealed(int fileNum, const char* comboKey);
void Combo_OnMmHintRevealed(int fileNum, int kind, int poolIndex, const char* key, const char* text);
