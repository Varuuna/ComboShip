#ifndef RANDO_H
#define RANDO_H

#include "StaticData/StaticData.h"
#include "Types.h"
#include "variables.h"

#define IS_RANDO (gSaveContext.save.shipSaveInfo.saveType == SAVETYPE_RANDO)
#define RANDO_SAVE_CHECKS gSaveContext.save.shipSaveInfo.rando.randoSaveChecks
#define RANDO_SAVE_OPTIONS gSaveContext.save.shipSaveInfo.rando.randoSaveOptions
#define RANDO_EVENTS gSaveContext.save.shipSaveInfo.rando.randoEvents

namespace Rando {

void Init();
void DrawItem(RandoItemId randoItemId, RandoCheckId randoCheckId = RC_UNKNOWN, Actor* actor = nullptr);
void GiveItem(RandoItemId randoItemId);
// ComboShip: a small key lives in TWO counters — inventory.dungeonKeys and the rando mirror that
// logic's KEY_COUNT reads — and both are -1 when fresh. Normalize each sentinel independently before
// bumping, so a pre-existing desync heals instead of leaving the mirror permanently one behind.
inline void AddSmallKey(s32 dungeonSceneIndex) {
    s8& inventory = DUNGEON_KEY_COUNT(dungeonSceneIndex);
    s8& mirror = gSaveContext.save.shipSaveInfo.rando.foundDungeonKeys[dungeonSceneIndex];
    inventory = (s8)((inventory < 0 ? 0 : inventory) + 1);
    mirror = (s8)((mirror < 0 ? 0 : mirror) + 1);
}
// ComboShip: Skeleton Key's per-dungeon small-key maxima, shared with the headless oracle give.
struct SkeletonKeyCount {
    s32 dungeonSceneIndex;
    s8 count;
};
inline constexpr SkeletonKeyCount skeletonKeyCounts[] = {
    { DUNGEON_SCENE_INDEX_WOODFALL_TEMPLE, 1 },
    { DUNGEON_SCENE_INDEX_SNOWHEAD_TEMPLE, 3 },
    { DUNGEON_SCENE_INDEX_GREAT_BAY_TEMPLE, 1 },
    { DUNGEON_SCENE_INDEX_STONE_TOWER_TEMPLE, 4 },
};
#ifdef COMBO_BUILD
// ComboShip: set while GiveItem delivers into a dormant MM save (gPlayState==NULL).
extern bool gComboDormantGive;
#endif
void RemoveItem(RandoItemId randoItemId);
RandoItemId CurrentJunkItem(RandoCheckId randoCheckId = RC_UNKNOWN);
RandoItemId CurrentTrapItem(RandoCheckId randoCheckId = RC_UNKNOWN);
bool IsItemObtainable(RandoItemId randoItemId, RandoCheckId randoCheckId = RC_UNKNOWN);
RandoItemId ConvertItem(RandoItemId randoItemId, RandoCheckId randoCheckId = RC_UNKNOWN);
// Container-matches-contents type for a check. Same as Items[ConvertItem(...)].randoItemType, except a
// ComboShip foreign check resolves the real cross-game item's category behind the junk sentinel.
RandoItemType GetItemTypeForCheck(RandoItemId randoItemId, RandoCheckId randoCheckId = RC_UNKNOWN);
RandoCheckId FindItemPlacement(RandoItemId randoItemId);
// Like GetLocationNameForHint(FindItemPlacement(id)); on ComboShip builds, falls back to the combo
// foreign map when the item was cross-placed into OOT instead of an MM check.
std::string GetItemLocationHintName(RandoItemId randoItemId, bool exact);
void RegisterMenu();

std::vector<RandoItemId> GetComputedStartingItems(RandoSaveInfo& randoSaveInfo);
void GrantStartingItems();
std::vector<RandoItemId> GetStartingItemsFromSpoiler(nlohmann::json& spoiler);
void SetStartingItemsInSpoiler(nlohmann::json& spoiler, std::vector<RandoItemId>& startingItems);
std::vector<RandoItemId> GetStartingItemsFromSave(RandoSaveInfo& randoSaveInfo);
void SetStartingItemsInSave(RandoSaveInfo& randoSaveInfo, std::vector<RandoItemId>& startingItems);
std::vector<RandoItemId> GetStartingItemsFromConfig();
void SetStartingItemsInConfig(std::vector<RandoItemId>& startingItems);

std::vector<RandoItemId> GetDefaultSariaPriorityItems();
std::vector<RandoItemId> GetSariaPriorityItemsFromSpoiler(nlohmann::json& spoiler);
void SetSariaPriorityItemsInSpoiler(nlohmann::json& spoiler, std::vector<RandoItemId>& priorityItems);
std::vector<RandoItemId> GetSariaPriorityItemsFromSave(RandoSaveInfo& randoSaveInfo);
void SetSariaPriorityItemsInSave(RandoSaveInfo& randoSaveInfo, std::vector<RandoItemId>& priorityItems);
std::vector<RandoItemId> GetSariaPriorityItemsFromConfig();
void SetSariaPriorityItemsInConfig(std::vector<RandoItemId>& priorityItems);
std::vector<RandoItemId> GetSariaPriorityItemCandidates();

std::vector<RandoCheckId> GetExcludedChecksFromConfig();
void SetExcludedChecksInConfig(std::vector<RandoCheckId>& excludedChecks);

std::vector<RandoCheckId> FindMultiItemPlacement(RandoItemId randoItemId);

} // namespace Rando

#endif
