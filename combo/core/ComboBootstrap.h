// ComboShip: startup checks — which archives/config exist, plus the JSON helpers the
// first-launch settings import uses.
#pragma once

#include <string>
#include <nlohmann/json.hpp>

bool OOTArchivesExist();
bool MMRomArchiveExists();
bool MMArchivesExist();
bool ComboConfigExists();
bool LoadJsonObject(const std::string& path, nlohmann::json& out);
void DeepMerge(nlohmann::json& base, const nlohmann::json& overlay);
int LauncherValidateShipConfig(const char* path);
