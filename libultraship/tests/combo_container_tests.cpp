#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/ComboContainer.h"

// ComboContainer owns every per-slot save read/write. It links here at all only because it names no
// DLL export — that is the same property that keeps it from calling one while holding its mutex.
// Its single external dependency is this one int, which ComboTransition owns in the launcher.
int g_MmSaveInMemorySlot = -1;

namespace {

// The container path is relative ("Save/fileN.combosav"), so each test runs in its own temp CWD.
class ComboContainerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mPrev = std::filesystem::current_path();
        mDir = std::filesystem::temp_directory_path() /
               ("combo_container_test_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "_" +
                std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        std::filesystem::remove_all(mDir);
        std::filesystem::create_directories(mDir);
        std::filesystem::current_path(mDir);
        // The container cache is process-global (one save dir per run, by design). Drop each slot so
        // tests don't inherit the previous one's cached container.
        for (int slot = 0; slot <= 2; ++slot)
            ComboEraseSlotStorage(slot);
    }
    void TearDown() override {
        std::filesystem::current_path(mPrev);
        std::error_code ec;
        std::filesystem::remove_all(mDir, ec);
    }
    static std::filesystem::path SlotPath(int slot) {
        return std::filesystem::path("Save") / ("file" + std::to_string(slot + 1) + ".combosav");
    }
    static void WriteRaw(int slot, const std::string& text) {
        std::filesystem::create_directories("Save");
        std::ofstream(SlotPath(slot)) << text;
    }
    static std::string ReadRaw(int slot) {
        std::ifstream in(SlotPath(slot));
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    std::filesystem::path mPrev, mDir;
};

// The whole reason the merged container exists: one game's write must not disturb the other's
// section. An Anchor-driven MM write during OOT play would otherwise clobber the OOT half.
TEST_F(ComboContainerTest, WritingOneGameLeavesTheOtherSectionIntact) {
    Combo_WriteGameSave(ComboRando::GAME_OOT, 0, R"({"oot":"alpha"})");
    Combo_WriteGameSave(ComboRando::GAME_MM, 0, R"({"mm":"beta"})");

    EXPECT_EQ(std::string(Combo_ReadGameSave(ComboRando::GAME_OOT, 0)), R"({"oot":"alpha"})");
    EXPECT_EQ(std::string(Combo_ReadGameSave(ComboRando::GAME_MM, 0)), R"({"mm":"beta"})");
}

TEST_F(ComboContainerTest, SentinelSlotsNeverCreateAContainer) {
    // 0xFF = no save loaded, 0xFE = Boss Rush; -1 turns up on unbound paths.
    for (int slot : { 255, 254, -1 }) {
        Combo_WriteGameSave(ComboRando::GAME_OOT, slot, R"({"x":1})");
        EXPECT_STREQ(Combo_ReadGameSave(ComboRando::GAME_OOT, slot), "");
    }
    EXPECT_FALSE(std::filesystem::exists("Save"));
}

TEST_F(ComboContainerTest, UnparseableContainerIsSetAsideNotOverwritten) {
    WriteRaw(0, "{ this is not json");
    Combo_WriteGameSave(ComboRando::GAME_OOT, 0, R"({"fresh":true})");

    bool sawCorrupt = false;
    for (auto& e : std::filesystem::directory_iterator("Save")) {
        if (e.path().filename().string().find(".corrupt-") != std::string::npos)
            sawCorrupt = true;
    }
    EXPECT_TRUE(sawCorrupt) << "a container that fails to parse must be renamed aside, never dropped";
    EXPECT_EQ(std::string(Combo_ReadGameSave(ComboRando::GAME_OOT, 0)), R"({"fresh":true})");
}

TEST_F(ComboContainerTest, NotesRoundTripAndAreSlotScoped) {
    Combo_SetNotes(0, "slot zero");
    Combo_SetNotes(1, "slot one");
    EXPECT_STREQ(Combo_GetNotes(0), "slot zero");
    EXPECT_STREQ(Combo_GetNotes(1), "slot one");
}

// Debounced editing calls SetNotes on every keystroke pause; an unchanged value must not rewrite.
TEST_F(ComboContainerTest, UnchangedNotesDoNotRewriteTheFile) {
    Combo_SetNotes(0, "steady");
    const std::string before = ReadRaw(0);
    Combo_SetNotes(0, "steady");
    EXPECT_EQ(ReadRaw(0), before);
}

TEST_F(ComboContainerTest, LastGameDefaultsToOotAndRoundTrips) {
    EXPECT_EQ(Combo_GetLastGame(0), ComboRando::GAME_OOT); // absent key => OOT
    Combo_SetLastGame(0, ComboRando::GAME_MM);
    EXPECT_EQ(Combo_GetLastGame(0), ComboRando::GAME_MM);
}

TEST_F(ComboContainerTest, CompletionAndGoalRoundTrip) {
    ComboBakeSeed(0, nlohmann::json{ { "goal", { { "type", "triforceHunt" }, { "requiredPieces", 20 },
                                                 { "totalPieces", 30 } } },
                                     { "startingGame", "MM" } });
    ComboWriteCompletion(0, true, false, false);

    const ComboSlotGoalState s = ComboReadGoalState(0);
    EXPECT_TRUE(s.ootDone);
    EXPECT_FALSE(s.mmDone);
    EXPECT_TRUE(s.hunt);
    EXPECT_EQ(s.required, 20);
    EXPECT_EQ(s.total, 30);
    EXPECT_TRUE(s.startingGameMM);
}

// A rebaked slot is a NEW seed: a finished prior hunt must not instantly complete it.
TEST_F(ComboContainerTest, BakingASeedClearsPriorCompletion) {
    ComboWriteCompletion(0, true, true, true);
    ComboBakeSeed(0, nlohmann::json{ { "goal", { { "type", "bosses" } } } });

    const ComboSlotGoalState s = ComboReadGoalState(0);
    EXPECT_FALSE(s.ootDone);
    EXPECT_FALSE(s.mmDone);
    EXPECT_FALSE(s.triforceDone);
}

TEST_F(ComboContainerTest, ReadBakedRandoIsEmptyUntilSeedIsBaked) {
    EXPECT_TRUE(ComboReadBakedRando(0).empty());
    ComboBakeSeed(0, nlohmann::json{ { "marker", 7 } });
    EXPECT_NE(ComboReadBakedRando(0).find("\"marker\""), std::string::npos);
}

TEST_F(ComboContainerTest, HintReadInsertIsASetAndFlushes) {
    const nlohmann::json v{ { "check", "Kokiri Sword Chest" } };
    EXPECT_TRUE(ComboInsertHintRead(0, "gossip", v, nullptr));
    EXPECT_FALSE(ComboInsertHintRead(0, "gossip", v, nullptr)) << "duplicate must not insert twice";
    EXPECT_NE(ReadRaw(0).find("Kokiri Sword Chest"), std::string::npos) << "an insert must flush";
}

// matchField exists for MM trap checks whose disguise text is re-rolled per talk: only the named
// member is compared, so a varying sibling cannot add a second entry. First write wins.
TEST_F(ComboContainerTest, HintReadMatchFieldCollapsesVaryingSiblings) {
    EXPECT_TRUE(ComboInsertHintRead(0, "checks",
                                    { { "check", "Woodfall Chest" }, { "text", "a Deku Nut" } }, "check"));
    EXPECT_FALSE(ComboInsertHintRead(0, "checks",
                                     { { "check", "Woodfall Chest" }, { "text", "a Bombchu" } }, "check"));
    EXPECT_NE(ReadRaw(0).find("a Deku Nut"), std::string::npos) << "first write wins";
    EXPECT_EQ(ReadRaw(0).find("a Bombchu"), std::string::npos);
}

TEST_F(ComboContainerTest, ResetForNewFileClearsHintsButKeepsAnEmptyNote) {
    Combo_SetNotes(0, "carried over");
    ComboInsertHintRead(0, "gossip", { { "check", "x" } }, nullptr);

    ComboResetSlotForNewFile(0);

    EXPECT_STREQ(Combo_GetNotes(0), "");
    EXPECT_EQ(ComboReadHintSlice(0).read, "{}");
    // The note key must still EXIST — an absent key is the "never migrated" sentinel.
    EXPECT_NE(ReadRaw(0).find("\"notes\""), std::string::npos);
}

// Save compat is gated on major.minor ONLY: a patch release must never retire saves.
TEST_F(ComboContainerTest, PatchReleaseDifferenceKeepsTheSave) {
    WriteRaw(0, R"({"comboVersion":1,"comboRelease":"0.3.99","slot":0,"oot":{"keep":"me"},"mm":null,"combo":{}})");
    EXPECT_EQ(std::string(Combo_ReadGameSave(ComboRando::GAME_OOT, 0)), R"({"keep":"me"})");
    EXPECT_EQ(Combo_TakeEvictionNotice(), -1) << "a patch difference must not evict";
}

TEST_F(ComboContainerTest, MinorReleaseDifferenceEvictsAndReportsTheSlot) {
    WriteRaw(1, R"({"comboVersion":1,"comboRelease":"0.2.0","slot":1,"oot":{"old":"seed"},"mm":null,"combo":{}})");
    EXPECT_STREQ(Combo_ReadGameSave(ComboRando::GAME_OOT, 1), "") << "outdated container starts fresh";

    bool sawBak = false;
    for (auto& e : std::filesystem::directory_iterator("Save")) {
        if (e.path().extension() == ".bak")
            sawBak = true;
    }
    EXPECT_TRUE(sawBak) << "the outdated container must be kept aside, not deleted";
    EXPECT_EQ(Combo_TakeEvictionNotice(), 1) << "the slot is reported once so OOT can pop the notice";
    EXPECT_EQ(Combo_TakeEvictionNotice(), -1) << "and only once";
}

TEST_F(ComboContainerTest, EraseRemovesTheSlotFile) {
    Combo_WriteGameSave(ComboRando::GAME_OOT, 0, R"({"a":1})");
    ASSERT_TRUE(std::filesystem::exists(SlotPath(0)));
    ComboEraseSlotStorage(0);
    EXPECT_FALSE(std::filesystem::exists(SlotPath(0)));
}

TEST_F(ComboContainerTest, CopyDuplicatesBothGameSections) {
    Combo_WriteGameSave(ComboRando::GAME_OOT, 0, R"({"oot":"src"})");
    Combo_WriteGameSave(ComboRando::GAME_MM, 0, R"({"mm":"src"})");

    ComboCopySlotStorage(0, 1);

    EXPECT_EQ(std::string(Combo_ReadGameSave(ComboRando::GAME_OOT, 1)), R"({"oot":"src"})");
    EXPECT_EQ(std::string(Combo_ReadGameSave(ComboRando::GAME_MM, 1)), R"({"mm":"src"})");
}

} // namespace
