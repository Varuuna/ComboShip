// ComboShip: the per-attempt setup both real fill paths run before CrossWorldCombinedFill.
//
// ComboShip.exe generates seeds and comborando certifies them (validate-seed.yml), so if these two
// drift the validator blesses a seed nobody can play. They shared this sequence by hand — same order,
// same comments, kept in step by review. This is that sequence, once.
//
// Header-only: comborando links nothing but nlohmann_json, and it resolves its own exports, so the
// DLL entry points come in as hooks rather than through core/ComboDllApi.h.
//
// NOT used by RunComboGenTest / RunComboPlaythrough: those deliberately skip the goal push and the
// Triforce pool check, and folding them in would change what they exercise.
#pragma once

#include <cstdint>
#include <string>

#include "rando/CrossWorldRando.h"

struct CwFillHooks {
    void (*SetSeedOot)(uint64_t) = nullptr;
    void (*SetSeedMm)(uint64_t) = nullptr;
    void (*SetGoalOot)(int, int, int) = nullptr;
    void (*SetGoalMm)(int, int, int) = nullptr;
    void (*SetStartingGame)(int) = nullptr;
    const char* (*DumpOot)() = nullptr;
    const char* (*DumpMm)() = nullptr;
    const char* (*GetForced)(uint32_t) = nullptr;
    int (*ShuffleEntrances)(uint64_t) = nullptr;
};

enum class CwPrologue {
    Ok,
    EmptyDump,     // a game returned no static data — unrecoverable
    TriforceShort, // pool holds fewer pieces than the hunt requires — unrecoverable
    ShuffleFailed, // no valid entrance layout for this seed — caller rerolls
};

struct CwPrologueOut {
    std::string sohDump, mmDump, forcedOot;
    ComboRando::OotAccess ootAccess = ComboRando::OotAccess::ALL_REACHABLE;
    int poolPieces = 0; // combined Triforce pieces actually in the pool (hunt goals only)
};

// Ordering here is load-bearing and matches the sequence both callers used before:
//   seeds -> goal -> starting game -> dumps -> Triforce check -> forced placements -> shuffle -> access
// Seeds precede the dumps because OOT's shop/scrub setup runs inside the dump and is seed-derived;
// goal and starting game precede them because they shape both games' pools; forced placements are
// read before the shuffle, whose ItemReset would wipe the placement they describe.
inline CwPrologue ComboFillPrologue(const CwFillHooks& h, uint32_t masterSeed, bool mmStart,
                                    const ComboRando::CwGoal& goal, CwPrologueOut& out) {
    if (h.SetSeedOot)
        h.SetSeedOot(masterSeed);
    if (h.SetSeedMm)
        h.SetSeedMm(masterSeed);
    if (h.SetGoalOot)
        h.SetGoalOot(goal.hunt ? 1 : 0, goal.required, ComboRando::CwOotPieces(goal.total));
    if (h.SetGoalMm)
        h.SetGoalMm(goal.hunt ? 1 : 0, goal.required, ComboRando::CwMmPieces(goal.total));
    if (h.SetStartingGame)
        h.SetStartingGame(mmStart ? 1 : 0);

    out.sohDump = h.DumpOot ? h.DumpOot() : "";
    out.mmDump = h.DumpMm ? h.DumpMm() : "";
    if (out.sohDump.empty() || out.mmDump.empty())
        return CwPrologue::EmptyDump;

    if (goal.hunt) {
        out.poolPieces = ComboRando::CountPoolTriforcePieces(out.sohDump, out.mmDump);
        if (out.poolPieces < goal.required)
            return CwPrologue::TriforceShort;
    }

    out.forcedOot = h.GetForced ? h.GetForced(masterSeed) : "";

    if (h.ShuffleEntrances && !h.ShuffleEntrances(masterSeed))
        return CwPrologue::ShuffleFailed;

    out.ootAccess = ComboRando::OotAccessFromDump(out.sohDump);
    return CwPrologue::Ok;
}
