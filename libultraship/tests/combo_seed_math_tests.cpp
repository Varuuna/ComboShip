#include <gtest/gtest.h>
#include <string>

#include "ComboSeedMath.h"

// ComboHash and ResolveStartingGameMM are the derivations ComboShip.exe and comborando share. If they
// ever diverge, a headless-validated seed stops matching the one the game generates — so these lock in
// the exact values, not just "both sides call the same function".

TEST(ComboSeedMath, FnvOffsetBasisForEmptyString) {
    EXPECT_EQ(ComboHash(""), 2166136261u); // FNV-1a 32-bit offset basis
}

TEST(ComboSeedMath, NullPointerHashesToZero) {
    // The headless copy had no null guard; the launcher's did. Both now take this path.
    EXPECT_EQ(ComboHash(static_cast<const char*>(nullptr)), 0u);
}

TEST(ComboSeedMath, KnownFnv1aValues) {
    EXPECT_EQ(ComboHash("a"), 0xE40C292Cu);
    EXPECT_EQ(ComboHash("foobar"), 0xBF9CF968u);
}

TEST(ComboSeedMath, StringAndCharPointerOverloadsAgree) {
    for (const char* s : { "", "a", "combo", "startingGame:12345", "Zelda" }) {
        EXPECT_EQ(ComboHash(s), ComboHash(std::string(s))) << "diverged on: " << s;
    }
}

TEST(ComboSeedMath, StartingGameHonoursExplicitConfig) {
    for (uint32_t seed : { 0u, 1u, 42u, 0xFFFFFFFFu }) {
        EXPECT_FALSE(ResolveStartingGameMM(0, seed)); // 0 = OOT
        EXPECT_TRUE(ResolveStartingGameMM(1, seed));  // 1 = MM
    }
}

TEST(ComboSeedMath, RandomStartingGameMatchesItsDerivationString) {
    // The derivation string is part of the seed contract: changing it rerolls every Random seed.
    for (uint32_t seed : { 0u, 1u, 7u, 12345u, 0x9E3779B9u }) {
        const bool expected = (ComboHash("startingGame:" + std::to_string(seed)) & 1u) != 0;
        EXPECT_EQ(ResolveStartingGameMM(2, seed), expected) << "seed " << seed;
    }
}
