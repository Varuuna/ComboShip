#include "core/ComboBootstrap.h"

#include <filesystem>
#include <fstream>

// ---------- O2R existence checks ----------

bool OOTArchivesExist() {
    // The OoT *ROM* archive (player-extracted) is oot.o2r / oot-mq.o2r. soh.o2r is the bundled PORT
    // archive (assets/fonts) that always ships with the build — it must NOT count here, or a genuine
    // first run (port archive present, ROM not yet extracted) would skip extraction and then hard-exit
    // inside Initialize() when oot.o2r is missing.
    return std::filesystem::exists("oot-mq.o2r") || std::filesystem::exists("oot.o2r");
}

// ROM-derived archive (must be extracted from the player's MM ROM)
bool MMRomArchiveExists() {
    return std::filesystem::exists("mm.o2r") || std::filesystem::exists("mm.zip") || std::filesystem::exists("mm.otr");
}

// ComboShip (issue 24): the combined config. Absent => fresh install => offer settings import.
bool ComboConfigExists() {
    return std::filesystem::exists("comboship.json");
}

// Parse a JSON object from disk. False on missing/parse-failure/non-object (slot then skipped).
bool LoadJsonObject(const std::string& path, nlohmann::json& out) {
    if (path.empty()) {
        return false;
    }
    try {
        std::ifstream f(path);
        if (!f) {
            return false;
        }
        nlohmann::json j = nlohmann::json::parse(f);
        if (!j.is_object()) {
            return false;
        }
        out = std::move(j);
        return true;
    } catch (...) { return false; }
}

// Per-leaf merge: objects recurse; on a leaf collision (scalar/array) the overlay wins. Keys unique
// to either side are kept. Used with 2Ship as base + SoH as overlay so SoH wins.
void DeepMerge(nlohmann::json& base, const nlohmann::json& overlay) {
    if (!base.is_object() || !overlay.is_object()) {
        base = overlay;
        return;
    }
    for (auto it = overlay.begin(); it != overlay.end(); ++it) {
        auto found = base.find(it.key());
        if (found != base.end() && found->is_object() && it->is_object()) {
            DeepMerge(*found, *it);
        } else {
            base[it.key()] = it.value();
        }
    }
}

// Soft validator (non-blocking hint): a Ship config is a JSON object with a CVars block.
int LauncherValidateShipConfig(const char* path) {
    nlohmann::json j;
    return (path && LoadJsonObject(path, j) && j.contains("CVars")) ? 1 : 0;
}
