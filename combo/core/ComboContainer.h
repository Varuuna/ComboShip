// ComboShip: the merged per-slot save container (Save/file{N+1}.combosav). One JSON file per slot
// holds both games' saves verbatim plus combo metadata; the launcher mediates every per-slot read and
// write so the in-process cache stays authoritative. See docs/deviations/boot-shutdown.md.
#pragma once

#include <mutex>
#include <string>
#include <nlohmann/json.hpp>

#include "rando/CrossForeign.h"

extern std::mutex g_containerMutex;

// Owned by the launcher; moves to ComboTransition when that module lands.
extern int g_MmSaveInMemorySlot;

bool ComboIsValidSlot(int fileNum);
int Combo_TakeEvictionNotice();
// Storage only: the caller notifies the DLLs afterwards (this module cannot — see below).
void ComboEraseSlotStorage(int slot);
void ComboCopySlotStorage(int from, int to);
const char* Combo_ReadGameSave(int game, int fileNum);
void Combo_WriteGameSave(int game, int fileNum, const char* json);
void Combo_SetLastGame(int fileNum, int game);
int Combo_GetLastGame(int fileNum);
const char* Combo_GetNotes(int fileNum);
void Combo_SetNotes(int fileNum, const char* text);

// Typed slot accessors. Each takes the lock, touches JSON, and returns — none of them can call into a
// game DLL, because this module never sees ComboDllApi.h. That is what keeps "release the lock before
// calling a DLL" structural instead of a convention spread across callers.

// Seed-bound goal + completion state for one slot (combo.completion + combo.rando.goal).
struct ComboSlotGoalState {
    bool ootDone = false;
    bool mmDone = false;
    bool triforceDone = false;
    bool hunt = false;
    int required = 0;
    int total = -1; // -1 = seed predates the combo-owned total
    bool startingGameMM = false;
};
ComboSlotGoalState ComboReadGoalState(int slot);
void ComboWriteCompletion(int slot, bool oot, bool mm, bool triforce);

// combo.rando.hints + combo.hintsRead, already serialized for the comboui push.
struct ComboHintSlice {
    std::string hints;
    std::string read;
};
ComboHintSlice ComboReadHintSlice(int slot);

// Set-semantics insert into combo.hintsRead[bucket]; true if it inserted (and flushed).
// matchField (object values only) compares just that member, so a varying sibling — an MM trap check's
// re-rolled disguise text — can't add a duplicate per talk. First write wins.
bool ComboInsertHintRead(int slot, const char* bucket, const nlohmann::json& value, const char* matchField);

// A fresh file: empty note (written, never erased — an absent key is the "never migrated" sentinel)
// and no inherited hint read state.
void ComboResetSlotForNewFile(int slot);

// Bake the consolidated seed into combo.rando and drop any prior completion (a rebaked slot is a NEW
// seed, so a finished hunt must not carry over).
void ComboBakeSeed(int slot, const nlohmann::json& seed);

// The slot's baked combo.rando, dumped; empty when absent.
std::string ComboReadBakedRando(int slot);
