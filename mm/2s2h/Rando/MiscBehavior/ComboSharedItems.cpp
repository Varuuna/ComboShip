// ComboShip: combo-owned file that upstream does not have — do NOT delete on upstream merges (see
// docs/UPSTREAM_MERGES.md). Shared cross-game items, MM side (rando/CrossShared.h,
// docs/CROSS_ITEMS_PLAN.md): the "give both" call CheckQueue.cpp makes after a local grant, and the
// already-owned rule that keeps a shared half from converting to junk.
#include "MiscBehavior.h"
#ifdef COMBO_BUILD

#include "rando/CrossShared.h"
#include "2s2h/BenGui/Notification.h"
#include "2s2h/Rando/StaticData/StaticData.h"
#include <spdlog/spdlog.h>

extern "C" {
#include "variables.h"
}

// Launcher seam and Anchor share, both defined elsewhere in 2s2h (BenPort.cpp / MMAnchor.cpp).
extern "C" void (*gMMComboCrossDeliver)(int targetGame, const char* itemName, const char* srcCheckName);
extern "C" void MMAnchor_BroadcastCrossItem(int targetGame, const char* itemName, const char* srcCheckName);

// Shared settings of the pushed seed, rebuilt when MM_LoadComboRando bumps the generation.
static const ComboRando::CwSharedSettings& ComboSharedSettings() {
    static uint64_t sGen = (uint64_t)-1;
    static ComboRando::CwSharedSettings sShared;
    if (sGen != Rando::MiscBehavior::ComboRandoGen()) {
        sShared = ComboRando::LoadSharedSettingsFromBlob();
        sGen = Rando::MiscBehavior::ComboRandoGen();
    }
    return sShared;
}

bool Rando::MiscBehavior::IsSharedPairItem(RandoItemId item) {
    const ComboRando::CwSharedSettings& shared = ComboSharedSettings();
    if (!shared.Any())
        return false;
    return ComboRando::CwSharedPairForItem(shared, ComboRando::GAME_MM, Rando::StaticData::GetItemDisplayName(item)) !=
           nullptr;
}

// A shared half MM already owns (its OOT half arrived first) is the same item again, not junk.
RandoItemId Rando::MiscBehavior::KeepSharedHalf(RandoItemId converted, RandoItemId raw) {
    if (converted == RI_JUNK && IsSharedPairItem(raw))
        return raw;
    return converted;
}

// After a LOCAL grant of a shared pair's MM half, hand the OOT half to OOT's resident save through the
// launcher seam and share it with teammates. Name-based, so an unshuffled copy counts too. Dormant grants
// and Anchor receives never come through here.
void Rando::MiscBehavior::ShareLocalItem(RandoCheckId rc, RandoItemId item) {
    const ComboRando::CwSharedSettings& shared = ComboSharedSettings();
    if (!shared.Any() || gSaveContext.fileNum == 0xFF)
        return;
    const std::string& itemName = Rando::StaticData::GetItemDisplayName(item);
    const ComboRando::CwSharedPair* pair = ComboRando::CwSharedPairForItem(shared, ComboRando::GAME_MM, itemName);
    if (pair == nullptr)
        return;
    const std::string checkName = Rando::StaticData::GetCheckDisplayName(rc);
    if (gMMComboCrossDeliver)
        gMMComboCrossDeliver((int)ComboRando::GAME_OOT, pair->ootName, checkName.c_str());
    MMAnchor_BroadcastCrossItem((int)ComboRando::GAME_OOT, pair->ootName, checkName.c_str());
    Notification::Emit({ .message = "Shared with Hyrule:", .suffix = pair->label });
    SPDLOG_INFO("[ComboShip] MM shared item '{}' -> OOT '{}' (from check '{}')", itemName, pair->ootName, checkName);
}

#endif // COMBO_BUILD
