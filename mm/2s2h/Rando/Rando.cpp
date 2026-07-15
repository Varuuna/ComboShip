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
    // ComboShip: this MM item was placed at an OOT check by the combo fill; look it up by item name
    // instead of check name. V1: raw check name, not full location-hint clarity text.
    int slot = gSaveContext.fileNum;
    if (slot != 0xFF) {
        static int s_slot = -1;
        static std::unordered_map<std::string, ComboRando::ForeignPlacement> s_map;
        if (slot != s_slot || s_map.empty()) {
            s_map = ComboRando::LoadForeignByItem(slot, ComboRando::GAME_MM);
            s_slot = slot;
        }
        const char* spoilerName = Rando::StaticData::Items[randoItemId].spoilerName;
        if (spoilerName && spoilerName[0] != '\0') {
            auto it = s_map.find(spoilerName);
            if (it != s_map.end()) {
                return "at " + it->second.checkName + " (OOT)";
            }
        }
    }
#endif
    return "in an Unknown Location";
}
