// combo/rando/CrossForeign.h
// ComboShip: cross-world foreign-item marker map — shared by soh.dll, 2ship.dll, ComboShip.exe.
// One JSON file per canonical OOT slot records which checks (in each game) hold an item that
// belongs to the OTHER game. At pickup, the check's own game places a sentinel item; the
// pickup code consults this map to learn the real foreign item + its destination game, then
// delivers it immediately into that game's resident save (see issue #3; the old JSON CrossMailbox
// stash + per-frame drain was replaced by immediate cross-DLL grant).
//
// Schema (saves/combo/slot{N}.foreign.json):
//   { "oot": { "<checkName>": {"itemGame":"mm","itemName":"RI_DEKU_MASK","displayName":"Deku Mask"} },
//     "mm":  { "<checkName>": {"itemGame":"oot","itemName":"Hookshot","displayName":"Hookshot"} } }
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
inline constexpr const char* kForeignSentinelNameMM  = "RI_COMBO_FOREIGN";   // MM spoilerName

struct ForeignItem {
    GameId      itemGame;     // the game the item belongs to / must be delivered to
    std::string itemName;     // item key in itemGame's namespace (English for OOT, RI_* for MM)
    std::string displayName;  // human string for the "sent"/"received" text
};

inline std::string GameIdToKey(GameId g) { return g == GAME_OOT ? "oot" : "mm"; }
inline GameId KeyToGameId(const std::string& s) { return s == "mm" ? GAME_MM : GAME_OOT; }

inline std::filesystem::path ForeignPath(int canonicalSlot) {
    return std::filesystem::path("saves") / "combo" /
           ("slot" + std::to_string(canonicalSlot) + ".foreign.json");
}

// Build and atomically write the per-slot foreign map from the combined spoiler's "foreign" array
// (each element: {checkGame, checkName, itemGame, itemName, [displayName]}).
inline bool WriteForeignFromAnnotations(int canonicalSlot, const nlohmann::json& foreignArray) {
    nlohmann::json out;
    out["oot"] = nlohmann::json::object();
    out["mm"]  = nlohmann::json::object();
    for (const auto& fm : foreignArray) {
        std::string checkGame = fm.value("checkGame", "");
        std::string checkName = fm.value("checkName", "");
        if (checkGame.empty() || checkName.empty()) continue;
        std::string itemName = fm.value("itemName", "");
        std::string itemGame = fm.value("itemGame", "");
        std::string displayName = fm.value("displayName", itemName);
        // ComboShip: tag foreign items with their home game — every display surface (shops, NPC
        // hints, trackers, toasts) reads this displayName, so one tag here covers them all.
        // Only tag known games (a malformed marker keeps its untagged name).
        if (!displayName.empty() && (itemGame == "mm" || itemGame == "oot")) {
            displayName += (itemGame == "mm") ? " (MM)" : " (OOT)";
        }
        out[checkGame][checkName] = {
            { "itemGame",    itemGame },
            { "itemName",    itemName },
            { "displayName", displayName },
        };
    }

    std::error_code ec;
    auto path = ForeignPath(canonicalSlot);
    std::filesystem::create_directories(path.parent_path(), ec);
    auto tmp = path; tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f.is_open()) return false;
        f << out.dump(2);
        if (!f.good()) { f.close(); std::filesystem::remove(tmp, ec); return false; }
    }
    std::filesystem::rename(tmp, path, ec);  // atomic on same volume
    return !ec;
}

// Load one game's foreign-check section, keyed by check name. Returns empty on missing/corrupt file
// (never throws across the channel).
inline std::unordered_map<std::string, ForeignItem> LoadForeignForGame(int canonicalSlot, GameId checkGame) {
    std::unordered_map<std::string, ForeignItem> map;
    std::ifstream in(ForeignPath(canonicalSlot));
    if (!in.is_open()) return map;
    try {
        nlohmann::json j; in >> j;
        const auto& section = j.value(GameIdToKey(checkGame), nlohmann::json::object());
        for (auto it = section.begin(); it != section.end(); ++it) {
            const auto& v = it.value();
            ForeignItem fi;
            fi.itemGame    = KeyToGameId(v.value("itemGame", ""));
            fi.itemName    = v.value("itemName", "");
            fi.displayName = v.value("displayName", fi.itemName);
            map.emplace(it.key(), std::move(fi));
        }
    } catch (...) { /* corrupt -> treat as empty */ }
    return map;
}

} // namespace ComboRando
