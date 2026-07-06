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
#include "CrossForeign.h" // for ComboRando::GameId

namespace ComboRando {

// ---------- Deterministic 64-bit LCG (Knuth / Newlib constants) ----------

struct CwRng {
    uint64_t s;
    explicit CwRng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {
    }
    uint32_t next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<uint32_t>(s >> 33);
    }
    uint32_t below(uint32_t n) {
        return n ? next() % n : 0;
    }
};

template <class T> inline void cwShuffle(std::vector<T>& v, CwRng& rng) {
    for (size_t i = v.size(); i > 1; --i) {
        size_t j = rng.below(static_cast<uint32_t>(i));
        std::swap(v[i - 1], v[j]);
    }
}

// ---------- Oracle function-pointer types (set by ComboShip.cpp) ----------

struct OracleFns {
    void (*Reset)(void);
    void (*SetOwnedItems)(const char*);
    const char* (*GetReachableChecks)(void);
    void (*PlaceItem)(const char*, const char*);
    // Optional portal-gate query: is this logic region reachable under the last GetReachableChecks
    // owned set? (1/0; -1 unknown). Regions, not checks — the Mask Shop scene holds no early check.
    int (*IsRegionReachable)(const char*) = nullptr;
    // Optional cross-entrance mark: JSON array of region keys (OOT: names, MM: decimal RandoRegionId)
    // this oracle must treat as externally reachable on subsequent queries. Null = no cross entrances.
    void (*SetExternallyReachableRegions)(const char*) = nullptr;
};

// One cross-entrance gate for the fixpoint: when the DOOR's exterior region is reachable in its
// game, the assigned interior's region becomes reachable in the interior's game (docs §4.3).
struct CrossGateInfo {
    bool doorIsOot;
    std::string doorRegion;
    bool interiorIsOot;
    std::string interiorRegion;
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
    CwItem item;
};

// ---------- Combined assumed fill ----------

struct CombinedFillResult {
    std::string spoilerJson;
    bool success;
    std::string error;
};

// portalGateRegion: the OOT logic region whose access gates MM (e.g. "Market Mask Shop" — interior
// shuffle can move the portal). Re-evaluated each fixpoint iteration via ootOracle.IsRegionReachable.
// Empty / null predicate / unknown region = gate open (fail open; final validation still catches it).
// progress: optional thread-safe progress struct polled by the UI. May be nullptr.
// forcedOotJson: OOT checks the dump can't carry (e.g. Link's Pocket). Each forced item is reserved
// out of the cross pool, owned-from-start for logic, and appended to the OOT placements.
inline CombinedFillResult CrossWorldCombinedFill(const std::string& sohDumpJson, const std::string& mmDumpJson,
                                                 uint32_t masterSeed, const OracleFns& ootOracle,
                                                 const OracleFns& mmOracle, const std::string& portalGateRegion = "",
                                                 ComboRando::ComboGenProgress* progress = nullptr,
                                                 const std::string& forcedOotJson = "",
                                                 const std::vector<CrossGateInfo>& crossGates = {}) {
    CombinedFillResult result;
    result.success = false;

    if (progress)
        progress->phase.store(1); // Preparing pools

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
            if (name.empty() || vi.empty())
                continue;
            allChecks.push_back({ game, name });
            bool adv = c.value("advancement", true);
            (adv ? advItems : junkItems).push_back({ game, vi, adv });
        }
    };

    try {
        parsePool(GAME_OOT, sohDumpJson);
        parsePool(GAME_MM, mmDumpJson);
    } catch (const std::exception& e) {
        result.error = std::string("Pool parse error: ") + e.what();
        return result;
    }

    // Forced OOT placements (e.g. Link's Pocket): reserve each item out of the cross pool and treat
    // it as owned-from-start (auto-granted at save creation). Appended to placements after the fill.
    std::vector<CwPlacement> forcedPlacements;
    std::vector<std::string> ootForcedOwned, mmForcedOwned;
    {
        auto removeOneItem = [](std::vector<CwItem>& v, Game g, const std::string& name) -> bool {
            for (size_t i = 0; i < v.size(); ++i) {
                if (v[i].game == g && v[i].name == name) {
                    v[i] = v.back();
                    v.pop_back();
                    return true;
                }
            }
            return false;
        };
        CwRng frng(masterSeed ^ 0xF0F0F0F0u);
        try {
            if (!forcedOotJson.empty()) {
                auto fj = nlohmann::json::parse(forcedOotJson);
                for (auto& [checkName, spec] : fj.items()) {
                    std::string itemName;
                    bool adv = true;
                    if (spec.contains("item")) {
                        itemName = spec.value("item", std::string{});
                        if (removeOneItem(advItems, GAME_OOT, itemName)) {
                            adv = true;
                        } else if (removeOneItem(junkItems, GAME_OOT, itemName)) {
                            adv = false;
                        } // else: not in pool — place anyway (it's owned at start regardless)
                    } else {
                        // "category": pick a random OOT item from the (settings-scoped) cross pool.
                        std::string cat = spec.value("category", std::string("any"));
                        std::vector<size_t> idx;
                        std::vector<CwItem>* src = nullptr;
                        for (size_t i = 0; i < advItems.size(); ++i)
                            if (advItems[i].game == GAME_OOT)
                                idx.push_back(i);
                        if (!idx.empty()) {
                            src = &advItems;
                            adv = true;
                        } else if (cat == "any") {
                            for (size_t i = 0; i < junkItems.size(); ++i)
                                if (junkItems[i].game == GAME_OOT)
                                    idx.push_back(i);
                            if (!idx.empty()) {
                                src = &junkItems;
                                adv = false;
                            }
                        }
                        if (src) {
                            size_t pick = idx[frng.below(static_cast<uint32_t>(idx.size()))];
                            itemName = (*src)[pick].name;
                            (*src)[pick] = src->back();
                            src->pop_back();
                        }
                    }
                    if (itemName.empty())
                        continue;
                    forcedPlacements.push_back({ { GAME_OOT, checkName }, { GAME_OOT, itemName, adv } });
                    ootForcedOwned.push_back(itemName);
                    // The forced check (Link's Pocket) is an EXTRA location with no vanilla item of
                    // its own, so reserving an item for it would leave the dump's checks one item
                    // short (an empty check). Add one OOT junk filler to keep items==checks balanced.
                    for (size_t qi = 0; qi < junkItems.size(); ++qi) {
                        if (junkItems[qi].game == GAME_OOT) {
                            CwItem filler = junkItems[qi]; // copy before push_back (may reallocate)
                            junkItems.push_back(filler);
                            break;
                        }
                    }
                }
            }
        } catch (...) {}
    }

    CwRng rng(masterSeed);
    cwShuffle(allChecks, rng);

    // OOT and MM check names are distinct namespaces; key fill bookkeeping by game+name.
    auto checkKey = [](const CwCheck& c) { return std::string(c.game == GAME_OOT ? "oot:" : "mm:") + c.name; };

    // Per-oracle query stats (count + total ms), logged on completion — the searches dominate
    // fill time, so this is the first thing to read when generation feels slow.
    struct QueryStats {
        uint32_t count = 0;
        int64_t ms = 0;
    };
    QueryStats ootStats, mmStats;

    auto queryReachable = [&](const OracleFns& oracle,
                              const std::vector<std::string>& ownedItems) -> std::unordered_set<std::string> {
        QueryStats& stats = (&oracle == &ootOracle) ? ootStats : mmStats;
        auto t0 = std::chrono::steady_clock::now();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& n : ownedItems)
            arr.push_back(n);
        oracle.Reset();
        oracle.SetOwnedItems(arr.dump().c_str());
        std::string raw = oracle.GetReachableChecks();
        std::unordered_set<std::string> out;
        try {
            auto parsed = nlohmann::json::parse(raw);
            for (const auto& name : parsed)
                out.insert(name.get<std::string>());
        } catch (...) {}
        stats.count++;
        stats.ms +=
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        return out;
    };

    // Cross-game sphere-collect fixpoint: starting from the per-game base sets (unplaced
    // advancement items), repeatedly query both oracles and credit any prior placement whose
    // CHECK became reachable to the ITEM's game's owned set, until nothing changes. This spans
    // both games because an MM item at an OOT check (or vice versa) can open progress anywhere,
    // including the portal itself — so portal openness is re-evaluated every iteration.
    // Cross-entrance gates: a door reachable in its game marks the assigned interior's region
    // externally reachable in the other (pushed before the queries of the NEXT round; the loop
    // keeps iterating while gates open, so the marks converge with the owned sets).
    std::vector<CwPlacement> placements;
    // Marks are query-time state inside the game DLLs — always cleared on the way out, or a stale
    // list would leak into the live game's own logic queries (tracker availability).
    struct CrossMarkCleaner {
        const OracleFns* a;
        const OracleFns* b;
        ~CrossMarkCleaner() {
            if (a->SetExternallyReachableRegions)
                a->SetExternallyReachableRegions("");
            if (b->SetExternallyReachableRegions)
                b->SetExternallyReachableRegions("");
        }
    } crossMarkCleaner{ &ootOracle, &mmOracle };
    auto pushCrossMarks = [&](const std::vector<bool>& gateOpen) {
        if (crossGates.empty())
            return;
        nlohmann::json ootMarks = nlohmann::json::array(), mmMarks = nlohmann::json::array();
        for (size_t i = 0; i < crossGates.size(); ++i) {
            if (!gateOpen[i])
                continue;
            (crossGates[i].interiorIsOot ? ootMarks : mmMarks).push_back(crossGates[i].interiorRegion);
        }
        if (ootOracle.SetExternallyReachableRegions)
            ootOracle.SetExternallyReachableRegions(ootMarks.dump().c_str());
        if (mmOracle.SetExternallyReachableRegions)
            mmOracle.SetExternallyReachableRegions(mmMarks.dump().c_str());
    };
    auto reachableFixpoint = [&](const std::vector<std::string>& ootBase, const std::vector<std::string>& mmBase)
        -> std::pair<std::unordered_set<std::string>, std::unordered_set<std::string>> {
        std::vector<std::string> ootOwned = ootBase, mmOwned = mmBase;
        // Forced placements (Link's Pocket etc.) are granted at save creation → owned from the start.
        ootOwned.insert(ootOwned.end(), ootForcedOwned.begin(), ootForcedOwned.end());
        mmOwned.insert(mmOwned.end(), mmForcedOwned.begin(), mmForcedOwned.end());
        std::vector<bool> credited(placements.size(), false);
        // Per-call: whether each cross door is reachable under THIS owned set (a gate open under a
        // larger assumed set is not necessarily open under a smaller one).
        std::vector<bool> gateOpen(crossGates.size(), false);
        std::unordered_set<std::string> ootReachable, mmReachable;
        for (;;) {
            pushCrossMarks(gateOpen);
            ootReachable = queryReachable(ootOracle, ootOwned);
            // Portal gate: region access reflects the OOT query above; -1 (unknown region) fails open.
            bool portalOpen = portalGateRegion.empty() || !ootOracle.IsRegionReachable ||
                              ootOracle.IsRegionReachable(portalGateRegion.c_str()) != 0;
            bool gatesChanged = false;
            if (ootOracle.IsRegionReachable) {
                for (size_t i = 0; i < crossGates.size(); ++i) {
                    if (!gateOpen[i] && crossGates[i].doorIsOot &&
                        ootOracle.IsRegionReachable(crossGates[i].doorRegion.c_str()) == 1) {
                        gateOpen[i] = true;
                        gatesChanged = true;
                    }
                }
            }
            mmReachable = portalOpen ? queryReachable(mmOracle, mmOwned) : std::unordered_set<std::string>{};
            if (mmOracle.IsRegionReachable && portalOpen) {
                for (size_t i = 0; i < crossGates.size(); ++i) {
                    if (!gateOpen[i] && !crossGates[i].doorIsOot &&
                        mmOracle.IsRegionReachable(crossGates[i].doorRegion.c_str()) == 1) {
                        gateOpen[i] = true;
                        gatesChanged = true;
                    }
                }
            }
            bool changed = gatesChanged;
            for (size_t i = 0; i < placements.size(); ++i) {
                if (credited[i])
                    continue;
                const auto& reach = placements[i].check.game == GAME_OOT ? ootReachable : mmReachable;
                if (reach.count(placements[i].check.name)) {
                    auto& owned = placements[i].item.game == GAME_OOT ? ootOwned : mmOwned;
                    owned.push_back(placements[i].item.name);
                    credited[i] = true;
                    changed = true;
                }
            }
            if (!changed)
                return { std::move(ootReachable), std::move(mmReachable) };
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
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - passStart)
                .count();
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
            size_t k = std::min({ batchCap, std::max<size_t>(1, toPlace.size() / 4), toPlace.size() });

            std::vector<CwItem> batch;
            batch.reserve(k);
            for (size_t i = 0; i < k; ++i) {
                batch.push_back(toPlace.back());
                toPlace.pop_back();
                removeOne(batch.back().game == GAME_OOT ? ootRemaining : mmRemaining, batch.back().name);
            }

            auto [ootReachable, mmReachable] = reachableFixpoint(ootRemaining, mmRemaining);

            std::vector<size_t> candidates;
            for (size_t ci = 0; ci < allChecks.size(); ++ci) {
                if (filledChecks.count(checkKey(allChecks[ci])))
                    continue;
                const auto& reach = allChecks[ci].game == GAME_OOT ? ootReachable : mmReachable;
                if (reach.count(allChecks[ci].name))
                    candidates.push_back(ci);
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
                std::cerr << "[ComboShip] CrossWorldCombinedFill: dead end placing '" << batch.front().name
                          << "' (pass " << pass << ", " << placements.size() << " placed, " << passMs()
                          << " ms) — retrying\n";
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
            if (progress)
                progress->placed.store(static_cast<int>(placements.size()));
        }
        if (deadEnd)
            continue;

        // Phase B: junk fast-fill into the remaining checks — zero oracle calls.
        std::vector<CwItem> junkToPlace = junkItems;
        cwShuffle(junkToPlace, rng);
        size_t ji = 0;
        for (size_t ci = 0; ci < allChecks.size() && ji < junkToPlace.size(); ++ci) {
            if (filledChecks.count(checkKey(allChecks[ci])))
                continue;
            placements.push_back({ allChecks[ci], junkToPlace[ji++] });
            filledChecks.insert(checkKey(allChecks[ci]));
        }
        if (ji < junkToPlace.size()) {
            std::cerr << "[ComboShip] CrossWorldCombinedFill: pool/check mismatch — " << (junkToPlace.size() - ji)
                      << " junk items left over\n";
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
            if (reach.count(p.check.name))
                continue;
            if (p.item.advancement) {
                ++advUnreachable;
            } else {
                ++junkUnreachable;
                if (p.check.game == GAME_OOT)
                    ++junkUnreachableOot;
            }
        }
        if (advUnreachable > 0) {
            std::cerr << "[ComboShip] CrossWorldCombinedFill: validation failed — " << advUnreachable
                      << " advancement items on unreachable checks (pass " << pass << ", " << passMs()
                      << " ms) — retrying\n";
            continue;
        }

        std::cout << "[ComboShip] CrossWorldCombinedFill: all " << advItems.size()
                  << " advancement items reachable from scratch (pass " << pass << ", " << passMs() << " ms; "
                  << junkItems.size() << " junk, " << junkUnreachable
                  << " junk on oracle-unreachable checks [oot=" << junkUnreachableOot
                  << " mm=" << (junkUnreachable - junkUnreachableOot) << "])\n";
        std::cout << "[ComboShip] CrossWorldCombinedFill: oracle queries — oot " << ootStats.count << "x/"
                  << ootStats.ms << " ms, mm " << mmStats.count << "x/" << mmStats.ms << " ms\n";
        fillOk = true;
    }

    if (!fillOk) {
        result.error = "assumed fill failed after " + std::to_string(kMaxPasses) +
                       " passes (dead ends or unreachable checks; see log)";
        return result;
    }

    // Append the reserved forced placements (Link's Pocket etc.) so they flow into the spoiler and
    // get committed to the oracle alongside the assumed-fill results.
    if (!forcedPlacements.empty()) {
        placements.insert(placements.end(), forcedPlacements.begin(), forcedPlacements.end());
        std::cout << "[ComboShip] CrossWorldCombinedFill: " << forcedPlacements.size()
                  << " forced placement(s) applied (e.g. Link's Pocket)\n";
    }

    if (progress) {
        progress->placed.store(progress->total.load()); // all placed
        progress->phase.store(3);                       // Finalizing
    }

    // --- Build spoiler (same shape as the no-logic generator) ---
    nlohmann::json spoiler;
    spoiler["masterSeed"] = masterSeed;
    spoiler["mode"] = "combined-logic assumed-fill";
    spoiler["fillStats"] = { { "advancementItems", static_cast<uint32_t>(advItems.size()) },
                             { "junkItems", static_cast<uint32_t>(junkItems.size()) },
                             { "passes", passesUsed } };

    nlohmann::json ootPlacements = nlohmann::json::object();
    nlohmann::json mmPlacements = nlohmann::json::object();
    nlohmann::json foreignMarkers = nlohmann::json::array();

    for (const auto& p : placements) {
        if (p.check.game == GAME_OOT) {
            ootPlacements[p.check.name] = p.item.name;
        } else {
            mmPlacements[p.check.name] = p.item.name;
        }
        if (p.check.game != p.item.game) {
            foreignMarkers.push_back({ { "checkGame", p.check.game == GAME_OOT ? "oot" : "mm" },
                                       { "checkName", p.check.name },
                                       { "itemGame", p.item.game == GAME_OOT ? "oot" : "mm" },
                                       { "itemName", p.item.name } });
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

inline std::string CrossWorldGenerateSpoiler(const std::string& sohDumpJson, const std::string& mmDumpJson,
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
                if (!c.contains("vanillaItem"))
                    continue;
                std::string v = c.value("vanillaItem", std::string{});
                if (v.empty())
                    continue;
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
        } catch (...) { spoiler[std::string(key) + "Error"] = true; }
        spoiler[key] = out;
    };

    doGame("oot", sohDumpJson, masterSeed ^ 0x4F4F5400u);
    doGame("mm", mmDumpJson, masterSeed ^ 0x4D4D0000u);

    return spoiler.dump(2);
}

} // namespace ComboRando
