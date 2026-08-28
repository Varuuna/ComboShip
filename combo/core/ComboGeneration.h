// ComboShip: the cross-world fill worker and the main-thread finalize that applies its result.
#pragma once

#include <filesystem>
#include <string>
#include <nlohmann/json.hpp>

#include "rando/CrossWorldRando.h"

int ComboRandRange(int minV, int maxV);
void Combo_FireGenRollHooksOnce(uint64_t masterSeed, bool force = false);

// Writes a seed's spoiler under its hash-icon name; pointing the CVar at it is a separate
// main-thread step (RememberComboSpoiler writes a CVar and saves the config).
std::filesystem::path WriteComboSpoiler(const nlohmann::json& fileHash, const std::string& json);
void RememberComboSpoiler(const std::filesystem::path& path);

void RunComboFill(std::string inputSeed, ComboRando::ComboGenProgress* progress);
int RunComboGenTest(int numSeeds, uint32_t seedBase);
void RunComboPlaythrough(const std::string& inputSeed);
void Combo_OnGenerateRequest(const char* inputSeed, ComboRando::ComboGenProgress* progress);
void Combo_OnGenerateThreaded(const char* inputSeed);
void Combo_FinalizeGenerate();
int Combo_PollFinalize();
