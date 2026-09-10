#include "core/ComboReload.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

#include "core/ComboContainer.h"
#include "core/ComboCrossItems.h"
#include "core/ComboDllApi.h"
#include "core/ComboGeneration.h"
#include "core/ComboGoal.h"
#include "core/ComboSeedFile.h"
#include "core/ComboSeedMath.h"
#include "core/ComboSeedState.h"
#include "rando/CrossForeign.h"
#include "rando/CrossHints.h"
#include "rando/CrossWorldRando.h"

// ComboShip: reload a consolidated seed file (the remembered pending file when path is null/empty, or
// a dropped file) and make it playable WITHOUT regenerating. Runs synchronously on the MAIN thread
// (called from the file-select), so the gSaveContext-mutating apply is safe. Restores both games'
// settings, runs the pool prep, re-applies the saved OOT placements + seed-hash, stashes the MM
// placements, and keeps the consolidated JSON in memory so "Start Randomizer" writes the per-slot
// file. Returns 1 on success. The hash string is recomputed from displaySeed for the per-slot name.
int Combo_OnReloadRequest(const char* path) {
    if (g_GenerateBusy.load() || g_ComboPendingFinalize.load()) {
        std::cerr << "[ComboShip] reload: skipped — a generation is in flight\n";
        return 0; // a generation is in flight — don't race it
    }
    // A null/empty path is the silent first-frame auto-reload; a non-empty path is an explicit drop
    // (a deliberate seed switch, so its settings are allowed to become the new persisted baseline).
    bool isSilentAutoLoad = !(path && path[0]);
    std::string file;
    nlohmann::json j;
    // The resolve/scan below touches the filesystem and may throw (path conversion, bad_alloc); this
    // runs under a C-ABI callback, so nothing may unwind past it.
    try {
        if (!isSilentAutoLoad) {
            // An explicit drop never falls back to another seed: a failed drop is a real error.
            auto dropped = ResolveComboSeedPath(path);
            if (!TryLoadComboSeedFile(dropped, j)) {
                std::cerr << "[ComboShip] reload: dropped file '" << path << "' could not be loaded\n";
                return 0;
            }
            file = dropped.string();
        } else {
            std::string remembered = SOH_GetComboSpoilerPath ? SOH_GetComboSpoilerPath() : "";
            if (remembered.empty()) {
                std::cerr << "[ComboShip] reload: no remembered seed path — scanning "
                          << ComboRando::ConsolidatedDir().string() << "\n";
            } else {
                auto resolved = ResolveComboSeedPath(remembered);
                if (TryLoadComboSeedFile(resolved, j))
                    file = resolved.string();
                else
                    std::cerr << "[ComboShip] reload: remembered seed '" << remembered
                              << "' is missing or unreadable — scanning " << ComboRando::ConsolidatedDir().string()
                              << "\n";
            }
            if (file.empty()) {
                auto recovered = FindNewestComboSeed(j);
                if (recovered.empty()) {
                    std::cerr << "[ComboShip] reload: no combo seed found to auto-load\n";
                    return 0;
                }
                file = recovered.string();
                std::cout << "[ComboShip] reload: recovered newest seed " << file << "\n";
                RememberComboSpoiler(recovered); // repair the lost/stale remembered path
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[ComboShip] reload: seed lookup failed: " << e.what() << "\n";
        return 0;
    } catch (...) {
        std::cerr << "[ComboShip] reload: seed lookup failed: non-std exception\n";
        return 0;
    }
    try {
        uint32_t masterSeed = j.value("masterSeed", 0u);
        ResetCrossItemDedupForSeed(masterSeed);
        uint32_t displaySeed = j.value("displaySeed", 0u);
        // ComboShip (#136): push the seed's goal before anything re-derives OOT's settings-scoped pool
        // or MM's — the forced wincon/hunt toggle decides what those pools contain.
        {
            auto g = j.value("goal", nlohmann::json::object());
            const int hunt = g.value("type", std::string("bosses")) == "triforceHunt" ? 1 : 0;
            const int required = hunt ? g.value("requiredPieces", 0) : 0;
            // Absent on seeds made before the combined total — -1 leaves each game's own slider alone.
            const int total = g.value("totalPieces", -1);
            if (SOH_SetComboGoal)
                SOH_SetComboGoal(hunt, required, ComboRando::CwOotPieces(total));
            if (MM_SetComboGoal)
                MM_SetComboGoal(hunt, required, ComboRando::CwMmPieces(total));
            // Log-only sanity check: a hand-edited/plandomized spoiler could ask for more pieces than it
            // actually places, which is unwinnable. Loud, but the load still proceeds (the file is the
            // player's own artifact and the rest of it may be perfectly fine).
            if (hunt) {
                int placed = 0;
                for (const char* gk : { "oot", "mm" }) {
                    const auto pl = j.value(gk, nlohmann::json::object()).value("placements", nlohmann::json::object());
                    for (const auto& [chk, item] : pl.items()) {
                        if (!item.is_string())
                            continue;
                        const std::string bare = ComboRando::StripGameSuffix(item.get<std::string>());
                        placed += (bare == ComboRando::kOotTriforcePiece || bare == ComboRando::kMmTriforcePiece);
                    }
                }
                if (placed < required)
                    std::cerr << "[ComboShip] Reload: ERROR — seed needs " << required << " Triforce Pieces but only "
                              << placed << " are placed; this seed cannot be completed\n";
            }
            // ComboShip (#135): same ordering rule — an MM start forces OOT settings that shape its pool.
            if (SOH_SetComboStartingGame)
                SOH_SetComboStartingGame(j.value("startingGame", std::string("OOT")) == "MM" ? 1 : 0);
        }
        auto oot = j.value("oot", nlohmann::json::object());
        auto mm = j.value("mm", nlohmann::json::object());
        std::string ootSettings = oot.value("settings", nlohmann::json::object()).dump();
        std::string mmSettings = mm.value("settings", nlohmann::json::object()).dump();

        // Only OOT's payload is rebuilt here — MM re-derives its own at slot-bind time.
        std::string ootPlacements = ComboRando::ApplyPayloadFromConsolidated(j, ComboRando::GAME_OOT).dump();

        // Spoiler prices override the seeded re-roll (settings may have drifted since generation).
        // Absent on pre-price spoilers: log and fall back to the re-roll (OOT) / zero prices (MM).
        auto ootPrices = oot.value("prices", nlohmann::json::object());
        auto mmPrices = mm.value("prices", nlohmann::json::object());
        if (ootPrices.empty() || mmPrices.empty())
            std::cout << "[ComboShip] Reload: spoiler predates price export; shop prices may not match logic\n";
        if (SOH_SetCheckPrices)
            SOH_SetCheckPrices(ootPrices.dump().c_str());
        if (MM_SetCheckPrices)
            MM_SetCheckPrices(mmPrices.dump().c_str());

        // Silent auto-load: snapshot the user's current settings so they can be put back once the
        // seed's OOT settings have done their job (reproduction), instead of persisting to disk.
        std::string userOotSnapshot;
        if (isSilentAutoLoad && SOH_DumpRandoSettings) {
            userOotSnapshot = SOH_DumpRandoSettings();
            if (userOotSnapshot.empty())
                std::cout << "[ComboShip] Reload: SOH_DumpRandoSettings returned empty snapshot\n";
        }

        // OOT: restore settings -> seed RNG -> prep settings-scoped pool -> re-derive entrances ->
        // apply placements -> hash.
        if (SOH_RestoreRandoSettings)
            SOH_RestoreRandoSettings(ootSettings.c_str());
        if (SOH_SetComboRandoSeed)
            SOH_SetComboRandoSeed(masterSeed);
        // MM too: MM_InitRandoSaveFile writes finalSeed (junk/trap variety, clock-shuffle roll) from
        // the combo seed — without this a reloaded seed gets finalSeed=0 and diverges from the author.
        if (MM_SetComboRandoSeed)
            MM_SetComboRandoSeed(masterSeed);
        if (SOH_PrepRandoContext)
            SOH_PrepRandoContext();
        // Same deterministic call as generation — reproduces (or clears) this seed's entrance layout.
        if (SOH_ShuffleEntrancesForCombo && !SOH_ShuffleEntrancesForCombo(masterSeed)) {
            std::cerr << "[ComboShip] reload: OOT entrance shuffle failed to re-derive — aborting\n";
            // Restore first: bailing here would otherwise leave the seed's OOT CVars as the baseline.
            if (isSilentAutoLoad && SOH_RestoreRandoSettings)
                SOH_RestoreRandoSettings(userOotSnapshot.c_str());
            return 0;
        }
        bool hintsPresent = j.value("hints", nlohmann::json::object()).contains("oot");
        if (SOH_SetComboHintsPresent)
            SOH_SetComboHintsPresent(hintsPresent ? 1 : 0);
        if (SOH_ApplyRandoPlacements)
            SOH_ApplyRandoPlacements(ootPlacements.c_str());
        if (SOH_SetComboSeedHash)
            SOH_SetComboSeedHash(displaySeed);
        if (hintsPresent && SOH_ApplyComboHints)
            SOH_ApplyComboHints(j.value("hints", nlohmann::json::object()).dump().c_str());
        // #169: a seed recipient never runs Combo_FinalizeGenerate, so roll here too — the ctx seed is
        // set by now, so it reproduces the generator's colors exactly. Not sync-gated: each hook
        // subscriber checks its own CVars, and the latch keeps this to once per seed.
        Combo_FireGenRollHooksOnce(masterSeed);

        // Reproduction is done — put the user's OOT settings back so comboship.json (and the menu)
        // stay authoritative. An explicit drop instead keeps the seed's settings as the new baseline.
        // Gated on isSilentAutoLoad alone: an empty dump (warned above) must not skip the restore,
        // else the seed's OOT CVars would stick and leak to comboship.json — the bug being fixed.
        if (isSilentAutoLoad && SOH_RestoreRandoSettings)
            SOH_RestoreRandoSettings(userOotSnapshot.c_str());

        // MM: MM_InitRandoSaveFile reads gRando.* CVars, but only at slot-bind time (Combo_OnOOTSaveInit),
        // which may be many frames away — stash the seed's settings there instead of writing them now,
        // so they never leak into comboship.json before (or without) a slot ever being started.
        if (isSilentAutoLoad && MM_DumpRandoSettings)
            g_UserMMSettingsSnapshot = MM_DumpRandoSettings();
        else
            g_UserMMSettingsSnapshot.clear();
        g_PendingMMSettingsJson = mmSettings;
        g_ComboReloadRestoreUserMM = isSilentAutoLoad;
        // An explicit drop makes the seed the new baseline immediately for OOT (above); mirror that
        // for MM here instead of waiting for slot-bind, so quit-before-Start doesn't persist a mixed
        // OOT=seed/MM=old-user comboship.json.
        if (!isSilentAutoLoad && MM_RestoreRandoSettings)
            MM_RestoreRandoSettings(mmSettings.c_str());

        // Keep the loaded seed so Start binds it to the chosen slot; recompute the hash-icon filename.
        g_ConsolidatedJson = j.dump(2);
        g_FinalizeDisplaySeed = displaySeed;
        // Remember it so it survives a restart before Start. Re-filed under its hash name so the
        // remembered path always holds the content just loaded, never a same-named older spoiler.
        RememberComboSpoiler(j.contains("file_hash") ? WriteComboSpoiler(j["file_hash"], g_ConsolidatedJson)
                                                     : std::filesystem::path(file));
        // Populate the shared progress so the comboui Generate panel shows the remembered seed
        // (seed string, per-game check counts, cross-game count) just like a fresh generation.
        g_ComboProgress.Reset();
        std::string seedStr = j.value("seed", std::string());
        std::strncpy(g_ComboProgress.seedStr, seedStr.c_str(), sizeof(g_ComboProgress.seedStr) - 1);
        g_ComboProgress.seedStr[sizeof(g_ComboProgress.seedStr) - 1] = '\0';
        g_ComboProgress.seed.store(masterSeed);
        g_ComboProgress.ootCheckCount.store(static_cast<int>(oot.value("placements", nlohmann::json::object()).size()));
        g_ComboProgress.mmCheckCount.store(static_cast<int>(mm.value("placements", nlohmann::json::object()).size()));
        g_ComboProgress.foreignCount.store(static_cast<int>(j.value("foreign", nlohmann::json::array()).size()));
        g_ComboProgress.success.store(true);
        g_ComboProgress.done.store(true);
        g_ComboProgress.running.store(false);

        std::cout << "[ComboShip] reloaded combo seed from " << file << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[ComboShip] reload failed: " << e.what() << "\n";
        return 0;
    } catch (...) {
        std::cerr << "[ComboShip] reload failed: non-std exception\n";
        return 0;
    }
}
