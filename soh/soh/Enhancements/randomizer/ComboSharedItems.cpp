// ComboShip: combo-owned file that upstream does not have — do NOT delete on upstream merges (see
// docs/UPSTREAM_MERGES.md). Shared cross-game items, OOT side (rando/CrossShared.h,
// docs/CROSS_ITEMS_PLAN.md): the "give both" hook that hands a shared pair's MM half to MM's resident
// save when its OOT half is collected locally. Self-registers its OnItemReceive hook so no upstream
// function needs an edit.
#include "soh/Enhancements/randomizer/hook_handlers.h"
#ifdef COMBO_BUILD

#include "rando/CrossShared.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Notification/Notification.h"
#include "soh/ShipInit.hpp"
#include <spdlog/spdlog.h>

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

// Launcher seam and Anchor share, both defined elsewhere in soh (OTRGlobals.cpp / Anchor.cpp).
extern "C" void (*gComboCrossDeliver)(int targetGame, const char* itemName, const char* srcCheckName);
extern "C" void Anchor_BroadcastCrossItem(int targetGame, const char* itemName, const char* srcCheckName);

// Shared settings of the pushed seed, rebuilt when SOH_LoadComboRando bumps the foreign-map generation.
static const ComboRando::CwSharedSettings& ComboSharedSettings() {
    static uint64_t sGen = (uint64_t)-1;
    static ComboRando::CwSharedSettings sShared;
    if (sGen != OOT_ForeignMapGen()) {
        sShared = ComboRando::LoadSharedSettingsFromBlob();
        sGen = OOT_ForeignMapGen();
    }
    return sShared;
}

// After a LOCAL collection of check rc whose placed item is a shared pair's OOT half, hand the MM half to
// MM's resident save through the launcher seam and share it with teammates. The OOT half was already
// granted by the normal give path. Name-based, so an unshuffled copy counts too.
void OOT_ShareLocalItem(RandomizerCheck rc) {
    if (rc == RC_UNKNOWN_CHECK)
        return;
    const ComboRando::CwSharedSettings& shared = ComboSharedSettings();
    if (!shared.Any())
        return;
    auto loc = Rando::Context::GetInstance()->GetItemLocation(rc);
    if (loc == nullptr)
        return;
    const RandomizerGet rg = loc->GetPlacedRandomizerGet();
    if (rg == RG_NONE || rg == RG_COMBO_FOREIGN)
        return;
    const std::string itemName = Rando::StaticData::RetrieveItem(rg).GetName().GetEnglish();
    const ComboRando::CwSharedPair* pair = ComboRando::CwSharedPairForItem(shared, ComboRando::GAME_OOT, itemName);
    if (pair == nullptr)
        return;
    const std::string checkName = Rando::StaticData::GetLocation(rc)->GetName();
    if (gComboCrossDeliver)
        gComboCrossDeliver((int)ComboRando::GAME_MM, pair->mmName, checkName.c_str());
    Anchor_BroadcastCrossItem((int)ComboRando::GAME_MM, pair->mmName, checkName.c_str());
    Notification::Emit({ .message = "Shared with Termina:", .suffix = pair->label });
    SPDLOG_INFO("[ComboShip] OOT shared item '{}' -> MM '{}' (from check '{}')", itemName, pair->mmName, checkName);
}

// OnItemReceive entry. The hook's own entry carries no check (a vanilla-table item arrives through
// Return_Item's table lookup), but the player's held get-item entry is the one the actor handed over,
// and Context::GetFinalGIEntry stamps comboForeignCheck on every entry it builds. Skipped for
// save-direct gives (gComboOotDormantGive), whose player entry would be stale.
static void OnItemReceiveShareLocalItem(GetItemEntry received) {
    if (gComboOotDormantGive > 0 || gPlayState == NULL)
        return;
    Player* player = GET_PLAYER(gPlayState);
    if (player == NULL)
        return;
    const GetItemEntry& pe = player->getItemEntry;
    if (pe.objectId == OBJECT_INVALID || pe.comboForeignCheck == RC_UNKNOWN_CHECK)
        return;
    if (pe.modIndex != received.modIndex || pe.itemId != received.itemId)
        return; // some other give (e.g. a side effect) fired the hook, not the held-up item
    OOT_ShareLocalItem((RandomizerCheck)pe.comboForeignCheck);
}

static void RegisterComboSharedItems() {
    static bool sRegistered = false;
    if (sRegistered)
        return;
    sRegistered = true;
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnItemReceive>(OnItemReceiveShareLocalItem);
}

static RegisterShipInitFunc registerComboSharedItems(RegisterComboSharedItems);

#endif // COMBO_BUILD
