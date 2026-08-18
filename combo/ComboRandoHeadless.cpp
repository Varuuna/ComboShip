// combo/ComboRandoHeadless.cpp
// ComboShip: standalone HEADLESS cross-world seed generator/validator. Loads soh.dll + 2ship.dll and
// drives the rando-only headless init (SOH_InitRandoHeadless / MM_InitRandoHeadless) + the cross-world
// fill directly — no game window, ResourceManager, audio, or ComboShip.exe boot.
//
// Modes (run from the game dir, next to soh.dll/2ship.dll):
//   comborando.exe [--seed <s>] [--count <n>]      generate+validate seed(s); also writes a consolidated
//                                                  spoiler to saves/combo/comborando.spoiler.json (count 1)
//   comborando.exe --playthrough <spoiler.json>    forward-traverse a finished seed to judge beatability
//
// --starting-game <oot|mm|random> overrides the menu's Starting Game CVar for generate mode (#135).
//
// Exact-seed repro (generate mode): --settings <consolidated.json> restores that seed's OOT/MM settings
// and tricks, and --master-seed <n> uses n directly instead of hashing --seed. Together they reproduce a
// reported seed's generation bit-for-bit, which plain --seed cannot.
//
// --playthrough runs two passes, ignoring the seed's No-Logic/Glitchless flag (it always evaluates real
// gates; permissiveness comes only from tricks): Pass 1 uses the seed's own tricks ("can this player beat
// it?"); if that sticks, Pass 2 enables every trick to tell "needs more tricks than configured" apart from
// "factually impossible" (e.g. a required item behind an unbreakable possession lock).
//
// Exit codes: 0 = beatable as configured; 3 = beatable only with all tricks; 1 = not beatable / seed
// failed; 2 = setup error.
#define NOMINMAX // windows.h min/max macros clash with std::min/std::max in CrossWorldRando.h
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "rando/CrossWorldRando.h"
#include "rando/ComboPlaythrough.h"
#include "rando/CrossForeign.h" // BuildForeignArray (foreign enrichment parity with RunComboFill)
#include "rando/CrossHints.h"

namespace {

typedef void (*FnVoidV)(void);
typedef const char* (*FnDump)(void);
typedef void (*FnSetSeed)(uint64_t);
typedef const char* (*FnGetForced)(uint32_t);
typedef void (*FnTakeStr)(const char*);
typedef void (*FnOracleVoid)(void);
typedef void (*FnOracleSetItems)(const char*);
typedef const char* (*FnOracleGetChecks)(void);
typedef void (*FnOraclePlaceItem)(const char*, const char*);
typedef uint8_t (*FnOracleGetPortalOpen)(void); // OOT->MM portal gate (see CrossWorldRando.h)

// Must match ComboShip.cpp's ComboHash (FNV-1a 32-bit) so headless seeds match in-game seeds.
uint32_t ComboHash(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s)
        h = (h ^ c) * 16777619u;
    return h;
}

// #135: resolve the starting-game config (0 = OOT, 1 = MM, 2 = Random) for one attempt. Must stay
// byte-identical to ComboShip.cpp's ResolveStartingGameMM, derivation string included.
bool ResolveStartingGameMM(int cfg, uint32_t masterSeed) {
    if (cfg == 2)
        return (ComboHash("startingGame:" + std::to_string(masterSeed)) & 1u) != 0;
    return cfg == 1;
}

template <typename T> T Sym(HMODULE m, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(m, name));
}

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Render the spoiler's "seed" (string or number) as a label.
std::string SeedLabel(const nlohmann::json& spoiler) {
    if (spoiler.contains("seed")) {
        const auto& s = spoiler["seed"];
        if (s.is_string())
            return s.get<std::string>();
        if (s.is_number())
            return std::to_string(s.get<int64_t>());
    }
    return "?";
}

} // namespace

int main(int argc, char** argv) {
    std::string seed = "1";
    int count = 1;
    std::string playthroughFile;
    std::string settingsFile; // consolidated spoiler to restore settings/tricks from (exact-seed repro)
    bool haveMasterSeed = false;
    uint32_t masterSeedArg = 0;
    // #136: combined goal. Absent => read the menu CVars (matches in-game); 0 => force bosses.
    int triforceRequiredArg = -1;
    // #136: combined pool total. Absent (-1) => the menu CVar, or `required` when that is overridden.
    int triforceTotalArg = -1;
    // #135: starting game. Absent (-1) => read the menu CVar, same as in-game.
    int startingGameArg = -1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--triforce-required" && i + 1 < argc)
            triforceRequiredArg = std::atoi(argv[++i]);
        else if (a == "--triforce-total" && i + 1 < argc)
            triforceTotalArg = std::atoi(argv[++i]);
        else if (a == "--starting-game" && i + 1 < argc) {
            std::string v = argv[++i];
            startingGameArg = v == "oot" ? 0 : v == "mm" ? 1 : v == "random" ? 2 : -2;
            if (startingGameArg == -2) {
                std::cerr << "[comborando] --starting-game must be oot, mm or random (got '" << v << "')\n";
                return 2;
            }
        } else if (a == "--seed" && i + 1 < argc)
            seed = argv[++i];
        else if (a == "--count" && i + 1 < argc)
            count = std::max(1, std::atoi(argv[++i]));
        else if (a == "--playthrough" && i + 1 < argc)
            playthroughFile = argv[++i];
        else if (a == "--settings" && i + 1 < argc)
            settingsFile = argv[++i];
        else if (a == "--master-seed" && i + 1 < argc) {
            masterSeedArg = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            haveMasterSeed = true;
        }
    }

    HMODULE soh = LoadLibraryA("soh.dll");
    HMODULE mm = LoadLibraryA("2ship.dll");
    if (!soh || !mm) {
        std::cerr << "[comborando] failed to load soh.dll / 2ship.dll — run from the game directory\n";
        return 2;
    }

    auto SOH_InitRandoHeadless = Sym<FnVoidV>(soh, "SOH_InitRandoHeadless");
    auto MM_InitRandoHeadless = Sym<FnVoidV>(mm, "MM_InitRandoHeadless");
    auto SOH_Dump = Sym<FnDump>(soh, "SOH_DumpRandoStaticData");
    auto MM_Dump = Sym<FnDump>(mm, "MM_DumpRandoStaticData");
    auto SOH_DumpSettings = Sym<FnDump>(soh, "SOH_DumpRandoSettings");
    auto MM_DumpSettings = Sym<FnDump>(mm, "MM_DumpRandoSettings");
    auto SOH_RestoreSettings = Sym<FnTakeStr>(soh, "SOH_RestoreRandoSettings");
    auto SOH_PrepContext = Sym<FnVoidV>(soh, "SOH_PrepRandoContext");
    auto MM_RestoreSettings = Sym<FnTakeStr>(mm, "MM_RestoreRandoSettings");
    auto SOH_SetSeed = Sym<FnSetSeed>(soh, "SOH_SetComboRandoSeed");
    auto MM_SetSeed = Sym<FnSetSeed>(mm, "MM_SetComboRandoSeed");
    auto SOH_GetForced = Sym<FnGetForced>(soh, "SOH_GetForcedPlacements");
    // OOT entrance shuffle (#90) — without this the validator checks a vanilla entrance graph.
    auto SOH_ShuffleEntrances = Sym<int (*)(uint64_t)>(soh, "SOH_ShuffleEntrancesForCombo");
    auto SOH_DumpEntranceOverrides = Sym<FnDump>(soh, "SOH_DumpEntranceOverrides");
    auto SOH_ApplyEntranceOverrides = Sym<int (*)(const char*)>(soh, "SOH_ApplyEntranceOverridesForCombo");
    auto MM_Restore = Sym<FnOracleVoid>(mm, "Combo_MM_Rando_Restore");
    auto SOH_DumpEnabledTricks = Sym<FnDump>(soh, "SOH_DumpEnabledTricks");
    auto SOH_DumpRandoHintData = Sym<FnDump>(soh, "SOH_DumpRandoHintData"); // cross-hint verification
    auto SOH_SetEnabledTricks = Sym<FnTakeStr>(soh, "SOH_SetEnabledTricks");
    auto SOH_SetAllTricks = Sym<FnVoidV>(soh, "SOH_SetAllTricks");
    auto SOH_SetCheckPrices = Sym<FnTakeStr>(soh, "SOH_SetCheckPrices");
    auto MM_SetCheckPrices = Sym<FnTakeStr>(mm, "MM_SetCheckPrices");
    // #136: combined goal — must be pushed before every dump (it shapes both games' pools).
    auto SOH_SetComboGoal = Sym<void (*)(int, int, int)>(soh, "SOH_SetComboGoal");
    auto MM_SetComboGoal = Sym<void (*)(int, int, int)>(mm, "MM_SetComboGoal");
    auto SOH_ReadComboGoalCVars = Sym<int (*)(int*, int*)>(soh, "SOH_ReadComboGoalCVars");
    // #135: starting game — also pushed before every dump (it forces OOT's age/forest/exclusions).
    auto SOH_SetComboStartingGame = Sym<void (*)(int)>(soh, "SOH_SetComboStartingGame");
    auto SOH_ReadComboStartingGameCVar = Sym<int (*)(void)>(soh, "SOH_ReadComboStartingGameCVar");

    ComboRando::OracleFns oot{ Sym<FnOracleVoid>(soh, "Combo_SOH_Rando_Reset"),
                               Sym<FnOracleSetItems>(soh, "Combo_SOH_Rando_SetOwnedItems"),
                               Sym<FnOracleGetChecks>(soh, "Combo_SOH_Rando_GetReachableChecks"),
                               Sym<FnOraclePlaceItem>(soh, "Combo_SOH_Rando_PlaceItem"),
                               Sym<FnOracleGetPortalOpen>(soh, "Combo_SOH_Rando_GetPortalOpen") };
    ComboRando::OracleFns mmO{ Sym<FnOracleVoid>(mm, "Combo_MM_Rando_Reset"),
                               Sym<FnOracleSetItems>(mm, "Combo_MM_Rando_SetOwnedItems"),
                               Sym<FnOracleGetChecks>(mm, "Combo_MM_Rando_GetReachableChecks"),
                               Sym<FnOraclePlaceItem>(mm, "Combo_MM_Rando_PlaceItem") };

    if (!SOH_InitRandoHeadless || !MM_InitRandoHeadless || !SOH_Dump || !MM_Dump || !oot.Reset || !oot.SetOwnedItems ||
        !oot.GetReachableChecks || !oot.PlaceItem || !oot.GetPortalOpen || !mmO.Reset || !mmO.SetOwnedItems ||
        !mmO.GetReachableChecks || !mmO.PlaceItem) {
        std::cerr << "[comborando] missing required DLL exports — rebuild soh.dll / 2ship.dll\n";
        return 2;
    }

    SOH_InitRandoHeadless();
    MM_InitRandoHeadless();

    // ---- Playthrough mode: replay a finished seed and judge beatability. ----
    if (!playthroughFile.empty()) {
        if (!SOH_RestoreSettings || !SOH_PrepContext || !MM_RestoreSettings) {
            std::cerr << "[playthrough] settings-restore exports unavailable — rebuild soh.dll / 2ship.dll\n";
            return 2;
        }
        nlohmann::json spoiler;
        try {
            spoiler = nlohmann::json::parse(ReadFile(playthroughFile));
        } catch (const std::exception& e) {
            std::cerr << "[playthrough] could not read/parse '" << playthroughFile << "': " << e.what() << "\n";
            return 2;
        }
        auto ootSettings = spoiler.value("oot", nlohmann::json::object()).value("settings", nlohmann::json::object());
        auto mmSettings = spoiler.value("mm", nlohmann::json::object()).value("settings", nlohmann::json::object());
        // Flat placement map that RunPlaythrough consumes.
        nlohmann::json flat;
        flat["oot"] = spoiler.value("oot", nlohmann::json::object()).value("placements", nlohmann::json::object());
        flat["mm"] = spoiler.value("mm", nlohmann::json::object()).value("placements", nlohmann::json::object());
        flat["foreign"] = spoiler.value("foreign", nlohmann::json::array());
        std::string label = SeedLabel(spoiler);
        uint32_t masterSeed = spoiler.value("masterSeed", 0u);
        // #136: the seed's own goal drives both the traversal win test and the pool shaping below.
        ComboRando::CwGoal goal;
        {
            auto g = spoiler.value("goal", nlohmann::json::object());
            goal.hunt = g.value("type", std::string("bosses")) == "triforceHunt";
            goal.required = goal.hunt ? g.value("requiredPieces", 0) : 0;
            goal.total = g.value("totalPieces", -1); // absent on seeds made before the combined total
        }
        // #135: the seed's starting game. Absent on old spoilers, which all started in OOT.
        const bool mmStart = spoiler.value("startingGame", std::string("OOT")) == "MM";

        auto ootEnabledTricks =
            spoiler.value("oot", nlohmann::json::object()).value("enabledTricks", nlohmann::json::array());
        auto ootEntrances = spoiler.value("entrances", nlohmann::json::object()).value("oot", nlohmann::json::array());

        // Prices are part of the seed: an unknown price can never be treated as buyable, so a spoiler
        // without them can't be validated honestly. Hard-fail rather than emit an optimistic verdict.
        auto ootPrices = spoiler.value("oot", nlohmann::json::object()).value("prices", nlohmann::json::object());
        auto mmPrices = spoiler.value("mm", nlohmann::json::object()).value("prices", nlohmann::json::object());
        if (ootPrices.empty() || mmPrices.empty() || !SOH_SetCheckPrices || !MM_SetCheckPrices) {
            std::cerr << "[playthrough] spoiler has no shop prices (predates price export) — regenerate the seed "
                         "to validate it\n";
            return 2;
        }

        bool entranceRestoreFailed = false; // set inside runPass; a wrong graph makes any verdict meaningless
        // One traversal pass. Forces real-logic evaluation (ignore the seed's No-Logic/Glitchless flag).
        // Tricks are set via SOH_SetEnabledTricks/SetAllTricks BEFORE SOH_Dump, whose SOH_PrepRandoContext
        // pushes them into the Context (Combo_ApplyEnabledTricks). SOH_Dump (+seed) sets the OOT/MM contexts
        // up exactly like the fill so reachability matches. MM has no tricks.
        auto runPass = [&](bool allTricks, nlohmann::json* ptOut) {
            // Restore the seed's ORIGINAL settings for the dump so the shuffled-shop-slot set matches
            // generation (a No-Logic seed shuffles 8 slots/shop; glitchless computes 7 — GAP-8).
            SOH_RestoreSettings(ootSettings.dump().c_str());
            nlohmann::json ms = mmSettings;
            for (auto it = ms.begin(); it != ms.end(); ++it)
                if (it.key().find("RO_LOGIC") != std::string::npos)
                    it.value() = 0; // MM glitchless — pool shape is logic-independent
            MM_RestoreSettings(ms.dump().c_str());
            if (allTricks) {
                if (SOH_SetAllTricks)
                    SOH_SetAllTricks();
            } else if (SOH_SetEnabledTricks) {
                SOH_SetEnabledTricks(ootEnabledTricks.dump().c_str());
            }
            if (SOH_SetSeed)
                SOH_SetSeed(masterSeed);
            if (MM_SetSeed)
                MM_SetSeed(masterSeed);
            if (SOH_SetComboGoal)
                SOH_SetComboGoal(goal.hunt ? 1 : 0, goal.required, ComboRando::CwOotPieces(goal.total));
            if (MM_SetComboGoal)
                MM_SetComboGoal(goal.hunt ? 1 : 0, goal.required, ComboRando::CwMmPieces(goal.total));
            if (SOH_SetComboStartingGame)
                SOH_SetComboStartingGame(mmStart ? 1 : 0);
            // A real in-game save's placement map lists only SHUFFLED checks; non-shuffled items the
            // oracle still gates on (OOT vanilla shop items, MM boss remains when not shuffled) live in
            // the dump's fixed[]. Merge them in so those gates (e.g. MM RemainsCount for the Moon) resolve.
            std::string sohDump = SOH_Dump(), mmDump = MM_Dump();
            // NOW force glitchless for traversal (real gates); the dump above froze the seed's slot
            // set in the DLL-side cache, so this re-prep can't shrink it.
            nlohmann::json os = ootSettings;
            for (auto it = os.begin(); it != os.end(); ++it)
                if (it.key().find("LogicRules") != std::string::npos)
                    it.value() = 0;
            SOH_RestoreSettings(os.dump().c_str());
            SOH_PrepContext();
            // The PrepContext above reset the OOT region graph to VANILLA wiring — not the graph the
            // seed was filled against. Install the seed's recorded entrance layout (empty array =
            // vanilla, still correct). Re-deriving via SOH_ShuffleEntrances is only a fallback for
            // old spoilers without the array: its logic-validated accepts depend on the trick set, so
            // the all-tricks pass could re-derive a DIFFERENT layout than generation.
            if (SOH_ApplyEntranceOverrides) {
                if (!SOH_ApplyEntranceOverrides(ootEntrances.dump().c_str())) {
                    std::cerr << "[playthrough] entrance apply failed — verdict unreliable\n";
                    entranceRestoreFailed = true;
                }
            } else if (!ootEntrances.empty()) {
                std::cerr << "[playthrough] spoiler has entrances but soh.dll lacks the apply export — "
                             "rebuild soh.dll; re-deriving (all-tricks pass may diverge)\n";
                if (!SOH_ShuffleEntrances || !SOH_ShuffleEntrances(masterSeed))
                    entranceRestoreFailed = true;
            }
            // Spoiler prices are the seed's truth — they override the dumps' re-rolls in every oracle
            // reset (SOH) / query (MM), so wallet gates evaluate against what the player actually pays.
            SOH_SetCheckPrices(ootPrices.dump().c_str());
            MM_SetCheckPrices(mmPrices.dump().c_str());
            nlohmann::json passFlat = flat;
            auto mergeFixed = [&](const std::string& dump, const char* game) {
                try {
                    for (auto& f : nlohmann::json::parse(dump).value("fixed", nlohmann::json::array())) {
                        std::string chk = f.value("check", ""), item = f.value("item", "");
                        if (!chk.empty() && !item.empty() && !passFlat[game].contains(chk))
                            passFlat[game][chk] = item;
                    }
                } catch (...) {}
            };
            mergeFixed(sohDump, "oot");
            mergeFixed(mmDump, "mm");
            return ComboRando::RunPlaythrough(passFlat.dump(), oot, mmO, label, MM_Restore, ptOut, sohDump, mmDump,
                                              ComboRando::OotAccessFromDump(sohDump) != ComboRando::OotAccess::NO_LOGIC,
                                              /*progressionOnly*/ false, goal, mmStart);
        };

        // Affordability canary: re-check every priced purchase in the walk against the wallets held
        // at that sphere. A violation means the oracle's price wiring regressed to "shops are free".
        // With Child Wallet not shuffled, logic starts at the 99-capacity tier (logic.cpp Reset).
        const int ootBaseWalletTier = ootSettings.value("gRandoSettings.ShuffleChildWallet", 0) == 0 ? 1 : 0;
        auto affordabilityViolations = [&](const nlohmann::json& pt) {
            int ow = 0, mw = 0, bad = 0;
            for (const auto& sph : pt) {
                const int owS = ow, mwS = mw; // wallets from earlier spheres only
                for (const auto& st : sph.value("steps", nlohmann::json::array())) {
                    std::string game = st.value("game", ""), chk = st.value("check", ""), item = st.value("item", "");
                    // Junk is never required, so an unaffordable junk slot must not fail a beatable seed.
                    if (!st.value("advancement", true))
                        continue;
                    if (game == "oot" && ootPrices.contains(chk)) {
                        static const int caps[5] = { 0, 99, 200, 500, 999 };
                        int price = ootPrices[chk].get<int>();
                        if (price > caps[std::min(ootBaseWalletTier + owS, 4)]) {
                            ++bad;
                            std::cerr << "[playthrough] AFFORDABILITY: [OOT] " << chk << " price=" << price << " with "
                                      << owS << " wallet(s) at sphere " << sph.value("sphere", -1) << "\n";
                        }
                    } else if (game == "mm" && mmPrices.contains(chk) &&
                               // ComboShip: friendly (prettified) forms of the MM purchase check names.
                               (chk.find("Shop Item") != std::string::npos ||
                                chk.find("Tingle Map") != std::string::npos ||
                                chk.find("Gorman Milk") != std::string::npos ||
                                chk.find("Milk Bar") != std::string::npos ||
                                chk.find("Curiosity") != std::string::npos)) {
                        int p = mmPrices[chk].get<int>();
                        if (!(p < 100 || (p <= 200 && mwS >= 1) || mwS >= 2)) {
                            ++bad;
                            std::cerr << "[playthrough] AFFORDABILITY: [MM] " << chk << " price=" << p << " with "
                                      << mwS << " wallet(s) at sphere " << sph.value("sphere", -1) << "\n";
                        }
                    }
                    // ComboShip: both games' wallet now normalizes to "Progressive Wallet"; attribute to
                    // the item's HOME game (itemGame), derived from the check game + foreign flag, so a
                    // cross-placed wallet still credits its owner's tier (matches the pre-rename semantics).
                    if (item == "Progressive Wallet") {
                        bool fgn = st.value("foreign", false);
                        std::string homeGame = fgn ? (game == "oot" ? "mm" : "oot") : game;
                        if (homeGame == "oot")
                            ++ow;
                        else
                            ++mw;
                    }
                }
            }
            return bad;
        };
        // Names the blocking win side from full-inventory reachability, so "stuck" is exact.
        auto blocked = [](const ComboRando::PlaythroughResult& r) -> const char* {
            if (r.hunt)
                return r.piecesReachable >= r.piecesRequired
                           ? "enough Triforce Pieces are placed but the forward walk dead-ends (item cycle)"
                           : "not enough Triforce Pieces are reachable";
            return (!r.ganonReachable && !r.majoraReachable) ? "neither Ganon nor Majora is reachable"
                   : !r.ganonReachable                       ? "Ganon (OOT) is unreachable"
                   : !r.majoraReachable
                       ? "Majora (MM) is unreachable"
                       : "both ends reachable with full inventory but the forward walk dead-ends (item cycle)";
        };

        // No Logic seeds may be OOT-unbeatable by design; the verdict below tolerates that as long as
        // MM stays fully reachable + Majora beatable.
        bool ootNoLogic = false;
        for (auto it = ootSettings.begin(); it != ootSettings.end(); ++it)
            if (it.key().find("LogicRules") != std::string::npos && it.value().is_number() &&
                it.value().get<int>() == 1)
                ootNoLogic = true;

        // Pass 1 — the seed's own settings + enabled tricks: "can this player beat it?"
        std::cout << "[playthrough] seed '" << label << "' — Pass 1 (" << ootEnabledTricks.size()
                  << " enabled trick(s), starts in " << (mmStart ? "MM" : "OOT") << ")\n";
        nlohmann::json pt1 = nlohmann::json::array();
        auto r1 = runPass(false, &pt1);
        if (entranceRestoreFailed) {
            std::cerr << "[playthrough] could not restore the seed's entrance layout — no verdict\n";
            return 2;
        }
        if (r1.beatable) {
            if (int bad = affordabilityViolations(pt1)) {
                std::cout << "[playthrough] RESULT: FAIL — beatable but " << bad
                          << " purchase(s) exceed the wallet held at their sphere (price wiring bug).\n";
                return 1;
            }
            std::cout << "[playthrough] RESULT: PASS — seed passed validation, should be beatable (sphere "
                      << r1.beatableSphere << ").\n";
            return 0;
        }
        // Pass 2 — all tricks: separates "needs more tricks than configured" from "impossible".
        std::cout << "[playthrough] Pass 1 stuck (" << blocked(r1) << ") — retrying with ALL tricks\n";
        nlohmann::json pt2 = nlohmann::json::array();
        auto r2 = runPass(true, &pt2);
        if (r2.beatable) {
            std::cout << "[playthrough] RESULT: beatable ONLY with tricks beyond the seed's settings (sphere "
                      << r2.beatableSphere << "). With the seed's tricks, Pass 1 got stuck: " << blocked(r1) << ".\n";
            return 3;
        }
        // No Logic: the forward-walk sphere ORDER is meaningless (in-game No Logic disables the very gates
        // the walk enforces), so beatability is judged on FULL-inventory reachability. Both win conditions
        // reachable with everything placed => genuinely beatable. Majora-only => tolerate OOT/Ganon (still a
        // valid No-Logic seed, MM win chain intact; MM-advancement reachability is guaranteed by the fill,
        // which retries on mmAdvUnreachable>0 — not gated on unreachableMm==0, ~1 MM junk check is always
        // under-modeled since the oracle evaluates MM with zeroed save options).
        if (ootNoLogic && r2.ganonReachable && r2.majoraReachable) {
            // Under a hunt both flags mirror the piece count, so name the pieces instead of the bosses.
            if (r2.hunt) {
                std::cout << "[playthrough] RESULT: PASS (No Logic) — beatable: " << r2.piecesReachable << "/"
                          << r2.piecesRequired
                          << " Triforce Pieces reachable with full inventory (forward-walk order N/A under No "
                             "Logic).\n";
            } else {
                std::cout << "[playthrough] RESULT: PASS (No Logic) — beatable: Ganon AND Majora both reachable "
                             "with full inventory (forward-walk order N/A under No Logic).\n";
            }
            return 0;
        }
        if (ootNoLogic && r2.majoraReachable) {
            std::cout << "[playthrough] RESULT: PASS (No Logic) — Majora beatable (MM reachable); Ganon (OOT) "
                         "unreachable even at full inventory, tolerated.\n";
            return 0;
        }
        std::cout << "[playthrough] RESULT: FAIL — not beatable even with all tricks; stuck: " << blocked(r2)
                  << " (OOT unreachable=" << r2.unreachableOot << ", MM unreachable=" << r2.unreachableMm
                  << "; see saves/combo/slot0.playthrough.txt).\n";
        return 1;
    }

    // ---- Generate + validate mode. ----
    // Restore a reported seed's settings so generation matches it; otherwise live CVar defaults apply.
    if (!settingsFile.empty()) {
        if (!SOH_RestoreSettings || !MM_RestoreSettings) {
            std::cerr << "[comborando] --settings needs the settings-restore exports — rebuild soh.dll / 2ship.dll\n";
            return 2;
        }
        try {
            auto sj = nlohmann::json::parse(ReadFile(settingsFile));
            const auto& oot = sj.contains("oot") ? sj["oot"] : sj;
            const auto& mm = sj.contains("mm") ? sj["mm"] : sj;
            if (oot.contains("settings"))
                SOH_RestoreSettings(oot["settings"].dump().c_str());
            if (mm.contains("settings"))
                MM_RestoreSettings(mm["settings"].dump().c_str());
            if (SOH_SetEnabledTricks && oot.contains("enabledTricks"))
                SOH_SetEnabledTricks(oot["enabledTricks"].dump().c_str());
            std::cout << "[comborando] restored settings from '" << settingsFile << "'\n";
        } catch (const std::exception& e) {
            std::cerr << "[comborando] could not read/parse '" << settingsFile << "': " << e.what() << "\n";
            return 2;
        }
    }
    // #136: goal resolution. --triforce-required wins; otherwise fall back to the menu CVars so a
    // headless seed reproduces the in-game one.
    ComboRando::CwGoal goal;
    if (triforceRequiredArg >= 0) {
        goal.hunt = triforceRequiredArg > 0;
        goal.required = std::min(triforceRequiredArg, ComboRando::kMaxComboTriforcePieces);
        // --triforce-total wins; else default the pool to exactly what the goal needs.
        goal.total = std::max(triforceTotalArg, goal.required);
    } else if (SOH_ReadComboGoalCVars) {
        int required = 0, total = -1;
        goal.hunt = SOH_ReadComboGoalCVars(&required, &total) != 0;
        goal.required = goal.hunt ? required : 0;
        goal.total = triforceTotalArg > 0 ? std::max(triforceTotalArg, goal.required) : total;
    }
    // Same cap the DLLs apply per game, so the recorded totalPieces can't disagree with the pools.
    goal.total = std::clamp(goal.total, goal.required, ComboRando::kMaxComboTriforcePieces);
    // #135: same fallback order — the flag wins, else the menu CVar.
    const int startCfg = startingGameArg >= 0            ? startingGameArg
                         : SOH_ReadComboStartingGameCVar ? SOH_ReadComboStartingGameCVar()
                                                         : 0;
    std::cout << "[comborando] validating " << count << " seed(s) from "
              << (haveMasterSeed ? "masterSeed " + std::to_string(masterSeedArg) : "'" + seed + "'")
              << (goal.hunt ? " (Triforce Hunt, " + std::to_string(goal.required) + " of " +
                                  std::to_string(goal.total) + " pieces)"
                            : std::string(" (both bosses)"))
              << (startCfg == 1   ? ", starting game MM"
                  : startCfg == 2 ? ", starting game random"
                                  : "")
              << "\n";
    int failures = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < count; ++i) {
        const uint32_t base = (haveMasterSeed ? masterSeedArg : ComboHash(seed)) + static_cast<uint32_t>(i);
        uint32_t masterSeed = base;
        std::string sohDump, mmDump, sohHintDump;
        ComboRando::CombinedFillResult r{};
        ComboRando::RequirednessResult pareDownResult; // cross-hint verification (headless mirror of RunComboFill)
        // Whole-fill retries with re-derived seeds, identical to RunComboFill (GAP-4) so a headless
        // seed reproduces the in-game one even when early attempts fail (shared budget, same header).
        const int kFillAttempts = ComboRando::kFillAttempts;
        bool pinStartOot = false, resolvedMmStart = false; // #135: Random falls back to OOT after a failure
        for (int attempt = 0; attempt < kFillAttempts; ++attempt) {
            masterSeed = base + attempt * 0x9E3779B9u;
            const bool mmStart = !pinStartOot && !(startCfg == 2 && attempt + 1 == kFillAttempts) &&
                                 ResolveStartingGameMM(startCfg, masterSeed);
            if (SOH_SetSeed)
                SOH_SetSeed(masterSeed);
            if (MM_SetSeed)
                MM_SetSeed(masterSeed);
            // Before the dumps: the goal shapes OOT's wincon placement and MM's hunt pool.
            if (SOH_SetComboGoal)
                SOH_SetComboGoal(goal.hunt ? 1 : 0, goal.required, ComboRando::CwOotPieces(goal.total));
            if (MM_SetComboGoal)
                MM_SetComboGoal(goal.hunt ? 1 : 0, goal.required, ComboRando::CwMmPieces(goal.total));
            // Same reason (#135): an MM start forces OOT's age/forest/exclusions.
            if (SOH_SetComboStartingGame)
                SOH_SetComboStartingGame(mmStart ? 1 : 0);
            sohDump = SOH_Dump();
            mmDump = MM_Dump();
            if (goal.hunt) {
                const int pieces = ComboRando::CountPoolTriforcePieces(sohDump, mmDump);
                if (pieces < goal.required) {
                    std::cerr << "[comborando] Triforce Hunt needs " << goal.required << " pieces but only " << pieces
                              << " are in the combined pool — raise --triforce-total / the Combo pool total\n";
                    return 2;
                }
            }
            std::string forced = SOH_GetForced ? SOH_GetForced(masterSeed) : "";
            // Same placement as RunComboFill: after the dump/forced read, before the fill, so logic
            // validates the shuffled world. No-op when the entrance options are off.
            if (SOH_ShuffleEntrances && !SOH_ShuffleEntrances(masterSeed)) {
                std::cerr << "[comborando] seed " << masterSeed << ": entrance shuffle found no valid layout\n";
                if (mmStart && startCfg == 2)
                    pinStartOot = true;
                continue;
            }
            ComboRando::OotAccess ootAccess = ComboRando::OotAccessFromDump(sohDump);
            r = ComboRando::CrossWorldCombinedFill(sohDump, mmDump, masterSeed, oot, mmO, nullptr, forced, ootAccess,
                                                   goal, mmStart ? ComboRando::GAME_MM : ComboRando::GAME_OOT);
            if (r.success) {
                resolvedMmStart = mmStart;
                // Cross-hint data (Phase 2/3 mirror of RunComboFill, incl. the same area maps so the
                // WotH/Foolish rollup matches in-game): needs this attempt's still-live oracle session.
                sohHintDump = SOH_DumpRandoHintData ? SOH_DumpRandoHintData() : "";
                std::unordered_map<std::string, std::string> ootAreas, mmAreas;
                try {
                    auto hd = nlohmann::json::parse(sohHintDump.empty() ? "{}" : sohHintDump);
                    for (auto& c : hd.value("checks", nlohmann::json::array())) {
                        std::string name = c.value("name", ""), area = c.value("area", "");
                        if (!name.empty() && !area.empty())
                            ootAreas.emplace(std::move(name), std::move(area));
                    }
                    auto d = nlohmann::json::parse(mmDump);
                    const auto locHints = d.value("locationHints", nlohmann::json::object());
                    for (auto& [chk, region] : locHints.items())
                        mmAreas.emplace(chk, region.get<std::string>());
                } catch (...) {}
                const bool noLogic = ootAccess == ComboRando::OotAccess::NO_LOGIC;
                // A hunt under NO_LOGIC has no meaningful requiredness (see RunComboFill) — skip it.
                if (goal.hunt && noLogic)
                    std::cout << "[comborando]   pare-down skipped (No Logic + Triforce Hunt)\n";
                else if (ComboRando::NeedsRequirednessPareDown(sohHintDump, mmDump))
                    // NO_LOGIC: gate requiredness on MM only (OOT may be unbeatable by design).
                    pareDownResult = ComboRando::PareDownPlaythrough(
                        r.spoilerJson, oot, mmO, nullptr, sohDump, mmDump, ootAreas, mmAreas,
                        goal.hunt ? ComboRando::MakeTriforceHuntGoal(goal.required)
                        : noLogic ? ComboRando::MmOnlyMajoraGoal
                                  : ComboRando::DefaultGanonMajoraGoal,
                        !noLogic, mmStart);
                else
                    std::cout << "[comborando]   pare-down skipped (no enabled hint surface needs requiredness)\n";
            }
            if (MM_Restore)
                MM_Restore();
            if (r.success)
                break;
            if (mmStart && startCfg == 2)
                pinStartOot = true;
            std::cerr << "[comborando]   attempt " << (attempt + 1) << "/" << kFillAttempts << " failed: " << r.error
                      << "\n";
        }
        std::string tag = "'" + seed + "'" + (count > 1 ? ("+" + std::to_string(i)) : "");
        if (r.success) {
            size_t foreign = 0;
            try {
                foreign = nlohmann::json::parse(r.spoilerJson).value("foreign", nlohmann::json::array()).size();
            } catch (...) {}
            std::cout << "[comborando]   seed " << tag << " (" << masterSeed << ") PASS (" << foreign
                      << " cross-game placements)\n";
            // For a single-seed run, emit a consolidated spoiler (settings + placements) so it can be fed
            // straight back to --playthrough. Mirrors ComboShip.cpp's consolidated shape.
            if (count == 1 && SOH_DumpSettings && MM_DumpSettings) {
                try {
                    auto fillSpoiler = nlohmann::json::parse(r.spoilerJson);
                    auto pricesOf = [](const std::string& dump) {
                        return nlohmann::json::parse(dump).value("prices", nlohmann::json::object());
                    };
                    // OOT's curated ice-trap disguise set (parity with RunComboFill's writer).
                    auto iceTrapModelsOf = [](const std::string& dump) {
                        return nlohmann::json::parse(dump).value("iceTrapModels", nlohmann::json::array());
                    };
                    nlohmann::json consolidated;
                    consolidated["fileType"] = "ComboShipRandomizer";
                    consolidated["seed"] = seed;
                    consolidated["masterSeed"] = masterSeed;
                    consolidated["goal"] = { { "type", goal.hunt ? "triforceHunt" : "bosses" },
                                             { "requiredPieces", goal.required },
                                             { "totalPieces", goal.total } };
                    consolidated["startingGame"] = resolvedMmStart ? "MM" : "OOT"; // #135
                    // ComboShip: suffix cross-game item-name collisions in the placements (parity with
                    // RunComboFill's consolidated writer) so the headless spoiler shows "(OOT)"/"(MM)".
                    nlohmann::json ootPl = fillSpoiler.value("oot", nlohmann::json::object());
                    nlohmann::json mmPl = fillSpoiler.value("mm", nlohmann::json::object());
                    ComboRando::SuffixCrossGameItems(ootPl, mmPl, fillSpoiler.value("foreign", nlohmann::json::array()),
                                                     sohDump, mmDump);
                    consolidated["oot"] = { { "settings", nlohmann::json::parse(SOH_DumpSettings()) },
                                            { "enabledTricks", SOH_DumpEnabledTricks
                                                                   ? nlohmann::json::parse(SOH_DumpEnabledTricks())
                                                                   : nlohmann::json::array() },
                                            { "placements", ootPl },
                                            { "prices", pricesOf(sohDump) },
                                            { "iceTrapModels", iceTrapModelsOf(sohDump) } };
                    consolidated["mm"] = { { "settings", nlohmann::json::parse(MM_DumpSettings()) },
                                           { "placements", mmPl },
                                           { "prices", pricesOf(mmDump) } };
                    // ComboShip: enrich the foreign array exactly like RunComboFill (displayName game
                    // suffix) so the headless consolidated file matches the in-game one.
                    auto foreignArr = fillSpoiler.value("foreign", nlohmann::json::array());
                    ComboRando::AssignTrapDisguises(foreignArr, fillSpoiler.value("oot", nlohmann::json::object()),
                                                    fillSpoiler.value("mm", nlohmann::json::object()), sohDump, mmDump,
                                                    masterSeed);
                    consolidated["foreign"] = ComboRando::BuildForeignArray(foreignArr);
                    // OOT entrance layout (parity with ComboShip.cpp's consolidated writer) — this is
                    // what --playthrough installs into the region graph before walking.
                    {
                        nlohmann::json ootEnt = nlohmann::json::array();
                        if (SOH_DumpEntranceOverrides) {
                            try {
                                ootEnt = nlohmann::json::parse(SOH_DumpEntranceOverrides());
                            } catch (...) {}
                        }
                        consolidated["entrances"] = { { "oot", std::move(ootEnt) } };
                    }
                    // Cross-hint generation (mirrors RunComboFill) — enables the headless determinism check.
                    consolidated["hints"] =
                        ComboRando::Generate(masterSeed, sohDump, sohHintDump, mmDump, consolidated["foreign"],
                                             r.spoilerJson, pareDownResult);
                    std::error_code ec;
                    std::filesystem::create_directories("saves/combo", ec);
                    std::ofstream f("saves/combo/comborando.spoiler.json", std::ios::trunc);
                    f << consolidated.dump(2);
                    std::cout << "[comborando]   consolidated spoiler -> saves/combo/comborando.spoiler.json\n";
                } catch (const std::exception& e) {
                    std::cerr << "[comborando]   spoiler write failed: " << e.what() << "\n";
                }
            }
        } else {
            std::cerr << "[comborando]   seed " << tag << " (" << masterSeed << ") FAIL: " << r.error << "\n";
            ++failures;
        }
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "[comborando] RESULT: " << (failures ? "FAIL" : "PASS") << " — " << (count - failures) << "/" << count
              << " completable, " << ms << " ms\n";
    return failures == 0 ? 0 : 1;
}
