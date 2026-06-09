// combo/rando/CrossWorldRando.h
// ComboShip: combined cross-world randomizer generator.
// Header-only, deterministic. No game source touched.
//
// Phase 1 (no-logic, native-only): permutation of each game's vanilla items.
// Phase 2 (combined-logic): assumed fill over the union of both games' pools,
//   joined by the portal, driving per-game reachability oracles.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <nlohmann/json.hpp>
#include "gui/ComboGenProgress.h"

namespace ComboRando {

// ---------- Deterministic 64-bit LCG (Knuth / Newlib constants) ----------

struct CwRng {
    uint64_t s;
    explicit CwRng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
    uint32_t next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<uint32_t>(s >> 33);
    }
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

template <class T>
inline void cwShuffle(std::vector<T>& v, CwRng& rng) {
    for (size_t i = v.size(); i > 1; --i) {
        size_t j = rng.below(static_cast<uint32_t>(i));
        std::swap(v[i - 1], v[j]);
    }
}

// ---------- Oracle function-pointer types (set by ComboShip.cpp) ----------

struct OracleFns {
    void        (*Reset)(void);
    void        (*SetOwnedItems)(const char*);
    const char* (*GetReachableChecks)(void);
    void        (*PlaceItem)(const char*, const char*);
};

// ---------- Data types ----------

// Reuses GameId from CrossMailbox.h (GAME_OOT = 0, GAME_MM = 1)
using Game = GameId;

struct CwItem {
    Game game;
    std::string name;
    bool advancement;
};

struct CwCheck {
    Game game;
    std::string name;
};

struct CwPlacement {
    CwCheck check;
    CwItem  item;
};

// ---------- Combined assumed fill ----------

struct CombinedFillResult {
    std::string spoilerJson;
    bool success;
    std::string error;
};

// portalCheckName: the OOT check/region name that gates access to MM (e.g. "Mido's House").
// If empty, MM is reachable from start.
// progress: optional thread-safe progress struct polled by the UI (Task 7). May be nullptr.
inline CombinedFillResult CrossWorldCombinedFill(
    const std::string& sohDumpJson,
    const std::string& mmDumpJson,
    uint32_t masterSeed,
    const OracleFns& ootOracle,
    const OracleFns& mmOracle,
    const std::string& portalCheckName = "",
    ComboRando::ComboGenProgress* progress = nullptr
) {
    CombinedFillResult result;
    result.success = false;

    if (progress) progress->phase.store(1); // Preparing pools

    // --- Parse pools ---
    std::vector<CwItem> advancementItems, junkItems;
    std::vector<CwCheck> allChecks;

    auto parsePool = [&](Game game, const std::string& dumpJson) {
        auto d = nlohmann::json::parse(dumpJson);
        std::unordered_set<std::string> vanillaItemNames;
        for (auto& c : d.value("checks", nlohmann::json::array())) {
            std::string name = c.value("name", "");
            std::string vi = c.value("vanillaItem", "");
            if (name.empty() || vi.empty()) continue;
            allChecks.push_back({game, name});
            vanillaItemNames.insert(vi);
        }
        // All vanilla items go into the item pools. For now, treat every item as advancement
        // (the oracles determine reachability; junk classification is deferred to when we have
        // IsAdvancement queries on each game).
        for (const auto& vi : vanillaItemNames) {
            advancementItems.push_back({game, vi, true});
        }
    };

    try {
        parsePool(GAME_OOT, sohDumpJson);
        parsePool(GAME_MM, mmDumpJson);
    } catch (const std::exception& e) {
        result.error = std::string("Pool parse error: ") + e.what();
        return result;
    }

    // Build the full item multiset from vanilla items per check (not unique names)
    std::vector<CwItem> allItems;
    auto buildItemPool = [&](Game game, const std::string& dumpJson) {
        auto d = nlohmann::json::parse(dumpJson);
        for (auto& c : d.value("checks", nlohmann::json::array())) {
            std::string vi = c.value("vanillaItem", "");
            if (vi.empty()) continue;
            allItems.push_back({game, vi, true});
        }
    };
    try {
        buildItemPool(GAME_OOT, sohDumpJson);
        buildItemPool(GAME_MM, mmDumpJson);
    } catch (...) {}

    // --- Shuffle ---
    CwRng rng(masterSeed);
    cwShuffle(allItems, rng);
    cwShuffle(allChecks, rng);

    // --- Assumed fill ---
    // For each item (in shuffled order):
    //   1. Remove it from the "assumed available" set
    //   2. Reset both oracles, push remaining assumed items
    //   3. Query reachable checks from both oracles (MM gated by portal)
    //   4. Place the item in a random reachable empty check
    std::vector<CwPlacement> placements;
    std::unordered_set<std::string> filledChecks;

    auto queryReachable = [&](const OracleFns& oracle, const nlohmann::json& ownedItems)
        -> std::unordered_set<std::string> {
        oracle.Reset();
        oracle.SetOwnedItems(ownedItems.dump().c_str());
        std::string raw = oracle.GetReachableChecks();
        std::unordered_set<std::string> out;
        try {
            auto arr = nlohmann::json::parse(raw);
            for (const auto& name : arr) out.insert(name.get<std::string>());
        } catch (...) {}
        return out;
    };

    if (progress) {
        progress->phase.store(2); // Placing items
        progress->total.store(static_cast<int>(allItems.size()));
    }

    for (size_t idx = 0; idx < allItems.size(); ++idx) {
        if (progress) progress->placed.store(static_cast<int>(idx));
        CwItem& item = allItems[idx];

        // Build assumed-available sets (all items EXCEPT this one) partitioned by game
        nlohmann::json ootAssumed = nlohmann::json::array();
        nlohmann::json mmAssumed  = nlohmann::json::array();
        for (size_t j = 0; j < allItems.size(); ++j) {
            if (j == idx) continue;
            if (allItems[j].game == GAME_OOT) ootAssumed.push_back(allItems[j].name);
            else                               mmAssumed.push_back(allItems[j].name);
        }

        // Query OOT reachable
        auto ootReachable = queryReachable(ootOracle, ootAssumed);

        // MM is reachable only if portal is reachable in OOT (or no portal gate)
        std::unordered_set<std::string> mmReachable;
        bool portalOpen = portalCheckName.empty() || ootReachable.count(portalCheckName) > 0;
        if (portalOpen) {
            mmReachable = queryReachable(mmOracle, mmAssumed);
        }

        // Collect candidate empty checks
        std::vector<size_t> candidates;
        for (size_t ci = 0; ci < allChecks.size(); ++ci) {
            if (filledChecks.count(allChecks[ci].name)) continue;
            if (allChecks[ci].game == GAME_OOT && ootReachable.count(allChecks[ci].name))
                candidates.push_back(ci);
            else if (allChecks[ci].game == GAME_MM && mmReachable.count(allChecks[ci].name))
                candidates.push_back(ci);
        }

        if (candidates.empty()) {
            // Fallback: place in any unfilled check (breaks logic but avoids a crash)
            for (size_t ci = 0; ci < allChecks.size(); ++ci) {
                if (!filledChecks.count(allChecks[ci].name)) {
                    candidates.push_back(ci);
                }
            }
            if (candidates.empty()) break;
        }

        size_t pick = candidates[rng.below(static_cast<uint32_t>(candidates.size()))];
        placements.push_back({allChecks[pick], item});
        filledChecks.insert(allChecks[pick].name);
    }

    if (progress) {
        progress->placed.store(progress->total.load()); // all placed
        progress->phase.store(3); // Finalizing
    }

    // --- Build spoiler (same shape as the no-logic generator) ---
    nlohmann::json spoiler;
    spoiler["masterSeed"] = masterSeed;
    spoiler["mode"] = "combined-logic assumed-fill";

    nlohmann::json ootPlacements = nlohmann::json::object();
    nlohmann::json mmPlacements  = nlohmann::json::object();
    nlohmann::json foreignMarkers = nlohmann::json::array();

    for (const auto& p : placements) {
        if (p.check.game == GAME_OOT) {
            ootPlacements[p.check.name] = p.item.name;
        } else {
            mmPlacements[p.check.name] = p.item.name;
        }
        if (p.check.game != p.item.game) {
            foreignMarkers.push_back({
                {"checkGame", p.check.game == GAME_OOT ? "oot" : "mm"},
                {"checkName", p.check.name},
                {"itemGame", p.item.game == GAME_OOT ? "oot" : "mm"},
                {"itemName", p.item.name}
            });
        }
    }

    spoiler["ootCount"] = static_cast<uint32_t>(ootPlacements.size());
    spoiler["oot"] = ootPlacements;
    spoiler["mmCount"] = static_cast<uint32_t>(mmPlacements.size());
    spoiler["mm"] = mmPlacements;
    spoiler["foreign"] = foreignMarkers;

    // --- Commit placements to oracles (for save consumption) ---
    for (const auto& p : placements) {
        if (p.check.game == GAME_OOT) {
            ootOracle.PlaceItem(p.check.name.c_str(), p.item.name.c_str());
        } else {
            mmOracle.PlaceItem(p.check.name.c_str(), p.item.name.c_str());
        }
    }

    result.spoilerJson = spoiler.dump(2);
    result.success = true;
    return result;
}

// ---------- Legacy no-logic generator (kept for fallback) ----------

inline std::string CrossWorldGenerateSpoiler(const std::string& sohDumpJson,
                                              const std::string& mmDumpJson,
                                              uint32_t masterSeed) {
    nlohmann::json spoiler;
    spoiler["masterSeed"] = masterSeed;
    spoiler["mode"] = "no-logic native-only (phase1)";

    auto doGame = [&](const char* key, const std::string& dumpJson, uint32_t seed) {
        nlohmann::json out = nlohmann::json::object();
        try {
            auto d = nlohmann::json::parse(dumpJson);
            std::vector<std::string> checkNames, vanillaItems;
            for (auto& c : d.value("checks", nlohmann::json::array())) {
                if (!c.contains("vanillaItem")) continue;
                std::string v = c.value("vanillaItem", std::string{});
                if (v.empty()) continue;
                checkNames.push_back(c.value("name", std::string{}));
                vanillaItems.push_back(v);
            }
            std::vector<std::string> shuffled = vanillaItems;
            CwRng rng(seed);
            cwShuffle(shuffled, rng);
            for (size_t i = 0; i < checkNames.size(); ++i) {
                out[checkNames[i]] = shuffled[i];
            }
            spoiler[std::string(key) + "Count"] = static_cast<uint32_t>(checkNames.size());
        } catch (...) {
            spoiler[std::string(key) + "Error"] = true;
        }
        spoiler[key] = out;
    };

    doGame("oot", sohDumpJson, masterSeed ^ 0x4F4F5400u);
    doGame("mm",  mmDumpJson,  masterSeed ^ 0x4D4D0000u);

    return spoiler.dump(2);
}

} // namespace ComboRando
