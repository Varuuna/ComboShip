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

// Must match ComboShip.cpp's ComboHash (FNV-1a 32-bit) so headless seeds match in-game seeds.
uint32_t ComboHash(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s)
        h = (h ^ c) * 16777619u;
    return h;
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
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--seed" && i + 1 < argc)
            seed = argv[++i];
        else if (a == "--count" && i + 1 < argc)
            count = std::max(1, std::atoi(argv[++i]));
        else if (a == "--playthrough" && i + 1 < argc)
            playthroughFile = argv[++i];
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
    auto MM_Restore = Sym<FnOracleVoid>(mm, "Combo_MM_Rando_Restore");
    auto SOH_DumpEnabledTricks = Sym<FnDump>(soh, "SOH_DumpEnabledTricks");
    auto SOH_DumpRandoHintData = Sym<FnDump>(soh, "SOH_DumpRandoHintData"); // cross-hint verification
    auto SOH_SetEnabledTricks = Sym<FnTakeStr>(soh, "SOH_SetEnabledTricks");
    auto SOH_SetAllTricks = Sym<FnVoidV>(soh, "SOH_SetAllTricks");
    auto SOH_SetCheckPrices = Sym<FnTakeStr>(soh, "SOH_SetCheckPrices");
    auto MM_SetCheckPrices = Sym<FnTakeStr>(mm, "MM_SetCheckPrices");

    ComboRando::OracleFns oot{ Sym<FnOracleVoid>(soh, "Combo_SOH_Rando_Reset"),
                               Sym<FnOracleSetItems>(soh, "Combo_SOH_Rando_SetOwnedItems"),
                               Sym<FnOracleGetChecks>(soh, "Combo_SOH_Rando_GetReachableChecks"),
                               Sym<FnOraclePlaceItem>(soh, "Combo_SOH_Rando_PlaceItem") };
    ComboRando::OracleFns mmO{ Sym<FnOracleVoid>(mm, "Combo_MM_Rando_Reset"),
                               Sym<FnOracleSetItems>(mm, "Combo_MM_Rando_SetOwnedItems"),
                               Sym<FnOracleGetChecks>(mm, "Combo_MM_Rando_GetReachableChecks"),
                               Sym<FnOraclePlaceItem>(mm, "Combo_MM_Rando_PlaceItem") };

    if (!SOH_InitRandoHeadless || !MM_InitRandoHeadless || !SOH_Dump || !MM_Dump || !oot.Reset || !oot.SetOwnedItems ||
        !oot.GetReachableChecks || !oot.PlaceItem || !mmO.Reset || !mmO.SetOwnedItems || !mmO.GetReachableChecks ||
        !mmO.PlaceItem) {
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

        auto ootEnabledTricks =
            spoiler.value("oot", nlohmann::json::object()).value("enabledTricks", nlohmann::json::array());

        // Prices are part of the seed: an unknown price can never be treated as buyable, so a spoiler
        // without them can't be validated honestly. Hard-fail rather than emit an optimistic verdict.
        auto ootPrices = spoiler.value("oot", nlohmann::json::object()).value("prices", nlohmann::json::object());
        auto mmPrices = spoiler.value("mm", nlohmann::json::object()).value("prices", nlohmann::json::object());
        if (ootPrices.empty() || mmPrices.empty() || !SOH_SetCheckPrices || !MM_SetCheckPrices) {
            std::cerr << "[playthrough] spoiler has no shop prices (predates price export) — regenerate the seed "
                         "to validate it\n";
            return 2;
        }

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
            return ComboRando::RunPlaythrough(passFlat.dump(), oot, mmO, label, MM_Restore, ptOut, sohDump, mmDump);
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
                    if (game == "oot" && ootPrices.contains(chk)) {
                        static const int caps[5] = { 0, 99, 200, 500, 999 };
                        int price = ootPrices[chk].get<int>();
                        if (price > caps[std::min(ootBaseWalletTier + owS, 4)]) {
                            ++bad;
                            std::cerr << "[playthrough] AFFORDABILITY: [OOT] " << chk << " price=" << price << " with "
                                      << owS << " wallet(s) at sphere " << sph.value("sphere", -1) << "\n";
                        }
                    } else if (game == "mm" && mmPrices.contains(chk) &&
                               (chk.find("SHOP_ITEM") != std::string::npos ||
                                chk.find("TINGLE_MAP") != std::string::npos ||
                                chk.find("GORMAN_MILK") != std::string::npos ||
                                chk.find("MILK_BAR") != std::string::npos ||
                                chk.find("CURIOSITY") != std::string::npos)) {
                        int p = mmPrices[chk].get<int>();
                        if (!(p < 100 || (p <= 200 && mwS >= 1) || mwS >= 2)) {
                            ++bad;
                            std::cerr << "[playthrough] AFFORDABILITY: [MM] " << chk << " price=" << p << " with "
                                      << mwS << " wallet(s) at sphere " << sph.value("sphere", -1) << "\n";
                        }
                    }
                    if (item == "Progressive Wallet")
                        ++ow;
                    if (item == "RI_PROGRESSIVE_WALLET")
                        ++mw;
                }
            }
            return bad;
        };
        // Names the blocking win side from full-inventory reachability, so "stuck" is exact.
        auto blocked = [](const ComboRando::PlaythroughResult& r) -> const char* {
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
                  << " enabled trick(s))\n";
        nlohmann::json pt1 = nlohmann::json::array();
        auto r1 = runPass(false, &pt1);
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
        // No Logic: tolerate OOT/Ganon unbeatability, but still hard-require Majora reachable (MM's win
        // chain intact). MM-advancement reachability is guaranteed by the GENERATOR (the fill fails/retries
        // on mmAdvUnreachable>0 in every mode); this verdict only re-confirms the Majora win-chain — it does
        // NOT gate on unreachableMm==0, since every seed has ~1 under-modeled MM junk check (oracle
        // evaluates MM with zeroed save options), present even under ALL_REACHABLE.
        if (ootNoLogic && r2.majoraReachable) {
            std::cout << "[playthrough] RESULT: PASS (No Logic) — Majora beatable (MM reachable); OOT/Ganon "
                         "unbeatability tolerated ("
                      << blocked(r2) << ").\n";
            return 0;
        }
        std::cout << "[playthrough] RESULT: FAIL — not beatable even with all tricks; stuck: " << blocked(r2)
                  << " (OOT unreachable=" << r2.unreachableOot << ", MM unreachable=" << r2.unreachableMm
                  << "; see saves/combo/slot0.playthrough.txt).\n";
        return 1;
    }

    // ---- Generate + validate mode. ----
    std::cout << "[comborando] validating " << count << " seed(s) from '" << seed << "'\n";
    int failures = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < count; ++i) {
        const uint32_t base = ComboHash(seed) + static_cast<uint32_t>(i);
        uint32_t masterSeed = base;
        std::string sohDump, mmDump, sohHintDump;
        ComboRando::CombinedFillResult r{};
        ComboRando::RequirednessResult pareDownResult; // cross-hint verification (headless mirror of RunComboFill)
        // Whole-fill retries with re-derived seeds, identical to RunComboFill (GAP-4) so a headless
        // seed reproduces the in-game one even when early attempts fail.
        for (int attempt = 0; attempt < 5; ++attempt) {
            masterSeed = base + attempt * 0x9E3779B9u;
            if (SOH_SetSeed)
                SOH_SetSeed(masterSeed);
            if (MM_SetSeed)
                MM_SetSeed(masterSeed);
            sohDump = SOH_Dump();
            mmDump = MM_Dump();
            std::string forced = SOH_GetForced ? SOH_GetForced(masterSeed) : "";
            ComboRando::OotAccess ootAccess = ComboRando::OotAccessFromDump(sohDump);
            r = ComboRando::CrossWorldCombinedFill(sohDump, mmDump, masterSeed, oot, mmO, "", nullptr, forced,
                                                   ootAccess);
            if (r.success) {
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
                if (ComboRando::NeedsRequirednessPareDown(sohHintDump, mmDump))
                    // NO_LOGIC: gate requiredness on MM only (OOT may be unbeatable by design).
                    pareDownResult = ComboRando::PareDownPlaythrough(
                        r.spoilerJson, oot, mmO, nullptr, sohDump, mmDump, ootAreas, mmAreas,
                        ootAccess == ComboRando::OotAccess::NO_LOGIC ? ComboRando::MmOnlyMajoraGoal
                                                                     : ComboRando::DefaultGanonMajoraGoal);
                else
                    std::cout << "[comborando]   pare-down skipped (no enabled hint surface needs requiredness)\n";
            }
            if (MM_Restore)
                MM_Restore();
            if (r.success)
                break;
            std::cerr << "[comborando]   attempt " << (attempt + 1) << "/5 failed: " << r.error << "\n";
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
                    nlohmann::json consolidated;
                    consolidated["fileType"] = "ComboShipRandomizer";
                    consolidated["seed"] = seed;
                    consolidated["masterSeed"] = masterSeed;
                    consolidated["oot"] = { { "settings", nlohmann::json::parse(SOH_DumpSettings()) },
                                            { "enabledTricks", SOH_DumpEnabledTricks
                                                                   ? nlohmann::json::parse(SOH_DumpEnabledTricks())
                                                                   : nlohmann::json::array() },
                                            { "placements", fillSpoiler.value("oot", nlohmann::json::object()) },
                                            { "prices", pricesOf(sohDump) } };
                    consolidated["mm"] = { { "settings", nlohmann::json::parse(MM_DumpSettings()) },
                                           { "placements", fillSpoiler.value("mm", nlohmann::json::object()) },
                                           { "prices", pricesOf(mmDump) } };
                    // ComboShip: enrich the foreign array exactly like RunComboFill (displayName game
                    // suffix + oot checkArea) so the headless consolidated file matches the in-game one.
                    std::unordered_map<std::string, std::string> ootCheckAreas;
                    try {
                        auto hd = nlohmann::json::parse(sohHintDump.empty() ? "{}" : sohHintDump);
                        for (auto& c : hd.value("checks", nlohmann::json::array())) {
                            std::string name = c.value("name", ""), area = c.value("area", "");
                            if (!name.empty() && !area.empty())
                                ootCheckAreas.emplace(std::move(name), std::move(area));
                        }
                    } catch (...) {}
                    consolidated["foreign"] = ComboRando::BuildForeignArray(
                        fillSpoiler.value("foreign", nlohmann::json::array()), ootCheckAreas);
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
