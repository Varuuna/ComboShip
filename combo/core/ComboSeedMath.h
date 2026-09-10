// ComboShip: seed derivations shared by ComboShip.exe and comborando. Both must produce identical
// values or a headless-validated seed would not match the one the game generates.
// Header-only on purpose: comborando links nothing but nlohmann_json.
#pragma once

#include <cstdint>
#include <string>

// FNV-1a 32-bit. Ship_Hash/Ship_Random are not exported from libultraship, hence the local copy.
inline uint32_t ComboHash(const char* str) {
    if (!str)
        return 0;
    uint32_t h = 2166136261u;
    while (*str) {
        h ^= static_cast<unsigned char>(*str++);
        h *= 16777619u;
    }
    return h;
}

inline uint32_t ComboHash(const std::string& s) {
    return ComboHash(s.c_str());
}

// #135: resolve the starting-game CVar (0 = OOT, 1 = MM, 2 = Random 50-50 off the master seed).
// The derivation string is part of the seed contract — changing it changes every Random seed.
inline bool ResolveStartingGameMM(int cfg, uint32_t masterSeed) {
    if (cfg == 2)
        return (ComboHash("startingGame:" + std::to_string(masterSeed)) & 1u) != 0;
    return cfg == 1;
}
