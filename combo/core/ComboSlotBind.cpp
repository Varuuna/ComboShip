#include "core/ComboSlotBind.h"

#include <iostream>
#include <string>
#include <nlohmann/json.hpp>

#include "core/ComboContainer.h"
#include "core/ComboCrossItems.h"
#include "core/ComboDllApi.h"
#include "core/ComboGeneration.h"
#include "core/ComboGoal.h"
#include "core/ComboHintReveal.h"
#include "core/ComboSeedState.h"
#include "core/ComboTransition.h"
#include "rando/CrossForeign.h"

void Combo_OnOOTSaveInit(int fileNum) {
    // A new file starts in OOT. Explicit because not every delete path clears the container
    // (DeleteFileOnDeath calls DeleteZeldaFile directly), so a stale MM could otherwise survive here.
    Combo_SetLastGame(fileNum, ComboRando::GAME_OOT);
    // ComboShip (#165/#164): a fresh file starts with an empty personal note (written, never erased —
    // an absent key is the "never migrated" sentinel) and must not inherit the hint read state.
    ComboResetSlotForNewFile(fileNum);
    // ComboShip: bind the pending consolidated seed to this slot — bake it into the container's
    // combo.rando (self-contained), then push it into both DLLs so foreign data is live immediately.
    nlohmann::json seed;
    if (!g_ConsolidatedJson.empty()) {
        try {
            seed = nlohmann::json::parse(g_ConsolidatedJson);
        } catch (...) {}
        ComboBakeSeed(fileNum, seed);
        LoadComboCompletion(fileNum); // refresh the in-memory latch + push this seed's goal into both DLLs
        if (SOH_LoadComboRando)
            SOH_LoadComboRando(g_ConsolidatedJson.c_str());
        if (MM_LoadComboRando)
            MM_LoadComboRando(g_ConsolidatedJson.c_str());
        std::cout << "[ComboShip] baked consolidated seed into container for slot " << fileNum << std::endl;
        // ComboShip (#135): an MM-start seed routes the fresh file into MM; the launcher handoff then
        // spawns it in South Clock Town (the same place the portal arrives at).
        if (seed.value("startingGame", std::string("OOT")) == "MM") {
            Combo_SetLastGame(fileNum, ComboRando::GAME_MM);
            std::cout << "[ComboShip] slot " << fileNum << " starts in Majora's Mask" << std::endl;
        }
    }
    // The new-save callback runs on OOT's thread with the entered file name current — carry it into
    // the matching MM save so both files show the player's name.
    unsigned char playerName[8] = { 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E }; // 0x3E = N64 blank glyph
    if (SOH_GetCurrentPlayerName)
        SOH_GetCurrentPlayerName(playerName);
    // Re-derived every creation, never cached — a consumed cache left later files with a vanilla MM
    // save, silently disabling every IS_RANDO behavior. See docs/deviations/rando.md.
    std::string mmPlacements;
    if (!seed.is_null())
        mmPlacements = ComboRando::ApplyPayloadFromConsolidated(seed, ComboRando::GAME_MM).dump();
    if (MM_InitRandoSaveFile && !seed.is_null()) {
        std::cout << "[ComboShip] Creating RANDO MM save for OOT slot " << fileNum << std::endl;
        // Re-assert prices from the seed being applied — a failed re-generation after a reload leaves
        // the MM DLL's captured price map holding the failed seed's rolls, not this spoiler's.
        if (MM_SetCheckPrices)
            MM_SetCheckPrices(
                seed.value("mm", nlohmann::json::object()).value("prices", nlohmann::json::object()).dump().c_str());
        // A reloaded seed's MM settings only get written here (MM_InitRandoSaveFile is where MM reads
        // them) — never at reload time, so they can't leak into comboship.json before a slot is bound.
        if (!g_PendingMMSettingsJson.empty() && MM_RestoreRandoSettings)
            MM_RestoreRandoSettings(g_PendingMMSettingsJson.c_str());
        if (MM_InitRandoSaveFile(fileNum, mmPlacements.c_str(), playerName) != 0) {
            std::cerr << "[ComboShip] ERROR: MM rando save creation FAILED for slot " << fileNum
                      << " — this slot's MM save has no placements. Re-create it." << std::endl;
        } else if (ComboUI_SyncRandomizedCosmetics) {
            // #169: MM has just rolled its own cosmetics (generation hook inside the call above); with
            // sync on, hand it OOT's colors instead so the shared elements match. Roll OOT first: the
            // latch skipped it at load time if the options were off, so enabling them mid-seed still
            // syncs this seed's fresh colors rather than whatever was persisted.
            if (ComboUI_CosmeticsSyncGateEnabled && ComboUI_CosmeticsSyncGateEnabled())
                Combo_FireGenRollHooksOnce(seed.value("masterSeed", 0u));
            ComboUI_SyncRandomizedCosmetics();
        }
        // Silent auto-load: the save now has the seed's settings baked in — return the CVars to the
        // user's config. An explicit drop leaves the seed's settings as the new persisted baseline.
        if (g_ComboReloadRestoreUserMM && MM_RestoreRandoSettings && !g_UserMMSettingsSnapshot.empty())
            MM_RestoreRandoSettings(g_UserMMSettingsSnapshot.c_str());
        g_PendingMMSettingsJson.clear();
        g_UserMMSettingsSnapshot.clear();
        g_ComboReloadRestoreUserMM = false;
    } else {
        // No seed bound to this slot. ComboShip has no vanilla mode, so there is no valid save to
        // create here — fail loudly instead of writing one that looks fine and misbehaves later.
        std::cerr << "[ComboShip] ERROR: no consolidated seed for slot " << fileNum
                  << " — cannot create an MM rando save. Generate or load a seed first." << std::endl;
    }
    // ComboShip (#164): the slot's hints are baked and its read state freshly erased — hand both to
    // comboui's Hint Tracker.
    Combo_PushHintTrackerData(fileNum);
    // The creation path builds the save in MM's live gSaveContext.
    g_MmSaveInMemorySlot = fileNum;
}


// ComboShip: OOT loaded a save (file select / warp). Bring the matching MM save into MM's dormant
// memory so the combo tracker peek shows real MM items before MM is visited. Skipped when that
// slot's MM save is already live in memory — reloading from disk would clobber newer progress.
void Combo_OnOOTSaveLoad(int fileNum) {
    LoadComboCompletion(fileNum); // refresh both-bosses-beaten flags for this slot
    // Push the slot's baked combo rando (foreign map + cross-hints) into both DLLs, once per load.
    {
        const std::string rando = ComboReadBakedRando(fileNum);
        if (!rando.empty()) {
            if (SOH_LoadComboRando)
                SOH_LoadComboRando(rando.c_str());
            if (MM_LoadComboRando)
                MM_LoadComboRando(rando.c_str());
        }
    }
    Combo_PushHintTrackerData(fileNum); // #164: this slot's hints + persisted read state
    if (!MM_LoadSaveForCombo || g_MmSaveInMemorySlot == fileNum) {
        Combo_OnTriforceProgress(0, fileNum);
        Combo_ResumeMMIfLastSavedThere(fileNum);
        return;
    }
    std::cout << "[ComboShip] Loading MM save for OOT slot " << fileNum << " (tracker peek)" << std::endl;
    // Read-only peek: on failure nothing was loaded, so the slot must NOT be marked resident — stale
    // dormant memory would otherwise pose as this slot's save (and a dormant write would persist it).
    if (MM_LoadSaveForCombo(fileNum) == 0) {
        g_MmSaveInMemorySlot = fileNum;
    }
    // Both counters are now live: catch a goal crossed while the game wasn't running (e.g. a teammate's
    // pieces applied to a dormant save). Latched, so it can't roll credits twice.
    Combo_OnTriforceProgress(0, fileNum);
    Combo_ResumeMMIfLastSavedThere(fileNum);
}
