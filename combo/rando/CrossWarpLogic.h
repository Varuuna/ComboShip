// combo/rando/CrossWarpLogic.h
// ComboShip: cross-game teleport-song LOGIC for the combined fill (split, i.e. unshared, copies too).
// MM's copy of an OOT warp song reaches that warp pad in Hyrule; OOT's Song of Soaring reaches any
// activated owl statue in Termina.
//
// NOTE: Neither oracle can see the other game, so each reports what its side can do (GetCrossOut,
// valid right after GetReachableChecks like GetPortalOpen) and the fill feeds that
// to the OTHER oracle as reserved pseudo-owned names.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ComboRando {

// Pseudo-owned names. '@' never starts a real pool item.
inline constexpr const char* kCwMmWarpPrefix = "@combo:mmWarp:";    // + '0'..'5' (OOT warp index) -> OOT oracle
inline constexpr const char* kCwMmOwlActive = "@combo:mmOwlActive"; // MM logic has an activated owl -> OOT oracle
inline constexpr const char* kCwMmStart = "@combo:mmStart";         // #135 MM start -> OOT oracle
inline constexpr const char* kCwOotSoaring = "@combo:ootSoaring";   // OOT can play Soaring -> MM oracle

// OOT oracle input bits from the pseudo names above
enum : uint32_t {
    CW_OOT_IN_WARP_MASK =
        0x3Fu, // bit n: MM can play warp song n (Minuet, Bolero, Serenade, Requiem, Nocturne, Prelude)
    CW_OOT_IN_MM_OWL = 1u << 6,
    CW_OOT_IN_MM_START = 1u << 7,
};
// GetCrossOut bits.
enum : uint32_t {
    CW_OOT_OUT_CAN_SOAR = 1u << 0, // owns Song of Soaring + an ocarina + its three buttons
    CW_MM_OUT_WARP_MASK = 0x3Fu,   // same bit order as CW_OOT_IN_WARP_MASK
    CW_MM_OUT_OWL = 1u << 6,
};

struct CrossWarpLatch {
    uint32_t ootIn = 0;
    bool mmIn = false;
};

// Give each game the other's new teleport-song abilities. True if anything changed: query again.
inline bool ApplyCrossWarps(uint32_t ootOut, uint32_t mmOut, bool mmOpen, bool mmStart,
                            std::vector<std::string>& ootOwned, std::vector<std::string>& mmOwned,
                            CrossWarpLatch& latch) {
    bool changed = false;
    if ((ootOut & CW_OOT_OUT_CAN_SOAR) && !latch.mmIn) {
        mmOwned.push_back(kCwOotSoaring);
        latch.mmIn = true;
        changed = true;
    }
    if (!mmOpen)
        return changed;
    uint32_t want = mmOut & CW_MM_OUT_WARP_MASK;
    if (mmOut & CW_MM_OUT_OWL)
        want |= CW_OOT_IN_MM_OWL;
    if (mmStart)
        want |= CW_OOT_IN_MM_START;
    // The owl / MM-start bits only matter alongside a warp song; hold them back until one exists so a
    // seed without MM warp songs keeps byte-identical owned lists and memo keys.
    if (!((want | latch.ootIn) & CW_OOT_IN_WARP_MASK))
        return changed;
    const uint32_t fresh = want & ~latch.ootIn;
    for (int n = 0; n < 6; ++n)
        if (fresh & (1u << n))
            ootOwned.push_back(std::string(kCwMmWarpPrefix) + static_cast<char>('0' + n));
    if (fresh & CW_OOT_IN_MM_OWL)
        ootOwned.push_back(kCwMmOwlActive);
    if (fresh & CW_OOT_IN_MM_START)
        ootOwned.push_back(kCwMmStart);
    latch.ootIn |= fresh;
    return changed || fresh != 0;
}

// DLL side: map a pseudo name to its OOT input bit (0 = not one of ours).
inline uint32_t CrossWarpOotInBit(const std::string& name) {
    const std::string prefix = kCwMmWarpPrefix;
    if (name.size() == prefix.size() + 1 && name.compare(0, prefix.size(), prefix) == 0) {
        const char c = name.back();
        return (c >= '0' && c <= '5') ? (1u << (c - '0')) : 0u;
    }
    if (name == kCwMmOwlActive)
        return CW_OOT_IN_MM_OWL;
    if (name == kCwMmStart)
        return CW_OOT_IN_MM_START;
    return 0;
}

} // namespace ComboRando
