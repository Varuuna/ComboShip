// ComboShip: the merged per-slot save container (Save/file{N+1}.combosav). One JSON file per slot
// holds both games' saves verbatim plus combo metadata; the launcher mediates every per-slot read and
// write so the in-process cache stays authoritative. See docs/deviations/boot-shutdown.md.
#pragma once

#include <mutex>
#include <string>
#include <nlohmann/json.hpp>

#include "rando/CrossForeign.h"

extern std::mutex g_containerMutex;

bool ComboIsValidSlot(int fileNum);
nlohmann::json& LoadOrCreateContainer(int fileNum);
void FlushContainer(int fileNum);
int Combo_TakeEvictionNotice();
void EraseComboContainer(int slot);
void Combo_CopyContainer(int from, int to);
const char* Combo_ReadGameSave(int game, int fileNum);
void Combo_WriteGameSave(int game, int fileNum, const char* json);
void Combo_SetLastGame(int fileNum, int game);
int Combo_GetLastGame(int fileNum);
const char* Combo_GetNotes(int fileNum);
void Combo_SetNotes(int fileNum, const char* text);
