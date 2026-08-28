// ComboShip: every soh.dll / 2ship.dll / comboui.dll export the launcher resolves, plus the two
// resolve entry points. Pointers stay at global scope with the exact export name, so a call site
// reads the same as the symbol and scripts/check-export-bindings.ps1 can verify the pairing.
#pragma once

#include <cstdint>

#include "core/ComboPlatform.h"
#include "ComboExtract.h"
#include "ComboSettingsImport.h"
#include "rando/CrossForeign.h"
#include "gui/ComboGenProgress.h"

// Resolves every soh/2ship export. Missing optional exports stay null and are checked at each call
// site; main() gates on the required ones after this returns.
void ComboResolveGameExports(DllHandle sohModule, DllHandle mmModule);

// Resolves comboui's exports. The launcher registers its providers afterwards, in main().
void ComboResolveComboUiExports(DllHandle comboUIModule);

// ---------- Function pointer types ----------

typedef void (*FnVoid)();
typedef bool (*FnExtract)(const char*);
typedef void (*FnRunMain)(int, char**);
typedef int (*FnInt)();
typedef void (*FnSetSaveCallback)(void (*)(int));
typedef void (*FnMMInitSave)(int);
typedef void (*FnSetSceneSwitchCallback)(void (*)(int));
typedef void (*FnMMRunGame)(int);
typedef void (*FnSOHDeinit)();
typedef void (*FnSOHPrepare)();
typedef void (*FnMMNotify)();
extern FnVoid SOH_Init;
extern FnExtract SOH_Extract;
extern FnRunMain SOH_RunMain;
extern FnVoid MM_InitArchives;
extern FnExtract MM_Extract;
extern FnInt MM_ArchiveCount;
extern FnSetSaveCallback SOH_SetOnNewSaveCallback;
extern FnSetSaveCallback SOH_SetOnLoadSaveCallback;
typedef void (*FnGetPlayerName)(unsigned char*);
extern FnGetPlayerName SOH_GetCurrentPlayerName;
// Nonzero = the slot's MM half is missing/broken; nothing was loaded (see SaveManager_LoadSaveFile).
typedef int (*FnMMLoadSave)(int);
extern FnMMLoadSave MM_LoadSaveForCombo;
// ComboShip (#182): MM caches which slot's owl save its gSaveContext came from; tell it when the
// launcher replaces a slot's mm section underneath it.
typedef void (*FnMMInvalidateOwlBlob)(void);
extern FnMMInvalidateOwlBlob MM_InvalidateOwlBlobSlot;
// #89 resume-into-MM: drop out of OOT's loop before it runs a Play frame / tell MM how it was entered.
// SOH_IsOnFileSelect distinguishes a real file-select load from the OnLoadGame that TitleSetup fires
// on the MM->OOT return (which must not bounce the player straight back into MM).
typedef unsigned char (*FnIsOnFileSelect)();
extern FnVoid SOH_ParkForComboMMResume;
extern FnMMInitSave MM_SetComboEntryIsResume;
extern FnIsOnFileSelect SOH_IsOnFileSelect;
extern FnSetSceneSwitchCallback SOH_SetOnSceneSwitchCallback;
extern FnMMRunGame MM_RunGame;
extern FnSOHDeinit SOH_Deinit;
extern FnSOHPrepare SOH_PrepareForTransition;
extern FnMMNotify MM_NotifyComboTransition;

typedef void (*FnMMSetReturnCb)(void (*)(int));
extern FnMMSetReturnCb MM_SetOnComboReturnCallback;

typedef void (*FnVoidArgless)(void);
extern FnVoidArgless SOH_ResumeGame;
extern FnVoidArgless SOH_NotifyComboReturn;

typedef void (*FnMMResume)(int);
extern FnMMResume MM_ResumeGame;
extern FnVoidArgless MM_PrepareForTransition;

// ComboShip: headless static-data dump exports
typedef const char* (*FnDumpData)(void);
extern FnDumpData SOH_DumpRandoStaticData;
extern FnDumpData MM_DumpRandoStaticData;
extern FnDumpData SOH_DumpRandoSettings; // {cvar:value} OOT rando settings snapshot
extern FnDumpData SOH_DumpEnabledTricks; // [NameTag,...] the player's enabled OOT tricks
extern FnDumpData MM_DumpRandoSettings; // {cvar:value} MM rando settings snapshot
extern FnDumpData SOH_DumpRandoHintData; // OOT hint text/options schema (cross-hint Phase 2)
// ComboShip: cross-hint Phase 3 — apply combo-generated hints + tell OOT whether this seed has any.
typedef void (*FnApplyHints)(const char*);
typedef void (*FnSetHintsPresent)(int);
extern FnApplyHints SOH_ApplyComboHints;
extern FnSetHintsPresent SOH_SetComboHintsPresent;
// #169: OOT's generation-completion hooks (cosmetics/audio randomize-on-gen) — combo's fill never
// reaches the vanilla fire site, so the launcher fires them itself (see Combo_FireGenRollHooksOnce).
extern FnVoidArgless SOH_FireGenerationCompleteHooks;
// Reload/remember-seed: restore settings + run the pool prep before re-applying saved placements.
typedef void (*FnVoidV)(void);
typedef void (*FnTakeStr)(const char*);
typedef void (*FnSetReloadCb)(int (*)(const char*));
extern FnVoidV SOH_PrepRandoContext;
extern FnTakeStr SOH_RestoreRandoSettings;
extern FnTakeStr MM_RestoreRandoSettings;
extern FnTakeStr SOH_SetCheckPrices;
extern FnTakeStr MM_SetCheckPrices;
extern FnSetReloadCb SOH_SetOnComboReloadCallback;
// Remembered spoiler path (CVAR_GENERAL("ComboSpoiler")) — soh owns the config, so the launcher goes
// through it rather than parsing comboship.json itself.
extern FnTakeStr SOH_SetComboSpoilerPath;
extern FnDumpData SOH_GetComboSpoilerPath;

// ComboShip merged per-slot save container: setters that push the launcher's save-IO callbacks into
// each DLL, plus the once-per-load push of the baked combo rando (foreign map + cross-hints).
typedef void (*FnSetComboSaveIO)(ComboRando::FnComboReadSave, ComboRando::FnComboWriteSave);
extern FnSetComboSaveIO SOH_SetComboSaveIO;
extern FnSetComboSaveIO MM_SetComboSaveIO;
extern FnTakeStr SOH_LoadComboRando;
extern FnTakeStr MM_LoadComboRando;
// OOT file-select "copy file": whole-container copy (both games + rando) through the launcher.
typedef void (*FnSetCopyContainer)(void (*)(int, int));
extern FnSetCopyContainer SOH_SetCopyContainer;
// OOT polls this each frame (main thread) for slots whose container was backed up for a release mismatch.
typedef void (*FnSetOutdatedSaveNotice)(int (*)());
extern FnSetOutdatedSaveNotice SOH_SetOutdatedSaveNotice;

// ComboShip: OOT forced placements (Link's Pocket etc.) the static dump can't carry — see
// SOH_GetForcedPlacements. Seed-parameterized so the pick is deterministic per generated seed.
typedef const char* (*FnGetForced)(uint32_t);
extern FnGetForced SOH_GetForcedPlacements;

// ComboShip: eager MM boot at startup (replaces the headless MM_InitRandoLogic warm-up).
extern FnVoidArgless MM_BootForCombo;
extern FnVoidArgless SOH_ResumeForeground;
extern FnVoidArgless MM_Deinit;

typedef void (*FnComboUIRegister)(void);
extern FnComboUIRegister ComboUI_Register;

// ComboShip: tracker visibility follows the active game (see combo/gui/ComboTrackerVisibility.cpp).
typedef void (*FnComboUIForeground)(int);
extern FnComboUIForeground ComboUI_OnForegroundGame;
extern FnComboUIRegister ComboUI_RestoreTrackerIntent;

// ComboShip: hand comboui the launcher-owned Anchor roster getter (the room window reads it).
typedef void (*FnComboUISetRosterProvider)(const char* (*)());
extern FnComboUISetRosterProvider ComboUI_SetAnchorRosterProvider;

// ComboShip (#165): hand comboui the launcher-owned per-slot notes accessors (combo.notes).
typedef void (*FnComboUISetNotesStore)(const char* (*)(int), void (*)(int, const char*));
extern FnComboUISetNotesStore ComboUI_SetNotesStore;

// ComboShip (#164): combo Hint Tracker — push the slot's hints slice + read state into comboui, and
// receive reveal reports back from both game DLLs.
typedef void (*FnComboUISetHintTrackerData)(int, const char*, const char*);
extern FnComboUISetHintTrackerData ComboUI_SetHintTrackerData;
typedef void (*FnSetHintRevealOot)(void (*)(int, const char*));
extern FnSetHintRevealOot SOH_SetComboHintRevealCb;
typedef void (*FnSetHintRevealMm)(void (*)(int, int, int, const char*, const char*));
extern FnSetHintRevealMm MM_SetComboHintRevealCb;

// ComboShip (#173): combo-owned overlay timers. MM's play time is wall clock between flushes, so it
// must be paused/resumed across every game swap or the time spent in OOT lands in MM's save.
typedef void (*FnComboUISetInt)(int);
extern FnComboUISetInt ComboUI_SetComboComplete;
extern FnVoidArgless MM_ComboPausePlaytime;
extern FnVoidArgless MM_ComboResumePlaytime;

// ComboShip (#169): combo-owned OOT->MM cosmetic color sync (combo/gui/ComboCosmeticsSync.cpp). The
// gate predicate is exported too, so the launcher never duplicates the CVar reads.
extern FnVoidArgless ComboUI_SyncRandomizedCosmetics;
typedef int (*FnComboUIGate)(void);
extern FnComboUIGate ComboUI_CosmeticsSyncGateEnabled;
// Per-seed latch for the gen-roll hooks (comboui owns it — the exe has no CVar API). 1 = not rolled yet.
typedef int (*FnClaimGenRollSeed)(unsigned long long);
extern FnClaimGenRollSeed ComboUI_ClaimGenRollSeed;


// ComboShip-owned unified ROM extraction (see ComboExtract.h). The split init lets us create the
// shared window from soh.o2r before any ROM exists, run the extraction screen, then finish.
extern FnVoid SOH_InitWindowOnly;
extern FnVoid SOH_FinishInit;
extern ComboFnValidateRom SOH_ValidateRom;
extern ComboFnValidateRom SOH_ClassifyRom;
extern ComboFnStartExtraction SOH_StartExtraction;
extern ComboFnGetProgress SOH_GetExtractionProgress;
extern ComboFnValidateRom MM_ValidateRom;
extern ComboFnValidateRom MM_ClassifyRom;
extern ComboFnStartExtraction MM_StartExtraction;
extern ComboFnGetProgress MM_GetExtractionProgress;
extern ComboFnRunExtraction ComboUI_RunExtraction;

// ComboShip-owned first-launch settings import (see ComboSettingsImport.h). comboui renders the
// screen; soh applies the launcher-merged config to the live Config.
extern ComboFnRunSettingsImport ComboUI_RunSettingsImport;
extern ComboFnApplyImportedConfig SOH_ApplyImportedConfig;

// ComboShip: per-game reachability oracle exports
typedef void (*FnOracleVoid)(void);
typedef void (*FnOracleSetItems)(const char*);
typedef const char* (*FnOracleGetChecks)(void);
typedef void (*FnOraclePlaceItem)(const char*, const char*);
typedef uint8_t (*FnOracleGetPortalOpen)(void);

extern FnOracleVoid Combo_SOH_Rando_Reset;
extern FnOracleSetItems Combo_SOH_Rando_SetOwnedItems;
extern FnOracleGetChecks Combo_SOH_Rando_GetReachableChecks;
extern FnOraclePlaceItem Combo_SOH_Rando_PlaceItem;
// OOT->MM portal gate (Happy Mask Shop region access) — see CrossWorldRando.h.
extern FnOracleGetPortalOpen Combo_SOH_Rando_GetPortalOpen;

extern FnOracleVoid Combo_MM_Rando_Reset;
extern FnOracleSetItems Combo_MM_Rando_SetOwnedItems;
extern FnOracleGetChecks Combo_MM_Rando_GetReachableChecks;
extern FnOraclePlaceItem Combo_MM_Rando_PlaceItem;
extern FnOracleVoid Combo_MM_Rando_Restore;

// ComboShip (#90): OOT entrance shuffle — the combo generator never runs native Fill(), so the
// entrance options need this explicit headless shuffle + an informational spoiler dump.
typedef int (*FnShuffleEntrances)(uint64_t);
extern FnShuffleEntrances SOH_ShuffleEntrancesForCombo;
extern FnDumpData SOH_DumpEntranceOverrides;

typedef void (*FnSetDeleteForeignSave)(void (*)(int));
typedef void (*FnDeleteSaveFile)(int);
extern FnSetDeleteForeignSave SOH_SetDeleteForeignSave;
extern FnSetDeleteForeignSave MM_SetDeleteForeignSave;
extern FnDeleteSaveFile SOH_DeleteSaveFile;
extern FnDeleteSaveFile MM_DeleteSaveFile;

// ComboShip: placement injection exports
typedef void (*FnSetGenerateCb)(void (*)(int));
typedef void (*FnApplyPlacements)(const char*);
// Returns 0 on success; nonzero means the placement apply failed and the slot has no MM placements.
typedef int (*FnMMInitRandoSave)(int, const char*, const unsigned char*);
typedef void (*FnSetComboRandoSeed)(uint64_t);
typedef void (*FnSetComboSeedHash)(uint32_t);
extern FnSetGenerateCb SOH_SetOnComboGenerateCallback;
extern FnApplyPlacements SOH_ApplyRandoPlacements;
extern FnMMInitRandoSave MM_InitRandoSaveFile;
extern FnSetComboRandoSeed SOH_SetComboRandoSeed;
extern FnSetComboRandoSeed MM_SetComboRandoSeed;
extern FnSetComboSeedHash SOH_SetComboSeedHash;

// ComboShip: window-driven generate request (threaded, progress-reporting)
typedef void (*FnSetGenReqCb)(void (*)(const char*));
typedef void (*FnSetSeedGenerated)(uint8_t);
typedef void (*FnSetComboProgressPtr)(const ComboRando::ComboGenProgress*);
typedef void (*FnSetComboFinalizeCb)(int (*)());
extern FnSetGenReqCb SOH_SetOnComboGenerateRequestCallback;
extern FnSetSeedGenerated SOH_SetSeedGenerated;
extern FnSetComboProgressPtr SOH_SetComboProgressPtr;
extern FnSetComboFinalizeCb SOH_SetOnComboFinalizeCallback;

// ---------- ComboShip-owned Anchor connection (Phase 1) ----------
// The persistent socket + receive thread live HERE (launcher) so the connection survives OOT<->MM
// transitions. soh's Anchor keeps its packet/handler/menu logic but redirects transport through the
// callbacks registered below and receives inbound via SOH_Anchor_RecvJson. See docs/deviations/anchor.md.
typedef void (*FnSetAnchorSend)(void (*)(const char*));
typedef void (*FnSetAnchorConnect)(void (*)(const char*, uint16_t));
typedef void (*FnSetAnchorDisconnect)(void (*)(void));
typedef void (*FnAnchorRecv)(const char*);
extern FnSetAnchorSend SOH_SetAnchorSend;
extern FnSetAnchorConnect SOH_SetAnchorConnect;
extern FnSetAnchorDisconnect SOH_SetAnchorDisconnect;
extern FnAnchorRecv SOH_Anchor_RecvJson;
extern FnVoidArgless SOH_Anchor_OnConnected;
extern FnVoidArgless SOH_Anchor_OnDisconnected;
// Bug 2: launcher-orchestrated resync, dormant-safe (see ComboAnchor::RequestFullResync below).
extern FnVoidArgless SOH_Anchor_RequestResync;

// MM Anchor adapter exports (Phase 2). MM piggybacks on the same launcher-owned connection; it is
// activated/deactivated on transitions and receives inbound packets when it is the active game.
extern FnSetAnchorSend MM_SetAnchorSend;
extern FnAnchorRecv MM_Anchor_RecvJson;
extern FnVoidArgless MM_Anchor_Activate;
extern FnVoidArgless MM_Anchor_Deactivate;
extern FnVoidArgless MM_Anchor_RequestResync;

// A6: live dormant-game co-op sync. The launcher feeds every inbound packet to BOTH games; the active
// game calls the registered pump each frame so the dormant sibling applies save-affecting packets on
// the game thread (never the receive thread — that would race the active game's save writes).
typedef void (*FnSetPumpDormant)(void (*)());
extern FnSetPumpDormant SOH_SetPumpDormant;
extern FnSetPumpDormant MM_SetPumpDormant;
extern FnVoidArgless SOH_Anchor_PumpDormant;
extern FnVoidArgless MM_Anchor_PumpDormant;

// Cross-game item delivery seam (issue #3). Each game's foreign-check detection (and the Anchor
// receive path) routes an item to the OTHER game through one launcher-owned dispatcher, which calls
// the target DLL's save-only grant export. The same dispatcher serves the single-player and
// networked paths. targetGame/srcGame use the GameId convention 0 = OOT, 1 = MM (== sActiveGame).
typedef void (*FnSetCrossRoute)(void (*)(int, const char*));
// Deliver callback carries srcCheckName too (bug 3: keys the launcher-side receive dedup below).
typedef void (*FnSetCrossDeliver)(void (*)(int, const char*, const char*));
typedef void (*FnGrantCrossItem)(const char*);
extern FnSetCrossDeliver SOH_SetCrossDeliver;
extern FnSetCrossDeliver MM_SetCrossDeliver;
extern FnGrantCrossItem SOH_GrantCrossItem;
extern FnGrantCrossItem MM_GrantCrossItem;
extern FnSetCrossRoute SOH_SetMarkForeignObtained;
extern FnSetCrossRoute MM_SetMarkForeignObtained;
extern FnGrantCrossItem SOH_MarkForeignObtained;
extern FnGrantCrossItem MM_MarkForeignObtained;

// ComboShip: gate the ending on BOTH final bosses. Each game calls the registered callback when its
// final boss dies (OOT Ganon / MM Majora): it records the kill in the per-slot completion sidecar and
// returns 1 iff both are now dead. The game then plays its native ending (finale) or warps the player
// back to the cross-game portal to finish the other game. See docs/UPSTREAM_MERGES.md.
typedef void (*FnSetBossDefeatedCb)(int (*)(int, int));
extern FnSetBossDefeatedCb SOH_SetFinalBossDefeatedCb;
extern FnSetBossDefeatedCb MM_SetFinalBossDefeatedCb;

// ComboShip (#136): Triforce Hunt is ONE combined goal — the launcher pushes it into both DLLs, sums
// both counters on every piece grant/merge, and dispatches the ending itself.
typedef void (*FnSetComboGoal)(int hunt, int required, int pieces);
typedef int (*FnReadComboGoalCVars)(int* required, int* total);
typedef int (*FnGetTriforceCount)(void);
typedef void (*FnTriggerTriforceCredits)(int dormant);
typedef void (*FnSetTriforceProgressCb)(void (*)(int, int));
typedef void (*FnSetOtherTriforceCountCb)(int (*)(void));
extern FnSetComboGoal SOH_SetComboGoal;
extern FnSetComboGoal MM_SetComboGoal;
extern FnReadComboGoalCVars SOH_ReadComboGoalCVars;
extern FnGetTriforceCount SOH_GetTriforcePieceCount;
extern FnGetTriforceCount MM_GetTriforcePieceCount;
extern FnTriggerTriforceCredits SOH_TriggerTriforceCredits;
extern FnTriggerTriforceCredits MM_TriggerTriforceCredits;
extern FnSetTriforceProgressCb SOH_SetTriforceProgressCb;
extern FnSetTriforceProgressCb MM_SetTriforceProgressCb;
extern FnSetOtherTriforceCountCb SOH_SetOtherTriforceCountCb;
extern FnSetOtherTriforceCountCb MM_SetOtherTriforceCountCb;

// ComboShip (#135): starting game. The menu CVar may say Random; the launcher resolves it per seed and
// pushes the concrete value, which soh's FinalizeSettings turns into forced age/forest/exclusions.
typedef void (*FnSetComboStartingGame)(int mmStart);
typedef int (*FnReadComboStartingGameCVar)(void);
extern FnSetComboStartingGame SOH_SetComboStartingGame;
extern FnReadComboStartingGameCVar SOH_ReadComboStartingGameCVar;
