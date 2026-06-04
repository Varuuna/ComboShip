#ifndef RANDO_MISC_BEHAVIOR_H
#define RANDO_MISC_BEHAVIOR_H

#include "Rando/Rando.h"

namespace Rando {

namespace MiscBehavior {

void Init();
void OnFileLoad();

void CheckQueue();
void CheckQueueReset();
#ifdef COMBO_BUILD
void InitCrossMailboxDrain();  // ComboShip: register cross-world mailbox drain hook
#endif
void InitFileSelect();
void InitKaleidoItemPage();
void InitOfferGetItemBehavior();
void BeforeEndOfCycleSave();
void AfterEndOfCycleSave();
void OnFileCreate(s16 fileNum);
void OnFlagSet(FlagType flagType, u32 flag);
void OnSceneFlagSet(s16 sceneId, FlagType flagType, u32 flag);
void OnSceneInit(s16 sceneId, s8 spawnNum);
void OfferTrapItem();
void SariasSongHint();
void BankSignHint();
void InitTycoonWallet();

} // namespace MiscBehavior

} // namespace Rando

#endif
