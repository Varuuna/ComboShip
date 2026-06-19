#ifndef RANDO_MISC_BEHAVIOR_H
#define RANDO_MISC_BEHAVIOR_H

#include "Rando/Rando.h"
#ifdef COMBO_BUILD
#include "rando/CrossForeign.h" // ComboShip: for MM_LookupForeign return type
#endif

namespace Rando {

namespace MiscBehavior {

void Init();
void OnFileLoad();

void CheckQueue();
void CheckQueueReset();
#ifdef COMBO_BUILD
void InitCrossMailboxDrain(); // ComboShip: register cross-world mailbox drain hook
// ComboShip: Returns the ForeignItem metadata for an MM check that holds a foreign OOT item,
// or nullptr if the check is not foreign. Keyed by Checks[].name (RC_*), NOT CheckNames[rc].
const ComboRando::ForeignItem* MM_LookupForeign(RandoCheckId rc);
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
