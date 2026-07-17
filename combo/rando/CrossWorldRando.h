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
};

// ---------- Data types ----------

// Per-game OOT accessibility, mapped from OOT's RSK_LOGIC_RULES + RSK_ALL_LOCATIONS_REACHABLE.
// MM is always ALL_REACHABLE. See CrossWorldCombinedFill for the per-mode fill/validation behavior.
//   ALL_REACHABLE  every OOT advancement item lands reachable (default; unchanged behavior)
//   BEATABLE_ONLY  OOT progression may strand off-path, but the seed stays beatable (ALR-off)
//   NO_LOGIC       OOT items land anywhere; only MM stays fully reachable
enum class OotAccess { ALL_REACHABLE, BEATABLE_ONLY, NO_LOGIC };

// Maps the OOT dump's "accessibility" block to a mode: No Logic wins; else ALR-off => BEATABLE_ONLY;
// else ALL_REACHABLE. Missing/old dumps default to ALL_REACHABLE (unchanged behavior).
inline OotAccess OotAccessFromDump(const std::string& sohDumpJson) {
    try {
        auto a = nlohmann::json::parse(sohDumpJson).value("accessibility", nlohmann::json::object());
        if (a.value("noLogic", false))
            return OotAccess::NO_LOGIC;
        if (!a.value("allLocationsReachable", true))
            return OotAccess::BEATABLE_ONLY;
    } catch (...) {}
    return OotAccess::ALL_REACHABLE;
}

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

// portalCheckName: the OOT check/region name that gates access to MM (e.g. "Mido's House").
// If empty, MM is reachable from start.
// progress: optional thread-safe progress struct polled by the UI. May be nullptr.
// forcedOotJson: OOT checks the dump can't carry (e.g. Link's Pocket). Each forced item is reserved
// out of the cross pool, owned-from-start for logic, and appended to the OOT placements.
inline CombinedFillResult CrossWorldCombinedFill(const std::string& sohDumpJson, const std::string& mmDumpJson,
                                                 uint32_t masterSeed, const OracleFns& ootOracle,
                                                 const OracleFns& mmOracle, const std::string& portalCheckName = "",
                                                 ComboRando::ComboGenProgress* progress = nullptr,
                                                 const std::string& forcedOotJson = "",
                                                 OotAccess ootAccess = OotAccess::ALL_REACHABLE) {
    CombinedFillResult result;
    result.success = false;

    if (progress)
        progress->phase.store(1); // Preparing pools

    // --- Parse pools (single pass per dump) ---
    // Checks from "checks", items from the real "pool" (split by advancement), confined
    // pre-placements from "fixed". Falls back to per-check vanillaItem if an old DLL omits "pool".
    std::vector<CwItem> advItems, junkItems;
    std::vector<CwCheck> allChecks;
    std::vector<CwPlacement> lockedPlacements; // confined pre-placements (own-dungeon keys, etc.)

    auto parsePool = [&](Game game, const std::string& dumpJson) {
        auto d = nlohmann::json::parse(dumpJson);

        // Fillable checks (name only; the pool below feeds the items).
        for (auto& c : d.value("checks", nlohmann::json::array())) {
            std::string name = c.value("name", "");
            if (!name.empty())
                allChecks.push_back({ game, name });
        }

        // Item pool: the game's real generated pool, split into advancement/junk.
        if (d.contains("pool")) {
            for (auto& it : d["pool"]) {
                std::string name = it.value("name", "");
                if (name.empty())
                    continue;
                bool adv = it.value("advancement", true);
                (adv ? advItems : junkItems).push_back({ game, name, adv });
            }
        } else {
            // Fallback for an older game DLL: reconstruct the pool from per-check vanilla items.
            for (auto& c : d.value("checks", nlohmann::json::array())) {
                std::string vi = c.value("vanillaItem", "");
                if (vi.empty())
                    continue;
                bool adv = c.value("advancement", true);
                (adv ? advItems : junkItems).push_back({ game, vi, adv });
            }
        }

        // Confined pre-placements: locked to their check, credited to logic when reached.
        for (auto& f : d.value("fixed", nlohmann::json::array())) {
            std::string check = f.value("check", "");
            std::string item = f.value("item", "");
            if (check.empty() || item.empty())
                continue;
            lockedPlacements.push_back({ { game, check }, { game, item, f.value("advancement", true) } });
        }
    };

    try {
        parsePool(GAME_OOT, sohDumpJson);
        parsePool(GAME_MM, mmDumpJson);
    } catch (const std::exception& e) {
        result.error = std::string("Pool parse error: ") + e.what();
        return result;
    }

    // Portal-key guard: Overworld Keys ON + Force Mask Shop Key OFF under a relaxed mode may strand the
    // Mask Shop Key (portalCheckName="" can't detect it) → MM unreachable. Warn. See docs/UPSTREAM_MERGES.
    if (ootAccess != OotAccess::ALL_REACHABLE && portalCheckName.empty()) {
        try {
            auto a = nlohmann::json::parse(sohDumpJson).value("accessibility", nlohmann::json::object());
            if (a.value("lockOverworldDoors", false) && !a.value("forceMaskShopKey", false))
                std::cerr << "[ComboShip] CrossWorldCombinedFill: WARNING — Overworld Keys ON + Force Mask Shop "
                             "Key OFF under a relaxed OOT mode: the Mask Shop Key's reach-path is not guaranteed "
                             "(portal ungated); MM may be unreachable. Enable Force Mask Shop Key.\n";
        } catch (...) {}
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

    // Locked (fixed) checks are each game's OWN vanilla/confined placements, already guaranteed
    // reachable by that game's own fill; the cross-fill neither shuffles nor re-validates them (the
    // oracles under-model some — e.g. vanilla GS spots — so they'd falsely fail validation).
    std::unordered_set<std::string> lockedCheckKeys;
    for (const auto& lp : lockedPlacements)
        lockedCheckKeys.insert(checkKey(lp.check));

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
    std::vector<CwPlacement> placements;
    // ootReachable/mmReachable = reachable checks at the fixpoint; ootOwned = OOT items collected along
    // the way (drives the BEATABLE_ONLY boss-key goal check without a second traversal).
    struct FixResult {
        std::unordered_set<std::string> ootReachable, mmReachable;
        std::vector<std::string> ootOwned;
    };
    auto reachableFixpoint = [&](const std::vector<std::string>& ootBase,
                                 const std::vector<std::string>& mmBase) -> FixResult {
        std::vector<std::string> ootOwned = ootBase, mmOwned = mmBase;
        // Forced placements (Link's Pocket etc.) are granted at save creation → owned from the start.
        ootOwned.insert(ootOwned.end(), ootForcedOwned.begin(), ootForcedOwned.end());
        mmOwned.insert(mmOwned.end(), mmForcedOwned.begin(), mmForcedOwned.end());
        std::vector<bool> credited(placements.size(), false);
        std::unordered_set<std::string> ootReachable, mmReachable;
        for (;;) {
            ootReachable = queryReachable(ootOracle, ootOwned);
            bool portalOpen = portalCheckName.empty() || ootReachable.count(portalCheckName) > 0;
            mmReachable = portalOpen ? queryReachable(mmOracle, mmOwned) : std::unordered_set<std::string>{};
            bool changed = false;
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
                return { std::move(ootReachable), std::move(mmReachable), std::move(ootOwned) };
        }
    };

    if (progress) {
        progress->phase.store(2);                                 // Placing items
        progress->total.store(static_cast<int>(advItems.size())); // tracked phase = advancement fill
        progress->placed.store(0);
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

        // Seed confined pre-placements: locked (never re-filled) and credited to logic when their
        // check is reached, via reachableFixpoint — the collected-in-place semantics for dungeon keys.
        placements = lockedPlacements;
        for (const auto& lp : lockedPlacements)
            filledChecks.insert(checkKey(lp.check));

        // Phase A: place advancement items with logic, assuming only UNPLACED ones are owned. NO_LOGIC
        // assumed-fills only MM adv (OOT adv rides Phase B junk); other modes keep advItems unreordered.
        std::vector<CwItem> toPlace;
        if (ootAccess == OotAccess::NO_LOGIC) {
            for (const auto& it : advItems)
                if (it.game == GAME_MM)
                    toPlace.push_back(it);
        } else {
            toPlace = advItems;
        }
        std::vector<CwItem> relaxedToJunk; // OOT adv stranded off-path under BEATABLE_ONLY dead-ends
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

            auto fr = reachableFixpoint(ootRemaining, mmRemaining);
            auto& ootReachable = fr.ootReachable;
            auto& mmReachable = fr.mmReachable;

            std::vector<size_t> candidates;
            for (size_t ci = 0; ci < allChecks.size(); ++ci) {
                if (filledChecks.count(checkKey(allChecks[ci])))
                    continue;
                const auto& reach = allChecks[ci].game == GAME_OOT ? ootReachable : mmReachable;
                if (reach.count(allChecks[ci].name))
                    candidates.push_back(ci);
            }

            if (candidates.size() < batch.size()) {
                // BEATABLE_ONLY: a single OOT advancement item with no reachable check may strand
                // off-path — move it to the junk fast-fill and continue (already removed from
                // toPlace/ootRemaining above). MM items and ALL_REACHABLE always retry instead.
                if (batch.size() == 1 && ootAccess == OotAccess::BEATABLE_ONLY && batch.front().game == GAME_OOT) {
                    relaxedToJunk.push_back(batch.front());
                    continue;
                }
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
                // Count advancement placed (placements started at lockedPlacements), so placed/total is
                // an honest 0..100% over the advancement fill — not locked+adv+junk over adv alone.
                progress->placed.store(static_cast<int>(placements.size() - lockedPlacements.size()));
        }
        if (deadEnd)
            continue;

        // Phase B: junk fast-fill into the remaining checks — zero oracle calls. Relaxed OOT
        // advancement (NO_LOGIC: all OOT adv; BEATABLE_ONLY: stranded dead-end items) rides this
        // stream — placed without logic, tolerated-if-unreachable by the mode-aware validation below.
        std::vector<CwItem> junkToPlace = junkItems;
        if (ootAccess == OotAccess::NO_LOGIC) {
            for (const auto& it : advItems)
                if (it.game == GAME_OOT)
                    junkToPlace.push_back(it);
        }
        junkToPlace.insert(junkToPlace.end(), relaxedToJunk.begin(), relaxedToJunk.end());
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
        auto vf = reachableFixpoint({}, {});
        auto& ootFinal = vf.ootReachable;
        auto& mmFinal = vf.mmReachable;
        // Classify unreachable advancement by ITEM game: MM adv unreachable is always fatal (MM must
        // stay all-reachable); OOT adv unreachable is fatal only under ALL_REACHABLE, tolerated under
        // the relaxed modes (it was placed off-logic on purpose).
        size_t mmAdvUnreachable = 0, ootAdvUnreachable = 0, junkUnreachable = 0, junkUnreachableOot = 0;
        for (const auto& p : placements) {
            // Locked placements are the game's own guaranteed-reachable vanilla/confined items — not
            // the cross-fill's responsibility, and the oracles under-model some. Don't validate them.
            if (lockedCheckKeys.count(checkKey(p.check)))
                continue;
            const auto& reach = p.check.game == GAME_OOT ? ootFinal : mmFinal;
            if (reach.count(p.check.name))
                continue;
            if (p.item.advancement) {
                (p.item.game == GAME_MM ? mmAdvUnreachable : ootAdvUnreachable)++;
            } else {
                ++junkUnreachable;
                if (p.check.game == GAME_OOT)
                    ++junkUnreachableOot;
            }
        }
        // BEATABLE_ONLY additionally requires the win still holds (mirrors DefaultGanonMajoraGoal;
        // duplicated here to keep this header free of ComboPlaythrough.h). NO_LOGIC accepts an
        // OOT-unbeatable seed; MM stays reachable+beatable regardless.
        auto beatableGoal = [&]() {
            static const char* kOotTowerTop = "Ganon's Castle Tower Boss Key Chest";
            static const char* kOotBossKey = "Ganon's Castle Boss Key";
            static const char* kMmWin = "RC_MOON_MAJORA_POT_01";
            return ootFinal.count(kOotTowerTop) > 0 &&
                   std::find(vf.ootOwned.begin(), vf.ootOwned.end(), kOotBossKey) != vf.ootOwned.end() &&
                   mmFinal.count(kMmWin) > 0;
        };
        bool goalOk = (ootAccess != OotAccess::BEATABLE_ONLY) || beatableGoal();
        bool needRetry = mmAdvUnreachable > 0 || (ootAccess == OotAccess::ALL_REACHABLE && ootAdvUnreachable > 0) ||
                         (ootAccess == OotAccess::BEATABLE_ONLY && !goalOk);
        if (needRetry) {
            std::cerr << "[ComboShip] CrossWorldCombinedFill: validation failed — mmAdvUnreachable=" << mmAdvUnreachable
                      << " ootAdvUnreachable=" << ootAdvUnreachable
                      << (ootAccess == OotAccess::BEATABLE_ONLY ? (goalOk ? " goal=ok" : " goal=UNBEATABLE") : "")
                      << " (pass " << pass << ", " << passMs() << " ms) — retrying\n";
            continue;
        }
        if (ootAdvUnreachable > 0)
            std::cout << "[ComboShip] CrossWorldCombinedFill: " << ootAdvUnreachable
                      << " OOT advancement item(s) on unreachable checks — tolerated (relaxed OOT mode)\n";

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
                                       { "itemName", p.item.name },
                                       // Propagate advancement so foreign checks holding an important item
                                       // still play the held-up pickup animation (else defaulted to junk).
                                       { "advancement", p.item.advancement } });
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
            std::vector<std::string> checkNames, poolItems;
            for (auto& c : d.value("checks", nlohmann::json::array())) {
                std::string n = c.value("name", std::string{});
                if (!n.empty())
                    checkNames.push_back(n);
            }
            if (d.contains("pool")) {
                for (auto& it : d["pool"]) {
                    std::string n = it.value("name", std::string{});
                    if (!n.empty())
                        poolItems.push_back(n);
                }
            } else {
                for (auto& c : d.value("checks", nlohmann::json::array())) {
                    std::string v = c.value("vanillaItem", std::string{});
                    if (!v.empty())
                        poolItems.push_back(v);
                }
            }
            // Confined pre-placements pass through unshuffled.
            for (auto& f : d.value("fixed", nlohmann::json::array())) {
                std::string chk = f.value("check", std::string{});
                std::string item = f.value("item", std::string{});
                if (!chk.empty() && !item.empty())
                    out[chk] = item;
            }
            std::vector<std::string> shuffled = poolItems;
            CwRng rng(seed);
            cwShuffle(shuffled, rng);
            for (size_t i = 0; i < checkNames.size() && i < shuffled.size(); ++i) {
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
