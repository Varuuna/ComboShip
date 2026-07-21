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
// ComboShip: Returns the ForeignItem metadata for an MM check that holds a foreign OOT item,
// or nullptr if the check is not foreign. Keyed by Checks[].name (RC_*), NOT CheckNames[rc].
const ComboRando::ForeignItem* MM_LookupForeign(RandoCheckId rc);
// ComboShip: deliver a foreign check's item to its home game + persist (caller sets obtained flags).
void SendForeignCheck(RandoCheckId rc);
// ComboShip: whether a foreign check plays the get-item cutscene, mirroring the foreign item's
// home-game importance against the skip-get-item-cutscene setting.
bool ShouldShowForeignCutscene(RandoCheckId rc);
// ComboShip (bug 1): shared post-grant co-op broadcast seam for grant paths outside CheckQueue (e.g.
// shop buys). Broadcasts iff wasObtained is false — cycleObtained wipes every Song of Time, so a
// caller must capture the check's `obtained` flag BEFORE setting it and pass it in here.
void BroadcastCheckObtainedIfFirst(RandoCheckId rc, RandoItemId rawItemId, bool wasObtained);
// ComboShip: consolidated-spoiler cache generation. MM_LoadComboRando bumps it; the foreign/hint
// lookup caches rebuild from the pushed blob when their stored gen is stale.
uint64_t ComboRandoGen();
void InvalidateComboForeignCache();
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
