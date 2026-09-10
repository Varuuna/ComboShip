// ComboShip: locating and validating a consolidated seed file on disk.
#pragma once

#include <filesystem>
#include <string>
#include <nlohmann/json.hpp>

bool TryLoadComboSeedFile(const std::filesystem::path& p, nlohmann::json& out);
std::filesystem::path ResolveComboSeedPath(const std::string& file);
std::filesystem::path FindNewestComboSeed(nlohmann::json& out);
