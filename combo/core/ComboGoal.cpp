#include "core/ComboGoal.h"

#include <iostream>

#include "core/ComboAnchorNet.h"
#include "core/ComboContainer.h"
#include "core/ComboDllApi.h"
#include "core/ComboHintReveal.h"
#include "rando/CrossWorldRando.h"

bool g_comboCompletion[2] = { false, false };
int g_comboCompletionSlot = -1;
// Active goal for the loaded slot (0 required = the both-bosses goal) + the one-shot completion latch.
bool g_goalHunt = false;
int g_goalRequired = 0;
int g_goalTotal = -1; // combined pieces the seed places; -1 = seed predates the combo-owned total
bool g_comboTriforceDone = false;
// Starting game of the LOADED slot (seed-bound, like g_goalHunt).
bool g_startingGameMM = false;

// Per-slot completion lives in the merged container's combo.completion object. Works in non-rando
// play too. Read on save-load, rewritten on each final-boss kill.
void LoadComboCompletion(int slot) {
    g_comboCompletion[0] = g_comboCompletion[1] = false;
    g_comboCompletionSlot = slot;
    g_comboTriforceDone = false;
    g_goalHunt = false;
    g_goalRequired = 0;
    g_goalTotal = -1;
    g_startingGameMM = false;
    const ComboSlotGoalState st = ComboReadGoalState(slot);
    g_comboCompletion[0] = st.ootDone;
    g_comboCompletion[1] = st.mmDone;
    g_comboTriforceDone = st.triforceDone;
    g_goalHunt = st.hunt;
    g_goalRequired = st.required;
    g_goalTotal = st.total;
    g_startingGameMM = st.startingGameMM;
    // Push outside the container lock — the DLL setters must never re-enter the sidecar.
    if (SOH_SetComboGoal)
        SOH_SetComboGoal(g_goalHunt ? 1 : 0, g_goalRequired, ComboRando::CwOotPieces(g_goalTotal));
    if (MM_SetComboGoal)
        MM_SetComboGoal(g_goalHunt ? 1 : 0, g_goalRequired, ComboRando::CwMmPieces(g_goalTotal));
    if (SOH_SetComboStartingGame)
        SOH_SetComboStartingGame(g_startingGameMM ? 1 : 0);
    if (ComboUI_SetComboComplete)
        ComboUI_SetComboComplete((g_comboCompletion[0] && g_comboCompletion[1]) ? 1 : 0);
}

// Generation pushes the MENU goal into both DLLs. If a slot is loaded, put its own (seed-bound) goal
// back afterwards so the DLL-side globals keep describing the loaded seed.
void RestoreLoadedSlotGoal() {
    if (g_comboCompletionSlot < 0)
        return;
    if (SOH_SetComboGoal)
        SOH_SetComboGoal(g_goalHunt ? 1 : 0, g_goalRequired, ComboRando::CwOotPieces(g_goalTotal));
    if (MM_SetComboGoal)
        MM_SetComboGoal(g_goalHunt ? 1 : 0, g_goalRequired, ComboRando::CwMmPieces(g_goalTotal));
    if (SOH_SetComboStartingGame)
        SOH_SetComboStartingGame(g_startingGameMM ? 1 : 0);
}

void SaveComboCompletion(int slot) {
    ComboWriteCompletion(slot, g_comboCompletion[0], g_comboCompletion[1], g_comboTriforceDone);
    // #173: tints the timer overlay's total green. Pushed outside the container lock — comboui must
    // never re-enter the sidecar.
    if (ComboUI_SetComboComplete)
        ComboUI_SetComboComplete((g_comboCompletion[0] && g_comboCompletion[1]) ? 1 : 0);
}

// Registered into both games: record THIS game's final-boss kill for its slot and return 1 iff BOTH
// games' bosses are now dead. game/fileNum use the GameId convention (0=OOT, 1=MM).
int Combo_OnFinalBossDefeated(int game, int fileNum) {
    if ((game != 0 && game != 1) || !ComboIsValidSlot(fileNum))
        return 0;
    if (fileNum != g_comboCompletionSlot)
        LoadComboCompletion(fileNum);
    // The OOT death cutscene re-enters this every frame during the fade; persist + log only on the
    // first report for this slot so we don't thrash the sidecar. Repeats just return the cached answer.
    if (!g_comboCompletion[game]) {
        g_comboCompletion[game] = true;
        SaveComboCompletion(fileNum);
        std::cout << "[ComboShip] Final boss defeated: game=" << game << " slot=" << fileNum
                  << " both=" << (g_comboCompletion[0] && g_comboCompletion[1]) << std::endl;
    }
    return (g_comboCompletion[0] && g_comboCompletion[1]) ? 1 : 0;
}

// ComboShip (#136): each game's piece-count getter, handed to the OTHER game so its pickup messages
// and hints can show combined progress.
int Combo_GetOotTriforceCount() {
    return SOH_GetTriforcePieceCount ? SOH_GetTriforcePieceCount() : 0;
}

int Combo_GetMmTriforceCount() {
    return MM_GetTriforcePieceCount ? MM_GetTriforcePieceCount() : 0;
}

// Poked after every Triforce Piece grant (own or dormant) and every Anchor team-state merge: sums both
// games' counters and, on the first crossing, latches completion and rolls the ending. game/fileNum use
// the GameId convention (0 = OOT, 1 = MM). No exception may cross the C-ABI boundary.
void Combo_OnTriforceProgress(int game, int fileNum) try {
    if ((game != 0 && game != 1) || !ComboIsValidSlot(fileNum))
        return; // Anchor pokes carry fileNum 0xFF at the file-select — no slot, nothing to evaluate
    if (fileNum != g_comboCompletionSlot)
        LoadComboCompletion(fileNum);
    if (!g_goalHunt || g_goalRequired <= 0 || g_comboTriforceDone)
        return;
    const int total = Combo_GetOotTriforceCount() + Combo_GetMmTriforceCount();
    if (total < g_goalRequired)
        return;
    g_comboTriforceDone = true;
    g_comboCompletion[0] = g_comboCompletion[1] = true;
    SaveComboCompletion(fileNum);
    const bool mmActive = ComboAnchor::ActiveGame() == 1;
    std::cout << "[ComboShip] Triforce Hunt complete: " << total << "/" << g_goalRequired << " slot=" << fileNum
              << " active=" << (mmActive ? "mm" : "oot") << std::endl;
    if (SOH_TriggerTriforceCredits)
        SOH_TriggerTriforceCredits(mmActive ? 1 : 0);
    if (MM_TriggerTriforceCredits)
        MM_TriggerTriforceCredits(mmActive ? 0 : 1);
} catch (const std::exception& e) {
    std::cerr << "[ComboShip] Combo_OnTriforceProgress threw: " << e.what() << std::endl;
} catch (...) { std::cerr << "[ComboShip] Combo_OnTriforceProgress threw a non-std exception" << std::endl; }
