// ComboShip: the loaded slot's goal and completion state — both-bosses or Triforce Hunt —
// plus the endings each game reports into. See docs/deviations/rando.md.
#pragma once

extern bool g_comboCompletion[2];
extern int g_comboCompletionSlot;
extern bool g_goalHunt;
extern int g_goalRequired;
extern int g_goalTotal;
extern bool g_comboTriforceDone;
extern bool g_startingGameMM;

void LoadComboCompletion(int slot);
void RestoreLoadedSlotGoal();
void SaveComboCompletion(int slot);
int Combo_OnFinalBossDefeated(int game, int fileNum);
int Combo_GetOotTriforceCount();
int Combo_GetMmTriforceCount();
void Combo_OnTriforceProgress(int game, int fileNum);
