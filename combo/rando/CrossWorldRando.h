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
    // OOT only (null for MM): is the OOT->MM portal region reachable? Valid only immediately after a
    // GetReachableChecks call, whose owned-set it describes (the DLL piggybacks on that search).
    uint8_t (*GetPortalOpen)(void) = nullptr;
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

// Combined win condition (#136). hunt=false is the default both-bosses goal; hunt=true wins on
// `required` Triforce Pieces from EITHER game's pool. These are the friendly names both dumps emit.
inline constexpr const char* kOotTriforcePiece = "Triforce Piece";
inline constexpr const char* kMmTriforcePiece = "Piece of the Triforce";

struct CwGoal {
    bool hunt = false;
    int required = 0;
};

inline bool CwIsTriforcePiece(GameId itemGame, const std::string& itemName) {
    return itemName == (itemGame == GAME_OOT ? kOotTriforcePiece : kMmTriforcePiece);
}

// How many pieces the two settings-scoped pools actually hold. Logged loudly: the names above are
// coupled to each game's item table, so a rename would otherwise silently drop that game's pieces.
inline int CountPoolTriforcePieces(const std::string& sohDumpJson, const std::string& mmDumpJson) {
    auto countIn = [](const std::string& dump, GameId g) {
        int n = 0;
        try {
            auto d = nlohmann::json::parse(dump);
            for (const auto& it : d.value("pool", nlohmann::json::array()))
                n += CwIsTriforcePiece(g, it.value("name", std::string())) ? 1 : 0;
        } catch (...) {}
        return n;
    };
    const int oot = countIn(sohDumpJson, GAME_OOT);
    const int mm = countIn(mmDumpJson, GAME_MM);
    std::cout << "[ComboShip] Triforce Hunt: combined pool holds " << oot << " OOT + " << mm << " MM piece(s)\n";
    if (oot == 0 || mm == 0)
        std::cerr << "[ComboShip] Triforce Hunt: WARNING — one game contributes no pieces (slider at 0, or its "
                     "piece item was renamed)\n";
    return oot + mm;
}

// Reuses GameId from CrossForeign.h (GAME_OOT = 0, GAME_MM = 1)
using Game = GameId;

// Native item category from each game's dump. Governs what may be DISCARDED, independently of
// `advancement` (which governs which fill phase places it). Only JUNK is ever discardable.
enum class CwCat { JUNK, LESSER, HEALTH, BOSS_KEY, SMALL_KEY, TOKEN, MAJOR, MASK, STRAY_FAIRY, UNKNOWN };

inline CwCat CwCatFromName(const std::string& s) {
    if (s == "junk")
        return CwCat::JUNK;
    if (s == "lesser")
        return CwCat::LESSER;
    if (s == "health")
        return CwCat::HEALTH;
    if (s == "bossKey")
        return CwCat::BOSS_KEY;
    if (s == "smallKey")
        return CwCat::SMALL_KEY;
    if (s == "token")
        return CwCat::TOKEN;
    if (s == "major")
        return CwCat::MAJOR;
    if (s == "mask")
        return CwCat::MASK;
    if (s == "strayFairy")
        return CwCat::STRAY_FAIRY;
    return CwCat::UNKNOWN; // older DLL or a new category — never discardable
}

inline const char* CwCatName(CwCat c) {
    switch (c) {
        case CwCat::JUNK:
            return "junk";
        case CwCat::LESSER:
            return "lesser";
        case CwCat::HEALTH:
            return "health";
        case CwCat::BOSS_KEY:
            return "bossKey";
        case CwCat::SMALL_KEY:
            return "smallKey";
        case CwCat::TOKEN:
            return "token";
        case CwCat::MAJOR:
            return "major";
        case CwCat::MASK:
            return "mask";
        case CwCat::STRAY_FAIRY:
            return "strayFairy";
        default:
            return "unknown";
    }
}

struct CwItem {
    Game game;
    std::string name;
    bool advancement;
    CwCat cat = CwCat::UNKNOWN;
    // Only native-JUNK may absorb a surplus, and never if it gates logic: OOT files bombchus and
    // Buy Blue Fire/Arrows as ITEM_CATEGORY_JUNK with advancement=true (item_list.cpp:278-325).
    bool discardable() const {
        return cat == CwCat::JUNK && !advancement;
    }
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

// Retry budgets. Pass counts, never wall-clock: a time limit would make success machine-dependent and
// break seed sharing. kFillAttempts is shared with ComboShip.cpp/ComboRandoHeadless.cpp — headless seeds
// only reproduce in-game ones while both loops agree.
// Measured: 25 seeds over 3 adult/Overworld-Keys configs all succeeded on pass 1 with 0 retries, so 6
// total passes is ~6x headroom while capping the worst-case wait (15 passes was ~3 min).
constexpr int kFillAttempts = 2;   // whole-fill retries (re-derives the seed, re-rolls the dumps)
constexpr int kMaxPasses = 3;      // passes within one fill
constexpr int kMaxPrereqTries = 4; // Tier-1 repicks of just the portal prerequisites

// MM access is gated on the OOT->MM portal region (Happy Mask Shop) via ootOracle.GetPortalOpen.
// progress: optional thread-safe progress struct polled by the UI. May be nullptr.
// forcedOotJson: OOT checks the dump can't carry (e.g. Link's Pocket). Each forced item is reserved
// out of the cross pool, owned-from-start for logic, and appended to the OOT placements.
inline CombinedFillResult CrossWorldCombinedFill(const std::string& sohDumpJson, const std::string& mmDumpJson,
                                                 uint32_t masterSeed, const OracleFns& ootOracle,
                                                 const OracleFns& mmOracle,
                                                 ComboRando::ComboGenProgress* progress = nullptr,
                                                 const std::string& forcedOotJson = "",
                                                 OotAccess ootAccess = OotAccess::ALL_REACHABLE, CwGoal goal = {}) {
    CombinedFillResult result;
    result.success = false;

    // Portal gate: NO_LOGIC deliberately skips it (an impossible seed is that mode's point). Any other
    // mode without the export would silently fill MM as sphere-0 reachable — the bug this gate fixes.
    const bool portalGated = ootAccess != OotAccess::NO_LOGIC;
    if (portalGated && ootOracle.GetPortalOpen == nullptr) {
        result.error = "OOT oracle is missing Combo_SOH_Rando_GetPortalOpen — rebuild soh.dll (the OOT->MM "
                       "portal cannot be gated without it)";
        return result;
    }

    if (progress)
        progress->phase.store(1); // Preparing pools

    // --- Parse pools (single pass per dump) ---
    // Checks from "checks", items from the real "pool" (split by advancement), confined
    // pre-placements from "fixed". Falls back to per-check vanillaItem if an old DLL omits "pool".
    std::vector<CwItem> advItems, junkItems;
    std::vector<CwCheck> allChecks;
    std::vector<CwPlacement> lockedPlacements; // confined pre-placements (own-dungeon keys, etc.)

    // Set when a dump carries pool[] but no category — a stale DLL, which would otherwise surface as an
    // unbalanceable pool ("no discardable junk") instead of the rebuild it actually needs.
    std::string missingCategory;

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
            size_t parsed = 0, withCategory = 0;
            for (auto& it : d["pool"]) {
                std::string name = it.value("name", "");
                if (name.empty())
                    continue;
                bool adv = it.value("advancement", true);
                CwCat cat = CwCatFromName(it.value("category", std::string{}));
                ++parsed;
                withCategory += (cat != CwCat::UNKNOWN) ? 1 : 0;
                (adv ? advItems : junkItems).push_back({ game, name, adv, cat });
            }
            if (parsed > 0 && withCategory == 0)
                missingCategory = (game == GAME_OOT ? "soh.dll" : "2ship.dll");
        } else {
            // Older DLL: reconstruct from per-check vanilla items — exactly one per check, so the
            // balancer no-ops. Class non-advancement as junk so a forced reservation can still pad.
            for (auto& c : d.value("checks", nlohmann::json::array())) {
                std::string vi = c.value("vanillaItem", "");
                if (vi.empty())
                    continue;
                bool adv = c.value("advancement", true);
                (adv ? advItems : junkItems).push_back({ game, vi, adv, adv ? CwCat::UNKNOWN : CwCat::JUNK });
            }
        }

        // Confined pre-placements: locked to their check, credited to logic when reached. No category —
        // locked items never enter junkItems, so they are never trim candidates.
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
    if (!missingCategory.empty()) {
        result.error = missingCategory +
                       " is stale: its pool[] has no per-item `category`, so the fill cannot tell junk from "
                       "progression and refuses to balance the pool — rebuild soh.dll / 2ship.dll";
        std::cerr << "[ComboShip] CrossWorldCombinedFill: " << result.error << "\n";
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
                    // No filler: Link's Pocket isn't in checks[], so reserving its item is already
                    // balanced. The old filler left a permanent +1 surplus Phase B silently discarded.
                }
            }
        } catch (...) {}
    }

    // --- Balance each game's pool against its checks: P_g == C_g ---
    // Both generators over-supply on purpose and the over-supply is PROGRESSION, so only junk may be
    // sacrificed. RNG-free and ahead of `rng`, so it can't shift the stream. See deviations/rando.md.
    size_t trimmedTotal = 0, paddedTotal = 0;
    {
        auto countFor = [](const std::vector<CwItem>& v, Game g) {
            return static_cast<size_t>(std::count_if(v.begin(), v.end(), [g](const CwItem& i) { return i.game == g; }));
        };
        std::string balanceLine;
        std::vector<std::string> trimNotes;
        for (Game g : { GAME_OOT, GAME_MM }) {
            const char* tag = (g == GAME_OOT ? "oot" : "mm");
            const size_t checkCount = static_cast<size_t>(
                std::count_if(allChecks.begin(), allChecks.end(), [g](const CwCheck& c) { return c.game == g; }));
            const size_t itemCount = countFor(advItems, g) + countFor(junkItems, g);
            size_t trimmed = 0, padded = 0;

            if (itemCount > checkCount) {
                // Most-duplicated discardable name first, (count desc, name asc) — a total order, so
                // hash iteration order can't leak in. Sheds bulk filler, keeps the last of rarer junk.
                const size_t surplus = itemCount - checkCount;
                std::unordered_map<std::string, int> avail;
                for (const auto& i : junkItems)
                    if (i.game == g && i.discardable())
                        ++avail[i.name];
                std::unordered_map<std::string, int> remove;
                for (size_t k = 0; k < surplus; ++k) {
                    const std::string* best = nullptr;
                    int bestCount = 0;
                    for (const auto& [n, c] : avail) {
                        if (c <= 0)
                            continue;
                        if (!best || c > bestCount || (c == bestCount && n < *best)) {
                            best = &n;
                            bestCount = c;
                        }
                    }
                    if (!best) {
                        result.error = std::string("cannot balance the ") + tag +
                                       " pool: " + std::to_string(surplus - k) +
                                       " surplus item(s) left and no discardable junk remains; only junk may be "
                                       "discarded, so refusing to drop anything else";
                        std::cerr << "[ComboShip] CrossWorldCombinedFill: " << result.error << "\n";
                        return result;
                    }
                    --avail[*best];
                    ++remove[*best];
                }
                for (const auto& [n, c] : remove)
                    trimNotes.push_back(std::string(tag) + " junk: " + n + " x" + std::to_string(c));
                // Single pass: drop exactly `remove[name]` instances of each chosen junk name.
                std::vector<CwItem> kept;
                kept.reserve(junkItems.size());
                for (auto& i : junkItems) {
                    auto it = (i.game == g && i.discardable()) ? remove.find(i.name) : remove.end();
                    if (it != remove.end() && it->second > 0) {
                        --it->second;
                        ++trimmed;
                        continue;
                    }
                    kept.push_back(std::move(i));
                }
                junkItems = std::move(kept);
            } else if (itemCount < checkCount) {
                // Clone one of THIS game's junk items so no check goes itemless. Never clone the other
                // game's: the name wouldn't resolve in this game's apply path (a silent empty check).
                const size_t deficit = checkCount - itemCount;
                const CwItem* src = nullptr;
                for (const auto& i : junkItems)
                    if (i.game == g && i.discardable()) {
                        src = &i;
                        break;
                    }
                if (!src) {
                    result.error = std::string("cannot balance the ") + tag + " pool: " + std::to_string(deficit) +
                                   " check(s) short and no " + tag + " junk to clone";
                    std::cerr << "[ComboShip] CrossWorldCombinedFill: " << result.error << "\n";
                    return result;
                }
                CwItem filler = *src; // copy before push_back (may reallocate)
                for (size_t k = 0; k < deficit; ++k)
                    junkItems.push_back(filler);
                padded = deficit;
                std::cout << "[ComboShip] CrossWorldCombinedFill: " << tag << " pool was " << deficit
                          << " item(s) short of its checks; padded with junk ('" << filler.name
                          << "') — benign if a forced placement reserved an item, else the dump under-filled\n";
            }

            trimmedTotal += trimmed;
            paddedTotal += padded;
            balanceLine += std::string(balanceLine.empty() ? "" : ", ") + tag +
                           " items=" + std::to_string(countFor(advItems, g) + countFor(junkItems, g)) +
                           "/checks=" + std::to_string(checkCount) + " (trimmed " + std::to_string(trimmed) +
                           ", padded " + std::to_string(padded) + ")";
        }
        std::cout << "[ComboShip] CrossWorldCombinedFill: pool balance — " << balanceLine << " (forced "
                  << forcedPlacements.size() << ")\n";
        for (const auto& n : trimNotes)
            std::cout << "[ComboShip] CrossWorldCombinedFill:     trimmed " << n << "\n";
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

    // OOT-only, portal SHUT: nothing assumed from the free pool, MM never queried. Credits a placement
    // only once its check is reachable — reachableFixpoint's rule, so this can't certify what it rejects.
    struct OotClosedResult {
        std::unordered_set<std::string> reach;
        bool portalOpen = false;
    };
    auto ootClosedFixpoint = [&](const std::vector<CwPlacement>& pl,
                                 const std::vector<std::string>& extraOwned) -> OotClosedResult {
        std::vector<std::string> owned = ootForcedOwned;
        owned.insert(owned.end(), extraOwned.begin(), extraOwned.end());
        std::vector<bool> credited(pl.size(), false);
        OotClosedResult r;
        for (;;) {
            r.reach = queryReachable(ootOracle, owned);
            r.portalOpen = r.portalOpen || ootOracle.GetPortalOpen == nullptr || ootOracle.GetPortalOpen() != 0;
            bool changed = false;
            for (size_t i = 0; i < pl.size(); ++i) {
                if (credited[i] || pl[i].check.game != GAME_OOT || pl[i].item.game != GAME_OOT)
                    continue;
                if (r.reach.count(pl[i].check.name)) {
                    owned.push_back(pl[i].item.name);
                    credited[i] = true;
                    changed = true;
                }
            }
            if (!changed)
                return r;
        }
    };

    // Portal prerequisites: a SUFFICIENT witness set for the OOT->MM portal, built FORWARD from empty.
    // RNG-free (canonical order) so it can't shift the seeded stream. See docs/deviations/rando.md.
    std::vector<std::string> portalPrereqs;
    if (portalGated) {
        // Each probe is a fixpoint (several queries), so this bounds queries, not probes.
        const int kMaxDeriveQueries = 1500;
        const uint32_t queries0 = ootStats.count;
        int used = 0;
        auto portalOpenWith = [&](const std::vector<std::string>& owned) {
            bool open = ootClosedFixpoint(lockedPlacements, owned).portalOpen;
            used = static_cast<int>(ootStats.count - queries0);
            return open;
        };
        const std::vector<std::string> base; // free pool only; locked items earn their way in by reach
        std::vector<std::string> rest;
        for (const auto& it : advItems)
            if (it.game == GAME_OOT)
                rest.push_back(it.name);
        std::sort(rest.begin(), rest.end());

        std::vector<std::string> witness;
        auto ownedWith = [&](size_t k) {
            std::vector<std::string> o = base;
            o.insert(o.end(), witness.begin(), witness.end());
            o.insert(o.end(), rest.begin(), rest.begin() + k);
            return o;
        };
        if (portalOpenWith(base)) {
            std::cout << "[ComboShip] CrossWorldCombinedFill: portal open from the start — no prerequisites\n";
        } else if (!portalOpenWith(ownedWith(rest.size()))) {
            // Tier 3: warn loudly, try anyway — a derivation bug must not block a config that generates.
            std::cerr << "[ComboShip] CrossWorldCombinedFill: WARNING — the OOT->MM portal looks unreachable even "
                         "with every OOT item owned; generating anyway, MM may end up unreachable\n";
        } else {
            // Bisect `rest` for the item that flips the portal, add it, drop everything after it, repeat.
            while (used < kMaxDeriveQueries && !rest.empty()) {
                size_t lo = 0, hi = rest.size(); // portal closed at lo, open at hi
                while (hi - lo > 1 && used < kMaxDeriveQueries) {
                    size_t mid = lo + (hi - lo) / 2;
                    if (portalOpenWith(ownedWith(mid)))
                        hi = mid;
                    else
                        lo = mid;
                }
                witness.push_back(rest[hi - 1]);
                rest.resize(hi - 1);
                if (portalOpenWith(ownedWith(0)))
                    break;
            }
            if (used >= kMaxDeriveQueries) {
                std::cerr << "[ComboShip] CrossWorldCombinedFill: WARNING — portal prerequisite derivation exceeded "
                             "its "
                          << kMaxDeriveQueries << "-query budget; falling back to an unconstrained fill\n";
            } else {
                portalPrereqs = witness;
                std::cout << "[ComboShip] CrossWorldCombinedFill: portal prerequisites (" << portalPrereqs.size()
                          << ", " << used << " queries):";
                for (const auto& n : portalPrereqs)
                    std::cout << " " << n;
                std::cout << "\n";
            }
        }
    }

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
        bool portalOpen = !portalGated; // latched: once OOT can reach the portal it stays open
        for (;;) {
            ootReachable = queryReachable(ootOracle, ootOwned);
            // Read the portal off THIS OOT query, before any MM check is credited below — that ordering
            // is what stops the fill proving the portal with an item that lives behind it.
            if (!portalOpen)
                portalOpen = ootOracle.GetPortalOpen() != 0;
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
    std::unordered_set<std::string> filledChecks; // checkKey()-keyed
    bool fillOk = false;
    int passesUsed = 0;
    std::string lastPassError; // names the terminal cause instead of a generic "fill failed"

    // Expected post-balance multiset, for the per-pass backstop below: every pool item must reach
    // `placements`. This is the assertion that catches item loss from ANY cause, not just truncation.
    std::unordered_map<std::string, int> expectedItems;
    struct ExpectedInfo {
        CwCat cat;
        bool discardable;
    };
    std::unordered_map<std::string, ExpectedInfo> expectedInfo;
    for (const auto* src : { &advItems, &junkItems })
        for (const auto& i : *src) {
            const std::string key = std::string(i.game == GAME_OOT ? "oot:" : "mm:") + i.name;
            ++expectedItems[key];
            expectedInfo[key] = { i.cat, i.discardable() };
        }

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

        // Phase A0: portal prerequisites FIRST, into portal-closed-reachable checks, each chosen RANDOMLY
        // across the whole valid set (variety comes from the choice, not the ordering).
        std::vector<CwItem> advRest = advItems;
        bool prereqOk = portalPrereqs.empty();
        for (int t = 1; t <= kMaxPrereqTries && !prereqOk; ++t) {
            placements = lockedPlacements;
            filledChecks.clear();
            for (const auto& lp : lockedPlacements)
                filledChecks.insert(checkKey(lp.check));
            advRest = advItems;
            std::vector<CwItem> prereq; // one pool instance per witness name
            for (const auto& name : portalPrereqs) {
                for (size_t i = 0; i < advRest.size(); ++i) {
                    if (advRest[i].game == GAME_OOT && advRest[i].name == name) {
                        prereq.push_back(advRest[i]);
                        advRest.erase(advRest.begin() + i);
                        break;
                    }
                }
            }
            cwShuffle(prereq, rng);
            bool placedAll = true;
            for (const auto& item : prereq) {
                auto fr = ootClosedFixpoint(placements, {});
                std::vector<size_t> cands;
                for (size_t ci = 0; ci < allChecks.size(); ++ci) {
                    if (allChecks[ci].game != GAME_OOT || filledChecks.count(checkKey(allChecks[ci])))
                        continue;
                    if (fr.reach.count(allChecks[ci].name))
                        cands.push_back(ci);
                }
                if (cands.empty()) {
                    placedAll = false;
                    break;
                }
                size_t pick = cands[rng.below(static_cast<uint32_t>(cands.size()))];
                placements.push_back({ allChecks[pick], item });
                filledChecks.insert(checkKey(allChecks[pick]));
            }
            prereqOk = placedAll && ootClosedFixpoint(placements, {}).portalOpen;
            if (!prereqOk)
                std::cerr << "[ComboShip] CrossWorldCombinedFill: portal still closed after prerequisite placement "
                             "(pass "
                          << pass << ", try " << t << "/" << kMaxPrereqTries << ") — repicking\n";
        }
        if (!prereqOk) {
            lastPassError = "the OOT->MM portal's prerequisites could not be placed within reach of the start";
            std::cerr << "[ComboShip] CrossWorldCombinedFill: " << lastPassError << " (pass " << pass << ", "
                      << passMs() << " ms) — retrying\n";
            continue;
        }

        // Phase A: place advancement items with logic, assuming only UNPLACED ones are owned. NO_LOGIC
        // assumed-fills only MM adv (OOT adv rides Phase B's fast fill); other modes keep advItems as-is.
        std::vector<CwItem> toPlace;
        if (ootAccess == OotAccess::NO_LOGIC) {
            for (const auto& it : advRest)
                if (it.game == GAME_MM)
                    toPlace.push_back(it);
        } else {
            toPlace = advRest;
        }
        std::vector<CwItem> relaxedToJunk; // OOT adv stranded off-path under BEATABLE_ONLY dead-ends
        cwShuffle(toPlace, rng);
        std::vector<std::string> ootRemaining, mmRemaining;
        for (const auto& it : toPlace)
            (it.game == GAME_OOT ? ootRemaining : mmRemaining).push_back(it.name);

        // Batch placement: pull K items and place all K into distinct reachable checks off ONE
        // fixpoint — still a strictly-conservative assumed fill (each lands on a check reachable
        // without itself and the rest of its batch), but divides the fixpoint cost by K. The cap
        // halves on a failed batch (never grows back within a pass); only a failed K=1 ends the pass.
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
                lastPassError = "dead end placing '" + batch.front().name + "' (no reachable check left for it)";
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

        // Phase B: fast-fill the remaining checks — zero oracle calls. Holds junk AND relaxed OOT
        // advancement, placed without logic and tolerated-if-unreachable by the validation below.
        std::vector<CwItem> fastFillItems = junkItems;
        if (ootAccess == OotAccess::NO_LOGIC) {
            for (const auto& it : advRest)
                if (it.game == GAME_OOT)
                    fastFillItems.push_back(it);
        }
        fastFillItems.insert(fastFillItems.end(), relaxedToJunk.begin(), relaxedToJunk.end());
        cwShuffle(fastFillItems, rng);
        // Advancement to the front so the truncatable tail is junk by construction. stable_partition is
        // RNG-free, so the shuffle above still decides placement; only adv/junk precedence is forced.
        auto dropFrom = std::stable_partition(fastFillItems.begin(), fastFillItems.end(),
                                              [](const CwItem& i) { return i.advancement; });
        const size_t mustPlace = static_cast<size_t>(dropFrom - fastFillItems.begin());
        size_t ji = 0;
        for (size_t ci = 0; ci < allChecks.size() && ji < fastFillItems.size(); ++ci) {
            if (filledChecks.count(checkKey(allChecks[ci])))
                continue;
            placements.push_back({ allChecks[ci], fastFillItems[ji++] });
            filledChecks.insert(checkKey(allChecks[ci]));
        }
        // The balancer made items == checks per game, so ANY leftover or unfilled check is an
        // arithmetic bug — nothing is discarded here. Non-junk leftovers are named first: they are the
        // rule-1 violations, and mislabelling one as junk is how the original item loss stayed hidden.
        size_t unfilled = 0;
        for (const auto& c : allChecks)
            if (!filledChecks.count(checkKey(c)))
                ++unfilled;
        size_t leftoverBad = 0;
        std::string leftoverNames;
        for (size_t k = ji; k < fastFillItems.size(); ++k) {
            if (fastFillItems[k].discardable())
                continue;
            ++leftoverBad;
            if (leftoverNames.size() < 300)
                leftoverNames += (leftoverNames.empty() ? "" : ", ") + fastFillItems[k].name + " [" +
                                 (fastFillItems[k].game == GAME_OOT ? "oot/" : "mm/") +
                                 CwCatName(fastFillItems[k].cat) + "]";
        }
        if (ji < fastFillItems.size() || unfilled > 0) {
            // Layout-independent arithmetic, identical on every pass — bail to the caller's seed reroll
            // instead of burning kMaxPasses reproducing the same wrong answer.
            result.error = "pool/check imbalance — " + std::to_string(fastFillItems.size() - ji) +
                           " item(s) had no check left (" + std::to_string(leftoverBad) + " non-junk" +
                           (leftoverNames.empty() ? "" : ": " + leftoverNames) + "), " + std::to_string(unfilled) +
                           " check(s) unfilled, placed " + std::to_string(ji) + " of which " +
                           std::to_string(mustPlace) + " were must-place";
            std::cerr << "[ComboShip] CrossWorldCombinedFill: " << result.error << "\n";
            return result;
        }

        // Backstop: every pool item must have landed somewhere. O(n), no oracle calls, and ahead of the
        // validation fixpoint so a validation retry can't mask it. Non-JUNK residue is the headline.
        {
            auto residual = expectedItems;
            size_t unexpected = 0;
            for (const auto& p : placements) {
                if (lockedCheckKeys.count(checkKey(p.check)))
                    continue;
                auto it = residual.find(std::string(p.item.game == GAME_OOT ? "oot:" : "mm:") + p.item.name);
                if (it == residual.end())
                    ++unexpected; // placed an item that was never in the pool
                else
                    --it->second;
            }
            size_t missingBad = 0, missingJunk = 0, overPlaced = 0;
            std::string missingNames;
            for (const auto& [key, n] : residual) {
                if (n < 0) {
                    overPlaced += static_cast<size_t>(-n);
                    continue;
                }
                if (n == 0)
                    continue;
                // Classify by the same predicate the trim uses, so an advancement item that happens to
                // be category JUNK (OOT bombchus, Buy Blue Fire) is reported as a real loss, not filler.
                const auto info =
                    expectedInfo.count(key) ? expectedInfo.at(key) : ExpectedInfo{ CwCat::UNKNOWN, false };
                (info.discardable ? missingJunk : missingBad) += static_cast<size_t>(n);
                if (!info.discardable && missingNames.size() < 300)
                    missingNames += (missingNames.empty() ? "" : ", ") + key + " x" + std::to_string(n) + " [" +
                                    CwCatName(info.cat) + "]";
            }
            if (missingBad > 0 || missingJunk > 0 || overPlaced > 0 || unexpected > 0) {
                result.error = missingBad > 0
                                   ? std::to_string(missingBad) + " non-junk pool item(s) never placed: " + missingNames
                                   : std::to_string(missingJunk) + " junk never placed, " + std::to_string(overPlaced) +
                                         " over-placed, " + std::to_string(unexpected) +
                                         " not from the pool (balancer arithmetic bug)";
                std::cerr << "[ComboShip] CrossWorldCombinedFill: INVARIANT VIOLATED — " << result.error << "\n";
                return result;
            }
        }

        // Validation: with nothing assumed, sphere-collecting placed items must reach every
        // ADVANCEMENT check (the assumed-fill guarantee). Junk-holding checks may legitimately be
        // oracle-unreachable (oracles under-model, e.g. MM with zeroed save options) — count and log
        // those, don't fail.
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
        // RC_GANON reachable = OOT beatable: its region logic gates on bridge + boss key (placed OR
        // force-granted), correct for every GBK/bridge mode — unlike the old tower-chest proxy (held a
        // random item, could be unreachable). Names are the friendly forms the OOT/MM oracles return.
        const bool ootWin = ootFinal.count("Ganon") > 0;
        const bool mmWin = mmFinal.count("Moon Majora Pot 01") > 0;
        // Triforce Hunt (#136): the win is `required` pieces from either pool, so count the pieces
        // sitting on reachable checks instead of the two boss markers.
        int reachablePieces = 0;
        if (goal.hunt) {
            for (const auto& p : placements) {
                if (CwIsTriforcePiece(p.item.game, p.item.name) &&
                    (p.check.game == GAME_OOT ? ootFinal : mmFinal).count(p.check.name)) {
                    ++reachablePieces;
                }
            }
        }
        bool goalOk = (ootAccess != OotAccess::BEATABLE_ONLY) ||
                      (goal.hunt ? reachablePieces >= goal.required : (ootWin && mmWin));
        bool needRetry = mmAdvUnreachable > 0 || (ootAccess == OotAccess::ALL_REACHABLE && ootAdvUnreachable > 0) ||
                         (ootAccess == OotAccess::BEATABLE_ONLY && !goalOk);
        if (needRetry) {
            // On UNBEATABLE, name which side blocked (ganon/majora) so a future win-marker rename or
            // logic regression is self-diagnosing instead of a silent all-passes-fail.
            std::string goalStr = ootAccess != OotAccess::BEATABLE_ONLY ? ""
                                  : goalOk                              ? " goal=ok"
                                  : goal.hunt ? " goal=UNBEATABLE(pieces=" + std::to_string(reachablePieces) + "/" +
                                                    std::to_string(goal.required) + ")"
                                              : " goal=UNBEATABLE(ganon=" + std::to_string(ootWin) +
                                                    " majora=" + std::to_string(mmWin) + ")";
            lastPassError = "validation failed — mmAdvUnreachable=" + std::to_string(mmAdvUnreachable) +
                            " ootAdvUnreachable=" + std::to_string(ootAdvUnreachable) + goalStr +
                            (mmAdvUnreachable > 0 ? " (MM items stranded: the portal closed mid-fill)" : "");
            std::cerr << "[ComboShip] CrossWorldCombinedFill: validation failed — mmAdvUnreachable=" << mmAdvUnreachable
                      << " ootAdvUnreachable=" << ootAdvUnreachable << goalStr << " (pass " << pass << ", " << passMs()
                      << " ms) — retrying\n";
            continue;
        }
        // ALL_REACHABLE covers every check, not just advancement ones: a junk-holding check the oracle
        // can't reach still violates it. Entrance shuffle is what makes this reachable in practice, and
        // it's layout-driven (the same seed strands the same checks under a completely different item
        // distribution), so re-placing items can't fix it — bail to the caller's seed reroll rather
        // than burn the remaining passes. MM is excluded: its oracle under-models 3 checks on every
        // seed, so enforcing there would fail everything.
        if (ootAccess == OotAccess::ALL_REACHABLE && junkUnreachableOot > 0) {
            std::cerr << "[ComboShip] CrossWorldCombinedFill: " << junkUnreachableOot
                      << " OOT check(s) unreachable under All Locations Reachable (pass " << pass
                      << ") — rerolling seed\n";
            result.error = "OOT All Locations Reachable violated (" + std::to_string(junkUnreachableOot) +
                           " unreachable checks) — reroll for a new entrance layout";
            return result;
        }
        if (ootAdvUnreachable > 0)
            std::cout << "[ComboShip] CrossWorldCombinedFill: " << ootAdvUnreachable
                      << " OOT advancement item(s) on unreachable checks — tolerated (relaxed OOT mode)\n";

        std::cout << "[ComboShip] CrossWorldCombinedFill: all " << advItems.size()
                  << " advancement items reachable from scratch (pass " << pass << ", " << passMs() << " ms; "
                  << junkItems.size() << " junk, " << junkUnreachable
                  << " junk on oracle-unreachable checks [oot=" << junkUnreachableOot
                  << " mm=" << (junkUnreachable - junkUnreachableOot) << "]; pool balanced: " << trimmedTotal
                  << " trimmed, " << paddedTotal << " padded)\n";
        std::cout << "[ComboShip] CrossWorldCombinedFill: oracle queries — oot " << ootStats.count << "x/"
                  << ootStats.ms << " ms, mm " << mmStats.count << "x/" << mmStats.ms << " ms\n";
        fillOk = true;
    }

    if (!fillOk) {
        result.error = "assumed fill failed after " + std::to_string(kMaxPasses) +
                       " passes; last cause: " + (lastPassError.empty() ? "unknown (see log)" : lastPassError);
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
    // poolTrimmed/poolPadded let a shipped spoiler self-report that it came from a balanced fill.
    spoiler["fillStats"] = { { "advancementItems", static_cast<uint32_t>(advItems.size()) },
                             { "junkItems", static_cast<uint32_t>(junkItems.size()) },
                             { "poolTrimmed", static_cast<uint32_t>(trimmedTotal) },
                             { "poolPadded", static_cast<uint32_t>(paddedTotal) },
                             { "checks", static_cast<uint32_t>(allChecks.size()) },
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
            nlohmann::json marker = { { "checkGame", p.check.game == GAME_OOT ? "oot" : "mm" },
                                      { "checkName", p.check.name },
                                      { "itemGame", p.item.game == GAME_OOT ? "oot" : "mm" },
                                      { "itemName", p.item.name },
                                      // Propagate advancement so foreign checks holding an important item
                                      // still play the held-up pickup animation (else defaulted to junk).
                                      { "advancement", p.item.advancement } };
            // Native item category, so CMC/CSMC can dress the container as the real item instead of
            // the junk sentinel. UNKNOWN is omitted so consumers use the advancement fallback.
            if (p.item.cat != CwCat::UNKNOWN)
                marker["category"] = CwCatName(p.item.cat);
            foreignMarkers.push_back(std::move(marker));
        }
    }

    spoiler["ootCount"] = static_cast<uint32_t>(ootPlacements.size());
    spoiler["oot"] = ootPlacements;
    spoiler["mmCount"] = static_cast<uint32_t>(mmPlacements.size());
    spoiler["mm"] = mmPlacements;
    spoiler["foreign"] = foreignMarkers;
    // Forced placements (e.g. Link's Pocket) are owned at start, so hints must never target them —
    // the dump's fixed[] skips forced checks, so the spoiler names them for CrossHints instead.
    nlohmann::json startKnown = nlohmann::json::array();
    for (const auto& fp : forcedPlacements)
        startKnown.push_back(
            { { "checkGame", fp.check.game == GAME_OOT ? "oot" : "mm" }, { "checkName", fp.check.name } });
    spoiler["startKnown"] = std::move(startKnown);

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
            // Same silent-truncation shape as the real fill had; this path only runs with no oracles.
            if (shuffled.size() != checkNames.size())
                std::cerr << "[ComboShip] CrossWorldGenerateSpoiler: " << key << " pool/check mismatch — "
                          << shuffled.size() << " items vs " << checkNames.size() << " checks\n";
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
