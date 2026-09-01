#include "core/ComboTransition.h"

#include <iostream>

#include "core/ComboContainer.h"
#include "core/ComboDllApi.h"
#include "core/ComboGoal.h"
#include "core/ComboHintReveal.h"
#include "core/ComboSeedState.h"

// OOT slot whose MM save is live in MM's dormant memory (-1 = none). Guards Combo_OnOOTSaveLoad
// against reloading stale disk state over MM's in-memory progress after round trips.
int g_MmSaveInMemorySlot = -1;
int g_PendingMMFileNum = -1;
bool g_pendingOOTReturn = false;

// Single place the foreground game changes, so every transition point notifies comboui consistently.
void Combo_SetForegroundGame(int game) {
    // #173: MM only accrues play time while it is foreground. OOT owns the foreground at startup.
    static int sPrevGame = ComboRando::GAME_OOT;
    if (sPrevGame == ComboRando::GAME_MM && MM_ComboPausePlaytime)
        MM_ComboPausePlaytime();
    if (game == ComboRando::GAME_MM && MM_ComboResumePlaytime)
        MM_ComboResumePlaytime();
    sPrevGame = game;
    if (ComboUI_OnForegroundGame)
        ComboUI_OnForegroundGame(game);
}

// Erase/copy the slot's container, then tell the DLLs. ComboContainer owns the storage but cannot make
// these calls itself (it never sees the export table), which is what keeps them outside the lock.
void EraseComboContainer(int slot) {
    ComboEraseSlotStorage(slot);
    // #182: MM caches which slot's owl save its gSaveContext came from; that section is gone.
    if (MM_InvalidateOwlBlobSlot)
        MM_InvalidateOwlBlobSlot();
    // #164: the Hint Tracker would otherwise keep showing the deleted slot's hints on file-select.
    if (ComboUI_SetHintTrackerData)
        ComboUI_SetHintTrackerData(-1, "", "");
}

// OOT file-select "copy file": the .combosav has no per-game file to copy, so the launcher owns it.
void Combo_CopyContainer(int from, int to) {
    ComboCopySlotStorage(from, to);
    // #182: the destination's owl save came from the donor, so MM's descent cache is wrong.
    if (MM_InvalidateOwlBlobSlot)
        MM_InvalidateOwlBlobSlot();
}

// ComboShip (issue #1): erasing a slot from either game's file-select wipes BOTH saves — each game
// fires its Set*-registered callback with the slot, the launcher routes it to the other game's
// save-only delete export (never re-entering a menu erase path). See docs/deviations/boot-shutdown.md.

// Registered into each game; invoked when that game erases a slot. Routes the (0-based) slot to the
// OTHER game's delete export, then removes the merged container. The launcher does no index math —
// MM's 1-based JSON naming is handled inside MM_DeleteSaveFile.
void DeleteForeignSaveFromOOT(int slot) {
    if (MM_DeleteSaveFile)
        MM_DeleteSaveFile(slot);
    EraseComboContainer(slot); // remove the slot's merged container (baked rando + both saves)
}
void DeleteForeignSaveFromMM(int slot) {
    if (SOH_DeleteSaveFile)
        SOH_DeleteSaveFile(slot);
    EraseComboContainer(slot);
}

// ComboShip (#89): resume MM instead of starting OOT when the slot was last played in MM. The
// file-select gate is load-bearing — OnLoadGame also fires on the MM->OOT return and from in-game
// reloads, where this would bounce the player back into MM forever.
void Combo_ResumeMMIfLastSavedThere(int fileNum) {
    if (!SOH_ParkForComboMMResume || !MM_RunGame || !SOH_IsOnFileSelect || !SOH_IsOnFileSelect()) {
        return;
    }
    if (fileNum < 0 || fileNum > 2) {
        return; // debug select (0xFF) / Boss Rush (0xFE) share FileChoose_LoadGame
    }
    if (Combo_GetLastGame(fileNum) != ComboRando::GAME_MM) {
        return;
    }
    std::cout << "[ComboShip] Slot " << fileNum << " was last saved in MM — resuming MM" << std::endl;
    if (MM_SetComboEntryIsResume)
        MM_SetComboEntryIsResume(1); // a real save load: honors MM's Remember Save Location
    g_PendingMMFileNum = fileNum;
    SOH_ParkForComboMMResume(); // drops out of OOT's game loop; the launcher then enters MM
}

void Combo_OnOOTSceneSwitch(int fileNum) {
    std::cout << "[ComboShip] Mask Shop entered — switching to MM, slot " << fileNum << std::endl;
    if (MM_SetComboEntryIsResume)
        MM_SetComboEntryIsResume(0); // portal entry: always arrives in South Clock Town
    g_PendingMMFileNum = fileNum;
    // OOT game loop is already exiting (gGameState->running = false set by the hook).
}

// Why MM handed control back: 0 = portal, 1 = Ctrl+R reset, 2 = owl-save quit (see BenPort.cpp).
int g_mmReturnKind = 0;

void Combo_OnMMReturn(int kind) {
    g_mmReturnKind = kind;
    std::cout << "[ComboShip] MM returning to OOT (kind=" << kind << ")" << std::endl;
    g_pendingOOTReturn = true;
}
