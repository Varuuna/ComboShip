// combo/rando/CrossWorldRando.h
// ComboShip: no-logic combined spoiler generator (phase 1 — native-only permutation).
// Header-only, pure function, deterministic. No game source touched.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace ComboRando {

// ---------- Deterministic 64-bit LCG (Knuth / Newlib constants) ----------

struct CwRng {
    uint64_t s;
    explicit CwRng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
    uint32_t next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<uint32_t>(s >> 33);
    }
    // Returns a value in [0, n) — caller ensures n > 0.
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

// Fisher-Yates in place (Knuth shuffle), using CwRng for determinism.
template <class T>
inline void cwShuffle(std::vector<T>& v, CwRng& rng) {
    for (size_t i = v.size(); i > 1; --i) {
        size_t j = rng.below(static_cast<uint32_t>(i));
        std::swap(v[i - 1], v[j]);
    }
}

// ---------- FNV-1a hash (used to derive per-game seeds from the master seed) ----------

inline uint64_t cwHash(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

// ---------- Generator ----------

// Returns a combined spoiler JSON string.
// Phase 1: per-game permutation (native-only).
// Each game's vanilla items are shuffled among that same game's checks.
// Cross-placement between games is reserved for a future phase.
//
// Spoiler shape:
// {
//   "masterSeed": <uint32>,
//   "mode": "no-logic native-only (phase1)",
//   "ootCount": <uint32>,   // number of shuffled OOT checks
//   "oot": { "<checkName>": "<assignedItem>", ... },
//   "mmCount": <uint32>,    // number of shuffled MM checks
//   "mm":  { "<checkName>": "<assignedItem>", ... }
// }
// On parse error the relevant game key is omitted and "<game>Error": true is set instead.
inline std::string CrossWorldGenerateSpoiler(const std::string& sohDumpJson,
                                              const std::string& mmDumpJson,
                                              uint32_t masterSeed) {
    nlohmann::json spoiler;
    spoiler["masterSeed"] = masterSeed;
    spoiler["mode"] = "no-logic native-only (phase1)";

    auto doGame = [&](const char* key, const std::string& dumpJson, uint32_t seed) {
        nlohmann::json out = nlohmann::json::object();
        try {
            auto d = nlohmann::json::parse(dumpJson);
            std::vector<std::string> checkNames, vanillaItems;
            for (auto& c : d.value("checks", nlohmann::json::array())) {
                if (!c.contains("vanillaItem")) continue;       // skip checks with no item
                std::string v = c.value("vanillaItem", std::string{});
                if (v.empty()) continue;
                checkNames.push_back(c.value("name", std::string{}));
                vanillaItems.push_back(v);
            }
            std::vector<std::string> shuffled = vanillaItems;   // permutation of the vanilla multiset
            CwRng rng(seed);
            cwShuffle(shuffled, rng);
            for (size_t i = 0; i < checkNames.size(); ++i) {
                out[checkNames[i]] = shuffled[i];
            }
            spoiler[std::string(key) + "Count"] = static_cast<uint32_t>(checkNames.size());
        } catch (...) {
            spoiler[std::string(key) + "Error"] = true;
        }
        spoiler[key] = out;
    };

    // Per-game seeds are XOR-derived from the master seed so OOT and MM diverge
    // even when masterSeed is 0 (the 0-seed guard in CwRng handles 0 for the game seed itself).
    doGame("oot", sohDumpJson, masterSeed ^ 0x4F4F5400u);   // XOR with "OOT\0"
    doGame("mm",  mmDumpJson,  masterSeed ^ 0x4D4D0000u);   // XOR with "MM\0\0"

    return spoiler.dump(2);
}

} // namespace ComboRando
