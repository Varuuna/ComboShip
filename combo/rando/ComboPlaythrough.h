// combo/rando/ComboPlaythrough.h
// ComboShip: forward "playthrough" simulation over a finished cross-world seed. Starting from an empty
// collected set, it repeatedly asks each game's reachability oracle what's reachable, collects the items
// on newly-reachable checks, and loops until the win is reachable (BEATABLE) or nothing new opens (stuck).
// Shared by the in-game generator (ComboShip.cpp) and the headless validator (ComboRandoHeadless.cpp) so
// the two never diverge. The oracle's logic mode / tricks are controlled by the caller before running.
#pragma once

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CrossWorldRando.h"

namespace ComboRando {

// A single placed item, parsed from a combined-fill spoiler (see ParseSpoilerPlacements). Shared by
// RunPlaythrough and PareDownPlaythrough so both traverse the identical placement set.
struct CwPlacedItem {
    GameId checkGame;
    std::string check;
    GameId itemGame;
    std::string item;
    bool advancement;
    bool major; // native IsMajorItem: drives the barren predicate (barren = no WotH + no major)
};

// Parses a combined-fill spoilerJson ("oot"/"mm" flat check->item maps + "foreign" array, the shape
// CrossWorldCombinedFill/CrossHints::Generate produce) into placements. sohDumpJson/mmDumpJson
// (optional) resolve each item's advancement flag from the pool; absent -> defaults to advancement
// (never hide progression). Mirrors RunPlaythrough's own parse (factored out for reuse).
inline std::vector<CwPlacedItem> ParseSpoilerPlacements(const std::string& spoilerJson,
                                                        const std::string& sohDumpJson = "",
                                                        const std::string& mmDumpJson = "") {
    std::vector<CwPlacedItem> placements;
    struct ForeignInfo {
        GameId itemGame;
        std::string itemName;
        bool advancement;
    };
    std::unordered_map<std::string, ForeignInfo> foreign; // "<cg>:<cn>"-keyed

    std::unordered_map<std::string, bool> advByName[2];
    std::unordered_map<std::string, bool> majorByName[2]; // absent (e.g. MM dump) -> falls back to advancement
    auto loadAdv = [&](GameId g, const std::string& dumpJson) {
        if (dumpJson.empty())
            return;
        try {
            auto d = nlohmann::json::parse(dumpJson);
            for (auto& it : d.value("pool", nlohmann::json::array())) {
                bool adv = it.value("advancement", true);
                advByName[g][it.value("name", "")] |= adv;
                majorByName[g][it.value("name", "")] |= it.value("major", adv);
            }
            for (auto& f : d.value("fixed", nlohmann::json::array())) {
                bool adv = f.value("advancement", true);
                advByName[g][f.value("item", "")] |= adv;
                majorByName[g][f.value("item", "")] |= f.value("major", adv);
            }
        } catch (...) {}
    };
    loadAdv(GAME_OOT, sohDumpJson);
    loadAdv(GAME_MM, mmDumpJson);
    auto lookupAdv = [&](GameId g, const std::string& item) {
        auto it = advByName[g].find(item);
        return it == advByName[g].end() ? true : it->second;
    };
    auto lookupMajor = [&](GameId g, const std::string& item) {
        auto it = majorByName[g].find(item);
        return it == majorByName[g].end() ? lookupAdv(g, item) : it->second;
    };
    // ComboShip: the consolidated file suffixes cross-game item-name collisions "(OOT)"/"(MM)"; strip
    // that here so oracle name resolution (bare friendly) works. No-op on the bare fill output.
    auto stripSuffix = [](std::string s) {
        for (const char* suf : { " (OOT)", " (MM)" }) {
            size_t n = std::string(suf).size();
            if (s.size() >= n && s.compare(s.size() - n, n, suf) == 0)
                return s.substr(0, s.size() - n);
        }
        return s;
    };
    try {
        auto j = nlohmann::json::parse(spoilerJson);
        for (auto& fm : j.value("foreign", nlohmann::json::array())) {
            std::string cg = fm.value("checkGame", ""), cn = fm.value("checkName", "");
            std::string ig = fm.value("itemGame", "");
            foreign[cg + ":" + cn] = { (ig == "mm") ? GAME_MM : GAME_OOT, stripSuffix(fm.value("itemName", "")),
                                       fm.value("advancement", true) };
        }
        auto addGame = [&](const char* key, GameId cg) {
            if (!j.contains(key) || !j[key].is_object())
                return;
            for (auto& [cn, iv] : j[key].items()) {
                auto fit = foreign.find(std::string(key) + ":" + cn);
                bool isForeign = fit != foreign.end();
                GameId ig = isForeign ? fit->second.itemGame : cg;
                std::string item = (isForeign && !fit->second.itemName.empty()) ? fit->second.itemName
                                                                                : stripSuffix(iv.get<std::string>());
                bool adv = isForeign ? fit->second.advancement : lookupAdv(ig, item);
                bool major = isForeign ? fit->second.advancement : lookupMajor(ig, item);
                placements.push_back({ cg, cn, ig, item, adv, major });
            }
        };
        addGame("oot", GAME_OOT);
        addGame("mm", GAME_MM);
    } catch (const std::exception& e) { std::cerr << "[PLAYTHROUGH] spoiler parse error: " << e.what() << "\n"; }
    return placements;
}

inline std::unordered_set<std::string> QueryReachable(const OracleFns& o, const std::vector<std::string>& owned) {
    nlohmann::json arr = nlohmann::json::array();
    for (auto& n : owned)
        arr.push_back(n);
    o.Reset();
    o.SetOwnedItems(arr.dump().c_str());
    std::unordered_set<std::string> out;
    try {
        for (auto& n : nlohmann::json::parse(o.GetReachableChecks()))
            out.insert(n.get<std::string>());
    } catch (...) {}
    return out;
}

// OOT->MM portal openness for the owned-set of the LAST QueryReachable on this oracle (see OracleFns).
// No export (MM oracle, or an old soh.dll) => ungated, matching the pre-gate behavior.
inline bool OraclePortalOpen(const OracleFns& o) {
    return o.GetPortalOpen == nullptr || o.GetPortalOpen() != 0;
}

using ReachSet = std::shared_ptr<const std::unordered_set<std::string>>;

// A memoized query result: the reachable set plus the portal bit that belonged to the same search.
struct ReachResult {
    ReachSet reach;
    bool portalOpen = true;
};

// Memo key for an owned-item MULTISET: reachability depends on which items and HOW MANY (1 vs 2
// Hookshots differ), not grant order. Sort only — dedupe would collapse progressive counts (stale hit).
inline std::string CanonicalOwnedKey(std::vector<std::string> owned) {
    std::sort(owned.begin(), owned.end());
    std::string key;
    for (auto& n : owned) {
        key += n;
        key += '\x1f';
    }
    return key;
}

// Memoized QueryReachable keyed on CanonicalOwnedKey: the same owned-set prefixes recur across the
// hundreds of counterfactual replays (each starts from empty), so caching collapses the repeats.
inline ReachResult QueryReachableMemo(const OracleFns& o, const std::vector<std::string>& owned,
                                      std::unordered_map<std::string, ReachResult>& memo) {
    std::string key = CanonicalOwnedKey(owned);
    auto it = memo.find(key);
    if (it != memo.end())
        return it->second;
    // Cache the portal bit with the set: a memo hit runs no search, so reading it later would be stale.
    ReachResult r{ std::make_shared<const std::unordered_set<std::string>>(QueryReachable(o, owned)), false };
    r.portalOpen = OraclePortalOpen(o);
    memo.emplace(std::move(key), r);
    return r;
}

// Default win condition: RC_GANON reachable (OOT) AND MM's in-lair check (Majora).
// A pluggable goal so another goal (e.g. Triforce hunt) can be swapped in without touching the
// traversal machinery below. The owned vectors let a goal test collected items, not just reachability.
using GoalPredicate =
    std::function<bool(const std::unordered_set<std::string>& ootReach, const std::unordered_set<std::string>& mmReach,
                       const std::vector<std::string>& ownedOot, const std::vector<std::string>& ownedMm)>;
inline bool DefaultGanonMajoraGoal(const std::unordered_set<std::string>& ootReach,
                                   const std::unordered_set<std::string>& mmReach,
                                   const std::vector<std::string>& /*ownedOot*/,
                                   const std::vector<std::string>& /*ownedMm*/) {
    // RC_GANON reachable = OOT beatable (bridge + boss key, placed or force-granted); see CrossWorldRando.h.
    static const char* kOotGanon = "Ganon";
    static const char* kMmWin = "Moon Majora Pot 01"; // ComboShip: friendly form of RC_MOON_MAJORA_POT_01
    return ootReach.count(kOotGanon) > 0 && mmReach.count(kMmWin) > 0;
}

// NO_LOGIC goal: OOT may be structurally unbeatable from empty (the combined goal would degenerate and
// every OOT item would look "required"), so gate requiredness on MM (Majora) only. Keeps MM-side WotH
// meaningful; OOT-side requiredness is not evaluated under No Logic (matching its semantics).
inline bool MmOnlyMajoraGoal(const std::unordered_set<std::string>& /*ootReach*/,
                             const std::unordered_set<std::string>& mmReach,
                             const std::vector<std::string>& /*ownedOot*/,
                             const std::vector<std::string>& /*ownedMm*/) {
    static const char* kMmWin = "Moon Majora Pot 01"; // friendly name the MM oracle returns (not raw RC_)
    return mmReach.count(kMmWin) > 0;
}

// Counts Triforce Pieces held across BOTH games (#136). Owned-based, not reachability-based, so the
// pare-down counterfactuals (which blank one placement) actually change the answer.
inline int CountOwnedTriforcePieces(const std::vector<std::string>& ownedOot, const std::vector<std::string>& ownedMm) {
    int n = 0;
    for (const auto& i : ownedOot)
        n += (i == kOotTriforcePiece) ? 1 : 0;
    for (const auto& i : ownedMm)
        n += (i == kMmTriforcePiece) ? 1 : 0;
    return n;
}

// Triforce Hunt goal: `required` pieces owned, from either game. Bosses are irrelevant under it.
inline GoalPredicate MakeTriforceHuntGoal(int required) {
    return [required](const std::unordered_set<std::string>&, const std::unordered_set<std::string>&,
                      const std::vector<std::string>& ownedOot, const std::vector<std::string>& ownedMm) {
        return CountOwnedTriforcePieces(ownedOot, ownedMm) >= required;
    };
}

struct RequirednessResult {
    // "oot:<check>"/"mm:<check>" -> required (WotH, true) or foolish-candidate (false).
    std::unordered_map<std::string, bool> requiredByCheck;
    // "oot:<area>"/"mm:<area>" -> true once ANY advancement item placed there is required.
    std::unordered_map<std::string, bool> areaHasRequired;
    int candidateCount = 0;
    int replayedCount = 0; // per-item counterfactual fixpoint replays actually run (cache misses)
    int64_t ms = 0;
};

// Requiredness pare-down over the COMBINED world: for each placed advancement item, tentatively
// remove it (check stays, item not credited) and re-run the sphere-collect from empty; still-reachable
// goal => NOT required (foolish), else required (WotH). Tested individually (no monotonicity assumed),
// restricted to the winning playthrough's checks. mmRestore resets the MM oracle snapshot afterward.
// ootCheckAreas/mmCheckAreas (checkName -> area) roll results up into per-area classification.
// progress (optional) is write-only reporting for the UI; it never influences the result.
inline RequirednessResult PareDownPlaythrough(const std::string& spoilerJson, const OracleFns& ootOracle,
                                              const OracleFns& mmOracle, void (*mmRestore)(),
                                              const std::string& sohDumpJson = "", const std::string& mmDumpJson = "",
                                              const std::unordered_map<std::string, std::string>& ootCheckAreas = {},
                                              const std::unordered_map<std::string, std::string>& mmCheckAreas = {},
                                              GoalPredicate goalReached = DefaultGanonMajoraGoal,
                                              bool portalGated = true, bool mmStart = false,
                                              ComboGenProgress* progress = nullptr) {
    RequirednessResult result;
    auto placements = ParseSpoilerPlacements(spoilerJson, sohDumpJson, mmDumpJson);

    auto checkKey = [](const CwPlacedItem& p) {
        return std::string(p.checkGame == GAME_OOT ? "oot:" : "mm:") + p.check;
    };

    // Per-invocation reachability memo (one per oracle): the same owned-set prefixes recur across
    // every counterfactual replay (sphere 0 is identical in all), so caching collapses the repeats.
    std::unordered_map<std::string, ReachResult> ootMemo, mmMemo;
    static const auto kEmptyReach = std::make_shared<const std::unordered_set<std::string>>();

    // Excludes a SET of placements from ever crediting their items, then sphere-collects everything
    // else from empty until stable. creditedOut (optional) reports what was credited when the run
    // ended (at a win: exactly what was collected strictly before the goal first held).
    auto winsWithout = [&](const std::unordered_set<size_t>& excludeIdx, std::vector<char>* creditedOut) {
        std::vector<std::string> ootOwned, mmOwned;
        std::vector<char> credited(placements.size(), 0);
        ReachSet ootReach, mmReach;
        bool won = false;
        // Latched: MM stays open once OOT can reach the Happy Mask Shop. Ungated (NO_LOGIC) = open,
        // and an MM start (#135) roots MM from the beginning.
        bool portalOpen = !portalGated || mmStart;
        for (;;) {
            auto ootQ = QueryReachableMemo(ootOracle, ootOwned, ootMemo);
            ootReach = ootQ.reach;
            portalOpen = portalOpen || ootQ.portalOpen;
            mmReach = portalOpen ? QueryReachableMemo(mmOracle, mmOwned, mmMemo).reach : kEmptyReach;
            // Test the goal per sphere and stop at the first win: we only break when the goal IS met
            // and never un-credit an item, so an early win is final regardless of oracle monotonicity.
            if (goalReached(*ootReach, *mmReach, ootOwned, mmOwned)) {
                won = true;
                break;
            }
            bool changed = false;
            for (size_t i = 0; i < placements.size(); ++i) {
                if (credited[i] || excludeIdx.count(i))
                    continue;
                const auto& p = placements[i];
                const auto& reach = (p.checkGame == GAME_OOT) ? *ootReach : *mmReach;
                if (reach.count(p.check)) {
                    (p.itemGame == GAME_OOT ? ootOwned : mmOwned).push_back(p.item);
                    credited[i] = true;
                    changed = true;
                }
            }
            if (!changed)
                break;
        }
        if (!won)
            won = goalReached(*ootReach, *mmReach, ootOwned, mmOwned);
        if (creditedOut)
            *creditedOut = std::move(credited);
        return won;
    };

    auto t0 = std::chrono::steady_clock::now();

    std::vector<size_t> candAll; // every advancement placement (drives the per-area rollup below)
    for (size_t i = 0; i < placements.size(); ++i)
        if (placements[i].advancement)
            candAll.push_back(i);

    std::vector<signed char> classified(placements.size(), -1); // -1 unknown, 0 not-required, 1 required

    // Baseline win (nothing excluded) yields the winning playthrough's collected set. We pare only
    // those checks: blanking a check the winning path never collected cannot break that path, so an
    // off-playthrough advancement item is not required regardless of monotonicity — no test needed.
    std::vector<char> baseCredited;
    ++result.replayedCount;
    bool baseWon = winsWithout({}, &baseCredited);

    // Candidates = advancement checks on the winning path. A stuck baseline can never win, so blanking
    // any one item still can't win: every advancement item is required, tested none.
    std::vector<size_t> cand;
    if (baseWon) {
        for (size_t i : candAll)
            if (baseCredited[i])
                cand.push_back(i);
            else
                classified[i] = 0;
    } else {
        for (size_t i : candAll)
            classified[i] = 1;
    }
    result.candidateCount = static_cast<int>(cand.size());
    // The pare-down owns the "Finalizing" bar: restart placed/total over the candidate replays.
    if (progress) {
        progress->total.store(result.candidateCount);
        progress->placed.store(0);
    }

    // Per-item WotH (native IsBeatableWithout): blank exactly one candidate check and replay from
    // empty. Still wins -> not required; loses -> required. Definitionally correct, no monotonicity
    // assumption (unlike the old group-test binary split, which our cross-game oracle broke).
    for (size_t i : cand) {
        ++result.replayedCount;
        classified[i] = winsWithout({ i }, nullptr) ? 0 : 1;
        if (progress)
            progress->placed.fetch_add(1);
    }

    for (size_t pi : candAll) {
        const auto& p = placements[pi];
        bool required = classified[pi] == 1;
        result.requiredByCheck[checkKey(p)] = required;
        const auto& areaMap = (p.checkGame == GAME_OOT) ? ootCheckAreas : mmCheckAreas;
        auto ait = areaMap.find(p.check);
        if (ait != areaMap.end()) {
            std::string areaKey = (p.checkGame == GAME_OOT ? "oot:" : "mm:") + ait->second;
            if (required)
                result.areaHasRequired[areaKey] = true;
            else
                result.areaHasRequired.emplace(areaKey, false);
        }
    }
    result.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    if (mmRestore)
        mmRestore();

    std::cout << "[PLAYTHROUGH] pare-down: " << result.candidateCount << " advancement candidates, "
              << result.replayedCount << " oracle replays, " << result.ms << " ms\n";
    return result;
}

struct PlaythroughResult {
    bool beatable = false;
    int beatableSphere = -1;
    size_t reachableOot = 0, unreachableOot = 0;
    size_t reachableMm = 0, unreachableMm = 0;
    // Win-side reachability at FULL placed inventory — names which side blocks a stuck seed
    // (Ganon = OOT tower-top + Boss Key; Majora = MM lair). Under a Triforce Hunt both mirror the
    // combined piece count, so every shared verdict path works without a goal branch.
    bool ganonReachable = false, majoraReachable = false;
    bool hunt = false;
    int piecesReachable = 0, piecesRequired = 0;
};

// Endgame proxies the oracles actually emit (see ComboShip.cpp for the rationale — the literal "Ganon"
// check needs CanUse(RG_MASTER_SWORD), an equip flag the headless engine doesn't model, so we use
// tower-top + Boss Key owned; MM uses the in-lair check which already encodes the remains/masks gate).
// sohDumpJson/mmDumpJson (optional): static-data dumps whose pool[]/fixed[] advancement flags let the
// text log show only progression items; unknown names default to advancement so nothing is hidden.
// progressionOnly also drops junk from playthroughOut — for the spoiler. Leave it false for validation:
// the affordability check needs every step, including junk sitting in a priced shop slot.
inline PlaythroughResult RunPlaythrough(const std::string& spoilerJson, const OracleFns& ootOracle,
                                        const OracleFns& mmOracle, const std::string& seedLabel, void (*mmRestore)(),
                                        nlohmann::json* playthroughOut = nullptr, const std::string& sohDumpJson = "",
                                        const std::string& mmDumpJson = "", bool portalGated = true,
                                        bool progressionOnly = false, CwGoal goal = {}, bool mmStart = false) {
    static const char* kOotGanon = "Ganon";           // RC_GANON reachable = OOT beatable (see CrossWorldRando.h)
    static const char* kMmWin = "Moon Majora Pot 01"; // ComboShip: friendly form of RC_MOON_MAJORA_POT_01

    PlaythroughResult result;
    result.hunt = goal.hunt;
    result.piecesRequired = goal.required;

    using Placed = CwPlacedItem;
    std::vector<Placed> placements = ParseSpoilerPlacements(spoilerJson, sohDumpJson, mmDumpJson);

    auto queryReachable = QueryReachable;

    std::vector<std::string> ownedOot, ownedMm;
    std::unordered_set<std::string> collected; // "<cg>:<cn>"
    std::ostringstream log;
    log << "Cross-world playthrough - seed '" << seedLabel << "'\n";
    if (goal.hunt) {
        log << "Beatable when: " << goal.required << " Triforce Pieces collected across both games.\n\n";
    } else {
        log << "Beatable when: Ganon reachable (OOT: RC_GANON, i.e. bridge + boss key)"
            << " AND Majora's Lair reachable (MM).\n\n";
    }

    int beatableSphere = -1;
    const int kMaxSpheres = 200;
    // Latched: MM stays open once OOT can reach the Happy Mask Shop. Ungated (NO_LOGIC) = open, and an
    // MM start (#135) roots MM from the beginning.
    bool portalOpen = !portalGated || mmStart;
    for (int sphere = 0; sphere < kMaxSpheres; ++sphere) {
        auto ootReach = queryReachable(ootOracle, ownedOot);
        // Portal bit belongs to the OOT query just made; read it before crediting any MM check.
        portalOpen = portalOpen || OraclePortalOpen(ootOracle);
        auto mmReach = portalOpen ? queryReachable(mmOracle, ownedMm) : std::unordered_set<std::string>{};
        bool canGanon = ootReach.count(kOotGanon) > 0;
        bool canMajora = mmReach.count(kMmWin) > 0;
        if (goal.hunt ? CountOwnedTriforcePieces(ownedOot, ownedMm) >= goal.required : (canGanon && canMajora)) {
            beatableSphere = sphere;
            break;
        }

        std::vector<Placed> newly;
        for (auto& p : placements) {
            std::string key = (p.checkGame == GAME_OOT ? "oot:" : "mm:") + p.check;
            if (collected.count(key))
                continue;
            const auto& reach = (p.checkGame == GAME_OOT) ? ootReach : mmReach;
            if (reach.count(p.check))
                newly.push_back(p);
        }
        if (newly.empty()) {
            log << "Sphere " << sphere << ": (stuck — nothing new reachable, not yet beatable)\n";
            break;
        }
        size_t newlyAdv = 0;
        for (auto& p : newly)
            newlyAdv += p.advancement ? 1 : 0;
        log << "Sphere " << sphere << "  (";
        if (goal.hunt) {
            log << "pieces=" << CountOwnedTriforcePieces(ownedOot, ownedMm) << "/" << goal.required;
        } else {
            log << "Ganon=" << (canGanon ? "Y" : "n") << " Majora=" << (canMajora ? "Y" : "n");
        }
        log << ", +" << newly.size() << " items, " << newlyAdv << " progression)\n";
        nlohmann::json sphereSteps = nlohmann::json::array();
        for (auto& p : newly) {
            std::string key = (p.checkGame == GAME_OOT ? "oot:" : "mm:") + p.check;
            collected.insert(key);
            (p.itemGame == GAME_OOT ? ownedOot : ownedMm).push_back(p.item);
            if (playthroughOut && (p.advancement || !progressionOnly)) {
                nlohmann::json step = { { "game", p.checkGame == GAME_OOT ? "oot" : "mm" },
                                        { "check", p.check },
                                        { "item", p.item },
                                        { "foreign", p.checkGame != p.itemGame } };
                if (!progressionOnly)
                    step["advancement"] = p.advancement; // implied in the spoiler; the validator needs it
                sphereSteps.push_back(std::move(step));
            }
            // Junk is still collected (it drives the traversal) but not printed.
            if (!p.advancement)
                continue;
            log << "    [" << (p.checkGame == GAME_OOT ? "OOT" : "MM ") << "] " << p.check << "  <-  " << p.item
                << (p.checkGame != p.itemGame ? (p.itemGame == GAME_OOT ? "  (OOT item)" : "  (MM item)") : "") << "\n";
        }
        if (playthroughOut)
            playthroughOut->push_back({ { "sphere", sphere }, { "steps", std::move(sphereSteps) } });
    }

    // True "ever reachable" sets — full placed-item inventory yields the maximal (monotonic) reachable
    // set. Differs from `collected`, which stops at the beatable sphere. Runs before the MM restore.
    std::vector<std::string> allOot, allMm;
    for (auto& p : placements)
        (p.itemGame == GAME_OOT ? allOot : allMm).push_back(p.item);
    auto everReachOot = queryReachable(ootOracle, allOot);
    bool everPortalOpen = OraclePortalOpen(ootOracle) || mmStart;
    auto everReachMm = everPortalOpen ? queryReachable(mmOracle, allMm) : std::unordered_set<std::string>{};
    result.ganonReachable = everReachOot.count(kOotGanon) > 0;
    result.majoraReachable = everReachMm.count(kMmWin) > 0;
    if (goal.hunt) {
        for (auto& p : placements) {
            if (CwIsTriforcePiece(p.itemGame, p.item) &&
                (p.checkGame == GAME_OOT ? everReachOot : everReachMm).count(p.check)) {
                ++result.piecesReachable;
            }
        }
        result.ganonReachable = result.majoraReachable = result.piecesReachable >= goal.required;
    }

    if (mmRestore)
        mmRestore();

    if (beatableSphere >= 0 && goal.hunt) {
        log << "\nBEATABLE at sphere " << beatableSphere << ": " << goal.required
            << " Triforce Pieces collected. Seed is completable.\n";
    } else if (beatableSphere >= 0) {
        log << "\nBEATABLE at sphere " << beatableSphere << ": Ganon AND Majora both reachable. Seed is completable.\n";
    } else {
        log << "\nNOT proven beatable within " << kMaxSpheres << " spheres (see stuck note above).\n";
    }

    auto emitGame = [&](GameId cg, const char* tag, const std::unordered_set<std::string>& everReach,
                        size_t& reachedOut, size_t& missingOut) {
        size_t reached = 0, missing = 0;
        log << "\n--- " << tag << " placements ---\n";
        for (auto& p : placements) {
            if (p.checkGame != cg)
                continue;
            bool got = everReach.count(p.check) > 0;
            got ? ++reached : ++missing;
            // Reached junk is noise; unreachable stays visible regardless (it's the diagnostic).
            if (got && !p.advancement)
                continue;
            log << "    " << (got ? "  " : "! ") << p.check << "  <-  " << p.item
                << (p.checkGame != p.itemGame ? (p.itemGame == GAME_OOT ? "  (OOT item)" : "  (MM item)") : "")
                << (got ? "" : "   [UNREACHABLE]") << "\n";
        }
        log << "  " << tag << ": " << reached << " reachable, " << missing << " unreachable\n";
        reachedOut = reached;
        missingOut = missing;
    };
    log << "\n==== Placement (progression + unreachable; counts cover all checks) ====\n";
    emitGame(GAME_OOT, "OOT", everReachOot, result.reachableOot, result.unreachableOot);
    emitGame(GAME_MM, "MM", everReachMm, result.reachableMm, result.unreachableMm);

    // In-game generation folds the playthrough into the consolidated spoiler (playthroughOut), so the
    // standalone text log is redundant there. Callers passing no playthroughOut get the .txt.
    if (!playthroughOut) {
        std::error_code ec;
        std::filesystem::create_directories("saves/combo", ec);
        std::ofstream f("saves/combo/slot0.playthrough.txt", std::ios::trunc);
        f << log.str();
        std::cout << "[PLAYTHROUGH] full sphere log -> saves/combo/slot0.playthrough.txt\n";
    }

    std::cout << "[PLAYTHROUGH] seed '" << seedLabel << "' - " << (beatableSphere >= 0 ? "BEATABLE" : "NOT beatable")
              << (beatableSphere >= 0 ? (" at sphere " + std::to_string(beatableSphere)) : "") << "\n";
    std::cout << "[PLAYTHROUGH] collected " << collected.size() << " items across "
              << (beatableSphere >= 0 ? beatableSphere : kMaxSpheres) << " spheres before the win\n";

    result.beatable = beatableSphere >= 0;
    result.beatableSphere = beatableSphere;
    return result;
}

// Flatten RunPlaythrough's structured spheres into player-facing lines, "[OOT] Check --> Item". The
// validator keeps consuming the structured form; only the spoiler shows these.
inline nlohmann::json PlaythroughLines(const nlohmann::json& spheres) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& sphere : spheres) {
        nlohmann::json steps = nlohmann::json::array();
        for (const auto& step : sphere.value("steps", nlohmann::json::array())) {
            // Which game owns the item isn't actionable for the player, so cross-game isn't marked.
            steps.push_back("[" + std::string(step.value("game", "") == "mm" ? "MM" : "OOT") + "] " +
                            step.value("check", "") + " --> " + step.value("item", ""));
        }
        out.push_back({ { "sphere", sphere.value("sphere", -1) }, { "steps", std::move(steps) } });
    }
    return out;
}

} // namespace ComboRando
