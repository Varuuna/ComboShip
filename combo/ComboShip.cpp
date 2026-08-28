// ComboShip - Unified Launcher for OOT (soh.dll) + MM (2ship.dll)
//
// Boot flow:
//   1. Load soh.dll + 2ship.dll and resolve exported functions
//   2. Ensure OOT archives exist (extract via SOH_Extract if missing)
//   3. Ensure MM archives exist (extract via MM_Extract if missing)
//   4. SOH_Init()    — OOT context + resource manager + window
//   5. SOH_RunMain() — blocks until OOT exits
//   6. MM_RunGame()  — MM reuses context/window via sComboTransitionActive (if triggered)
//   7. Cleanup

#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <unordered_set>
#include <exception>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <map>
#include <thread>
#include <mutex>
#include <queue>
#include <nlohmann/json.hpp>

#include "rando/CrossForeign.h"
#include "rando/CrossWorldRando.h"
#include "rando/ComboPlaythrough.h"
#include "rando/CrossHints.h"
#include "gui/ComboGenProgress.h"
#include "ComboExtract.h"
#include "ComboSettingsImport.h"
#include "core/ComboPlatform.h"
#include "core/ComboDllApi.h"
#include "core/ComboSeedMath.h"
#include "core/ComboContainer.h"
#include "core/ComboSeedFile.h"
#include "core/ComboBootstrap.h"
#include "core/ComboAnchorNet.h"
#include "core/ComboCrossItems.h"
#include "core/ComboSeedState.h"
#include "core/ComboGeneration.h"
#include "core/ComboReload.h"
#include "core/ComboGoal.h"
#include "core/ComboHintReveal.h"

// OOT slot whose MM save is live in MM's dormant memory (-1 = none). Guards Combo_OnOOTSaveLoad
// against reloading stale disk state over MM's in-memory progress after round trips.
int g_MmSaveInMemorySlot = -1;
static bool g_pendingOOTReturn = false;
static DllHandle comboUIModule = nullptr;

// Single place the foreground game changes, so every transition point notifies comboui consistently.
static void Combo_SetForegroundGame(int game) {
    // #173: MM only accrues play time while it is foreground. OOT owns the foreground at startup.
    static int sPrevGame = ComboRando::GAME_OOT;
    if (sPrevGame == ComboRando::GAME_MM && MM_ComboPausePlaytime)
        MM_ComboPausePlaytime();
    if (game == ComboRando::GAME_MM && MM_ComboResumePlaytime)
        MM_ComboResumePlaytime();
    sPrevGame = game;
    if (ComboUI_OnForegroundGame)
        ComboUI_OnForegroundGame(game);
}

// Erase/copy the slot's container, then tell the DLLs. ComboContainer owns the storage but cannot make
// these calls itself (it never sees the export table), which is what keeps them outside the lock.
static void EraseComboContainer(int slot) {
    ComboEraseSlotStorage(slot);
    // #182: MM caches which slot's owl save its gSaveContext came from; that section is gone.
    if (MM_InvalidateOwlBlobSlot)
        MM_InvalidateOwlBlobSlot();
    // #164: the Hint Tracker would otherwise keep showing the deleted slot's hints on file-select.
    if (ComboUI_SetHintTrackerData)
        ComboUI_SetHintTrackerData(-1, "", "");
}

// OOT file-select "copy file": the .combosav has no per-game file to copy, so the launcher owns it.
static void Combo_CopyContainer(int from, int to) {
    ComboCopySlotStorage(from, to);
    // #182: the destination's owl save came from the donor, so MM's descent cache is wrong.
    if (MM_InvalidateOwlBlobSlot)
        MM_InvalidateOwlBlobSlot();
}

// ComboShip (issue #1): erasing a slot from either game's file-select wipes BOTH saves — each game
// fires its Set*-registered callback with the slot, the launcher routes it to the other game's
// save-only delete export (never re-entering a menu erase path). See docs/deviations/boot-shutdown.md.

// Registered into each game; invoked when that game erases a slot. Routes the (0-based) slot to the
// OTHER game's delete export, then removes the merged container. The launcher does no index math —
// MM's 1-based JSON naming is handled inside MM_DeleteSaveFile.
static void DeleteForeignSaveFromOOT(int slot) {
    if (MM_DeleteSaveFile)
        MM_DeleteSaveFile(slot);
    EraseComboContainer(slot); // remove the slot's merged container (baked rando + both saves)
}
static void DeleteForeignSaveFromMM(int slot) {
    if (SOH_DeleteSaveFile)
        SOH_DeleteSaveFile(slot);
    EraseComboContainer(slot);
}

static void Combo_OnOOTSaveInit(int fileNum) {
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

static void Combo_ResumeMMIfLastSavedThere(int fileNum);

// ComboShip: OOT loaded a save (file select / warp). Bring the matching MM save into MM's dormant
// memory so the combo tracker peek shows real MM items before MM is visited. Skipped when that
// slot's MM save is already live in memory — reloading from disk would clobber newer progress.
static void Combo_OnOOTSaveLoad(int fileNum) {
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

// ComboShip (#89): resume MM instead of starting OOT when the slot was last played in MM. The
// file-select gate is load-bearing — OnLoadGame also fires on the MM->OOT return and from in-game
// reloads, where this would bounce the player back into MM forever.
static void Combo_ResumeMMIfLastSavedThere(int fileNum) {
    if (!SOH_ParkForComboMMResume || !MM_RunGame || !SOH_IsOnFileSelect || !SOH_IsOnFileSelect()) {
        return;
    }
    if (fileNum < 0 || fileNum > 2) {
        return; // debug select (0xFF) / Boss Rush (0xFE) share FileChoose_LoadGame
    }
    if (Combo_GetLastGame(fileNum) != ComboRando::GAME_MM) {
        return;
    }
    std::cout << "[ComboShip] Slot " << fileNum << " was last saved in MM — resuming MM" << std::endl;
    if (MM_SetComboEntryIsResume)
        MM_SetComboEntryIsResume(1); // a real save load: honors MM's Remember Save Location
    g_PendingMMFileNum = fileNum;
    SOH_ParkForComboMMResume(); // drops out of OOT's game loop; the launcher then enters MM
}

static void Combo_OnOOTSceneSwitch(int fileNum) {
    std::cout << "[ComboShip] Mask Shop entered — switching to MM, slot " << fileNum << std::endl;
    if (MM_SetComboEntryIsResume)
        MM_SetComboEntryIsResume(0); // portal entry: always arrives in South Clock Town
    g_PendingMMFileNum = fileNum;
    // OOT game loop is already exiting (gGameState->running = false set by the hook).
}

// Why MM handed control back: 0 = portal, 1 = Ctrl+R reset, 2 = owl-save quit (see BenPort.cpp).
static int g_mmReturnKind = 0;

static void Combo_OnMMReturn(int kind) {
    g_mmReturnKind = kind;
    std::cout << "[ComboShip] MM returning to OOT (kind=" << kind << ")" << std::endl;
    g_pendingOOTReturn = true;
}

// ---------- Entry point ----------

int main(int argc, char** argv) {
    std::cout << "ComboShip Launcher - Starting..." << std::endl;

#ifdef _WIN32
    // Match SoH's DPI awareness (its SHIPOFHARKINIAN.manifest declares permonitorv2). ComboShip.exe
    // ships no such manifest, so without this Windows renders the framebuffer at the logical
    // (down-scaled) resolution and upscales it — making the whole menu/UI larger and blurrier on
    // >100% display scaling. Must run before any window is created.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif

    std::set_terminate(ComboTerminateHandler);

    // --- 1. Load DLLs ---

#ifdef _WIN32
    const char* sohDll = "soh.dll";
    const char* twoShipDll = "2ship.dll";
#elif defined(__APPLE__)
    const char* sohDll = "libsoh.dylib";
    const char* twoShipDll = "lib2ship.dylib";
#else
    const char* sohDll = "libsoh.so";
    const char* twoShipDll = "lib2ship.so";
#endif

    DllHandle sohModule = LoadDll(sohDll);
    if (!sohModule) {
        std::cerr << "ERROR: Failed to load " << sohDll << " (" << DllError() << ")" << std::endl;
        return 1;
    }

    DllHandle mmModule = LoadDll(twoShipDll);
    if (!mmModule) {
        std::cerr << "ERROR: Failed to load " << twoShipDll << " (" << DllError() << ")" << std::endl;
        FreeDll(sohModule);
        return 1;
    }

    ComboResolveGameExports(sohModule, mmModule);

    if (!SOH_Init || !SOH_RunMain) {
        std::cerr << "ERROR: soh.dll is missing required ComboShip exports (SOH_Init / SOH_RunMain)." << std::endl;
        std::cerr << "       Rebuild soh.dll from this ComboShip branch." << std::endl;
        FreeDll(mmModule);
        FreeDll(sohModule);
        return 1;
    }

    if (!MM_InitArchives) {
        std::cerr << "ERROR: 2ship.dll is missing required ComboShip exports (MM_InitArchives)." << std::endl;
        std::cerr << "       Rebuild 2ship.dll from this ComboShip branch." << std::endl;
        FreeDll(mmModule);
        FreeDll(sohModule);
        return 1;
    }

    // --- 2/3. Ensure BOTH ROM archives exist (ComboShip-owned unified extraction) ---
    // ComboShip needs an OoT ROM and an MM ROM. If either ROM archive is missing, create the shared
    // window from the bundled soh.o2r (SOH_InitWindowOnly — no ROM needed), then run comboui's
    // combo-owned extraction screen, which gathers BOTH ROMs and extracts them with progress bars.
    // It returns false if the player quits or extraction fails -> we exit. When the archives are
    // present we skip this entirely and use the monolithic SOH_Init() fast path below.
    bool windowInitialized = false;
    // Capture BEFORE any window/config init: a fresh install (no comboship.json) gets the settings
    // import offer. (The Config ctor doesn't create the file, but capturing early stays robust.)
    const bool freshInstall = !ComboConfigExists();
    const bool needOot = !OOTArchivesExist();
    const bool needMm = !MMRomArchiveExists();
    if (needOot || needMm) {
        if (!SOH_InitWindowOnly || !SOH_FinishInit || !SOH_ValidateRom || !SOH_StartExtraction ||
            !SOH_GetExtractionProgress || !MM_ValidateRom || !MM_StartExtraction || !MM_GetExtractionProgress) {
            std::cerr << "ERROR: game DLLs missing the ComboShip extraction primitives (rebuild required)."
                      << std::endl;
            FreeDll(mmModule);
            FreeDll(sohModule);
            return 1;
        }
        std::cout << "[ComboShip] ROM archive(s) missing (OoT=" << needOot << " MM=" << needMm
                  << ") — opening extraction screen." << std::endl;
        SOH_InitWindowOnly(); // shared window + ImGui from soh.o2r; no ROM required
        windowInitialized = true;

        if (!comboUIModule) {
            comboUIModule = LoadDll("comboui.dll");
        }
        if (comboUIModule) {
            ComboUI_RunExtraction = (ComboFnRunExtraction)GetSym(comboUIModule, "ComboUI_RunExtraction");
        }
        if (!ComboUI_RunExtraction) {
            std::cerr << "ERROR: comboui.dll missing ComboUI_RunExtraction (rebuild required)." << std::endl;
            if (comboUIModule)
                FreeDll(comboUIModule);
            FreeDll(mmModule);
            FreeDll(sohModule);
            return 1;
        }

        ComboExtractCallbacks cb = {};
        cb.sohValidate = SOH_ValidateRom;
        cb.sohClassify = SOH_ClassifyRom;
        cb.sohStart = SOH_StartExtraction;
        cb.sohProgress = SOH_GetExtractionProgress;
        cb.sohNeeded = needOot ? 1 : 0;
        cb.mmValidate = MM_ValidateRom;
        cb.mmClassify = MM_ClassifyRom;
        cb.mmStart = MM_StartExtraction;
        cb.mmProgress = MM_GetExtractionProgress;
        cb.mmNeeded = needMm ? 1 : 0;

        if (!ComboUI_RunExtraction(&cb)) {
            std::cerr << "[ComboShip] Extraction cancelled or failed — exiting." << std::endl;
            if (comboUIModule)
                FreeDll(comboUIModule);
            FreeDll(mmModule);
            FreeDll(sohModule);
            return 1;
        }
        if (!OOTArchivesExist() || !MMRomArchiveExists()) {
            std::cerr << "ERROR: ROM archives still missing after extraction — exiting." << std::endl;
            if (comboUIModule)
                FreeDll(comboUIModule);
            FreeDll(mmModule);
            FreeDll(sohModule);
            return 1;
        }
        std::cout << "[ComboShip] Extraction complete." << std::endl;
    }

    // --- 3b. First-launch settings import (ComboShip-owned, issue 24) ---
    // Fresh install: offer to import an existing SoH/2Ship config (after extraction, ROMs first). The
    // window/config exist only post-SOH_InitWindowOnly, so we merge here (SoH wins) and apply to the
    // LIVE config before SOH_FinishInit's version updates. Optional — any missing piece skips it.
    if (freshInstall) {
        if (!windowInitialized && SOH_InitWindowOnly) {
            SOH_InitWindowOnly();
            windowInitialized = true;
        }
        if (!comboUIModule) {
            comboUIModule = LoadDll("comboui.dll");
        }
        if (comboUIModule && !ComboUI_RunSettingsImport) {
            ComboUI_RunSettingsImport = (ComboFnRunSettingsImport)GetSym(comboUIModule, "ComboUI_RunSettingsImport");
        }
        if (windowInitialized && ComboUI_RunSettingsImport && SOH_ApplyImportedConfig) {
            ComboSettingsImportCallbacks cb = {};
            cb.sohValidate = LauncherValidateShipConfig;
            cb.mmValidate = LauncherValidateShipConfig;
            ComboSettingsImportResult res = {};
            if (ComboUI_RunSettingsImport(&cb, &res) && res.action == 1) {
                nlohmann::json merged = nlohmann::json::object(), sohJson, mmJson;
                const bool haveMm = LoadJsonObject(res.mmPath, mmJson);
                const bool haveSoh = LoadJsonObject(res.sohPath, sohJson);
                if (haveMm) {
                    merged = mmJson; // 2Ship is the base (lower priority)
                }
                if (haveSoh) {
                    DeepMerge(merged, sohJson); // SoH overlays and wins on collisions
                }
                if (haveSoh || haveMm) {
                    merged.erase("Window"); // machine-specific
                    // ConfigVersion drives OOT's updaters; keep it only when it actually came from the
                    // SoH source (a 2Ship version, or none, would misdirect them).
                    if (!haveSoh || !sohJson.contains("ConfigVersion")) {
                        merged.erase("ConfigVersion");
                    }
                    SOH_ApplyImportedConfig(merged.dump().c_str());
                    std::cout << "[ComboShip] Settings imported (SoH=" << haveSoh << " MM=" << haveMm << ")."
                              << std::endl;
                }
            }
        }
    }

    // Wire the Anchor transport to the launcher-owned connection BEFORE SOH_Init(): OOT auto-enables
    // Anchor during init when the persisted "Enabled" CVar is set (OTRGlobals.cpp). If the connect
    // callback isn't registered yet, that auto-enable sets isEnabled without ever opening a socket,
    // wedging on "Connecting..." after a restart.
    if (SOH_SetAnchorSend && SOH_SetAnchorConnect && SOH_SetAnchorDisconnect) {
        SOH_SetAnchorSend(ComboAnchor::Send);
        SOH_SetAnchorConnect(ComboAnchor::Connect);
        SOH_SetAnchorDisconnect(ComboAnchor::Disconnect);
        std::cout << "[ComboShip] OOT Anchor transport seam registered." << std::endl;
    }
    if (MM_SetAnchorSend) {
        MM_SetAnchorSend(ComboAnchor::Send);
        std::cout << "[ComboShip] MM Anchor transport seam registered." << std::endl;
    }

    // Register the cross-game delivery dispatcher into both DLLs (issue #3). Done before SOH_Init so
    // a resumed save that immediately drains a queued foreign item has the route available.
    if (SOH_SetCrossDeliver)
        SOH_SetCrossDeliver(DeliverCrossItem);
    if (MM_SetCrossDeliver)
        MM_SetCrossDeliver(DeliverCrossItem);
    if (SOH_SetMarkForeignObtained)
        SOH_SetMarkForeignObtained(MarkForeignObtained);
    if (MM_SetMarkForeignObtained)
        MM_SetMarkForeignObtained(MarkForeignObtained);
    // A6: register the per-frame dormant-pump seam into both DLLs.
    if (SOH_SetPumpDormant)
        SOH_SetPumpDormant(PumpDormant);
    if (MM_SetPumpDormant)
        MM_SetPumpDormant(PumpDormant);
    std::cout << "[ComboShip] Dormant co-op pump seams: soh=" << (SOH_SetPumpDormant && SOH_Anchor_PumpDormant)
              << " mm=" << (MM_SetPumpDormant && MM_Anchor_PumpDormant) << std::endl;
    if (SOH_SetFinalBossDefeatedCb)
        SOH_SetFinalBossDefeatedCb(Combo_OnFinalBossDefeated);
    if (MM_SetFinalBossDefeatedCb)
        MM_SetFinalBossDefeatedCb(Combo_OnFinalBossDefeated);
    // #136: combined Triforce goal — progress pokes in, each game reads the other's count out.
    if (SOH_SetTriforceProgressCb)
        SOH_SetTriforceProgressCb(Combo_OnTriforceProgress);
    if (MM_SetTriforceProgressCb)
        MM_SetTriforceProgressCb(Combo_OnTriforceProgress);
    if (SOH_SetOtherTriforceCountCb)
        SOH_SetOtherTriforceCountCb(Combo_GetMmTriforceCount);
    if (MM_SetOtherTriforceCountCb)
        MM_SetOtherTriforceCountCb(Combo_GetOotTriforceCount);
    if (SOH_SetCrossDeliver || MM_SetCrossDeliver) {
        std::cout << "[ComboShip] Cross-game item delivery seam registered." << std::endl;
    }
    // #164: combo Hint Tracker — both games report a hint the moment it displays.
    if (SOH_SetComboHintRevealCb)
        SOH_SetComboHintRevealCb(Combo_OnOotHintRevealed);
    if (MM_SetComboHintRevealCb)
        MM_SetComboHintRevealCb(Combo_OnMmHintRevealed);

    // --- 4. Initialize OOT game ---

    std::cout << "[ComboShip] Initializing Ship of Harkinian (OOT)..." << std::endl;
    try {
        if (windowInitialized) {
            // Window already created for the extraction screen; finish the ROM-dependent init.
            SOH_FinishInit();
        } else {
            // Fast path: both ROM archives present, monolithic init (creates window + finishes).
            SOH_Init();
        }
    } catch (const std::exception& e) {
        std::cerr << "[ComboShip] SOH_Init threw std::exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "[ComboShip] SOH_Init threw a non-std exception" << std::endl;
        return 1;
    }
    std::cout << "[ComboShip] OOT initialized." << std::endl;

    // ComboShip: load the combo-owned menu DLL and install the unified menu now that
    // OOT has created the shared Gui. comboui owns the menu for the whole process.
    // (It may already be loaded if the extraction screen ran — reuse that handle.)
    if (!comboUIModule) {
        comboUIModule = LoadDll("comboui.dll");
    }
    if (comboUIModule) {
        ComboResolveComboUiExports(comboUIModule);
        if (ComboUI_SetAnchorRosterProvider)
            ComboUI_SetAnchorRosterProvider(&ComboAnchor::Combo_Anchor_GetRoster);
        if (ComboUI_SetNotesStore)
            ComboUI_SetNotesStore(&Combo_GetNotes, &Combo_SetNotes);
        if (ComboUI_Register) {
            ComboUI_Register();
            std::cout << "[ComboShip] comboui registered (unified menu installed)." << std::endl;
        } else {
            std::cerr << "[ComboShip] WARNING: comboui.dll missing ComboUI_Register" << std::endl;
        }
    } else {
        std::cerr << "[ComboShip] WARNING: failed to load comboui.dll (" << DllError() << ")" << std::endl;
    }

    // ComboShip: eagerly boot MM now (after OOT init) so the cross-world rando oracle runs against a
    // fully-initialized MM. Does one OOT->MM->OOT transition with MM's game loop skipped: hand the
    // foreground to MM (SOH_PrepareForTransition), boot MM without its loop (MM_BootForCombo), then
    // hand it back to OOT (MM_PrepareForTransition stops MM's audio; SOH_ResumeForeground re-activates
    // OOT's RM/audio/GUI). MM stays resident, so the first portal transition is a normal resume.
    bool mmEagerBooted = false;
    if (MM_BootForCombo && SOH_PrepareForTransition && MM_PrepareForTransition && SOH_ResumeForeground) {
        std::cout << "[ComboShip] Eager MM boot: begin" << std::endl;
        SOH_PrepareForTransition(); // stop OOT audio + tear down OOT GUI (Context/RM kept alive)
        MM_BootForCombo();          // full MM init on the shared Context, MM's RM active, no loop
        MM_PrepareForTransition();  // stop MM's audio (MM started it during InitOTR)
        SOH_ResumeForeground();     // re-activate OOT's RM/audio/GUI as the foreground game
        mmEagerBooted = true;
        std::cout << "[ComboShip] Eager MM boot: complete" << std::endl;
    } else {
        std::cerr << "[ComboShip] Eager MM boot: required exports missing — oracle will be unavailable" << std::endl;
    }

    // ComboShip: OOT owns the foreground at startup — hide MM's (now-registered) tracker windows so
    // only OOT's Check/Item trackers can show. See combo/gui/ComboTrackerVisibility.cpp.
    Combo_SetForegroundGame(ComboRando::GAME_OOT);

    // --- 5. Register OOT callbacks ---
    // Note: MM_InitArchives (dormant archive pre-load) is skipped — Ship::ArchiveManager::Init
    // requires a live context which doesn't exist until MM_RunMain runs InitOTR().
    // Archives are loaded correctly when MM_RunGame is called after OOT exits.

    // ComboShip: register the window-driven generate-request handler.
    // Generation is window-driven; Sram_InitSave only forces QUEST_RANDOMIZER.
    if (SOH_SetOnComboGenerateRequestCallback) {
        SOH_SetOnComboGenerateRequestCallback(Combo_OnGenerateThreaded);
        std::cout << "[ComboShip] Combo generate-request handler registered (threaded)." << std::endl;
    }
    // Share the single progress struct with soh.dll (read-only) and register the main-thread
    // finalize poll the file-select loop drives.
    if (SOH_SetComboProgressPtr)
        SOH_SetComboProgressPtr(&g_ComboProgress);
    if (SOH_SetOnComboFinalizeCallback)
        SOH_SetOnComboFinalizeCallback(Combo_PollFinalize);
    if (SOH_SetOnComboReloadCallback)
        SOH_SetOnComboReloadCallback(Combo_OnReloadRequest);

    // ComboShip: env-gated headless generate — COMBO_AUTOGEN_SEED=<seed> runs the cross-world
    // fill once at startup (timed) so fill changes are verifiable without driving the UI.
    if (const char* autogenSeed = std::getenv("COMBO_AUTOGEN_SEED")) {
        std::cout << "[ComboShip] COMBO_AUTOGEN_SEED='" << autogenSeed << "' — running fill\n";
        auto t0 = std::chrono::steady_clock::now();
        Combo_OnGenerateRequest(autogenSeed, nullptr);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "[ComboShip] autogen fill finished in " << ms << " ms" << std::endl;
    }

    // ComboShip: env-gated cross-world generation TEST — COMBO_GENTEST=<count> generates <count>
    // seeds and asserts each is fully completable (every advancement item reachable from an empty
    // start across both games). Exits the process with the failure count so it can run in CI.
    if (const char* genTest = std::getenv("COMBO_GENTEST")) {
        int n = std::atoi(genTest);
        if (n <= 0)
            n = 20;
        uint32_t seedBase = 1;
        if (const char* b = std::getenv("COMBO_GENTEST_SEED_BASE")) {
            seedBase = static_cast<uint32_t>(std::strtoul(b, nullptr, 10));
        }
        int failures = RunComboGenTest(n, seedBase);
        std::cout.flush();
        std::cerr.flush();
        std::exit(failures == 0 ? 0 : 1);
    }

    // ComboShip: env-gated playthrough log — COMBO_PLAYTHROUGH=<seed> generates that seed and writes a
    // sphere-by-sphere "what you grab, in what order, until Ganon+Majora are both killable" log.
    if (const char* ptSeed = std::getenv("COMBO_PLAYTHROUGH")) {
        RunComboPlaythrough(std::string(ptSeed));
        std::cout.flush();
        std::cerr.flush();
        std::exit(0);
    }

    if (SOH_SetOnNewSaveCallback && MM_InitRandoSaveFile) {
        SOH_SetOnNewSaveCallback(Combo_OnOOTSaveInit);
        std::cout << "[ComboShip] OOT new-save callback registered." << std::endl;
    }

    if (SOH_SetOnLoadSaveCallback && MM_LoadSaveForCombo) {
        SOH_SetOnLoadSaveCallback(Combo_OnOOTSaveLoad);
        std::cout << "[ComboShip] OOT save-load callback registered." << std::endl;
    }

    if (SOH_SetOnSceneSwitchCallback) {
        SOH_SetOnSceneSwitchCallback(Combo_OnOOTSceneSwitch);
        std::cout << "[ComboShip] OOT scene-switch callback registered." << std::endl;
    }

    // Merged per-slot save container: mediate each game's per-slot save IO through the launcher.
    if (SOH_SetComboSaveIO)
        SOH_SetComboSaveIO(&Combo_ReadGameSave, &Combo_WriteGameSave);
    if (MM_SetComboSaveIO)
        MM_SetComboSaveIO(&Combo_ReadGameSave, &Combo_WriteGameSave);
    if (SOH_SetCopyContainer)
        SOH_SetCopyContainer(&Combo_CopyContainer);
    // OOT owns the shared file-select; it polls for release-evicted slots and shows the outdated-save popup.
    if (SOH_SetOutdatedSaveNotice)
        SOH_SetOutdatedSaveNotice(&Combo_TakeEvictionNotice);

    // Cross-game erase seam (issue #1): erasing a save slot in either game wipes both saves.
    if (SOH_SetDeleteForeignSave)
        SOH_SetDeleteForeignSave(DeleteForeignSaveFromOOT);
    if (MM_SetDeleteForeignSave)
        MM_SetDeleteForeignSave(DeleteForeignSaveFromMM);

    // --- 6. Bidirectional game-switch loop ---
    // OOT boots first (SOH_RunMain), then each game's loop returns when it signals a switch:
    //   OOT sets g_PendingMMFileNum (Mask Shop) -> hand off / resume MM.
    //   MM sets g_pendingOOTReturn (Clock Tower) -> hand off / resume OOT.
    // The one-time per-process init (heaps/threads) runs only on the FIRST entry into each game;
    // subsequent entries resume the existing process on the shared context/window.

    enum ComboGame { GAME_OOT, GAME_MM };
    ComboGame current = GAME_OOT;
    bool ootBooted = false;
    // MM was already booted at startup (eager boot) — the first portal transition must RESUME MM,
    // not run MM_RunGame (which would re-run MM_RunMain on an already-initialized MM).
    bool mmBooted = mmEagerBooted;
    for (;;) {
        if (current == GAME_OOT) {
            g_PendingMMFileNum = -1;
            if (!ootBooted) {
                std::cout << "[ComboShip] OOT boot\n";
                SOH_RunMain(argc, argv);
                ootBooted = true;
            } else {
                std::cout << "[ComboShip] OOT resume\n";
                if (SOH_ResumeGame)
                    SOH_ResumeGame();
            }
            if (g_PendingMMFileNum >= 0 && MM_RunGame) {
                if (SOH_PrepareForTransition)
                    SOH_PrepareForTransition();
                if (MM_NotifyComboTransition)
                    MM_NotifyComboTransition();
                if (MM_SetOnComboReturnCallback)
                    MM_SetOnComboReturnCallback(Combo_OnMMReturn);
                ComboAnchor::SetActiveGame(1);                // route Anchor to MM, activate MM's adapter
                Combo_SetForegroundGame(ComboRando::GAME_MM); // hide OOT trackers, show MM's
                if (g_PendingMMFileNum >= 0)
                    Combo_SetLastGame(g_PendingMMFileNum, ComboRando::GAME_MM);
                current = GAME_MM;
            } else {
                break;
            }
        } else {
            g_pendingOOTReturn = false;
            // MM's own boot/resume path loads this slot's save into gSaveContext.
            g_MmSaveInMemorySlot = g_PendingMMFileNum;
            if (!mmBooted) {
                std::cout << "[ComboShip] MM boot\n";
                MM_RunGame(g_PendingMMFileNum);
                mmBooted = true;
            } else {
                std::cout << "[ComboShip] MM resume\n";
                if (MM_ResumeGame)
                    MM_ResumeGame(g_PendingMMFileNum);
            }
            if (g_pendingOOTReturn) {
                if (MM_PrepareForTransition)
                    MM_PrepareForTransition();
                if (SOH_NotifyComboReturn)
                    SOH_NotifyComboReturn();
                ComboAnchor::SetActiveGame(0);                 // route Anchor back to OOT, deactivate MM's adapter
                Combo_SetForegroundGame(ComboRando::GAME_OOT); // hide MM trackers, restore OOT's
                if (g_PendingMMFileNum >= 0) {
                    if (g_mmReturnKind == 0) {
                        // Portal return: the player is continuing in OOT, so that's where a reload goes.
                        // A reset or owl-save quit ends the session in MM — leave lastGame alone.
                        Combo_SetLastGame(g_PendingMMFileNum, ComboRando::GAME_OOT);
                    } else {
                        // Session over: MM's dormant gSaveContext is post-quit state, so force the next
                        // save-load to re-read it from the container (else the tracker peek shows junk).
                        g_MmSaveInMemorySlot = -1;
                    }
                }
                current = GAME_OOT;
            } else {
                break;
            }
        }
    }

    // Teardown order is load-bearing: MM before SOH (MM holds a shared_ptr to the SHARED Context;
    // SOH's DeinitOTR releases the last ref, running ~Context here — saves geometry/config, destroys
    // the window). All thread-owners must be joined before the FreeDll calls (joining under the loader
    // lock deadlocks). std::cerr markers: spdlog dies mid-teardown. See docs/deviations/boot-shutdown.md.

    // Join the generate worker before unloading any game DLL it calls into — a still-joinable
    // std::thread would std::terminate() at static destruction, and the worker must not run past
    // the DLLs it touches.
    if (g_GenerateThread.joinable()) {
        std::cerr << "[ComboShip] shutdown: joining generate worker" << std::endl;
        g_GenerateThread.join();
    }

    // Stop the Anchor receive thread first: it calls into soh.dll exports, so it must be joined
    // while soh.dll is still mapped and before SOH_Deinit tears Anchor down.
    std::cerr << "[ComboShip] shutdown: Anchor disconnect" << std::endl;
    ComboAnchor::Shutdown();

    // ComboShip: the active-game gating zeroes the backgrounded game's tracker CVars. Restore both
    // games' remembered intent now, before SOH_Deinit's ~Context saves config — otherwise a game
    // that was backgrounded at exit would persist its tracker as "off". (comboui is still mapped.)
    if (ComboUI_RestoreTrackerIntent)
        ComboUI_RestoreTrackerIntent();

    if (MM_Deinit && mmBooted) {
        std::cerr << "[ComboShip] shutdown: MM_Deinit" << std::endl;
        MM_Deinit();
    }
    if (SOH_Deinit) {
        std::cerr << "[ComboShip] shutdown: SOH_Deinit" << std::endl;
        SOH_Deinit();
    }
    std::cerr << "[ComboShip] shutdown: deinit done" << std::endl;
#ifdef _WIN32
    // ~Context destroyed lus's CrashHandler (and its filter is Context-dependent anyway);
    // install the late-crash filter for the FreeLibrary + CRT-exit window.
    SetUnhandledExceptionFilter(ComboLateCrashFilter);
#endif

    // --- 7. Cleanup ---

    if (comboUIModule)
        FreeDll(comboUIModule);
    std::cerr << "[ComboShip] shutdown: comboui freed" << std::endl;
    FreeDll(mmModule);
    std::cerr << "[ComboShip] shutdown: 2ship freed" << std::endl;
    FreeDll(sohModule);
    std::cerr << "[ComboShip] shutdown: soh freed - exiting normally" << std::endl;
    return 0;
}
