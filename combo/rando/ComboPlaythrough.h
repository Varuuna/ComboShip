// combo/rando/ComboPlaythrough.h
// ComboShip: forward "playthrough" simulation over a finished cross-world seed. Starting from an empty
// collected set, it repeatedly asks each game's reachability oracle what's reachable, collects the items
// on newly-reachable checks, and loops until the win is reachable (BEATABLE) or nothing new opens (stuck).
// Shared by the in-game generator (ComboShip.cpp) and the headless validator (ComboRandoHeadless.cpp) so
// the two never diverge. The oracle's logic mode / tricks are controlled by the caller before running.
#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CrossWorldRando.h"

namespace ComboRando {

struct PlaythroughResult {
    bool beatable = false;
    int beatableSphere = -1;
    size_t reachableOot = 0, unreachableOot = 0;
    size_t reachableMm = 0, unreachableMm = 0;
};

// Endgame proxies the oracles actually emit (see ComboShip.cpp for the rationale — the literal "Ganon"
// check needs CanUse(RG_MASTER_SWORD), an equip flag the headless engine doesn't model, so we use
// tower-top + Boss Key owned; MM uses the in-lair check which already encodes the remains/masks gate).
inline PlaythroughResult RunPlaythrough(const std::string& spoilerJson, const OracleFns& ootOracle,
                                        const OracleFns& mmOracle, const std::string& seedLabel,
                                        void (*mmRestore)(), nlohmann::json* playthroughOut = nullptr) {
    static const char* kOotTowerTop = "Ganon's Castle Tower Boss Key Chest";
    static const char* kOotBossKey = "Ganon's Castle Boss Key";
    static const char* kMmWin = "RC_MOON_MAJORA_POT_01";

    PlaythroughResult result;

    struct Placed {
        GameId checkGame;
        std::string check;
        GameId itemGame;
        std::string item;
    };
    std::vector<Placed> placements;
    std::unordered_set<std::string> foreignKey;
    std::unordered_map<std::string, GameId> foreignItemGame;
    try {
        auto j = nlohmann::json::parse(spoilerJson);
        for (auto& fm : j.value("foreign", nlohmann::json::array())) {
            std::string cg = fm.value("checkGame", ""), cn = fm.value("checkName", "");
            std::string ig = fm.value("itemGame", "");
            foreignKey.insert(cg + ":" + cn);
            foreignItemGame[cg + ":" + cn] = (ig == "mm") ? GAME_MM : GAME_OOT;
        }
        auto addGame = [&](const char* key, GameId cg) {
            if (!j.contains(key) || !j[key].is_object())
                return;
            for (auto& [cn, iv] : j[key].items()) {
                std::string fk = std::string(key) + ":" + cn;
                GameId ig = foreignKey.count(fk) ? foreignItemGame[fk] : cg;
                placements.push_back({ cg, cn, ig, iv.get<std::string>() });
            }
        };
        addGame("oot", GAME_OOT);
        addGame("mm", GAME_MM);
    } catch (const std::exception& e) {
        std::cerr << "[PLAYTHROUGH] spoiler parse error: " << e.what() << "\n";
        return result;
    }

    auto queryReachable = [&](const OracleFns& o, const std::vector<std::string>& owned) {
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
    };

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
        log << "Sphere " << sphere << "  (Ganon=" << (canGanon ? "Y" : "n") << " Majora=" << (canMajora ? "Y" : "n")
            << ", +" << newly.size() << " items)\n";
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
            log << "    " << (got ? "  " : "! ") << p.check << "  <-  " << p.item
                << (p.checkGame != p.itemGame ? (p.itemGame == GAME_OOT ? "  (OOT item)" : "  (MM item)") : "")
                << (got ? "" : "   [UNREACHABLE]") << "\n";
        }
        log << "  " << tag << ": " << reached << " reachable, " << missing << " unreachable\n";
        reachedOut = reached;
        missingOut = missing;
    };
    log << "\n==== Full placement (all checks) ====\n";
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
