#include "Rando.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "Rando/ActorBehavior/ActorBehavior.h"
#include "Rando/MiscBehavior/MiscBehavior.h"
#include "Rando/MiscBehavior/ClockShuffle.h"
#include "Rando/Spoiler/Spoiler.h"
#include "Rando/CheckTracker/CheckTracker.h"
#include "2s2h/ShipInit.hpp"
#include <ship/window/FileDropMgr.h>
#include <ship/Context.h>
#ifdef COMBO_BUILD
#include "rando/CrossForeign.h" // ComboShip: family-B foreign-item-location fallback
#endif

// When a save is loaded, we want to unregister all hooks and re-register them if it's a rando save
void OnSaveLoadHandler(s16 fileNum) {
    Rando::MiscBehavior::OnFileLoad();
    Rando::ActorBehavior::OnFileLoad();
    Rando::CheckTracker::OnFileLoad();
    Rando::ClockShuffle::OnFileLoad();

    // Re-initalizes enhancements that are effected by the save being rando or not
    ShipInit::Init("IS_RANDO");
}

// Entry point for the module, run once on game boot
void Rando::Init() {
    Rando::Spoiler::RefreshOptions();
    Rando::MiscBehavior::Init();
    Rando::ActorBehavior::Init();
    Rando::CheckTracker::Init();
    Ship::Context::GetInstance()->GetFileDropMgr()->RegisterDropHandler(Rando::Spoiler::HandleFileDropped);

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSaveLoad>(OnSaveLoadHandler);
}

RandoCheckId Rando::FindItemPlacement(RandoItemId randoItemId) {
    for (auto& [randoCheckId, check] : Rando::StaticData::Checks) {
        if (RANDO_SAVE_CHECKS[randoCheckId].randoItemId == randoItemId) {
            return randoCheckId;
        }
    }

    return RC_UNKNOWN;
}

std::vector<RandoCheckId> Rando::FindMultiItemPlacement(RandoItemId randoItemId) {
    std::vector<RandoCheckId> itemPlacements;
    for (auto& [randocheckId, check] : Rando::StaticData::Checks) {
        if (RANDO_SAVE_CHECKS[randocheckId].randoItemId == randoItemId) {
            itemPlacements.push_back(randocheckId);
        }
    }
    return itemPlacements;
}

std::string Rando::GetItemLocationHintName(RandoItemId randoItemId, bool exact) {
    RandoCheckId rc = FindItemPlacement(randoItemId);
    if (rc != RC_UNKNOWN) {
        return Rando::StaticData::GetLocationNameForHint(rc, exact);
    }
#ifdef COMBO_BUILD
    // ComboShip: this MM item was placed at an OOT check by the combo fill. Try Phase 3's
    // pre-rendered region text first; fall back to Phase 1's raw check-name string for seeds
    // generated before hints.mm.itemLocations existed.
    int slot = gSaveContext.fileNum;
    if (slot != 0xFF) {
        // ComboShip: the consolidated file keys foreign items by the friendly combo-spoiler name.
        const std::string& friendlyName = Rando::StaticData::GetItemDisplayName(randoItemId);
        if (!friendlyName.empty()) {
            static uint64_t s_hintsGen = (uint64_t)-1;
            static ComboRando::MmHints s_hints;
            if (s_hintsGen != Rando::MiscBehavior::ComboRandoGen()) {
                s_hints = ComboRando::LoadHintsMM(slot);
                s_hintsGen = Rando::MiscBehavior::ComboRandoGen();
            }
            auto hintIt = s_hints.itemLocations.find(friendlyName);
            if (hintIt != s_hints.itemLocations.end()) {
                return hintIt->second;
            }

            static uint64_t s_mapGen = (uint64_t)-1;
            static std::unordered_map<std::string, ComboRando::ForeignPlacement> s_map;
            if (s_mapGen != Rando::MiscBehavior::ComboRandoGen()) {
                s_map = ComboRando::LoadForeignByItem(slot, ComboRando::GAME_MM);
                s_mapGen = Rando::MiscBehavior::ComboRandoGen();
            }
            auto it = s_map.find(friendlyName);
            if (it != s_map.end()) {
                return "at " + it->second.checkName + " (OOT)";
            }
        }
    }
#endif
    return "in an Unknown Location";
}
