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
    auto loadAdv = [&](GameId g, const std::string& dumpJson) {
        if (dumpJson.empty())
            return;
        try {
            auto d = nlohmann::json::parse(dumpJson);
            for (auto& it : d.value("pool", nlohmann::json::array()))
                advByName[g][it.value("name", "")] |= it.value("advancement", true);
            for (auto& f : d.value("fixed", nlohmann::json::array()))
                advByName[g][f.value("item", "")] |= f.value("advancement", true);
        } catch (...) {}
    };
    loadAdv(GAME_OOT, sohDumpJson);
    loadAdv(GAME_MM, mmDumpJson);
    auto lookupAdv = [&](GameId g, const std::string& item) {
        auto it = advByName[g].find(item);
        return it == advByName[g].end() ? true : it->second;
    };
    try {
        auto j = nlohmann::json::parse(spoilerJson);
        for (auto& fm : j.value("foreign", nlohmann::json::array())) {
            std::string cg = fm.value("checkGame", ""), cn = fm.value("checkName", "");
            std::string ig = fm.value("itemGame", "");
            foreign[cg + ":" + cn] = { (ig == "mm") ? GAME_MM : GAME_OOT, fm.value("itemName", ""),
                                       fm.value("advancement", true) };
        }
        auto addGame = [&](const char* key, GameId cg) {
            if (!j.contains(key) || !j[key].is_object())
                return;
            for (auto& [cn, iv] : j[key].items()) {
                auto fit = foreign.find(std::string(key) + ":" + cn);
                bool isForeign = fit != foreign.end();
                GameId ig = isForeign ? fit->second.itemGame : cg;
                std::string item =
                    (isForeign && !fit->second.itemName.empty()) ? fit->second.itemName : iv.get<std::string>();
                bool adv = isForeign ? fit->second.advancement : lookupAdv(ig, item);
                placements.push_back({ cg, cn, ig, item, adv });
            }
        };
        addGame("oot", GAME_OOT);
        addGame("mm", GAME_MM);
    } catch (const std::exception& e) {
        std::cerr << "[PLAYTHROUGH] spoiler parse error: " << e.what() << "\n";
    }
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

// Default win condition: OOT tower-top + Boss Key owned (Ganon) AND MM's in-lair check (Majora).
// A pluggable goal so a future goal (e.g. Triforce hunt) can be swapped in without touching the
// traversal machinery below.
using GoalPredicate = std::function<bool(const std::unordered_set<std::string>& ootReach,
                                        const std::unordered_set<std::string>& mmReach,
                                        const std::vector<std::string>& ownedOot)>;
inline bool DefaultGanonMajoraGoal(const std::unordered_set<std::string>& ootReach,
                                   const std::unordered_set<std::string>& mmReach,
                                   const std::vector<std::string>& ownedOot) {
    static const char* kOotTowerTop = "Ganon's Castle Tower Boss Key Chest";
    static const char* kOotBossKey = "Ganon's Castle Boss Key";
    static const char* kMmWin = "RC_MOON_MAJORA_POT_01";
    bool canGanon = ootReach.count(kOotTowerTop) > 0 &&
                    std::find(ownedOot.begin(), ownedOot.end(), kOotBossKey) != ownedOot.end();
    bool canMajora = mmReach.count(kMmWin) > 0;
    return canGanon && canMajora;
}

struct RequirednessResult {
    // "oot:<check>"/"mm:<check>" -> required (WotH, true) or foolish-candidate (false).
    std::unordered_map<std::string, bool> requiredByCheck;
    // "oot:<area>"/"mm:<area>" -> true once ANY advancement item placed there is required.
    std::unordered_map<std::string, bool> areaHasRequired;
    int candidateCount = 0;
    int64_t ms = 0;
};

// Requiredness pare-down over the COMBINED world: for each placed advancement item, tentatively
// remove it (its check still exists, but never credits the item to the owned set) and re-run the
// sphere-collect fixpoint from empty; if the goal is still reachable without it, the item is NOT
// required (foolish candidate); otherwise it IS required (WotH). ootCheckAreas/mmCheckAreas (checkName
// -> area/region string) let the caller roll this up into per-area foolish/WotH classification.
// mmRestore resets the MM oracle's snapshot guard afterward (same contract as RunPlaythrough).
inline RequirednessResult PareDownPlaythrough(const std::string& spoilerJson, const OracleFns& ootOracle,
                                              const OracleFns& mmOracle, void (*mmRestore)(),
                                              const std::string& sohDumpJson = "", const std::string& mmDumpJson = "",
                                              const std::unordered_map<std::string, std::string>& ootCheckAreas = {},
                                              const std::unordered_map<std::string, std::string>& mmCheckAreas = {},
                                              GoalPredicate goalReached = DefaultGanonMajoraGoal) {
    RequirednessResult result;
    auto placements = ParseSpoilerPlacements(spoilerJson, sohDumpJson, mmDumpJson);

    auto checkKey = [](const CwPlacedItem& p) { return std::string(p.checkGame == GAME_OOT ? "oot:" : "mm:") + p.check; };

    // Excludes ONE placement's check from ever crediting its item, then sphere-collects everything
    // else from empty until stable. Cheaper than a full fixpoint per test would suggest: most
    // candidates settle in a handful of iterations since only one item is missing from the world.
    auto reachableWithoutOne = [&](const std::string& excludeKey) {
        std::vector<std::string> ootOwned, mmOwned;
        std::vector<bool> credited(placements.size(), false);
        std::unordered_set<std::string> ootReach, mmReach;
        for (;;) {
            ootReach = QueryReachable(ootOracle, ootOwned);
            mmReach = QueryReachable(mmOracle, mmOwned);
            bool changed = false;
            for (size_t i = 0; i < placements.size(); ++i) {
                if (credited[i])
                    continue;
                const auto& p = placements[i];
                std::string key = checkKey(p);
                if (key == excludeKey) {
                    credited[i] = true; // never collect the excluded check's item
                    continue;
                }
                const auto& reach = (p.checkGame == GAME_OOT) ? ootReach : mmReach;
                if (reach.count(p.check)) {
                    (p.itemGame == GAME_OOT ? ootOwned : mmOwned).push_back(p.item);
                    credited[i] = true;
                    changed = true;
                }
            }
            if (!changed)
                break;
        }
        return goalReached(ootReach, mmReach, ootOwned);
    };

    auto t0 = std::chrono::steady_clock::now();
    for (const auto& p : placements) {
        if (!p.advancement)
            continue; // junk is never required/foolish-relevant
        ++result.candidateCount;
        std::string key = checkKey(p);
        bool winnableWithout = reachableWithoutOne(key);
        bool required = !winnableWithout;
        result.requiredByCheck[key] = required;
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
    result.ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    if (mmRestore)
        mmRestore();

    std::cout << "[PLAYTHROUGH] pare-down: " << result.candidateCount << " advancement candidates, " << result.ms
              << " ms\n";
    return result;
}

struct PlaythroughResult {
    bool beatable = false;
    int beatableSphere = -1;
    size_t reachableOot = 0, unreachableOot = 0;
    size_t reachableMm = 0, unreachableMm = 0;
    // Win-side reachability at FULL placed inventory — names which side blocks a stuck seed
    // (Ganon = OOT tower-top + Boss Key; Majora = MM lair).
    bool ganonReachable = false, majoraReachable = false;
};

// Endgame proxies the oracles actually emit (see ComboShip.cpp for the rationale — the literal "Ganon"
// check needs CanUse(RG_MASTER_SWORD), an equip flag the headless engine doesn't model, so we use
// tower-top + Boss Key owned; MM uses the in-lair check which already encodes the remains/masks gate).
// sohDumpJson/mmDumpJson (optional): static-data dumps whose pool[]/fixed[] advancement flags let the
// text log show only progression items; unknown names default to advancement so nothing is hidden.
inline PlaythroughResult RunPlaythrough(const std::string& spoilerJson, const OracleFns& ootOracle,
                                        const OracleFns& mmOracle, const std::string& seedLabel, void (*mmRestore)(),
                                        nlohmann::json* playthroughOut = nullptr, const std::string& sohDumpJson = "",
                                        const std::string& mmDumpJson = "") {
    static const char* kOotTowerTop = "Ganon's Castle Tower Boss Key Chest";
    static const char* kOotBossKey = "Ganon's Castle Boss Key";
    static const char* kMmWin = "RC_MOON_MAJORA_POT_01";

    PlaythroughResult result;

    using Placed = CwPlacedItem;
    std::vector<Placed> placements = ParseSpoilerPlacements(spoilerJson, sohDumpJson, mmDumpJson);

    auto queryReachable = QueryReachable;

    std::vector<std::string> ownedOot, ownedMm;
    std::unordered_set<std::string> collected; // "<cg>:<cn>"
    std::ostringstream log;
    log << "Cross-world playthrough - seed '" << seedLabel << "'\n"
        << "Beatable when: Ganondorf reachable (OOT: all trials cleared + Boss Key owned)"
        << " AND Majora's Lair reachable (MM).\n\n";

    int beatableSphere = -1;
    const int kMaxSpheres = 200;
    for (int sphere = 0; sphere < kMaxSpheres; ++sphere) {
        auto ootReach = queryReachable(ootOracle, ownedOot);
        auto mmReach = queryReachable(mmOracle, ownedMm);
        bool canGanon = ootReach.count(kOotTowerTop) > 0 &&
                        std::find(ownedOot.begin(), ownedOot.end(), kOotBossKey) != ownedOot.end();
        bool canMajora = mmReach.count(kMmWin) > 0;
        if (canGanon && canMajora) {
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
        log << "Sphere " << sphere << "  (Ganon=" << (canGanon ? "Y" : "n") << " Majora=" << (canMajora ? "Y" : "n")
            << ", +" << newly.size() << " items, " << newlyAdv << " progression)\n";
        nlohmann::json sphereSteps = nlohmann::json::array();
        for (auto& p : newly) {
            std::string key = (p.checkGame == GAME_OOT ? "oot:" : "mm:") + p.check;
            collected.insert(key);
            (p.itemGame == GAME_OOT ? ownedOot : ownedMm).push_back(p.item);
            if (playthroughOut)
                sphereSteps.push_back({ { "game", p.checkGame == GAME_OOT ? "oot" : "mm" },
                                        { "check", p.check },
                                        { "item", p.item },
                                        { "foreign", p.checkGame != p.itemGame } });
            // Junk is still collected (and kept in playthroughOut for hints) but not printed.
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
    auto everReachMm = queryReachable(mmOracle, allMm);
    result.ganonReachable =
        everReachOot.count(kOotTowerTop) > 0 && std::find(allOot.begin(), allOot.end(), kOotBossKey) != allOot.end();
    result.majoraReachable = everReachMm.count(kMmWin) > 0;

    if (mmRestore)
        mmRestore();

    if (beatableSphere >= 0) {
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

} // namespace ComboRando
