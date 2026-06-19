// combo/rando/CrossWorldRando.h
// ComboShip: combined cross-world randomizer generator.
// Header-only, deterministic. No game source touched.
//
// Phase 1 (no-logic, native-only): permutation of each game's vanilla items.
// Phase 2 (combined-logic): assumed fill over the union of both games' pools,
//   joined by the portal, driving per-game reachability oracles.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <nlohmann/json.hpp>
#include "gui/ComboGenProgress.h"
#include "CrossForeign.h"  // for ComboRando::GameId

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

// Reuses GameId from CrossForeign.h (GAME_OOT = 0, GAME_MM = 1)
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
// progress: optional thread-safe progress struct polled by the UI. May be nullptr.
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

    // --- Parse pools (single pass per dump) ---
    // One CwItem per check's vanillaItem (multiset), partitioned by the dump's advancement flag.
    // Missing flag (older game DLL) defaults to advancement=true: slower, never less logical.
    std::vector<CwItem> advItems, junkItems;
    std::vector<CwCheck> allChecks;

    auto parsePool = [&](Game game, const std::string& dumpJson) {
        auto d = nlohmann::json::parse(dumpJson);
        for (auto& c : d.value("checks", nlohmann::json::array())) {
            std::string name = c.value("name", "");
            std::string vi = c.value("vanillaItem", "");
            if (name.empty() || vi.empty()) continue;
            allChecks.push_back({game, name});
            bool adv = c.value("advancement", true);
            (adv ? advItems : junkItems).push_back({game, vi, adv});
        }
    };

    try {
        parsePool(GAME_OOT, sohDumpJson);
        parsePool(GAME_MM, mmDumpJson);
    } catch (const std::exception& e) {
        result.error = std::string("Pool parse error: ") + e.what();
        return result;
    }

    CwRng rng(masterSeed);
    cwShuffle(allChecks, rng);

    // OOT and MM check names are distinct namespaces; key fill bookkeeping by game+name.
    auto checkKey = [](const CwCheck& c) {
        return std::string(c.game == GAME_OOT ? "oot:" : "mm:") + c.name;
    };

    // Per-oracle query stats (count + total ms), logged on completion — the searches dominate
    // fill time, so this is the first thing to read when generation feels slow.
    struct QueryStats { uint32_t count = 0; int64_t ms = 0; };
    QueryStats ootStats, mmStats;

    auto queryReachable = [&](const OracleFns& oracle, const std::vector<std::string>& ownedItems)
        -> std::unordered_set<std::string> {
        QueryStats& stats = (&oracle == &ootOracle) ? ootStats : mmStats;
        auto t0 = std::chrono::steady_clock::now();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& n : ownedItems) arr.push_back(n);
        oracle.Reset();
        oracle.SetOwnedItems(arr.dump().c_str());
        std::string raw = oracle.GetReachableChecks();
        std::unordered_set<std::string> out;
        try {
            auto parsed = nlohmann::json::parse(raw);
            for (const auto& name : parsed) out.insert(name.get<std::string>());
        } catch (...) {}
        stats.count++;
        stats.ms += std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();
        return out;
    };

    // Cross-game sphere-collect fixpoint: starting from the per-game base sets (unplaced
    // advancement items), repeatedly query both oracles and credit any prior placement whose
    // CHECK became reachable to the ITEM's game's owned set, until nothing changes. This spans
    // both games because an MM item at an OOT check (or vice versa) can open progress anywhere,
    // including the portal itself — so portal openness is re-evaluated every iteration.
    std::vector<CwPlacement> placements;
    auto reachableFixpoint = [&](const std::vector<std::string>& ootBase,
                                 const std::vector<std::string>& mmBase)
        -> std::pair<std::unordered_set<std::string>, std::unordered_set<std::string>> {
        std::vector<std::string> ootOwned = ootBase, mmOwned = mmBase;
        std::vector<bool> credited(placements.size(), false);
        std::unordered_set<std::string> ootReachable, mmReachable;
        for (;;) {
            ootReachable = queryReachable(ootOracle, ootOwned);
            bool portalOpen = portalCheckName.empty() || ootReachable.count(portalCheckName) > 0;
            mmReachable = portalOpen ? queryReachable(mmOracle, mmOwned)
                                     : std::unordered_set<std::string>{};
            bool changed = false;
            for (size_t i = 0; i < placements.size(); ++i) {
                if (credited[i]) continue;
                const auto& reach =
                    placements[i].check.game == GAME_OOT ? ootReachable : mmReachable;
                if (reach.count(placements[i].check.name)) {
                    auto& owned = placements[i].item.game == GAME_OOT ? ootOwned : mmOwned;
                    owned.push_back(placements[i].item.name);
                    credited[i] = true;
                    changed = true;
                }
            }
            if (!changed) return { std::move(ootReachable), std::move(mmReachable) };
        }
    };

    if (progress) {
        progress->phase.store(2); // Placing items
        progress->total.store(static_cast<int>(advItems.size()));
    }

    // --- Assumed fill of advancement items, then junk fast-fill; retry whole passes on dead
    // ends or failed validation (deterministic: one rng stream continues across passes) ---
    const int kMaxPasses = 10;
    std::unordered_set<std::string> filledChecks; // checkKey()-keyed
    bool fillOk = false;
    int passesUsed = 0;

    for (int pass = 1; pass <= kMaxPasses && !fillOk; ++pass) {
        passesUsed = pass;
        auto passStart = std::chrono::steady_clock::now();
        auto passMs = [&] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - passStart).count();
        };
        placements.clear();
        filledChecks.clear();

        // Phase A: place advancement items with logic, assuming only UNPLACED ones are owned.
        std::vector<CwItem> toPlace = advItems;
        cwShuffle(toPlace, rng);
        std::vector<std::string> ootRemaining, mmRemaining;
        for (const auto& it : toPlace)
            (it.game == GAME_OOT ? ootRemaining : mmRemaining).push_back(it.name);

        // Batch placement: remove K items from the assumed set and place all K into distinct
        // reachable checks off ONE fixpoint. Still a valid (strictly conservative) assumed fill —
        // every item lands on a check reachable without itself AND the rest of its batch — and it
        // divides the dominant cost (fixpoint sphere searches) by K. K shrinks as the pool drains
        // (conservatism bites hardest when few items remain); a failed batch halves a sticky cap
        // (it never grows back within a pass — late fill only gets tighter) and only a failed
        // K=1 abandons the pass.
        size_t batchCap = 16;
        auto removeOne = [](std::vector<std::string>& v, const std::string& name) {
            for (size_t i = 0; i < v.size(); ++i) {
                if (v[i] == name) {
                    v[i] = v.back();
                    v.pop_back();
                    return;
                }
            }
        };

        bool deadEnd = false;
        while (!toPlace.empty()) {
            size_t k = std::min({ batchCap, std::max<size_t>(1, toPlace.size() / 4),
                                  toPlace.size() });

            std::vector<CwItem> batch;
            batch.reserve(k);
            for (size_t i = 0; i < k; ++i) {
                batch.push_back(toPlace.back());
                toPlace.pop_back();
                removeOne(batch.back().game == GAME_OOT ? ootRemaining : mmRemaining,
                          batch.back().name);
            }

            auto [ootReachable, mmReachable] = reachableFixpoint(ootRemaining, mmRemaining);

            std::vector<size_t> candidates;
            for (size_t ci = 0; ci < allChecks.size(); ++ci) {
                if (filledChecks.count(checkKey(allChecks[ci]))) continue;
                const auto& reach = allChecks[ci].game == GAME_OOT ? ootReachable : mmReachable;
                if (reach.count(allChecks[ci].name)) candidates.push_back(ci);
            }

            if (candidates.size() < batch.size()) {
                // Put the batch back (reverse order restores the pop sequence — deterministic).
                for (auto it = batch.rbegin(); it != batch.rend(); ++it) {
                    (it->game == GAME_OOT ? ootRemaining : mmRemaining).push_back(it->name);
                    toPlace.push_back(*it);
                }
                if (batch.size() > 1) {
                    batchCap = batch.size() / 2; // too conservative at this depth — shrink
                    continue;
                }
                std::cerr << "[ComboShip] CrossWorldCombinedFill: dead end placing '"
                          << batch.front().name << "' (pass " << pass << ", "
                          << placements.size() << " placed, " << passMs() << " ms) — retrying\n";
                deadEnd = true;
                break;
            }

            for (const auto& item : batch) {
                size_t slot = rng.below(static_cast<uint32_t>(candidates.size()));
                size_t pick = candidates[slot];
                candidates[slot] = candidates.back();
                candidates.pop_back();
                placements.push_back({ allChecks[pick], item });
                filledChecks.insert(checkKey(allChecks[pick]));
            }
            if (progress) progress->placed.store(static_cast<int>(placements.size()));
        }
        if (deadEnd) continue;

        // Phase B: junk fast-fill into the remaining checks — zero oracle calls.
        std::vector<CwItem> junkToPlace = junkItems;
        cwShuffle(junkToPlace, rng);
        size_t ji = 0;
        for (size_t ci = 0; ci < allChecks.size() && ji < junkToPlace.size(); ++ci) {
            if (filledChecks.count(checkKey(allChecks[ci]))) continue;
            placements.push_back({ allChecks[ci], junkToPlace[ji++] });
            filledChecks.insert(checkKey(allChecks[ci]));
        }
        if (ji < junkToPlace.size()) {
            std::cerr << "[ComboShip] CrossWorldCombinedFill: pool/check mismatch — "
                      << (junkToPlace.size() - ji) << " junk items left over\n";
        }

        // Validation: with NOTHING assumed, sphere-collecting only placed items must reach every
        // ADVANCEMENT-holding check — the assumed-fill guarantee (all progression collectible).
        // Junk-holding checks may legitimately be oracle-unreachable: both oracles under-model
        // (e.g. MM logic evaluated with zeroed save options), so some checks never appear
        // reachable even with full inventory — the fill can only ever put junk there. Count and
        // log those, don't fail on them.
        auto [ootFinal, mmFinal] = reachableFixpoint({}, {});
        size_t advUnreachable = 0, junkUnreachable = 0, junkUnreachableOot = 0;
        for (const auto& p : placements) {
            const auto& reach = p.check.game == GAME_OOT ? ootFinal : mmFinal;
            if (reach.count(p.check.name)) continue;
            if (p.item.advancement) {
                ++advUnreachable;
            } else {
                ++junkUnreachable;
                if (p.check.game == GAME_OOT) ++junkUnreachableOot;
            }
        }
        if (advUnreachable > 0) {
            std::cerr << "[ComboShip] CrossWorldCombinedFill: validation failed — " << advUnreachable
                      << " advancement items on unreachable checks (pass " << pass << ", "
                      << passMs() << " ms) — retrying\n";
            continue;
        }

        std::cout << "[ComboShip] CrossWorldCombinedFill: all " << advItems.size()
                  << " advancement items reachable from scratch (pass " << pass << ", "
                  << passMs() << " ms; "
                  << junkItems.size() << " junk, " << junkUnreachable
                  << " junk on oracle-unreachable checks [oot=" << junkUnreachableOot
                  << " mm=" << (junkUnreachable - junkUnreachableOot) << "])\n";
        std::cout << "[ComboShip] CrossWorldCombinedFill: oracle queries — oot " << ootStats.count
                  << "x/" << ootStats.ms << " ms, mm " << mmStats.count << "x/" << mmStats.ms
                  << " ms\n";
        fillOk = true;
    }

    if (!fillOk) {
        result.error = "assumed fill failed after " + std::to_string(kMaxPasses) +
                       " passes (dead ends or unreachable checks; see log)";
        return result;
    }

    if (progress) {
        progress->placed.store(progress->total.load()); // all placed
        progress->phase.store(3); // Finalizing
    }

    // --- Build spoiler (same shape as the no-logic generator) ---
    nlohmann::json spoiler;
    spoiler["masterSeed"] = masterSeed;
    spoiler["mode"] = "combined-logic assumed-fill";
    spoiler["fillStats"] = {
        { "advancementItems", static_cast<uint32_t>(advItems.size()) },
        { "junkItems", static_cast<uint32_t>(junkItems.size()) },
        { "passes", passesUsed }
    };

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
