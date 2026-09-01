// ComboShip: which game is in front, and the pending-handoff state main()'s switch loop reads.
// One concept, previously spread over six declaration sites.
#pragma once

#include "rando/CrossForeign.h"

// >= 0: OOT wants MM for this slot. main()'s loop clears it to -1 on each OOT entry.
extern int g_PendingMMFileNum;
// MM asked to hand control back.
extern bool g_pendingOOTReturn;
// Why MM handed back: 0 = portal, 1 = Ctrl+R reset, 2 = owl-save quit (see BenPort.cpp).
extern int g_mmReturnKind;
// OOT slot whose MM save is live in MM's dormant memory (-1 = none).
extern int g_MmSaveInMemorySlot;

// The single place the foreground game changes; notifies comboui and pauses/resumes MM's play time.
void Combo_SetForegroundGame(int game);

// Storage + the DLL notifications ComboContainer deliberately cannot make itself.
void EraseComboContainer(int slot);
void Combo_CopyContainer(int from, int to);

void DeleteForeignSaveFromOOT(int slot);
void DeleteForeignSaveFromMM(int slot);

void Combo_ResumeMMIfLastSavedThere(int fileNum);
void Combo_OnOOTSceneSwitch(int fileNum);
void Combo_OnMMReturn(int kind);
