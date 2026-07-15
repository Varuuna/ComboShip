// combo/rando/CrossForeign.h
// ComboShip: cross-world foreign-item lookup — shared by soh.dll, 2ship.dll, ComboShip.exe.
// The consolidated per-slot spoiler file (saves/combo/save{N}-Randomizer-<hash>.json, written at
// save creation) records, in its "foreign" array, which checks (in each game) hold an item that
// belongs to the OTHER game. At pickup, the check's own game places a sentinel item; the pickup
// code consults this array to learn the real foreign item + its destination game, then delivers it
// immediately into that game's resident save (see issue #3). Both games resolve the file by
// gSaveContext.fileNum, so a save and its consolidated file share one slot number.
//
// "foreign" array element:
//   { "checkGame":"oot|mm", "checkName":"<RC name>", "itemGame":"oot|mm",
//     "itemName":"<item key in itemGame's namespace>", "displayName":"<human name + (MM)/(OOT)>" }
//
// Note: itemName is in the DESTINATION game's namespace (the item's home game), since that is the
// game that ultimately grants it. OOT uses English item names; MM uses RI_* spoiler names.
#pragma once

#include <string>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace ComboRando {

// Game identity used across the cross-world rando layer (soh.dll, 2ship.dll, ComboShip.exe).
enum GameId : uint8_t { GAME_OOT = 0, GAME_MM = 1 };

// Sentinel item names written into each game's own placement table for a foreign check.
// They differ because the two games use different item-name namespaces (see header note).
inline constexpr const char* kForeignSentinelNameOOT = "Combo Foreign Item"; // OOT English name
inline constexpr const char* kForeignSentinelNameMM = "RI_COMBO_FOREIGN";    // MM spoilerName

struct ForeignItem {
    GameId itemGame;          // the game the item belongs to / must be delivered to
    std::string itemName;     // item key in itemGame's namespace (English for OOT, RI_* for MM)
    std::string displayName;  // human string for the "sent"/"received" text
    bool advancement = false; // progression in its home game -> drives the held-up pickup animation
};

inline std::string GameIdToKey(GameId g) {
    return g == GAME_OOT ? "oot" : "mm";
}
inline GameId KeyToGameId(const std::string& s) {
    return s == "mm" ? GAME_MM : GAME_OOT;
}

inline std::filesystem::path ConsolidatedDir() {
    return std::filesystem::path("Randomizer");
}

// Pending (unbound) seed written at Generate; remembered so the player can Start without regenerating.
inline std::filesystem::path PendingPath() {
    return ConsolidatedDir() / "Last-Generated-Randomizer.json";
}

// Per-slot consolidated file written when a save is created in slot N. Both the runtime foreign
// source (read by either game via gSaveContext.fileNum) and the shareable artifact.
inline std::filesystem::path SlotWritePath(int slot, const std::string& hashStr) {
    return ConsolidatedDir() / ("save" + std::to_string(slot) + "-Randomizer-" + hashStr + ".json");
}

inline bool HasSlotPrefix(const std::string& name, const std::string& prefix) {
    return name.size() > 5 && name.compare(0, prefix.size(), prefix) == 0 &&
           name.compare(name.size() - 5, 5, ".json") == 0;
}

// Resolve slot N's consolidated file by prefix (newest match). Empty if none.
inline std::filesystem::path SlotReadPath(int slot) {
    std::error_code ec;
    auto dir = ConsolidatedDir();
    const std::string prefix = "save" + std::to_string(slot) + "-Randomizer-";
    std::filesystem::path best;
    std::filesystem::file_time_type bestTime{};
    if (std::filesystem::exists(dir, ec)) {
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            std::error_code ec2;
            if (!e.is_regular_file(ec2))
                continue;
            const std::string name = e.path().filename().string();
            if (HasSlotPrefix(name, prefix)) {
                auto t = e.last_write_time(ec2);
                if (best.empty() || t > bestTime) {
                    best = e.path();
                    bestTime = t;
                }
            }
        }
    }
    return best;
}

// Remove any existing consolidated file(s) for slot N (stale seed from a prior generate in that slot).
inline void CleanSlotFiles(int slot) {
    std::error_code ec;
    auto dir = ConsolidatedDir();
    const std::string prefix = "save" + std::to_string(slot) + "-Randomizer-";
    if (!std::filesystem::exists(dir, ec))
        return;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        std::error_code ec2;
        if (!e.is_regular_file(ec2))
            continue;
        if (HasSlotPrefix(e.path().filename().string(), prefix))
            std::filesystem::remove(e.path(), ec2);
    }
}

// Tag a spoiler "foreign" array's displayNames with their home-game suffix for the consolidated file.
// Every display surface (shops, hints, trackers, toasts) reads displayName, so tag once here.
// ootCheckAreas (checkName -> OOT area name, from SOH_DumpRandoHintData's "checks" list) is optional;
// when given, oot-side entries get a "checkArea" field for the combo hint layer's foolish-area logic.
// MM-side entries omit it — MM's own dump carries its per-check "locationHints" (region names) instead.
inline nlohmann::json BuildForeignArray(const nlohmann::json& foreignArray,
                                        const std::unordered_map<std::string, std::string>& ootCheckAreas = {}) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& fm : foreignArray) {
        std::string checkGame = fm.value("checkGame", "");
        std::string checkName = fm.value("checkName", "");
        if (checkGame.empty() || checkName.empty())
            continue;
        std::string itemGame = fm.value("itemGame", "");
        std::string itemName = fm.value("itemName", "");
        std::string displayName = fm.value("displayName", itemName);
        if (!displayName.empty() && (itemGame == "mm" || itemGame == "oot"))
            displayName += (itemGame == "mm") ? " (MM)" : " (OOT)";
        nlohmann::json entry = { { "checkGame", checkGame },
                                 { "checkName", checkName },
                                 { "itemGame", itemGame },
                                 { "itemName", itemName },
                                 { "displayName", displayName },
                                 { "advancement", fm.value("advancement", false) } };
        if (checkGame == "oot") {
            auto it = ootCheckAreas.find(checkName);
            if (it != ootCheckAreas.end())
                entry["checkArea"] = it->second;
        }
        out.push_back(std::move(entry));
    }
    return out;
}

// Load one game's foreign-check section from slot N's consolidated file, keyed by check name.
// Returns empty on missing/corrupt file (never throws across the channel). Per-slot file is
// authoritative (written at save creation by fileNum) — no cross-slot fallback.
inline std::unordered_map<std::string, ForeignItem> LoadForeignForGame(int slot, GameId checkGame) {
    std::unordered_map<std::string, ForeignItem> map;
    auto path = SlotReadPath(slot);
    if (path.empty())
        return map;
    std::ifstream in(path);
    if (!in.is_open())
        return map;
    try {
        nlohmann::json j;
        in >> j;
        const std::string key = GameIdToKey(checkGame);
        for (const auto& fm : j.value("foreign", nlohmann::json::array())) {
            if (fm.value("checkGame", "") != key)
                continue;
            ForeignItem fi;
            fi.itemGame = KeyToGameId(fm.value("itemGame", ""));
            fi.itemName = fm.value("itemName", "");
            fi.displayName = fm.value("displayName", fi.itemName);
            fi.advancement = fm.value("advancement", false);
            map.emplace(fm.value("checkName", ""), std::move(fi));
        }
    } catch (...) { /* corrupt -> treat as empty */
    }
    return map;
}

// A foreign check's location, keyed the other way round (by itemName) for a game that wants to know
// where ITS OWN item ended up when placed at a check in the other game (family-B: MM item -> OOT check).
struct ForeignPlacement {
    GameId checkGame;
    std::string checkName;
    std::string displayName;
    bool advancement = false;
};

// Load itemGame's cross-placed items, keyed by itemName (the item's own namespace). Used when a
// display routine's local check scan fails and it needs to know which OTHER game's check holds it.
inline std::unordered_map<std::string, ForeignPlacement> LoadForeignByItem(int slot, GameId itemGame) {
    std::unordered_map<std::string, ForeignPlacement> map;
    auto path = SlotReadPath(slot);
    if (path.empty())
        return map;
    std::ifstream in(path);
    if (!in.is_open())
        return map;
    try {
        nlohmann::json j;
        in >> j;
        const std::string key = GameIdToKey(itemGame);
        for (const auto& fm : j.value("foreign", nlohmann::json::array())) {
            if (fm.value("itemGame", "") != key)
                continue;
            ForeignPlacement fp;
            fp.checkGame = KeyToGameId(fm.value("checkGame", ""));
            fp.checkName = fm.value("checkName", "");
            fp.displayName = fm.value("displayName", fp.checkName);
            fp.advancement = fm.value("advancement", false);
            map.emplace(fm.value("itemName", ""), std::move(fp));
        }
    } catch (...) { /* corrupt -> treat as empty */
    }
    return map;
}

} // namespace ComboRando
