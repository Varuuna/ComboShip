// combo/rando/CrossEntrances.h
// ComboShip: cross-game entrance shuffle (docs/ENTRANCE_RANDO_PREP.md §4, Phase B).
// Header-only, deterministic. Union shuffle over both games' leaf interiors: any selected door can
// lead to any selected interior, same-game outcomes included. Leaf-only permutation — doors stay
// put, only what's behind them changes — so connectivity can't break and no rejection sampling is
// needed. Pools come from the games (SOH_/MM_DumpInteriorEntrancePairs); this module computes the
// assignment, the per-game runtime rule slices, the fill-gating pairs, and the spoiler section.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "CrossForeign.h"    // ComboRando::GameId
#include "CrossWorldRando.h" // CwRng / cwShuffle

namespace ComboRando {

// One leaf interior with its door, as reported by the owning game.
// entry  = entrance value INTO the interior (what walking through the door produces natively).
// exit   = entrance value leaving the interior produces (the exterior spawn beside the door).
// region = the game's logic identifier: OOT uses region NAMES, MM uses RandoRegionId ints (kept as
//          strings; MM's are decimal ints) — consumed by the respective IsRegionReachable /
//          SetExternallyReachableRegions exports.
struct CrossInterior {
    GameId game;
    int entry;
    int exit;
    std::string doorRegion;     // exterior logic region (gates the door's reachability)
    std::string interiorRegion; // interior logic region (marked reachable in the target game)
    std::string doorName;       // readable, for the spoiler
    std::string interiorName;
};

// door -> assigned interior (coupled: exiting the interior returns beside the door).
struct CrossAssignment {
    CrossInterior door;     // supplies the door side (game, entry key, exterior spawn, door region)
    CrossInterior interior; // supplies the target side (game, interior entry/exit, interior region)
};

inline constexpr uint64_t kCrossEntranceSalt = 0xC0553E17ULL;

// Portal interiors must never sit behind cross doors: their scene-entry triggers ARE the
// OOT<->MM portal. Matched on the readable interior name reported by the games.
inline bool CrossEntrancePortalInterior(GameId game, const std::string& interiorName,
                                        const std::string& interiorRegion) {
    if (game == GAME_OOT)
        return interiorRegion == "Market Mask Shop" || interiorName.find("Mask Shop") != std::string::npos;
    return interiorName.find("Clock Tower") != std::string::npos;
}

inline std::vector<CrossInterior> ParseInteriorPairs(GameId game, const std::string& pairsJson) {
    std::vector<CrossInterior> out;
    try {
        auto arr = nlohmann::json::parse(pairsJson);
        for (auto& p : arr) {
            CrossInterior ci;
            ci.game = game;
            if (game == GAME_OOT) {
                ci.entry = p.value("index", -1);
                ci.exit = p.value("reverseIndex", -1);
                ci.doorRegion = p.value("doorRegion", "");
                ci.interiorRegion = p.value("interiorRegion", "");
            } else {
                ci.entry = p.value("entrance", -1);
                ci.exit = p.value("exit", -1);
                ci.doorRegion = std::to_string(p.value("doorRegion", -1));
                ci.interiorRegion = std::to_string(p.value("interiorRegion", -1));
            }
            ci.doorName = p.value("door", "?");
            ci.interiorName = p.value("interior", "?");
            if (ci.entry < 0 || ci.exit < 0)
                continue;
            if (CrossEntrancePortalInterior(game, ci.interiorName, ci.interiorRegion))
                continue;
            out.push_back(std::move(ci));
        }
    } catch (const std::exception& e) {
        std::cerr << "[ComboShip] ParseInteriorPairs(" << (game == GAME_OOT ? "oot" : "mm") << "): " << e.what()
                  << "\n";
    }
    return out;
}

// The union shuffle: permute the interiors over the doors. Deterministic per master seed.
inline std::vector<CrossAssignment> BuildCrossAssignments(const std::string& sohPairsJson,
                                                          const std::string& mmPairsJson, uint32_t masterSeed) {
    std::vector<CrossInterior> pool = ParseInteriorPairs(GAME_OOT, sohPairsJson);
    {
        auto mm = ParseInteriorPairs(GAME_MM, mmPairsJson);
        pool.insert(pool.end(), mm.begin(), mm.end());
    }
    std::vector<CrossInterior> targets = pool;
    CwRng rng(static_cast<uint64_t>(masterSeed) ^ kCrossEntranceSalt);
    cwShuffle(targets, rng);
    std::vector<CrossAssignment> out;
    out.reserve(pool.size());
    for (size_t i = 0; i < pool.size(); ++i)
        out.push_back({ pool[i], targets[i] });
    return out;
}

// Per-game runtime slice for SOH_/MM_SetCrossEntranceTable. Two rule kinds per assignment:
//   door rule   (in door's game):     pending == door.entry    -> interior.entry (interior's game)
//   return rule (in interior's game): pending == interior.exit -> door.exit     (door's game)
// "park" = where the save stays when the rule crosses games (the player "never left" this game):
// beside the door for door rules, inside the interior for return rules. "exclude" lists the game's
// door-side pairs so the native interior shuffles skip them (partition).
inline std::string BuildCrossTableSlice(const std::vector<CrossAssignment>& assignments, GameId forGame) {
    nlohmann::json rules = nlohmann::json::array();
    nlohmann::json exclude = nlohmann::json::array();
    auto gameStr = [](GameId g) { return g == GAME_OOT ? "oot" : "mm"; };
    for (const auto& a : assignments) {
        if (a.door.game == forGame) {
            rules.push_back({ { "key", a.door.entry },
                              { "targetGame", gameStr(a.interior.game) },
                              { "target", a.interior.entry },
                              { "park", a.door.exit } });
            exclude.push_back(a.door.entry);
            // MM's logic sever is destination-keyed (FindReachableRegions skip): the door edge's
            // destination is door.entry, the native interior's return edge's is door.exit — both
            // must go. OOT ignores the extra value (its exclusion is door-index keyed; the sever
            // disconnects the pair's both directions via the Entrance objects).
            if (forGame == GAME_MM)
                exclude.push_back(a.door.exit);
        }
        if (a.interior.game == forGame) {
            rules.push_back({ { "key", a.interior.exit },
                              { "targetGame", gameStr(a.door.game) },
                              { "target", a.door.exit },
                              { "park", a.interior.entry } });
        }
    }
    return nlohmann::json{ { "rules", rules }, { "exclude", exclude } }.dump();
}

// Readable spoiler section ("entrances.cross"), sorted by door name.
inline nlohmann::json BuildCrossSpoiler(const std::vector<CrossAssignment>& assignments) {
    std::vector<nlohmann::json> rows;
    auto gameStr = [](GameId g) { return g == GAME_OOT ? "oot" : "mm"; };
    for (const auto& a : assignments) {
        rows.push_back({ { "doorGame", gameStr(a.door.game) },
                         { "door", a.door.doorName },
                         { "toGame", gameStr(a.interior.game) },
                         { "to", a.interior.interiorName },
                         { "doorEntry", a.door.entry },
                         { "interiorEntry", a.interior.entry } });
    }
    std::sort(rows.begin(), rows.end(), [](const nlohmann::json& x, const nlohmann::json& y) {
        return x["door"].get<std::string>() < y["door"].get<std::string>();
    });
    return nlohmann::json(rows);
}

} // namespace ComboRando
