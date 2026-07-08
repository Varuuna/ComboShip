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
        std::string flatStr = flat.dump();
        std::string label = SeedLabel(spoiler);
        uint32_t masterSeed = spoiler.value("masterSeed", 0u);

        // Apply the seed's settings for one pass, but force real-logic evaluation (ignore the seed's
        // No-Logic/Glitchless flag) and optionally flip every OOT trick on. MM has no tricks; its oracle
        // re-reads CVars on Reset, so MM_RestoreRandoSettings is enough (no prep step).
        auto applyPass = [&](bool allTricks) {
            nlohmann::json os = ootSettings;
            for (auto it = os.begin(); it != os.end(); ++it) {
                if (it.key().find("LogicRules") != std::string::npos)
                    it.value() = 0; // RO_LOGIC_GLITCHLESS — evaluate real gates
                else if (allTricks && it.key().find(".LogicTricks.") != std::string::npos)
                    it.value() = 1; // RO_GENERIC_ON
            }
            SOH_RestoreSettings(os.dump().c_str());
            nlohmann::json ms = mmSettings;
            for (auto it = ms.begin(); it != ms.end(); ++it) {
                if (it.key().find("RO_LOGIC") != std::string::npos)
                    it.value() = 0; // MM RO_LOGIC_GLITCHLESS
            }
            MM_RestoreSettings(ms.dump().c_str());
            // Set up the contexts EXACTLY like the fill does — SOH_PrepContext alone under-initializes OOT
            // (the fill goes through SOH_Dump + seed + confined placement), which otherwise leaves most OOT
            // checks unreachable. Discard the dumps; we only need their side effect on the live contexts.
            if (SOH_SetSeed)
                SOH_SetSeed(masterSeed);
            if (MM_SetSeed)
                MM_SetSeed(masterSeed);
            SOH_Dump();
            MM_Dump();
            return ComboRando::RunPlaythrough(flatStr, oot, mmO, label, MM_Restore);
        };

        std::cout << "[playthrough] seed '" << label << "' — Pass 1 (seed's tricks)\n";
        auto r1 = applyPass(false);
        if (r1.beatable) {
            std::cout << "[playthrough] RESULT: BEATABLE as configured (sphere " << r1.beatableSphere
                      << ") — this seed's settings/tricks can complete it.\n";
            return 0;
        }
        std::cout << "[playthrough] stuck with the seed's tricks — Pass 2 (all tricks)\n";
        auto r2 = applyPass(true);
        if (r2.beatable) {
            std::cout << "[playthrough] RESULT: beatable ONLY with tricks beyond the seed's settings (sphere "
                      << r2.beatableSphere << ").\n";
            return 3;
        }
        std::cout << "[playthrough] RESULT: NOT beatable even with all tricks — likely FACTUALLY IMPOSSIBLE. "
                  << "OOT unreachable=" << r2.unreachableOot << " MM unreachable=" << r2.unreachableMm
                  << " (see saves/combo/slot0.playthrough.txt for the locked checks).\n";
        return 1;
    }

    // ---- Generate + validate mode. ----
    std::cout << "[comborando] validating " << count << " seed(s) from '" << seed << "'\n";
    int failures = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < count; ++i) {
        uint32_t masterSeed = ComboHash(seed) + static_cast<uint32_t>(i);
        if (SOH_SetSeed)
            SOH_SetSeed(masterSeed);
        if (MM_SetSeed)
            MM_SetSeed(masterSeed);
        std::string sohDump = SOH_Dump();
        std::string mmDump = MM_Dump();
        std::string forced = SOH_GetForced ? SOH_GetForced(masterSeed) : "";
        auto r = ComboRando::CrossWorldCombinedFill(sohDump, mmDump, masterSeed, oot, mmO, "", nullptr, forced);
        if (MM_Restore)
            MM_Restore();
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
                    nlohmann::json consolidated;
                    consolidated["fileType"] = "ComboShipRandomizer";
                    consolidated["seed"] = seed;
                    consolidated["masterSeed"] = masterSeed;
                    consolidated["oot"] = { { "settings", nlohmann::json::parse(SOH_DumpSettings()) },
                                            { "placements", fillSpoiler.value("oot", nlohmann::json::object()) } };
                    consolidated["mm"] = { { "settings", nlohmann::json::parse(MM_DumpSettings()) },
                                           { "placements", fillSpoiler.value("mm", nlohmann::json::object()) } };
                    consolidated["foreign"] = fillSpoiler.value("foreign", nlohmann::json::array());
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
