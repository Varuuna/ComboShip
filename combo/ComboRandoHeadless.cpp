// combo/ComboRandoHeadless.cpp
// ComboShip: standalone HEADLESS cross-world seed generator/validator. Loads soh.dll + 2ship.dll and
// drives the rando-only headless init (SOH_InitRandoHeadless / MM_InitRandoHeadless) + the cross-world
// fill directly — no game window, ResourceManager, audio, or ComboShip.exe boot.
//
// Usage (run from the game dir, next to soh.dll/2ship.dll):  comborando.exe [--seed <s>] [--count <n>]
// Exit code: 0 = all seeds completable, 1 = a seed failed, 2 = setup error.
#define NOMINMAX // windows.h min/max macros clash with std::min/std::max in CrossWorldRando.h
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

#include "rando/CrossWorldRando.h"

namespace {

typedef void (*FnVoidV)(void);
typedef const char* (*FnDump)(void);
typedef void (*FnSetSeed)(uint64_t);
typedef const char* (*FnGetForced)(uint32_t);
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

} // namespace

int main(int argc, char** argv) {
    std::string seed = "1";
    int count = 1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--seed" && i + 1 < argc)
            seed = argv[++i];
        else if (a == "--count" && i + 1 < argc)
            count = std::max(1, std::atoi(argv[++i]));
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

    if (!SOH_InitRandoHeadless || !MM_InitRandoHeadless || !SOH_Dump || !MM_Dump || !oot.Reset ||
        !oot.SetOwnedItems || !oot.GetReachableChecks || !oot.PlaceItem || !mmO.Reset || !mmO.SetOwnedItems ||
        !mmO.GetReachableChecks || !mmO.PlaceItem) {
        std::cerr << "[comborando] missing required DLL exports — rebuild soh.dll / 2ship.dll\n";
        return 2;
    }

    SOH_InitRandoHeadless();
    MM_InitRandoHeadless();

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
