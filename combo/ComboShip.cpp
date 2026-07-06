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
#include <thread>
#include <mutex>
#include <queue>

// SDL_net.h pulls in SDL.h -> SDL_main.h, which #defines main -> SDL_main unless we opt out.
// ComboShip provides its own main(), so suppress SDL's entry-point hijack.
#define SDL_MAIN_HANDLED
#include <SDL2/SDL_net.h>

#include "rando/CrossForeign.h"
#include "rando/CrossWorldRando.h"
#include "rando/CrossEntrances.h"
#include "gui/ComboGenProgress.h"
#include "ComboExtract.h"
#include "ComboSettingsImport.h"

// Surfaces the real exception behind a silent terminate()/exit(3). With the shared dynamic
// CRT, exceptions thrown in soh.dll/2ship.dll propagate across the DLL boundary to here.
static void ComboTerminateHandler() {
    std::cerr << "[ComboShip] std::terminate";
    if (auto ep = std::current_exception()) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) { std::cerr << " — uncaught std::exception: " << e.what(); } catch (...) {
            std::cerr << " — uncaught non-std exception";
        }
    }
    std::cerr << std::endl;
    std::abort();
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <dlfcn.h>
#endif

#ifdef _WIN32
// Last-chance crash capture for LATE crashes (post-Context teardown, FreeLibrary, CRT exit /
// static destructors). libultraship's CrashHandler can't cover this window: its seh_filter
// dereferences Context::GetInstance(), which is already an expired weak_ptr by then, and the
// handler itself is destroyed with the Context. Installed after the deinit calls in main;
// ComboShip.exe stays loaded through process exit so the filter survives DLL unloads.
// Writes module+symbol frames to combo_late_crash.txt (PDBs sit next to the DLLs in Debug).
static LONG WINAPI ComboLateCrashFilter(PEXCEPTION_POINTERS ex) {
    FILE* f = nullptr;
    fopen_s(&f, "combo_late_crash.txt", "w");
    if (!f) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    fprintf(f, "[ComboShip] late crash: exception 0x%08lX at %p\n", ex->ExceptionRecord->ExceptionCode,
            ex->ExceptionRecord->ExceptionAddress);

    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);

    CONTEXT ctx = *ex->ContextRecord;
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    for (int i = 0; i < 64; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        char module[MAX_PATH] = "???";
        HMODULE hMod = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)frame.AddrPC.Offset, &hMod);
        if (hMod) {
            GetModuleFileNameA(hMod, module, sizeof(module));
        }
        char symBuf[sizeof(SYMBOL_INFO) + 256] = {};
        SYMBOL_INFO* sym = (SYMBOL_INFO*)symBuf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &disp, sym)) {
            fprintf(f, "  %s + 0x%llx in %s\n", sym->Name, (unsigned long long)disp, module);
        } else {
            fprintf(f, "  0x%llx in %s\n", (unsigned long long)frame.AddrPC.Offset, module);
        }
    }
    fclose(f);
    return EXCEPTION_EXECUTE_HANDLER; // die quietly; the report is on disk
}
#endif

// ---------- DLL helpers ----------

#ifdef _WIN32
typedef HMODULE DllHandle;
static DllHandle LoadDll(const char* name) {
    return LoadLibraryA(name);
}
static void* GetSym(DllHandle h, const char* sym) {
    return (void*)GetProcAddress(h, sym);
}
static void FreeDll(DllHandle h) {
    FreeLibrary(h);
}
static std::string DllError() {
    return std::to_string(GetLastError());
}
#else
typedef void* DllHandle;
static DllHandle LoadDll(const char* name) {
    return dlopen(name, RTLD_NOW | RTLD_GLOBAL);
}
static void* GetSym(DllHandle h, const char* sym) {
    return dlsym(h, sym);
}
static void FreeDll(DllHandle h) {
    dlclose(h);
}
static std::string DllError() {
    return dlerror();
}
#endif

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
static FnVoid SOH_Init = nullptr;
static FnExtract SOH_Extract = nullptr;
static FnRunMain SOH_RunMain = nullptr;
static FnVoid MM_InitArchives = nullptr;
static FnExtract MM_Extract = nullptr;
static FnInt MM_ArchiveCount = nullptr;
static FnSetSaveCallback SOH_SetOnNewSaveCallback = nullptr;
static FnSetSaveCallback SOH_SetOnLoadSaveCallback = nullptr;
static FnMMInitSave MM_InitSaveFile = nullptr;
static FnMMInitSave MM_LoadSaveForCombo = nullptr;
// OOT slot whose MM save is live in MM's dormant memory (-1 = none). Guards Combo_OnOOTSaveLoad
// against reloading stale disk state over MM's in-memory progress after round trips.
static int g_MmSaveInMemorySlot = -1;
static FnSetSceneSwitchCallback SOH_SetOnSceneSwitchCallback = nullptr;
static FnMMRunGame MM_RunGame = nullptr;
static FnSOHDeinit SOH_Deinit = nullptr;
static FnSOHPrepare SOH_PrepareForTransition = nullptr;
static FnMMNotify MM_NotifyComboTransition = nullptr;

typedef void (*FnMMSetReturnCb)(void (*)(void));
static FnMMSetReturnCb MM_SetOnComboReturnCallback = nullptr;
static bool g_pendingOOTReturn = false;

typedef void (*FnVoidArgless)(void);
static FnVoidArgless SOH_ResumeGame = nullptr;
static FnVoidArgless SOH_NotifyComboReturn = nullptr;

typedef void (*FnMMResume)(int);
static FnMMResume MM_ResumeGame = nullptr;
static FnVoidArgless MM_PrepareForTransition = nullptr;

// ComboShip: cross-interiors PoC — carry a target entrance across the switch (both directions).
// Get* drains the source game's staged target; Set* stages the target game's arrival override.
typedef int (*FnGetCrossTarget)(void);
typedef void (*FnSetTargetEntrance)(int);
static FnGetCrossTarget SOH_GetPendingCrossTarget = nullptr;
static FnSetTargetEntrance SOH_SetTargetEntrance = nullptr;
static FnGetCrossTarget MM_GetPendingCrossTarget = nullptr;
static FnSetTargetEntrance MM_SetTargetEntrance = nullptr;

// ComboShip: headless static-data dump exports
typedef const char* (*FnDumpData)(void);
static FnDumpData SOH_DumpRandoStaticData = nullptr;
static FnDumpData MM_DumpRandoStaticData = nullptr;
static FnDumpData SOH_DumpRandoSettings = nullptr; // {cvar:value} OOT rando settings snapshot
static FnDumpData MM_DumpRandoSettings = nullptr;  // {cvar:value} MM rando settings snapshot
// Reload/remember-seed: restore settings + run the pool prep before re-applying saved placements.
typedef void (*FnVoidV)(void);
typedef void (*FnTakeStr)(const char*);
typedef void (*FnSetReloadCb)(int (*)(const char*));
static FnVoidV SOH_PrepRandoContext = nullptr;
static FnTakeStr SOH_RestoreRandoSettings = nullptr;
static FnTakeStr MM_RestoreRandoSettings = nullptr;
static FnSetReloadCb SOH_SetOnComboReloadCallback = nullptr;

// ComboShip: OOT forced placements (Link's Pocket etc.) the static dump can't carry — see
// SOH_GetForcedPlacements. Seed-parameterized so the pick is deterministic per generated seed.
typedef const char* (*FnGetForced)(uint32_t);
static FnGetForced SOH_GetForcedPlacements = nullptr;

// ComboShip: eager MM boot at startup (replaces the headless MM_InitRandoLogic warm-up).
static FnVoidArgless MM_BootForCombo = nullptr;
static FnVoidArgless SOH_ResumeForeground = nullptr;
static FnVoidArgless MM_Deinit = nullptr;

typedef void (*FnComboUIRegister)(void);
static DllHandle comboUIModule = nullptr;
static FnComboUIRegister ComboUI_Register = nullptr;

// ComboShip: tracker visibility follows the active game (see combo/gui/ComboTrackerVisibility.cpp).
typedef void (*FnComboUIForeground)(int);
static FnComboUIForeground ComboUI_OnForegroundGame = nullptr;
static FnComboUIRegister ComboUI_RestoreTrackerIntent = nullptr;

// ComboShip-owned unified ROM extraction (see ComboExtract.h). The split init lets us create the
// shared window from soh.o2r before any ROM exists, run the extraction screen, then finish.
static FnVoid SOH_InitWindowOnly = nullptr;
static FnVoid SOH_FinishInit = nullptr;
static ComboFnValidateRom SOH_ValidateRom = nullptr;
static ComboFnStartExtraction SOH_StartExtraction = nullptr;
static ComboFnGetProgress SOH_GetExtractionProgress = nullptr;
static ComboFnValidateRom MM_ValidateRom = nullptr;
static ComboFnStartExtraction MM_StartExtraction = nullptr;
static ComboFnGetProgress MM_GetExtractionProgress = nullptr;
static ComboFnRunExtraction ComboUI_RunExtraction = nullptr;

// ComboShip-owned first-launch settings import (see ComboSettingsImport.h). comboui renders the
// screen; soh applies the launcher-merged config to the live Config.
static ComboFnRunSettingsImport ComboUI_RunSettingsImport = nullptr;
static ComboFnApplyImportedConfig SOH_ApplyImportedConfig = nullptr;

// ComboShip: per-game reachability oracle exports
typedef void (*FnOracleVoid)(void);
typedef void (*FnOracleSetItems)(const char*);
typedef const char* (*FnOracleGetChecks)(void);
typedef void (*FnOraclePlaceItem)(const char*, const char*);

static FnOracleVoid Combo_SOH_Rando_Reset = nullptr;
static FnOracleSetItems Combo_SOH_Rando_SetOwnedItems = nullptr;
static FnOracleGetChecks Combo_SOH_Rando_GetReachableChecks = nullptr;
static FnOraclePlaceItem Combo_SOH_Rando_PlaceItem = nullptr;

static FnOracleVoid Combo_MM_Rando_Reset = nullptr;
static FnOracleSetItems Combo_MM_Rando_SetOwnedItems = nullptr;
static FnOracleGetChecks Combo_MM_Rando_GetReachableChecks = nullptr;
static FnOraclePlaceItem Combo_MM_Rando_PlaceItem = nullptr;
static FnOracleVoid Combo_MM_Rando_Restore = nullptr;

// ComboShip: entrance-shuffle wiring (docs/ENTRANCE_RANDO_PREP.md §3). OOT: headless shuffle +
// override dump + portal-gate region query. MM: shared finalSeed + shuffle-failure probe.
typedef int (*FnShuffleEntrances)(uint64_t);
typedef int (*FnIsRegionReachable)(const char*);
typedef void (*FnSetFinalSeed)(uint32_t);
typedef int (*FnIntV)(void);
typedef int (*FnGetCVarInt)(const char*, int);
static FnShuffleEntrances SOH_ShuffleEntrancesForCombo = nullptr;
static FnDumpData SOH_DumpEntranceOverrides = nullptr;
static FnIsRegionReachable Combo_SOH_Rando_IsRegionReachable = nullptr;
static FnSetFinalSeed MM_SetComboFinalSeed = nullptr;
static FnIntV Combo_MM_Rando_EntranceShuffleOk = nullptr;
static FnDumpData MM_DumpEntranceMap = nullptr;

// Cross-game entrance shuffle (Phase B, docs §4): pool dumps, per-game table pushes, oracle
// extensions, and the toggle read (via soh.dll — the launcher doesn't link libultraship).
static FnDumpData SOH_DumpInteriorEntrancePairs = nullptr;
static FnDumpData MM_DumpInteriorEntrancePairs = nullptr;
static FnTakeStr SOH_SetCrossEntranceTable = nullptr;
static FnTakeStr MM_SetCrossEntranceTable = nullptr;
static FnTakeStr Combo_SOH_Rando_SetExternallyReachableRegions = nullptr;
static FnTakeStr Combo_MM_Rando_SetExternallyReachableRegions = nullptr;
static FnIsRegionReachable Combo_MM_Rando_IsRegionReachable = nullptr;
static FnGetCVarInt SOH_GetCVarInteger = nullptr;
static const char* kCrossEntrancesCVar = "gCombo.Entrances.CrossInteriors";

// Compute + push this seed's cross-entrance tables. Clears both tables first (so the pool dumps
// are unfiltered and a previous seed's table can't leak), then no-ops unless enabled.
// enabled: -1 = read the toggle CVar (generation), 0/1 = explicit (reload honors the SEED, not the
// live toggle). Returns the fill's gates; spoilerOut (optional) receives the readable section.
static std::vector<ComboRando::CrossGateInfo> Combo_SetupCrossEntrances(uint32_t masterSeed,
                                                                        nlohmann::json* spoilerOut, int enabled) {
    std::vector<ComboRando::CrossGateInfo> gates;
    if (SOH_SetCrossEntranceTable)
        SOH_SetCrossEntranceTable("");
    if (MM_SetCrossEntranceTable)
        MM_SetCrossEntranceTable("");
    bool on = enabled == 1 ||
              (enabled < 0 && SOH_GetCVarInteger && SOH_GetCVarInteger(kCrossEntrancesCVar, 0) != 0);
    if (!on || !SOH_DumpInteriorEntrancePairs || !MM_DumpInteriorEntrancePairs || !SOH_SetCrossEntranceTable ||
        !MM_SetCrossEntranceTable)
        return gates;
    auto assignments =
        ComboRando::BuildCrossAssignments(SOH_DumpInteriorEntrancePairs(), MM_DumpInteriorEntrancePairs(), masterSeed);
    if (assignments.empty())
        return gates;
    SOH_SetCrossEntranceTable(ComboRando::BuildCrossTableSlice(assignments, ComboRando::GAME_OOT).c_str());
    MM_SetCrossEntranceTable(ComboRando::BuildCrossTableSlice(assignments, ComboRando::GAME_MM).c_str());
    for (const auto& a : assignments)
        gates.push_back({ a.door.game == ComboRando::GAME_OOT, a.door.doorRegion,
                          a.interior.game == ComboRando::GAME_OOT, a.interior.interiorRegion });
    if (spoilerOut)
        *spoilerOut = ComboRando::BuildCrossSpoiler(assignments);
    std::cout << "[ComboShip] cross entrances: " << assignments.size() << " doors in the union pool\n";
    return gates;
}

// (Re-)push the cross tables for the seed a consolidated JSON describes. The tables are process
// statics, so whichever save becomes ACTIVE last must have asserted its own state: fresh Start
// re-asserts from the pending JSON (Combo_OnPreOOTSaveInit), Continue from the slot file
// (Combo_OnOOTSaveLoad). Deterministic — same seed always re-derives the same assignment.
static void Combo_PushCrossTablesForSeed(const nlohmann::json& j) {
    uint32_t masterSeed = j.value("masterSeed", 0u);
    bool crossOn = !j.value("entrances", nlohmann::json::object()).value("cross", nlohmann::json::array()).empty();
    Combo_SetupCrossEntrances(masterSeed, nullptr, crossOn ? 1 : 0);
}

// OOT region gating the OOT->MM portal: with interior shuffle the Mask Shop can be anywhere, so MM
// only joins the fill once this region is reachable.
static const char* kPortalGateRegion = "Market Mask Shop";

// Entrance re-derivation deferred from the file-select reload to Start (it costs ~1s and froze the
// first file-select visit). Consumed by Combo_OnPreOOTSaveInit, which z_sram fires right before
// Randomizer_InitSaveFile serializes the ctx into the save. The placements are stashed too: the
// shuffle's ItemReset wipes the ones the reload applied, so they are re-applied after it.
static uint64_t g_DeferredEntranceSeed = 0;
static bool g_DeferredEntrancePending = false;
static std::string g_DeferredOotPlacements;

// ComboShip (issue #1): cross-game erase seam. A save slot is one combined OOT+MM playthrough, so
// erasing it from either game's file-select wipes both saves. Each game fires its Set*-registered
// callback with the 0-based slot when the user erases; the launcher routes it to the OTHER game's
// save-only delete export. The exports never re-enter a menu erase path, so there is no loop.
typedef void (*FnSetDeleteForeignSave)(void (*)(int));
typedef void (*FnDeleteSaveFile)(int);
static FnSetDeleteForeignSave SOH_SetDeleteForeignSave = nullptr;
static FnSetDeleteForeignSave MM_SetDeleteForeignSave = nullptr;
static FnDeleteSaveFile SOH_DeleteSaveFile = nullptr;
static FnDeleteSaveFile MM_DeleteSaveFile = nullptr;

// Registered into each game; invoked when that game erases a slot. Routes the (0-based) slot to the
// OTHER game's delete export. The launcher does no index math — MM's 1-based JSON naming is handled
// inside MM_DeleteSaveFile.
static void DeleteForeignSaveFromOOT(int slot) {
    if (MM_DeleteSaveFile)
        MM_DeleteSaveFile(slot);
    ComboRando::CleanSlotFiles(slot); // also remove the slot's consolidated seed file
}
static void DeleteForeignSaveFromMM(int slot) {
    if (SOH_DeleteSaveFile)
        SOH_DeleteSaveFile(slot);
    ComboRando::CleanSlotFiles(slot);
}

// ComboShip: placement injection exports
typedef void (*FnSetGenerateCb)(void (*)(int));
typedef void (*FnApplyPlacements)(const char*);
typedef void (*FnMMInitRandoSave)(int, const char*);
typedef void (*FnSetComboRandoSeed)(uint64_t);
typedef void (*FnSetComboSeedHash)(uint32_t);
static FnSetGenerateCb SOH_SetOnComboGenerateCallback = nullptr;
static FnApplyPlacements SOH_ApplyRandoPlacements = nullptr;
static FnMMInitRandoSave MM_InitRandoSaveFile = nullptr;
static FnSetComboRandoSeed SOH_SetComboRandoSeed = nullptr;
static FnSetComboSeedHash SOH_SetComboSeedHash = nullptr;

// ComboShip: window-driven generate request (threaded, progress-reporting)
typedef void (*FnSetGenReqCb)(void (*)(const char*));
typedef void (*FnSetSeedGenerated)(uint8_t);
typedef void (*FnSetComboProgressPtr)(const ComboRando::ComboGenProgress*);
typedef void (*FnSetComboFinalizeCb)(int (*)());
static FnSetGenReqCb SOH_SetOnComboGenerateRequestCallback = nullptr;
static FnSetSeedGenerated SOH_SetSeedGenerated = nullptr;
static FnSetComboProgressPtr SOH_SetComboProgressPtr = nullptr;
static FnSetComboFinalizeCb SOH_SetOnComboFinalizeCallback = nullptr;

static std::atomic<bool> g_GenerateBusy{ false };

// ComboShip: non-blocking generation. The heavy fill runs on g_GenerateThread; the main thread
// keeps rendering + playing music + showing progress, and runs the gSaveContext-mutating apply
// itself via Combo_PollFinalize (see the file-select poll). g_ComboProgress is the single source
// of truth, shared read-only with soh.dll via SOH_SetComboProgressPtr.
static std::thread g_GenerateThread;
static ComboRando::ComboGenProgress g_ComboProgress;
static std::atomic<bool> g_ComboPendingFinalize{ false }; // worker succeeded, main-thread apply not yet run
// Main-thread finalize inputs stashed by the worker (consumed by Combo_FinalizeGenerate).
static std::string g_FinalizeOotApply;
static uint32_t g_FinalizeDisplaySeed = 0;
// Consolidated spoiler JSON for the just-generated seed + its hash string (NN-NN-NN-NN-NN). The
// worker writes the pending file from it; Combo_OnOOTSaveInit writes the per-slot file at Start.
static std::string g_ConsolidatedJson;
static std::string g_ConsolidatedHashStr;

// ---------- ComboShip-owned Anchor connection (Phase 1) ----------
// The persistent TCP socket + receive thread live HERE, in the launcher, so the online connection
// survives OOT<->MM portal transitions instead of being torn down with each game. soh's in-place
// Anchor keeps all its packet/handler/menu logic but redirects its transport through the callbacks
// we register below (SOH_SetAnchorSend/Connect/Disconnect) and receives inbound packets via the
// SOH_Anchor_RecvJson export. See docs/UPSTREAM_MERGES.md.
typedef void (*FnSetAnchorSend)(void (*)(const char*));
typedef void (*FnSetAnchorConnect)(void (*)(const char*, uint16_t));
typedef void (*FnSetAnchorDisconnect)(void (*)(void));
typedef void (*FnAnchorRecv)(const char*);
static FnSetAnchorSend SOH_SetAnchorSend = nullptr;
static FnSetAnchorConnect SOH_SetAnchorConnect = nullptr;
static FnSetAnchorDisconnect SOH_SetAnchorDisconnect = nullptr;
static FnAnchorRecv SOH_Anchor_RecvJson = nullptr;
static FnVoidArgless SOH_Anchor_OnConnected = nullptr;
static FnVoidArgless SOH_Anchor_OnDisconnected = nullptr;

// MM Anchor adapter exports (Phase 2). MM piggybacks on the same launcher-owned connection; it is
// activated/deactivated on transitions and receives inbound packets when it is the active game.
static FnSetAnchorSend MM_SetAnchorSend = nullptr;
static FnAnchorRecv MM_Anchor_RecvJson = nullptr;
static FnVoidArgless MM_Anchor_Activate = nullptr;
static FnVoidArgless MM_Anchor_Deactivate = nullptr;

// Cross-game item delivery seam (issue #3). Each game's foreign-check detection (and the Anchor
// receive path) routes an item to the OTHER game through one launcher-owned dispatcher, which calls
// the target DLL's save-only grant export. The same dispatcher serves the single-player and
// networked paths. targetGame/srcGame use the GameId convention 0 = OOT, 1 = MM (== sActiveGame).
typedef void (*FnSetCrossRoute)(void (*)(int, const char*));
typedef void (*FnGrantCrossItem)(const char*);
static FnSetCrossRoute SOH_SetCrossDeliver = nullptr;
static FnSetCrossRoute MM_SetCrossDeliver = nullptr;
static FnGrantCrossItem SOH_GrantCrossItem = nullptr;
static FnGrantCrossItem MM_GrantCrossItem = nullptr;
static FnSetCrossRoute SOH_SetMarkForeignObtained = nullptr;
static FnSetCrossRoute MM_SetMarkForeignObtained = nullptr;
static FnGrantCrossItem SOH_MarkForeignObtained = nullptr;
static FnGrantCrossItem MM_MarkForeignObtained = nullptr;

namespace ComboAnchor {
static std::thread sThread;
static std::atomic<bool> sEnabled{ false };
static std::atomic<bool> sConnected{ false };
static std::string sHost;
static uint16_t sPort = 0;
static std::mutex sOutMutex;
static std::queue<std::string> sOutQueue;
// Which game inbound packets are dispatched to. 0 = OOT, 1 = MM. Updated by the game-switch loop
// via SetActiveGame on each transition. Phase 3 will route per-packet by TARGET game instead.
static std::atomic<int> sActiveGame{ 0 };

// Background loop: connect, then relay outbound packets and feed inbound packets to the active
// game. Mirrors soh's original Network::ReceiveFromServer framing (NUL-delimited JSON), only the
// socket now lives in the launcher so it persists across transitions.
static void ReceiveLoop() {
    IPaddress address;
    if (SDLNet_ResolveHost(&address, sHost.c_str(), sPort) == -1) {
        std::cerr << "[ComboShip][Anchor] ResolveHost failed: " << SDLNet_GetError() << std::endl;
        sEnabled = false;
        return;
    }

    std::string received;
    while (sEnabled) {
        TCPsocket socket = nullptr;
        while (sEnabled && !socket) {
            socket = SDLNet_TCP_Open(&address);
            if (!socket && sEnabled) {
                // Back off between attempts so an unreachable server doesn't spin a core at 100%.
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        if (!sEnabled) {
            if (socket)
                SDLNet_TCP_Close(socket);
            break;
        }

        received.clear();
        sConnected = true;
        // OOT's OnConnected sends the room HANDSHAKE (establishes our client id) regardless of
        // which game is foreground. If MM is the active game (e.g. we connected while already in
        // MM, or resumed straight into it), also activate MM so it announces its presence — MM
        // otherwise only announces on a transition or scene load, neither of which happens here.
        if (SOH_Anchor_OnConnected)
            SOH_Anchor_OnConnected();
        if (sActiveGame.load() == 1 && MM_Anchor_Activate)
            MM_Anchor_Activate();

        SDLNet_SocketSet set = SDLNet_AllocSocketSet(1);
        SDLNet_TCP_AddSocket(set, socket);

        while (sEnabled && sConnected) {
            int ready = SDLNet_CheckSockets(set, 0);
            if (ready == -1)
                break;

            // Drain outbound queue (packets handed to us by the game via Send()).
            std::queue<std::string> toSend;
            {
                std::lock_guard<std::mutex> lk(sOutMutex);
                toSend.swap(sOutQueue);
            }
            while (!toSend.empty()) {
                const std::string& p = toSend.front();
                // Include the NUL delimiter in the framing (matches Network::SendDataToRemote).
                SDLNet_TCP_Send(socket, p.c_str(), (int)p.size() + 1);
                toSend.pop();
            }

            if (ready == 0)
                continue;

            char buf[512];
            memset(buf, 0, sizeof(buf));
            int len = SDLNet_TCP_Recv(socket, buf, sizeof(buf));
            if (len <= 0)
                break;
            received.append(buf, len);

            size_t pos = received.find('\0');
            while (pos != std::string::npos) {
                std::string packet = received.substr(0, pos);
                received.erase(0, pos + 1);
                // Dispatch to whichever game is currently active (Phase 2). Phase 3 will route
                // per-packet by target game so dormant-game items/flags still apply.
                if (sActiveGame.load() == 1) {
                    if (MM_Anchor_RecvJson)
                        MM_Anchor_RecvJson(packet.c_str());
                } else {
                    if (SOH_Anchor_RecvJson)
                        SOH_Anchor_RecvJson(packet.c_str());
                }
                pos = received.find('\0');
            }
        }

        SDLNet_FreeSocketSet(set);
        SDLNet_TCP_Close(socket);
        sConnected = false;
        if (SOH_Anchor_OnDisconnected)
            SOH_Anchor_OnDisconnected();
    }
}

// Registered into the game as the connect request (Network::Enable redirects here).
static void Connect(const char* host, uint16_t port) {
    if (sEnabled)
        return;
    static bool sNetInit = false;
    if (!sNetInit) {
        SDLNet_Init();
        sNetInit = true;
    }
    sHost = host ? host : "";
    sPort = port;
    sEnabled = true;
    if (sThread.joinable())
        sThread.join();
    sThread = std::thread(ReceiveLoop);
}

// Registered into the game as the disconnect request (Network::Disable redirects here).
static void Disconnect() {
    if (!sEnabled)
        return;
    sEnabled = false;
    sConnected = false;
    if (sThread.joinable())
        sThread.join();
    std::lock_guard<std::mutex> lk(sOutMutex);
    std::queue<std::string> empty;
    sOutQueue.swap(empty);
}

// Registered into the game as the send callback (Network::SendDataToRemote redirects here).
static void Send(const char* json) {
    if (!json)
        return;
    std::lock_guard<std::mutex> lk(sOutMutex);
    sOutQueue.push(json);
}

// Called during launcher shutdown, BEFORE any game DLL is unloaded: the receive thread calls
// into soh.dll exports, so it must be joined while soh.dll is still mapped (joining across a
// FreeDll boundary would run under the loader lock).
static void Shutdown() {
    Disconnect();
}

// Called by the game-switch loop on every transition. Routes inbound packets to the new active
// game and activates/deactivates MM's Anchor. OOT self-reactivates through its own GameInteractor
// hooks (OnSceneSpawnActors/OnPlayerUpdate) when it resumes, so it needs no explicit activate.
static void SetActiveGame(int game /* 0 = OOT, 1 = MM */) {
    sActiveGame.store(game);
    if (game == 1) {
        if (MM_Anchor_Activate)
            MM_Anchor_Activate();
    } else {
        if (MM_Anchor_Deactivate)
            MM_Anchor_Deactivate();
    }
}
} // namespace ComboAnchor

// Cross-game delivery dispatcher (issue #3). Registered into BOTH game DLLs; invoked by the
// collector game's foreign-check detection (local) and by the active game's Anchor receive handler
// (network). Grants the item into the TARGET game's resident save via its save-only export — the
// target is normally the dormant game, which isn't ticking, so its save struct isn't being mutated
// underneath us. The grant export persists the target save immediately.
static void DeliverCrossItem(int targetGame, const char* itemName) {
    if (targetGame == 1) {
        if (MM_GrantCrossItem)
            MM_GrantCrossItem(itemName);
    } else {
        if (SOH_GrantCrossItem)
            SOH_GrantCrossItem(itemName);
    }
}

// Network-receive idempotency: mark the SOURCE check obtained in the source game so this client
// won't later physically collect the same check and double-deliver. Save-only; persists.
static void MarkForeignObtained(int srcGame, const char* checkName) {
    if (srcGame == 1) {
        if (MM_MarkForeignObtained)
            MM_MarkForeignObtained(checkName);
    } else {
        if (SOH_MarkForeignObtained)
            SOH_MarkForeignObtained(checkName);
    }
}

// Seed utilities — Ship_Hash/Ship_Random are not exported from libultraship, so implement inline.
// FNV-1a 32-bit hash: deterministic string-to-uint32 used to derive the master seed.
static uint32_t ComboHash(const char* str) {
    if (!str)
        return 0;
    uint32_t h = 2166136261u;
    while (*str) {
        h ^= static_cast<unsigned char>(*str++);
        h *= 16777619u;
    }
    return h;
}
// Simple xorshift32 used for a random seed when none is provided.
static int ComboRandRange(int minV, int maxV) {
    static uint32_t s =
        0x9E3779B9u ^ static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFFFFu);
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    int range = maxV - minV + 1;
    return minV + (range > 0 ? static_cast<int>(s % static_cast<uint32_t>(range)) : 0);
}

static int g_PendingMMFileNum = -1;

// ComboShip: the "mm" placement slice from the most recent generate run, stashed so the
// later Combo_OnOOTSaveInit callback can hand it to MM_InitRandoSaveFile.
static std::string g_PendingMMPlacements;

// Forward decl: defined later, called from RunComboFill on every successful in-game generation.
// playthroughOut (optional) receives the structured sphere playthrough for the consolidated file.
static void WriteComboPlaythrough(const std::string& spoilerJson, const ComboRando::OracleFns& ootOracle,
                                  const ComboRando::OracleFns& mmOracle, const std::string& seedLabel,
                                  nlohmann::json* playthroughOut = nullptr,
                                  const std::vector<ComboRando::CrossGateInfo>& crossGates = {});

// ComboShip: worker that runs the combined-logic fill (or no-logic fallback) on a background
// thread, reports progress via the ComboGenProgress struct, and stashes placements.
static void RunComboFill(std::string inputSeed, ComboRando::ComboGenProgress* progress) {
    auto fail = [&](const char* msg) {
        if (progress) {
            progress->SetError(msg);
            progress->success.store(false);
            progress->done.store(true);
            progress->running.store(false);
        }
        std::cerr << "[ComboShip] RunComboFill: " << msg << "\n";
        g_GenerateBusy.store(false);
    };

    if (!SOH_DumpRandoStaticData || !MM_DumpRandoStaticData) {
        fail("dump functions not resolved");
        return;
    }

    if (inputSeed.empty())
        inputSeed = std::to_string(ComboRandRange(0, 1000000));
    uint32_t masterSeed = ComboHash(inputSeed.c_str());

    // ComboShip: seed OOT's rando RNG BEFORE the dump so its shop/scrub/merchant setup (which runs
    // both inside the dump and again at SOH_ApplyRandoPlacements) makes identical choices each time.
    if (SOH_SetComboRandoSeed)
        SOH_SetComboRandoSeed(masterSeed);

    std::string sohDump = SOH_DumpRandoStaticData();
    std::string mmDump = MM_DumpRandoStaticData();
    if (sohDump.empty() || mmDump.empty()) {
        fail("empty static-data dump");
        return;
    }

    // ComboShip: cross-game entrance tables must be pushed BEFORE the native shuffles (partition +
    // sever read them). Honors the live toggle at generation time; the section lands in the spoiler.
    nlohmann::json crossSpoiler = nlohmann::json::array();
    auto crossGates = Combo_SetupCrossEntrances(masterSeed, &crossSpoiler, -1);

    // ComboShip: entrance shuffle (docs/ENTRANCE_RANDO_PREP.md §3). OOT shuffles the live region
    // graph here (no-op when off); MM derives its map inside the oracle's Reset from the finalSeed.
    g_DeferredEntrancePending = false; // fresh generation supersedes any reloaded seed's deferral
    if (SOH_ShuffleEntrancesForCombo && !SOH_ShuffleEntrancesForCombo(masterSeed)) {
        fail("OOT entrance shuffle found no valid layout (5 retries) — regenerate or relax entrance settings");
        return;
    }
    if (MM_SetComboFinalSeed)
        MM_SetComboFinalSeed(masterSeed);

    std::string spoiler;
    bool usedCombinedFill = false;
    std::string mmEntranceMapJson = "[]"; // resolved MM entrance map, captured while the oracle map is live
    nlohmann::json playthroughJson = nlohmann::json::array(); // structured sphere playthrough (combined-fill only)

    if (Combo_SOH_Rando_Reset && Combo_SOH_Rando_SetOwnedItems && Combo_SOH_Rando_GetReachableChecks &&
        Combo_SOH_Rando_PlaceItem && Combo_MM_Rando_Reset && Combo_MM_Rando_SetOwnedItems &&
        Combo_MM_Rando_GetReachableChecks && Combo_MM_Rando_PlaceItem && Combo_MM_Rando_Restore) {

        ComboRando::OracleFns ootOracle = { Combo_SOH_Rando_Reset,
                                            Combo_SOH_Rando_SetOwnedItems,
                                            Combo_SOH_Rando_GetReachableChecks,
                                            Combo_SOH_Rando_PlaceItem,
                                            Combo_SOH_Rando_IsRegionReachable,
                                            Combo_SOH_Rando_SetExternallyReachableRegions };
        ComboRando::OracleFns mmOracle = { Combo_MM_Rando_Reset,
                                           Combo_MM_Rando_SetOwnedItems,
                                           Combo_MM_Rando_GetReachableChecks,
                                           Combo_MM_Rando_PlaceItem,
                                           Combo_MM_Rando_IsRegionReachable,
                                           Combo_MM_Rando_SetExternallyReachableRegions };

        // ComboShip: fail fast if MM's sampler couldn't produce a connected entrance layout (it keeps
        // the broken map and only warns). One probe covers the fill — every Reset re-derives the same
        // layout from the same finalSeed. The derived map is captured here for the spoiler.
        if (Combo_MM_Rando_EntranceShuffleOk) {
            Combo_MM_Rando_Reset();
            if (!Combo_MM_Rando_EntranceShuffleOk()) {
                Combo_MM_Rando_Restore();
                fail("MM entrance shuffle found no fully-connected layout (256 attempts) — try another seed");
                return;
            }
            if (MM_DumpEntranceMap)
                mmEntranceMapJson = MM_DumpEntranceMap();
        }

        // ComboShip: OOT forced placements (Link's Pocket) the static dump can't carry. The fill
        // reserves these out of the cross pool and commits them so the check isn't left unplaced.
        std::string forcedOot;
        if (SOH_GetForcedPlacements)
            forcedOot = SOH_GetForcedPlacements(masterSeed);

        auto result = ComboRando::CrossWorldCombinedFill(sohDump, mmDump, masterSeed, ootOracle, mmOracle,
                                                         kPortalGateRegion, progress, forcedOot, crossGates);

        if (result.success) {
            spoiler = result.spoilerJson;
            usedCombinedFill = true;
            std::cout << "[ComboShip] RunComboFill: combined-logic fill succeeded (seed=" << masterSeed << ")\n";
            // ComboShip: write the sphere-by-sphere playthrough log. Replays reachability via the
            // oracles BEFORE SOH_ApplyRandoPlacements restores the live OOT context, so it can't
            // corrupt the generated seed. Restores MM itself.
            WriteComboPlaythrough(result.spoilerJson, ootOracle, mmOracle, inputSeed, &playthroughJson, crossGates);
        } else {
            Combo_MM_Rando_Restore();
            fail((std::string("combined fill failed: ") + result.error).c_str());
            return;
        }
        Combo_MM_Rando_Restore();
    }

    if (!usedCombinedFill) {
        spoiler = ComboRando::CrossWorldGenerateSpoiler(sohDump, mmDump, masterSeed);
        std::cout << "[ComboShip] RunComboFill: using no-logic fallback (seed=" << masterSeed << ")\n";
    }

    try {
        std::error_code ec;
        std::filesystem::create_directories(ComboRando::ConsolidatedDir(), ec);
        // ComboShip: all per-seed data (placements, foreign, settings, structured playthrough) is
        // assembled into one consolidated spoiler below and written to the pending file.

        auto j = nlohmann::json::parse(spoiler);
        auto foreignArr = j.value("foreign", nlohmann::json::array());

        // ComboShip: resolve human display names for foreign items from the dumps' items arrays
        // (each entry: {name, displayName}). The fill only carries itemName (the grant key:
        // English for OOT, RI_* for MM); displayName feeds toasts/shop text in the check's game.
        auto buildNameMap = [](const std::string& dump) {
            std::unordered_map<std::string, std::string> m;
            try {
                auto d = nlohmann::json::parse(dump);
                for (auto& it : d.value("items", nlohmann::json::array())) {
                    std::string n = it.value("name", "");
                    std::string dn = it.value("displayName", "");
                    if (!n.empty() && !dn.empty())
                        m.emplace(std::move(n), std::move(dn));
                }
            } catch (...) {}
            return m;
        };
        auto ootNames = buildNameMap(sohDump);
        auto mmNames = buildNameMap(mmDump);
        for (auto& fm : foreignArr) {
            std::string itemGame = fm.value("itemGame", "");
            std::string itemName = fm.value("itemName", "");
            if (itemGame != "mm" && itemGame != "oot")
                continue; // malformed marker: leave unstamped
            const auto& names = (itemGame == "mm") ? mmNames : ootNames;
            auto it = names.find(itemName);
            if (it != names.end()) {
                fm["displayName"] = it->second;
            }
        }

        nlohmann::json ootApply = j.value("oot", nlohmann::json::object());
        nlohmann::json mmApply = j.value("mm", nlohmann::json::object());
        for (const auto& fm : foreignArr) {
            std::string cg = fm.value("checkGame", "");
            std::string cn = fm.value("checkName", "");
            if (cn.empty())
                continue;
            if (cg == "oot")
                ootApply[cn] = ComboRando::kForeignSentinelNameOOT;
            else if (cg == "mm")
                mmApply[cn] = ComboRando::kForeignSentinelNameMM;
        }

        // ComboShip: the gSaveContext-mutating apply (SOH_ApplyRandoPlacements) and the seed-hash set
        // MUST run on the main thread — the worker only computes. Stash their inputs for
        // Combo_FinalizeGenerate, which the main-thread file-select poll runs once it sees done.
        // The OOT seed-hash folds in input-seed + both settings dumps so the icons identify seed and
        // settings (same seed+settings -> matching icons across players).
        uint32_t displaySeed = ComboHash((inputSeed + sohDump + mmDump).c_str());
        g_FinalizeOotApply = ootApply.dump();
        g_FinalizeDisplaySeed = displaySeed;
        g_PendingMMPlacements = mmApply.dump();

        // ComboShip: file_hash = the 5 icon indexes the file-select shows, derived from displaySeed
        // exactly as OOT's GenerateHash (decimal padded to 10, five 2-digit pairs). Doubles as the
        // per-slot filename suffix (NN-NN-NN-NN-NN).
        std::string seedDigits = std::to_string(displaySeed);
        while (seedDigits.size() < 10)
            seedDigits = "0" + seedDigits;
        nlohmann::json fileHashArr = nlohmann::json::array();
        g_ConsolidatedHashStr.clear();
        for (int i = 0; i < 5; ++i) {
            int v = std::stoi(seedDigits.substr(i * 2, 2));
            fileHashArr.push_back(v);
            char b[4];
            std::snprintf(b, sizeof(b), "%02d", v);
            if (i)
                g_ConsolidatedHashStr += "-";
            g_ConsolidatedHashStr += b;
        }

        // ComboShip: assemble the single consolidated spoiler — the shareable artifact + the runtime
        // foreign source + remember/drop/hint data. Settings are CVar snapshots so a dropped seed
        // reproduces on any machine. Written now to the pending file (remembered); bound to a per-slot
        // file at Start (Combo_OnOOTSaveInit).
        auto parseOrEmpty = [](FnDumpData fn) -> nlohmann::json {
            if (!fn)
                return nlohmann::json::object();
            try {
                return nlohmann::json::parse(fn());
            } catch (...) { return nlohmann::json::object(); }
        };
        nlohmann::json consolidated;
        consolidated["fileType"] = "ComboShipRandomizer";
        consolidated["version"] = 1;
        consolidated["seed"] = inputSeed;
        consolidated["masterSeed"] = masterSeed;
        consolidated["displaySeed"] = displaySeed;
        consolidated["file_hash"] = fileHashArr;
        consolidated["oot"] = { { "settings", parseOrEmpty(SOH_DumpRandoSettings) }, { "placements", ootApply } };
        consolidated["mm"] = { { "settings", parseOrEmpty(MM_DumpRandoSettings) }, { "placements", mmApply } };
        consolidated["foreign"] = ComboRando::BuildForeignArray(foreignArr);
        consolidated["playthrough"] = playthroughJson;
        // ComboShip: entrance layouts (§3.4). Informational — reload re-derives both from masterSeed
        // (MM's flags live in mm.settings). Empty arrays = no shuffle.
        {
            nlohmann::json ootEnt = nlohmann::json::array();
            if (SOH_DumpEntranceOverrides) {
                try {
                    ootEnt = nlohmann::json::parse(SOH_DumpEntranceOverrides());
                } catch (...) {}
            }
            nlohmann::json mmEnt = nlohmann::json::array();
            try {
                mmEnt = nlohmann::json::parse(mmEntranceMapJson);
            } catch (...) {}
            consolidated["entrances"] = { { "oot", std::move(ootEnt) },
                                          { "mm", { { "finalSeed", masterSeed }, { "map", std::move(mmEnt) } } },
                                          { "cross", crossSpoiler } };
        }
        g_ConsolidatedJson = consolidated.dump(2);

        // Write the pending (unbound) file so the seed is remembered and Start-able without regenerating.
        {
            std::ofstream pf(ComboRando::PendingPath(), std::ios::trunc);
            if (pf.is_open())
                pf << g_ConsolidatedJson;
        }
        std::cout << "[ComboShip] RunComboFill: placements computed; consolidated pending seed written\n";

        if (progress) {
            progress->seed.store(masterSeed);
            // The reproducible token is the (resolved) input seed string, not masterSeed: paste it
            // back into the Seed field + same settings to reproduce. For a blank input this is the
            // concrete random string chosen above.
            std::strncpy(progress->seedStr, inputSeed.c_str(), sizeof(progress->seedStr) - 1);
            progress->seedStr[sizeof(progress->seedStr) - 1] = '\0';
            progress->foreignCount.store(static_cast<int>(foreignArr.size()));
            // Per-game contributed check counts = size of each settings-scoped dump pool.
            try {
                progress->ootCheckCount.store(
                    static_cast<int>(nlohmann::json::parse(sohDump).value("checks", nlohmann::json::array()).size()));
                progress->mmCheckCount.store(
                    static_cast<int>(nlohmann::json::parse(mmDump).value("checks", nlohmann::json::array()).size()));
            } catch (...) {}
            progress->success.store(true);
            progress->done.store(true);
        }
        g_ComboPendingFinalize.store(true);
    } catch (const std::exception& e) {
        fail((std::string("post-fill exception: ") + e.what()).c_str());
        return;
    }
    g_GenerateBusy.store(false);
}

// ComboShip: headless cross-world generation TEST. Runs the combined assumed fill over a range of
// seeds and asserts each succeeds. A seed "succeeds" via CrossWorldCombinedFill's completability
// check: after placing every item it sphere-collects from an empty start, across both games and
// honoring the OOT->MM portal gate, and fails unless every advancement check in either game is
// reachable — i.e. a passing seed is provably 100%-completable from scratch. Uses the same oracles
// as the real generator under the current CVar options, so changing shuffle options in the menu and
// re-running exercises those configs too. Returns the FAILED seed count (0 == all good).
// Env-gated via COMBO_GENTEST=<count>.
static int RunComboGenTest(int numSeeds, uint32_t seedBase) {
    if (!(Combo_SOH_Rando_Reset && Combo_SOH_Rando_SetOwnedItems && Combo_SOH_Rando_GetReachableChecks &&
          Combo_SOH_Rando_PlaceItem && Combo_MM_Rando_Reset && Combo_MM_Rando_SetOwnedItems &&
          Combo_MM_Rando_GetReachableChecks && Combo_MM_Rando_PlaceItem && Combo_MM_Rando_Restore)) {
        std::cerr << "[GENTEST] oracle exports unavailable — cannot run\n";
        return -1;
    }
    if (!SOH_DumpRandoStaticData || !MM_DumpRandoStaticData) {
        std::cerr << "[GENTEST] dump functions not resolved — cannot run\n";
        return -1;
    }
    std::string sohDump = SOH_DumpRandoStaticData();
    std::string mmDump = MM_DumpRandoStaticData();
    if (sohDump.empty() || mmDump.empty()) {
        std::cerr << "[GENTEST] empty dump — cannot run\n";
        return -1;
    }

    ComboRando::OracleFns ootOracle = { Combo_SOH_Rando_Reset,
                                        Combo_SOH_Rando_SetOwnedItems,
                                        Combo_SOH_Rando_GetReachableChecks,
                                        Combo_SOH_Rando_PlaceItem,
                                        Combo_SOH_Rando_IsRegionReachable,
                                        Combo_SOH_Rando_SetExternallyReachableRegions };
    ComboRando::OracleFns mmOracle = { Combo_MM_Rando_Reset,
                                       Combo_MM_Rando_SetOwnedItems,
                                       Combo_MM_Rando_GetReachableChecks,
                                       Combo_MM_Rando_PlaceItem,
                                       Combo_MM_Rando_IsRegionReachable,
                                       Combo_MM_Rando_SetExternallyReachableRegions };

    std::cout << "[GENTEST] running " << numSeeds << " cross-world generations (seedBase=" << seedBase
              << ") — asserting every advancement item is reachable from an empty start in both games\n";
    int failures = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < numSeeds; ++i) {
        uint32_t seed = seedBase + static_cast<uint32_t>(i);
        // Per-seed cross tables + entrance layouts, exactly like the real generator.
        auto crossGates = Combo_SetupCrossEntrances(seed, nullptr, -1);
        if (SOH_ShuffleEntrancesForCombo && !SOH_ShuffleEntrancesForCombo(seed)) {
            std::cerr << "[GENTEST]   seed " << seed << " FAIL: OOT entrance shuffle found no valid layout\n";
            ++failures;
            continue;
        }
        if (MM_SetComboFinalSeed)
            MM_SetComboFinalSeed(seed);
        if (Combo_MM_Rando_EntranceShuffleOk) {
            Combo_MM_Rando_Reset();
            if (!Combo_MM_Rando_EntranceShuffleOk()) {
                Combo_MM_Rando_Restore();
                std::cerr << "[GENTEST]   seed " << seed << " FAIL: MM entrance shuffle not fully connected\n";
                ++failures;
                continue;
            }
        }
        auto result = ComboRando::CrossWorldCombinedFill(sohDump, mmDump, seed, ootOracle, mmOracle,
                                                         kPortalGateRegion, nullptr, "", crossGates);
        Combo_MM_Rando_Restore(); // reset the MM oracle's snapshot guard for the next fill
        if (result.success) {
            std::cout << "[GENTEST]   seed " << seed << " PASS\n";
        } else {
            std::cerr << "[GENTEST]   seed " << seed << " FAIL: " << result.error << "\n";
            ++failures;
        }
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    if (failures == 0) {
        std::cout << "[GENTEST] RESULT: PASS — " << numSeeds << "/" << numSeeds
                  << " seeds fully completable (cross-game), " << ms << " ms\n";
    } else {
        std::cerr << "[GENTEST] RESULT: FAIL — " << failures << "/" << numSeeds
                  << " seeds could not place all progression reachably, " << ms << " ms\n";
    }
    return failures;
}

// ComboShip: headless cross-world PLAYTHROUGH log. Generates one seed, then replays it sphere by
// sphere (a sphere = everything newly reachable given what you have so far), listing each item in
// the order it becomes obtainable across both games (OOT->MM portal honored), until the seed is
// BEATABLE: can kill Ganon AND can kill Majora.
//   - "can kill Ganon": the "Ganon" goal location is reachable (SOH's win check).
//   - "can kill Majora": Majora's Lair is reachable (gated on RemainsCount/MoonMaskCount in
//     Logic.cpp); surfaced via its in-region check RC_MOON_MAJORA_POT_01.
// Full sphere log goes to saves/combo/slot0.playthrough.txt; a summary prints to stdout.
// Env-gated via COMBO_PLAYTHROUGH=<seed>.
// Writes the sphere-by-sphere log from an ALREADY-GENERATED spoiler, driving the oracles to
// sphere-collect. Ends by restoring the MM oracle's pre-generation snapshot (it drives MM here).
// Called both from the env-gated entry below and from RunComboFill on every in-game generation.
static void WriteComboPlaythrough(const std::string& spoilerJson, const ComboRando::OracleFns& ootOracle,
                                  const ComboRando::OracleFns& mmOracle, const std::string& seedLabel,
                                  nlohmann::json* playthroughOut,
                                  const std::vector<ComboRando::CrossGateInfo>& crossGates) {
    using namespace ComboRando; // GameId / GAME_OOT / GAME_MM
    // Endgame signals the oracles actually emit:
    //   OOT — top of Ganon's Tower (all four trials cleared) reachable AND the Ganon's Castle Boss Key
    //   owned = standing at Ganondorf's door, able to enter the fight. (The literal "Ganon" location
    //   needs CanUse(RG_MASTER_SWORD), which gates on the master-sword EQUIP flag that the headless
    //   reachability engine doesn't model, so it never reports reachable — this is the reliable proxy.)
    //   MM  — Majora's Lair reachable (its access already encodes the remains/masks requirement),
    //   surfaced via the in-lair check RC_MOON_MAJORA_POT_01.
    static const char* kOotTowerTop = "Ganon's Castle Tower Boss Key Chest";
    static const char* kOotBossKey = "Ganon's Castle Boss Key";
    static const char* kMmWin = "RC_MOON_MAJORA_POT_01";

    // Parse placements: check -> (itemName, itemGame). itemGame defaults to the check's game unless a
    // foreign marker says otherwise (foreign = cross-game placement).
    struct Placed {
        GameId checkGame;
        std::string check;
        GameId itemGame;
        std::string item;
    };
    std::vector<Placed> placements;
    std::unordered_set<std::string> foreignKey; // "<cg>:<cn>" -> item is from the other game
    std::unordered_map<std::string, GameId> foreignItemGame;
    try {
        auto j = nlohmann::json::parse(spoilerJson);
        for (auto& fm : j.value("foreign", nlohmann::json::array())) {
            std::string cg = fm.value("checkGame", ""), cn = fm.value("checkName", "");
            std::string ig = fm.value("itemGame", "");
            foreignKey.insert(cg + ":" + cn);
            foreignItemGame[cg + ":" + cn] = (ig == "mm") ? GAME_MM : GAME_OOT;
        }
        auto addGame = [&](const char* key, GameId cg) {
            if (!j.contains(key) || !j[key].is_object())
                return;
            for (auto& [cn, iv] : j[key].items()) { // iterate the lvalue, not a temporary copy
                std::string fk = std::string(key) + ":" + cn;
                GameId ig = foreignKey.count(fk) ? foreignItemGame[fk] : cg;
                placements.push_back({ cg, cn, ig, iv.get<std::string>() });
            }
        };
        addGame("oot", GAME_OOT);
        addGame("mm", GAME_MM);
    } catch (const std::exception& e) {
        std::cerr << "[PLAYTHROUGH] spoiler parse error: " << e.what() << "\n";
        return;
    }

    auto queryReachable = [&](const ComboRando::OracleFns& o, const std::vector<std::string>& owned) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& n : owned)
            arr.push_back(n);
        o.Reset();
        o.SetOwnedItems(arr.dump().c_str());
        std::unordered_set<std::string> out;
        try {
            for (auto& n : nlohmann::json::parse(o.GetReachableChecks()))
                out.insert(n.get<std::string>());
        } catch (...) {}
        return out;
    };

    std::vector<std::string> ownedOot, ownedMm;
    std::unordered_set<std::string> collected; // "<cg>:<cn>"
    std::ostringstream log;
    log << "Cross-world playthrough - seed '" << seedLabel << "'\n"
        << "Beatable when: Ganondorf reachable (OOT: all trials cleared + Boss Key owned)"
        << " AND Majora's Lair reachable (MM).\n\n";

    int beatableSphere = -1;
    const int kMaxSpheres = 200;
    // Cross-entrance gates, mirrored from the fill's fixpoint. Monotone across spheres here — the
    // replay's owned sets only grow. Marks cleared at the end (the fill's cleaner pattern).
    std::vector<bool> gateOpen(crossGates.size(), false);
    auto pushCrossMarks = [&]() {
        if (crossGates.empty())
            return;
        nlohmann::json ootMarks = nlohmann::json::array(), mmMarks = nlohmann::json::array();
        for (size_t i = 0; i < crossGates.size(); ++i) {
            if (gateOpen[i])
                (crossGates[i].interiorIsOot ? ootMarks : mmMarks).push_back(crossGates[i].interiorRegion);
        }
        if (ootOracle.SetExternallyReachableRegions)
            ootOracle.SetExternallyReachableRegions(ootMarks.dump().c_str());
        if (mmOracle.SetExternallyReachableRegions)
            mmOracle.SetExternallyReachableRegions(mmMarks.dump().c_str());
    };
    for (int sphere = 0; sphere < kMaxSpheres; ++sphere) {
        pushCrossMarks();
        auto ootReach = queryReachable(ootOracle, ownedOot);
        // Same portal gate as the fill (region flags reflect the OOT query that just ran).
        bool portalOpen = !ootOracle.IsRegionReachable || ootOracle.IsRegionReachable(kPortalGateRegion) != 0;
        if (ootOracle.IsRegionReachable) {
            for (size_t i = 0; i < crossGates.size(); ++i) {
                if (!gateOpen[i] && crossGates[i].doorIsOot &&
                    ootOracle.IsRegionReachable(crossGates[i].doorRegion.c_str()) == 1)
                    gateOpen[i] = true;
            }
        }
        auto mmReach = portalOpen ? queryReachable(mmOracle, ownedMm) : std::unordered_set<std::string>{};
        if (mmOracle.IsRegionReachable && portalOpen) {
            for (size_t i = 0; i < crossGates.size(); ++i) {
                if (!gateOpen[i] && !crossGates[i].doorIsOot &&
                    mmOracle.IsRegionReachable(crossGates[i].doorRegion.c_str()) == 1)
                    gateOpen[i] = true;
            }
        }
        bool canGanon = ootReach.count(kOotTowerTop) > 0 &&
                        std::find(ownedOot.begin(), ownedOot.end(), kOotBossKey) != ownedOot.end();
        bool canMajora = mmReach.count(kMmWin) > 0;
        if (canGanon && canMajora) {
            beatableSphere = sphere;
            break;
        }

        std::vector<Placed> newly;
        for (auto& p : placements) {
            std::string key = (p.checkGame == GAME_OOT ? "oot:" : "mm:") + p.check;
            if (collected.count(key))
                continue;
            const auto& reach = (p.checkGame == GAME_OOT) ? ootReach : mmReach;
            if (reach.count(p.check))
                newly.push_back(p);
        }
        if (newly.empty()) {
            log << "Sphere " << sphere << ": (stuck — nothing new reachable, not yet beatable)\n";
            break;
        }
        log << "Sphere " << sphere << "  (Ganon=" << (canGanon ? "Y" : "n") << " Majora=" << (canMajora ? "Y" : "n")
            << ", +" << newly.size() << " items)\n";
        // ComboShip: structured sphere for the consolidated file (drives the "Get a hint" feature).
        nlohmann::json sphereSteps = nlohmann::json::array();
        for (auto& p : newly) {
            std::string key = (p.checkGame == GAME_OOT ? "oot:" : "mm:") + p.check;
            collected.insert(key);
            (p.itemGame == GAME_OOT ? ownedOot : ownedMm).push_back(p.item);
            if (playthroughOut)
                sphereSteps.push_back({ { "game", p.checkGame == GAME_OOT ? "oot" : "mm" },
                                        { "check", p.check },
                                        { "item", p.item },
                                        { "foreign", p.checkGame != p.itemGame } });
            log << "    [" << (p.checkGame == GAME_OOT ? "OOT" : "MM ") << "] " << p.check << "  <-  " << p.item
                << (p.checkGame != p.itemGame ? (p.itemGame == GAME_OOT ? "  (OOT item)" : "  (MM item)") : "") << "\n";
        }
        if (playthroughOut)
            playthroughOut->push_back({ { "sphere", sphere }, { "steps", std::move(sphereSteps) } });
    }
    // ComboShip: true "ever reachable" sets — give each oracle the FULL placed-item set and ask
    // what's reachable. Reachability is monotonic, so full inventory yields the maximal set. Differs
    // from `collected`, which stops at the beatable sphere (post-win checks would look unreachable).
    // Must run before the MM restore below, which rolls the MM oracle back to its pre-gen snapshot.
    std::vector<std::string> allOot, allMm;
    for (auto& p : placements)
        (p.itemGame == GAME_OOT ? allOot : allMm).push_back(p.item);
    // Full inventory reaches every cross door, so every gate is open for the maximal-set queries.
    std::fill(gateOpen.begin(), gateOpen.end(), true);
    pushCrossMarks();
    auto everReachOot = queryReachable(ootOracle, allOot);
    auto everReachMm = queryReachable(mmOracle, allMm);

    Combo_MM_Rando_Restore();
    // Clear the marks — they are query-time state inside the game DLLs, and the live game's own
    // logic queries (tracker availability) must not see them.
    if (ootOracle.SetExternallyReachableRegions)
        ootOracle.SetExternallyReachableRegions("");
    if (mmOracle.SetExternallyReachableRegions)
        mmOracle.SetExternallyReachableRegions("");

    if (beatableSphere >= 0) {
        log << "\nBEATABLE at sphere " << beatableSphere << ": Ganon AND Majora both reachable. Seed is completable.\n";
    } else {
        log << "\nNOT proven beatable within " << kMaxSpheres << " spheres (see stuck note above).\n";
    }

    // ComboShip: full placement record (supersedes the old slot0.spoiler.json). Lists every
    // check->item in both games in placement order, flagging any check the oracles can never reach
    // even with full inventory ([UNREACHABLE], via everReach* above). This is the artifact for
    // debugging reachability dead-ends: it shows what got placed in checks the oracles can't see.
    auto emitGame = [&](GameId cg, const char* tag, const std::unordered_set<std::string>& everReach) {
        size_t reached = 0, missing = 0;
        log << "\n--- " << tag << " placements ---\n";
        for (auto& p : placements) {
            if (p.checkGame != cg)
                continue;
            bool got = everReach.count(p.check) > 0;
            got ? ++reached : ++missing;
            log << "    " << (got ? "  " : "! ") << p.check << "  <-  " << p.item
                << (p.checkGame != p.itemGame ? (p.itemGame == GAME_OOT ? "  (OOT item)" : "  (MM item)") : "")
                << (got ? "" : "   [UNREACHABLE]") << "\n";
        }
        log << "  " << tag << ": " << reached << " reachable, " << missing << " unreachable\n";
    };
    log << "\n==== Full placement (all checks) ====\n";
    emitGame(GAME_OOT, "OOT", everReachOot);
    emitGame(GAME_MM, "MM", everReachMm);

    // ComboShip: in-game generation folds the playthrough into the consolidated spoiler JSON
    // (playthroughOut), so the standalone text log is redundant there and no longer written. The
    // env-gated COMBO_PLAYTHROUGH debug tool passes no playthroughOut and still gets the .txt.
    if (!playthroughOut) {
        std::error_code ec;
        std::filesystem::create_directories("saves/combo", ec);
        std::ofstream f("saves/combo/slot0.playthrough.txt", std::ios::trunc);
        f << log.str();
        std::cout << "[PLAYTHROUGH] full sphere log -> saves/combo/slot0.playthrough.txt\n";
    }

    std::cout << "[PLAYTHROUGH] seed '" << seedLabel << "' - " << (beatableSphere >= 0 ? "BEATABLE" : "NOT beatable")
              << (beatableSphere >= 0 ? (" at sphere " + std::to_string(beatableSphere)) : "") << "\n";
    std::cout << "[PLAYTHROUGH] collected " << collected.size() << " items across "
              << (beatableSphere >= 0 ? beatableSphere : kMaxSpheres) << " spheres before the win\n";
}

// Env-gated entry: COMBO_PLAYTHROUGH=<seed> generates that seed headless, then writes its log.
static void RunComboPlaythrough(const std::string& inputSeed) {
    if (!(Combo_SOH_Rando_Reset && Combo_SOH_Rando_SetOwnedItems && Combo_SOH_Rando_GetReachableChecks &&
          Combo_SOH_Rando_PlaceItem && Combo_MM_Rando_Reset && Combo_MM_Rando_SetOwnedItems &&
          Combo_MM_Rando_GetReachableChecks && Combo_MM_Rando_PlaceItem && Combo_MM_Rando_Restore)) {
        std::cerr << "[PLAYTHROUGH] oracle exports unavailable\n";
        return;
    }
    if (!SOH_DumpRandoStaticData || !MM_DumpRandoStaticData) {
        std::cerr << "[PLAYTHROUGH] dump functions not resolved\n";
        return;
    }
    ComboRando::OracleFns ootOracle = { Combo_SOH_Rando_Reset,
                                        Combo_SOH_Rando_SetOwnedItems,
                                        Combo_SOH_Rando_GetReachableChecks,
                                        Combo_SOH_Rando_PlaceItem,
                                        Combo_SOH_Rando_IsRegionReachable,
                                        Combo_SOH_Rando_SetExternallyReachableRegions };
    ComboRando::OracleFns mmOracle = { Combo_MM_Rando_Reset,
                                       Combo_MM_Rando_SetOwnedItems,
                                       Combo_MM_Rando_GetReachableChecks,
                                       Combo_MM_Rando_PlaceItem,
                                       Combo_MM_Rando_IsRegionReachable,
                                       Combo_MM_Rando_SetExternallyReachableRegions };
    std::string seedStr = inputSeed.empty() ? "1" : inputSeed;
    uint32_t masterSeed = ComboHash(seedStr.c_str());
    std::string sohDump = SOH_DumpRandoStaticData();
    std::string mmDump = MM_DumpRandoStaticData();
    // Cross tables + entrance layouts, same as the real generator (no-ops when the options are off).
    auto crossGates = Combo_SetupCrossEntrances(masterSeed, nullptr, -1);
    if (SOH_ShuffleEntrancesForCombo && !SOH_ShuffleEntrancesForCombo(masterSeed)) {
        std::cerr << "[PLAYTHROUGH] OOT entrance shuffle found no valid layout\n";
        return;
    }
    if (MM_SetComboFinalSeed)
        MM_SetComboFinalSeed(masterSeed);
    auto fill = ComboRando::CrossWorldCombinedFill(sohDump, mmDump, masterSeed, ootOracle, mmOracle,
                                                   kPortalGateRegion, nullptr, "", crossGates);
    if (!fill.success) {
        Combo_MM_Rando_Restore();
        std::cerr << "[PLAYTHROUGH] seed '" << seedStr << "' did not generate: " << fill.error << "\n";
        return;
    }
    WriteComboPlaythrough(fill.spoilerJson, ootOracle, mmOracle, seedStr, nullptr, crossGates); // restores MM
}

// ComboShip: generate-request handler — called by SOH_TriggerComboGenerate from the UI. Runs
// synchronously on the calling (game) thread; a background thread raced the games' single-threaded
// rando logic and crashed. A reentrancy guard prevents double-invocation.
static void Combo_OnGenerateRequest(const char* inputSeed, ComboRando::ComboGenProgress* progress) {
    if (g_GenerateBusy.exchange(true)) {
        // Already running — ignore the duplicate request.
        if (progress) {
            progress->SetError("generate already in progress");
            progress->done.store(true);
        }
        return;
    }
    RunComboFill(std::string(inputSeed ? inputSeed : ""), progress);
}

// ComboShip: UI-driven (non-blocking) generate — registered as the generate-request callback and
// invoked on the main thread from SOH_TriggerComboGenerate. Spawns the worker so the main loop keeps
// rendering + playing music + animating progress. The previous worker is always finished by now
// (reentry is gated on RandoGenerating in soh + g_GenerateBusy here), but join it to recycle the
// std::thread object. The gSaveContext apply happens later on the main thread (Combo_PollFinalize).
static void Combo_OnGenerateThreaded(const char* inputSeed) {
    // Reject if a worker is running OR a finalize is still pending (apply not yet run on main thread).
    if (g_ComboPendingFinalize.load() || g_GenerateBusy.exchange(true)) {
        std::cerr << "[ComboShip] generate already in progress — ignoring duplicate request\n";
        return;
    }
    if (g_GenerateThread.joinable())
        g_GenerateThread.join(); // recycle the finished previous worker's thread object
    g_ComboProgress.Reset();
    g_ComboProgress.done.store(false);
    g_ComboProgress.running.store(true);
    std::string seed(inputSeed ? inputSeed : "");
    // RunComboFill clears g_GenerateBusy when it finishes; the finalize gate then blocks re-trigger
    // until the main-thread apply runs. Call RunComboFill directly (busy is already held).
    g_GenerateThread = std::thread([seed]() { RunComboFill(seed, &g_ComboProgress); });
}

// ComboShip: main-thread finalize — the gSaveContext-mutating apply + seed-hash set. Runs from
// Combo_PollFinalize on the main thread once the worker has stashed its result. NEVER call from the
// worker thread (that race crashed the prior threaded attempt).
static void Combo_FinalizeGenerate() {
    if (SOH_ApplyRandoPlacements) {
        SOH_ApplyRandoPlacements(g_FinalizeOotApply.c_str());
        std::cout << "[ComboShip] Combo_FinalizeGenerate: OOT placements applied\n";
    } else if (SOH_SetSeedGenerated) {
        SOH_SetSeedGenerated(1);
    }
    if (SOH_SetComboSeedHash)
        SOH_SetComboSeedHash(g_FinalizeDisplaySeed);
    g_ComboProgress.running.store(false);
}

// ComboShip: poll callback the file-select loop calls each frame on the main thread. Runs the
// pending finalize (apply) when the worker has succeeded. Returns 1 once generation is fully
// resolved (finalized or failed) so the caller can clear RandoGenerating; 0 while still working.
static int Combo_PollFinalize() {
    if (g_ComboPendingFinalize.exchange(false)) {
        Combo_FinalizeGenerate();
        return 1;
    }
    // No pending finalize: resolved iff the worker is done and not still running.
    return (g_ComboProgress.done.load() && !g_GenerateBusy.load()) ? 1 : 0;
}

// ComboShip: reload a consolidated seed file (the remembered pending file when path is null/empty, or
// a dropped file) and make it playable WITHOUT regenerating. Runs synchronously on the MAIN thread
// (called from the file-select), so the gSaveContext-mutating apply is safe. Restores both games'
// settings, runs the pool prep, re-applies the saved OOT placements + seed-hash, stashes the MM
// placements, and keeps the consolidated JSON in memory so "Start Randomizer" writes the per-slot
// file. Returns 1 on success. The hash string is recomputed from displaySeed for the per-slot name.
static int Combo_OnReloadRequest(const char* path) {
    if (g_GenerateBusy.load() || g_ComboPendingFinalize.load())
        return 0; // a generation is in flight — don't race it
    std::string file = (path && path[0]) ? std::string(path) : ComboRando::PendingPath().string();
    std::ifstream in(file);
    if (!in.is_open())
        return 0;
    try {
        nlohmann::json j;
        in >> j;
        if (j.value("fileType", std::string()) != "ComboShipRandomizer")
            return 0;
        uint32_t masterSeed = j.value("masterSeed", 0u);
        uint32_t displaySeed = j.value("displaySeed", 0u);
        auto oot = j.value("oot", nlohmann::json::object());
        auto mm = j.value("mm", nlohmann::json::object());
        std::string ootSettings = oot.value("settings", nlohmann::json::object()).dump();
        std::string ootPlacements = oot.value("placements", nlohmann::json::object()).dump();
        std::string mmSettings = mm.value("settings", nlohmann::json::object()).dump();
        std::string mmPlacements = mm.value("placements", nlohmann::json::object()).dump();

        // OOT: the seed's settings are needed only while rebuilding the ctx (prep -> entrances ->
        // placements). The menu CVars are the USER's intent — snapshot them and put them back after,
        // so reloading a seed (incl. the boot-time pending reload) doesn't clobber menu toggles.
        std::string userOotSettings = SOH_DumpRandoSettings ? SOH_DumpRandoSettings() : "";
        if (SOH_RestoreRandoSettings)
            SOH_RestoreRandoSettings(ootSettings.c_str());
        if (SOH_SetComboRandoSeed)
            SOH_SetComboRandoSeed(masterSeed);
        if (SOH_PrepRandoContext)
            SOH_PrepRandoContext();
        // Cross-entrance tables: honor the SEED's cross state (spoiler section presence), not the
        // live toggle — deterministic re-derivation, same as generation. Must precede the deferred
        // entrance shuffle (partition + sever read the pushed tables).
        {
            bool crossOn =
                !j.value("entrances", nlohmann::json::object()).value("cross", nlohmann::json::array()).empty();
            Combo_SetupCrossEntrances(masterSeed, nullptr, crossOn ? 1 : 0);
        }
        // Entrance re-derivation (item pool + shuffle + validation, ~1s) is DEFERRED to Start
        // (Combo_OnPreOOTSaveInit) — running it here froze the first file-select visit. The ctx
        // options it reads were finalized by the prep above and survive the CVar snap-back below;
        // placements applied next don't depend on the entrance graph (and are re-applied after the
        // deferred shuffle, whose ItemReset clears them).
        g_DeferredEntranceSeed = masterSeed;
        g_DeferredEntrancePending = true;
        g_DeferredOotPlacements = ootPlacements;
        if (SOH_ApplyRandoPlacements)
            SOH_ApplyRandoPlacements(ootPlacements.c_str());
        if (SOH_SetComboSeedHash)
            SOH_SetComboSeedHash(displaySeed);
        if (SOH_RestoreRandoSettings && !userOotSettings.empty())
            SOH_RestoreRandoSettings(userOotSettings.c_str());

        // MM: stash placements + finalSeed. No CVar writes here — the save-bind (Combo_OnOOTSaveInit)
        // re-asserts the seed's MM settings around MM_InitRandoSaveFile itself.
        if (MM_SetComboFinalSeed)
            MM_SetComboFinalSeed(masterSeed);
        g_PendingMMPlacements = mmPlacements;

        // Keep the loaded seed so Start binds it to the chosen slot; recompute the hash-icon filename.
        g_ConsolidatedJson = j.dump(2);
        g_FinalizeDisplaySeed = displaySeed;
        // Make this the remembered pending seed (so a dropped seed survives a restart before Start).
        {
            std::error_code ec;
            std::filesystem::create_directories(ComboRando::ConsolidatedDir(), ec);
            std::ofstream pf(ComboRando::PendingPath(), std::ios::trunc);
            if (pf.is_open())
                pf << g_ConsolidatedJson;
        }
        std::string d = std::to_string(displaySeed);
        while (d.size() < 10)
            d = "0" + d;
        g_ConsolidatedHashStr.clear();
        for (int i = 0; i < 5; ++i) {
            char b[4];
            std::snprintf(b, sizeof(b), "%02d", std::stoi(d.substr(i * 2, 2)));
            if (i)
                g_ConsolidatedHashStr += "-";
            g_ConsolidatedHashStr += b;
        }
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
    }
}

// ComboShip: pre-save-init hook (z_sram, before Randomizer_InitSaveFile) — run the entrance
// re-derivation deferred by the reload path so the overrides land in the ctx before the OOT save
// serializes them. Failure can't abort here (deterministic re-run of a layout that generated fine,
// so it only fails on version drift) — log loudly and continue.
static void Combo_OnPreOOTSaveInit(int fileNum) {
    (void)fileNum;
    // Re-assert THIS seed's cross tables — a Continue on another slot since generation/reload may
    // have replaced the process statics (see Combo_OnOOTSaveLoad). Idempotent for fresh generates.
    if (!g_ConsolidatedJson.empty()) {
        try {
            Combo_PushCrossTablesForSeed(nlohmann::json::parse(g_ConsolidatedJson));
        } catch (...) {}
    }
    if (!g_DeferredEntrancePending)
        return;
    g_DeferredEntrancePending = false;
    if (SOH_ShuffleEntrancesForCombo && !SOH_ShuffleEntrancesForCombo(g_DeferredEntranceSeed)) {
        std::cerr << "[ComboShip] deferred entrance re-derivation FAILED — OOT save may not match the seed\n";
    }
    // The shuffle's ItemReset cleared the reload-applied placements — put them back before
    // Randomizer_InitSaveFile serializes the ctx (also restores shop setup deterministically).
    if (SOH_ApplyRandoPlacements && !g_DeferredOotPlacements.empty())
        SOH_ApplyRandoPlacements(g_DeferredOotPlacements.c_str());
    g_DeferredOotPlacements.clear();
}

static void Combo_OnOOTSaveInit(int fileNum) {
    // ComboShip: bind the pending consolidated seed to this slot — the runtime foreign source +
    // shareable per-slot file (save{fileNum}-Randomizer-<hash>.json). Clean any stale file for the
    // slot first (a prior seed generated into this slot).
    if (!g_ConsolidatedJson.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(ComboRando::ConsolidatedDir(), ec);
        ComboRando::CleanSlotFiles(fileNum);
        std::ofstream sf(ComboRando::SlotWritePath(fileNum, g_ConsolidatedHashStr), std::ios::trunc);
        if (sf.is_open())
            sf << g_ConsolidatedJson;
        std::cout << "[ComboShip] wrote consolidated seed file for slot " << fileNum << std::endl;
    }
    if (MM_InitRandoSaveFile && !g_PendingMMPlacements.empty()) {
        std::cout << "[ComboShip] Creating RANDO MM save for OOT slot " << fileNum << std::endl;
        // MM_InitRandoSaveFile persists the LIVE CVar options into the save, but the fill validated
        // the generation-time ones — a toggle between Generate and Start would ship an unvalidated
        // config (with entrance shuffle: a different world than logic checked). Re-assert the seed's
        // settings + finalSeed around the save build only; the menu CVars are the user's intent and
        // are put back after.
        std::string userMmSettings = MM_DumpRandoSettings ? MM_DumpRandoSettings() : "";
        if (!g_ConsolidatedJson.empty()) {
            try {
                auto cj = nlohmann::json::parse(g_ConsolidatedJson);
                std::string mmSettings = cj.value("mm", nlohmann::json::object())
                                             .value("settings", nlohmann::json::object())
                                             .dump();
                if (MM_RestoreRandoSettings)
                    MM_RestoreRandoSettings(mmSettings.c_str());
                if (MM_SetComboFinalSeed)
                    MM_SetComboFinalSeed(cj.value("masterSeed", 0u));
            } catch (const std::exception& e) {
                std::cerr << "[ComboShip] slot bind: settings re-assert failed: " << e.what() << "\n";
            }
        }
        MM_InitRandoSaveFile(fileNum, g_PendingMMPlacements.c_str());
        if (MM_RestoreRandoSettings && !userMmSettings.empty())
            MM_RestoreRandoSettings(userMmSettings.c_str());
        g_PendingMMPlacements.clear();
    } else if (MM_InitSaveFile) {
        // No placement available (e.g. generation was skipped) — fall back to a vanilla MM save.
        std::cout << "[ComboShip] Creating MM save for OOT slot " << fileNum << std::endl;
        MM_InitSaveFile(fileNum);
    }
    // Both creation paths build the save in MM's live gSaveContext.
    g_MmSaveInMemorySlot = fileNum;
}

// ComboShip: OOT loaded a save (file select / warp). Bring the matching MM save into MM's dormant
// memory so the combo tracker peek shows real MM items before MM is visited. Skipped when that
// slot's MM save is already live in memory — reloading from disk would clobber newer progress.
static void Combo_OnOOTSaveLoad(int fileNum) {
    // Cross-entrance tables are process statics — re-derive them for THIS slot's seed, or clear
    // them when the slot has none. Without this, a seed generated/reloaded earlier in the session
    // leaks its tables into a different save's doors and native-pool partition (review finding).
    // Runs before the peek's early return: re-generating at file select then continuing the same
    // slot must also re-assert.
    {
        auto path = ComboRando::SlotReadPath(fileNum);
        bool pushed = false;
        if (!path.empty() && std::filesystem::exists(path)) {
            try {
                std::ifstream in(path);
                nlohmann::json j;
                in >> j;
                Combo_PushCrossTablesForSeed(j);
                pushed = true;
            } catch (const std::exception& e) {
                std::cerr << "[ComboShip] slot " << fileNum << " cross-table re-derive failed: " << e.what() << "\n";
            }
        }
        if (!pushed)
            Combo_SetupCrossEntrances(0, nullptr, 0); // no consolidated seed for this slot — clear
    }
    if (!MM_LoadSaveForCombo || g_MmSaveInMemorySlot == fileNum) {
        return;
    }
    std::cout << "[ComboShip] Loading MM save for OOT slot " << fileNum << " (tracker peek)" << std::endl;
    MM_LoadSaveForCombo(fileNum);
    g_MmSaveInMemorySlot = fileNum;
}

static void Combo_OnOOTSceneSwitch(int fileNum) {
    std::cout << "[ComboShip] Mask Shop entered — switching to MM, slot " << fileNum << std::endl;
    g_PendingMMFileNum = fileNum;
    // OOT game loop is already exiting (gGameState->running = false set by the hook).
}

static void Combo_OnMMReturn(void) {
    std::cout << "[ComboShip] MM Clock Tower entered -- returning to OOT" << std::endl;
    g_pendingOOTReturn = true;
}

// ---------- O2R existence checks ----------

static bool OOTArchivesExist() {
    // The OoT *ROM* archive (player-extracted) is oot.o2r / oot-mq.o2r. soh.o2r is the bundled PORT
    // archive (assets/fonts) that always ships with the build — it must NOT count here, or a genuine
    // first run (port archive present, ROM not yet extracted) would skip extraction and then hard-exit
    // inside Initialize() when oot.o2r is missing.
    return std::filesystem::exists("oot-mq.o2r") || std::filesystem::exists("oot.o2r");
}

// ROM-derived archive (must be extracted from the player's MM ROM)
static bool MMRomArchiveExists() {
    return std::filesystem::exists("mm.o2r") || std::filesystem::exists("mm.zip") || std::filesystem::exists("mm.otr");
}

// Any MM archive at all (used for general "is MM set up" check)
static bool MMArchivesExist() {
    return MMRomArchiveExists() || std::filesystem::exists("2ship.o2r");
}

// ComboShip (issue 24): the combined config. Absent => fresh install => offer settings import.
static bool ComboConfigExists() {
    return std::filesystem::exists("comboship.json");
}

// Parse a JSON object from disk. False on missing/parse-failure/non-object (slot then skipped).
static bool LoadJsonObject(const std::string& path, nlohmann::json& out) {
    if (path.empty()) {
        return false;
    }
    try {
        std::ifstream f(path);
        if (!f) {
            return false;
        }
        nlohmann::json j = nlohmann::json::parse(f);
        if (!j.is_object()) {
            return false;
        }
        out = std::move(j);
        return true;
    } catch (...) { return false; }
}

// Per-leaf merge: objects recurse; on a leaf collision (scalar/array) the overlay wins. Keys unique
// to either side are kept. Used with 2Ship as base + SoH as overlay so SoH wins.
static void DeepMerge(nlohmann::json& base, const nlohmann::json& overlay) {
    if (!base.is_object() || !overlay.is_object()) {
        base = overlay;
        return;
    }
    for (auto it = overlay.begin(); it != overlay.end(); ++it) {
        auto found = base.find(it.key());
        if (found != base.end() && found->is_object() && it->is_object()) {
            DeepMerge(*found, *it);
        } else {
            base[it.key()] = it.value();
        }
    }
}

// Soft validator (non-blocking hint): a Ship config is a JSON object with a CVars block.
static int LauncherValidateShipConfig(const char* path) {
    nlohmann::json j;
    return (path && LoadJsonObject(path, j) && j.contains("CVars")) ? 1 : 0;
}

// ---------- Entry point ----------

int main(int argc, char** argv) {
    std::cout << "ComboShip Launcher - Starting..." << std::endl;

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

    // Resolve soh.dll exports
    SOH_Init = (FnVoid)GetSym(sohModule, "SOH_Init");
    SOH_RunMain = (FnRunMain)GetSym(sohModule, "SOH_RunMain");
    SOH_Extract = (FnExtract)GetSym(sohModule, "SOH_Extract");

    if (!SOH_Init || !SOH_RunMain) {
        std::cerr << "ERROR: soh.dll is missing required ComboShip exports (SOH_Init / SOH_RunMain)." << std::endl;
        std::cerr << "       Rebuild soh.dll from this ComboShip branch." << std::endl;
        FreeDll(mmModule);
        FreeDll(sohModule);
        return 1;
    }

    // Resolve 2ship.dll exports
    MM_InitArchives = (FnVoid)GetSym(mmModule, "MM_InitArchives");
    MM_Extract = (FnExtract)GetSym(mmModule, "MM_Extract");
    MM_ArchiveCount = (FnInt)GetSym(mmModule, "MM_ArchiveCount");
    SOH_SetOnNewSaveCallback = (FnSetSaveCallback)GetSym(sohModule, "SOH_SetOnNewSaveCallback");
    SOH_SetOnLoadSaveCallback = (FnSetSaveCallback)GetSym(sohModule, "SOH_SetOnLoadSaveCallback");
    MM_InitSaveFile = (FnMMInitSave)GetSym(mmModule, "MM_InitSaveFile");
    MM_LoadSaveForCombo = (FnMMInitSave)GetSym(mmModule, "MM_LoadSaveForCombo");
    SOH_SetOnSceneSwitchCallback = (FnSetSceneSwitchCallback)GetSym(sohModule, "SOH_SetOnSceneSwitchCallback");
    MM_RunGame = (FnMMRunGame)GetSym(mmModule, "MM_RunGame");
    SOH_Deinit = (FnSOHDeinit)GetSym(sohModule, "SOH_Deinit");
    SOH_PrepareForTransition = (FnSOHPrepare)GetSym(sohModule, "SOH_PrepareForTransition");
    MM_NotifyComboTransition = (FnMMNotify)GetSym(mmModule, "MM_NotifyComboTransition");
    MM_SetOnComboReturnCallback = (FnMMSetReturnCb)GetSym(mmModule, "MM_SetOnComboReturnCallback");
    SOH_ResumeGame = (FnVoidArgless)GetSym(sohModule, "SOH_ResumeGame");
    SOH_NotifyComboReturn = (FnVoidArgless)GetSym(sohModule, "SOH_NotifyComboReturn");
    MM_ResumeGame = (FnMMResume)GetSym(mmModule, "MM_ResumeGame");
    MM_PrepareForTransition = (FnVoidArgless)GetSym(mmModule, "MM_PrepareForTransition");
    SOH_GetPendingCrossTarget = (FnGetCrossTarget)GetSym(sohModule, "SOH_GetPendingCrossTarget");
    SOH_SetTargetEntrance = (FnSetTargetEntrance)GetSym(sohModule, "SOH_SetTargetEntrance");
    MM_GetPendingCrossTarget = (FnGetCrossTarget)GetSym(mmModule, "MM_GetPendingCrossTarget");
    MM_SetTargetEntrance = (FnSetTargetEntrance)GetSym(mmModule, "MM_SetTargetEntrance");
    SOH_DumpRandoStaticData = (FnDumpData)GetSym(sohModule, "SOH_DumpRandoStaticData");
    MM_DumpRandoStaticData = (FnDumpData)GetSym(mmModule, "MM_DumpRandoStaticData");
    SOH_DumpRandoSettings = (FnDumpData)GetSym(sohModule, "SOH_DumpRandoSettings");
    MM_DumpRandoSettings = (FnDumpData)GetSym(mmModule, "MM_DumpRandoSettings");
    SOH_PrepRandoContext = (FnVoidV)GetSym(sohModule, "SOH_PrepRandoContext");
    SOH_RestoreRandoSettings = (FnTakeStr)GetSym(sohModule, "SOH_RestoreRandoSettings");
    MM_RestoreRandoSettings = (FnTakeStr)GetSym(mmModule, "MM_RestoreRandoSettings");
    SOH_SetOnComboReloadCallback = (FnSetReloadCb)GetSym(sohModule, "SOH_SetOnComboReloadCallback");
    MM_InitRandoSaveFile = (FnMMInitRandoSave)GetSym(mmModule, "MM_InitRandoSaveFile");
    SOH_SetOnComboGenerateCallback = (FnSetGenerateCb)GetSym(sohModule, "SOH_SetOnComboGenerateCallback");
    SOH_ApplyRandoPlacements = (FnApplyPlacements)GetSym(sohModule, "SOH_ApplyRandoPlacements");
    SOH_GetForcedPlacements = (FnGetForced)GetSym(sohModule, "SOH_GetForcedPlacements");
    SOH_SetComboRandoSeed = (FnSetComboRandoSeed)GetSym(sohModule, "SOH_SetComboRandoSeed");
    SOH_SetComboSeedHash = (FnSetComboSeedHash)GetSym(sohModule, "SOH_SetComboSeedHash");
    SOH_SetOnComboGenerateRequestCallback = (FnSetGenReqCb)GetSym(sohModule, "SOH_SetOnComboGenerateRequestCallback");
    SOH_SetSeedGenerated = (FnSetSeedGenerated)GetSym(sohModule, "SOH_SetSeedGenerated");
    SOH_SetComboProgressPtr = (FnSetComboProgressPtr)GetSym(sohModule, "SOH_SetComboProgressPtr");
    SOH_SetOnComboFinalizeCallback = (FnSetComboFinalizeCb)GetSym(sohModule, "SOH_SetOnComboFinalizeCallback");
    MM_BootForCombo = (FnVoidArgless)GetSym(mmModule, "MM_BootForCombo");
    MM_Deinit = (FnVoidArgless)GetSym(mmModule, "MM_Deinit");
    SOH_ResumeForeground = (FnVoidArgless)GetSym(sohModule, "SOH_ResumeForeground");

    // ComboShip-owned unified extraction primitives + split init
    SOH_InitWindowOnly = (FnVoid)GetSym(sohModule, "SOH_InitWindowOnly");
    SOH_FinishInit = (FnVoid)GetSym(sohModule, "SOH_FinishInit");
    SOH_ValidateRom = (ComboFnValidateRom)GetSym(sohModule, "SOH_ValidateRom");
    SOH_StartExtraction = (ComboFnStartExtraction)GetSym(sohModule, "SOH_StartExtraction");
    SOH_GetExtractionProgress = (ComboFnGetProgress)GetSym(sohModule, "SOH_GetExtractionProgress");
    MM_ValidateRom = (ComboFnValidateRom)GetSym(mmModule, "MM_ValidateRom");
    MM_StartExtraction = (ComboFnStartExtraction)GetSym(mmModule, "MM_StartExtraction");
    MM_GetExtractionProgress = (ComboFnGetProgress)GetSym(mmModule, "MM_GetExtractionProgress");
    SOH_ApplyImportedConfig = (ComboFnApplyImportedConfig)GetSym(sohModule, "SOH_ApplyImportedConfig");

    // Anchor transport seam exports (Phase 1)
    SOH_SetAnchorSend = (FnSetAnchorSend)GetSym(sohModule, "SOH_SetAnchorSend");
    SOH_SetAnchorConnect = (FnSetAnchorConnect)GetSym(sohModule, "SOH_SetAnchorConnect");
    SOH_SetAnchorDisconnect = (FnSetAnchorDisconnect)GetSym(sohModule, "SOH_SetAnchorDisconnect");
    SOH_Anchor_RecvJson = (FnAnchorRecv)GetSym(sohModule, "SOH_Anchor_RecvJson");
    SOH_Anchor_OnConnected = (FnVoidArgless)GetSym(sohModule, "SOH_Anchor_OnConnected");
    SOH_Anchor_OnDisconnected = (FnVoidArgless)GetSym(sohModule, "SOH_Anchor_OnDisconnected");
    MM_SetAnchorSend = (FnSetAnchorSend)GetSym(mmModule, "MM_SetAnchorSend");
    MM_Anchor_RecvJson = (FnAnchorRecv)GetSym(mmModule, "MM_Anchor_RecvJson");
    MM_Anchor_Activate = (FnVoidArgless)GetSym(mmModule, "MM_Anchor_Activate");
    MM_Anchor_Deactivate = (FnVoidArgless)GetSym(mmModule, "MM_Anchor_Deactivate");

    // Cross-game item delivery seam (issue #3)
    SOH_SetCrossDeliver = (FnSetCrossRoute)GetSym(sohModule, "SOH_SetCrossDeliver");
    MM_SetCrossDeliver = (FnSetCrossRoute)GetSym(mmModule, "MM_SetCrossDeliver");
    SOH_GrantCrossItem = (FnGrantCrossItem)GetSym(sohModule, "SOH_GrantCrossItem");
    MM_GrantCrossItem = (FnGrantCrossItem)GetSym(mmModule, "MM_GrantCrossItem");
    SOH_SetMarkForeignObtained = (FnSetCrossRoute)GetSym(sohModule, "SOH_SetMarkForeignObtained");
    MM_SetMarkForeignObtained = (FnSetCrossRoute)GetSym(mmModule, "MM_SetMarkForeignObtained");
    SOH_MarkForeignObtained = (FnGrantCrossItem)GetSym(sohModule, "SOH_MarkForeignObtained");
    MM_MarkForeignObtained = (FnGrantCrossItem)GetSym(mmModule, "MM_MarkForeignObtained");

    // Oracle exports
    Combo_SOH_Rando_Reset = (FnOracleVoid)GetSym(sohModule, "Combo_SOH_Rando_Reset");
    Combo_SOH_Rando_SetOwnedItems = (FnOracleSetItems)GetSym(sohModule, "Combo_SOH_Rando_SetOwnedItems");
    Combo_SOH_Rando_GetReachableChecks = (FnOracleGetChecks)GetSym(sohModule, "Combo_SOH_Rando_GetReachableChecks");
    Combo_SOH_Rando_PlaceItem = (FnOraclePlaceItem)GetSym(sohModule, "Combo_SOH_Rando_PlaceItem");
    Combo_MM_Rando_Reset = (FnOracleVoid)GetSym(mmModule, "Combo_MM_Rando_Reset");
    Combo_MM_Rando_SetOwnedItems = (FnOracleSetItems)GetSym(mmModule, "Combo_MM_Rando_SetOwnedItems");
    Combo_MM_Rando_GetReachableChecks = (FnOracleGetChecks)GetSym(mmModule, "Combo_MM_Rando_GetReachableChecks");
    Combo_MM_Rando_PlaceItem = (FnOraclePlaceItem)GetSym(mmModule, "Combo_MM_Rando_PlaceItem");
    Combo_MM_Rando_Restore = (FnOracleVoid)GetSym(mmModule, "Combo_MM_Rando_Restore");

    // Entrance-shuffle wiring
    SOH_ShuffleEntrancesForCombo = (FnShuffleEntrances)GetSym(sohModule, "SOH_ShuffleEntrancesForCombo");
    SOH_DumpEntranceOverrides = (FnDumpData)GetSym(sohModule, "SOH_DumpEntranceOverrides");
    Combo_SOH_Rando_IsRegionReachable = (FnIsRegionReachable)GetSym(sohModule, "Combo_SOH_Rando_IsRegionReachable");
    MM_SetComboFinalSeed = (FnSetFinalSeed)GetSym(mmModule, "MM_SetComboFinalSeed");
    Combo_MM_Rando_EntranceShuffleOk = (FnIntV)GetSym(mmModule, "Combo_MM_Rando_EntranceShuffleOk");
    MM_DumpEntranceMap = (FnDumpData)GetSym(mmModule, "MM_DumpEntranceMap");

    // Cross-game entrance shuffle (Phase B)
    SOH_DumpInteriorEntrancePairs = (FnDumpData)GetSym(sohModule, "SOH_DumpInteriorEntrancePairs");
    MM_DumpInteriorEntrancePairs = (FnDumpData)GetSym(mmModule, "MM_DumpInteriorEntrancePairs");
    SOH_SetCrossEntranceTable = (FnTakeStr)GetSym(sohModule, "SOH_SetCrossEntranceTable");
    MM_SetCrossEntranceTable = (FnTakeStr)GetSym(mmModule, "MM_SetCrossEntranceTable");
    Combo_SOH_Rando_SetExternallyReachableRegions =
        (FnTakeStr)GetSym(sohModule, "Combo_SOH_Rando_SetExternallyReachableRegions");
    Combo_MM_Rando_SetExternallyReachableRegions =
        (FnTakeStr)GetSym(mmModule, "Combo_MM_Rando_SetExternallyReachableRegions");
    Combo_MM_Rando_IsRegionReachable = (FnIsRegionReachable)GetSym(mmModule, "Combo_MM_Rando_IsRegionReachable");
    SOH_GetCVarInteger = (FnGetCVarInt)GetSym(sohModule, "SOH_GetCVarInteger");

    // Cross-game erase seam (issue #1)
    SOH_SetDeleteForeignSave = (FnSetDeleteForeignSave)GetSym(sohModule, "SOH_SetDeleteForeignSave");
    MM_SetDeleteForeignSave = (FnSetDeleteForeignSave)GetSym(mmModule, "MM_SetDeleteForeignSave");
    SOH_DeleteSaveFile = (FnDeleteSaveFile)GetSym(sohModule, "SOH_DeleteSaveFile");
    MM_DeleteSaveFile = (FnDeleteSaveFile)GetSym(mmModule, "MM_DeleteSaveFile");

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
        cb.sohStart = SOH_StartExtraction;
        cb.sohProgress = SOH_GetExtractionProgress;
        cb.sohNeeded = needOot ? 1 : 0;
        cb.mmValidate = MM_ValidateRom;
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
    if (SOH_SetCrossDeliver || MM_SetCrossDeliver) {
        std::cout << "[ComboShip] Cross-game item delivery seam registered." << std::endl;
    }

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
        ComboUI_Register = (FnComboUIRegister)GetSym(comboUIModule, "ComboUI_Register");
        ComboUI_OnForegroundGame = (FnComboUIForeground)GetSym(comboUIModule, "ComboUI_OnForegroundGame");
        ComboUI_RestoreTrackerIntent = (FnComboUIRegister)GetSym(comboUIModule, "ComboUI_RestoreTrackerIntent");
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
    if (ComboUI_OnForegroundGame)
        ComboUI_OnForegroundGame(0);

        // ComboShip: dump OOT/MM static rando data (headless, safe AFTER SOH_Init) to
        // saves/combo/{oot,mm}_dump.json so the check set is verifiable and an empty MM dump (eager-boot
        // regression) is caught at startup. Pure diagnostic — generation re-dumps independently. Debug only.
#ifndef NDEBUG
    {
        std::error_code ec;
        std::filesystem::create_directories("saves/combo", ec);

        if (SOH_DumpRandoStaticData) {
            std::string sohDump = SOH_DumpRandoStaticData();
            {
                std::ofstream f("saves/combo/oot_dump.json", std::ios::trunc);
                f << sohDump;
            }
            auto j = nlohmann::json::parse(sohDump);
            std::cout << "[ComboShip] OOT coherent dump: " << j["checks"].size() << " checks, " << j["items"].size()
                      << " items -> saves/combo/oot_dump.json\n";
        }
        if (MM_DumpRandoStaticData) {
            std::string mmDump = MM_DumpRandoStaticData();
            {
                std::ofstream f("saves/combo/mm_dump.json", std::ios::trunc);
                f << mmDump;
            }
            auto j = nlohmann::json::parse(mmDump);
            std::cout << "[ComboShip] MM static dump: " << j["checks"].size() << " checks, " << j["items"].size()
                      << " items -> saves/combo/mm_dump.json\n";
        }
    }
#endif

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

    if (SOH_SetOnNewSaveCallback && MM_InitSaveFile) {
        SOH_SetOnNewSaveCallback(Combo_OnOOTSaveInit);
        std::cout << "[ComboShip] OOT new-save callback registered." << std::endl;
    }
    // Pre-save-init hook (z_sram): runs the reload path's deferred entrance re-derivation.
    if (SOH_SetOnComboGenerateCallback)
        SOH_SetOnComboGenerateCallback(Combo_OnPreOOTSaveInit);

    if (SOH_SetOnLoadSaveCallback && MM_LoadSaveForCombo) {
        SOH_SetOnLoadSaveCallback(Combo_OnOOTSaveLoad);
        std::cout << "[ComboShip] OOT save-load callback registered." << std::endl;
    }

    if (SOH_SetOnSceneSwitchCallback) {
        SOH_SetOnSceneSwitchCallback(Combo_OnOOTSceneSwitch);
        std::cout << "[ComboShip] OOT scene-switch callback registered." << std::endl;
    }

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
            // Cross-interiors PoC: MM may have staged an OOT arrival entrance for this resume.
            if (MM_GetPendingCrossTarget && SOH_SetTargetEntrance) {
                int target = MM_GetPendingCrossTarget();
                if (target >= 0)
                    SOH_SetTargetEntrance(target);
            }
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
                ComboAnchor::SetActiveGame(1); // route Anchor to MM, activate MM's adapter
                if (ComboUI_OnForegroundGame)  // hide OOT trackers, show MM's
                    ComboUI_OnForegroundGame(1);
                current = GAME_MM;
            } else {
                break;
            }
        } else {
            g_pendingOOTReturn = false;
            // MM's own boot/resume path loads this slot's save into gSaveContext.
            g_MmSaveInMemorySlot = g_PendingMMFileNum;
            // Cross-interiors PoC: OOT may have staged an MM arrival entrance for this switch.
            if (SOH_GetPendingCrossTarget && MM_SetTargetEntrance) {
                int target = SOH_GetPendingCrossTarget();
                if (target >= 0)
                    MM_SetTargetEntrance(target);
            }
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
                ComboAnchor::SetActiveGame(0); // route Anchor back to OOT, deactivate MM's adapter
                if (ComboUI_OnForegroundGame)  // hide MM trackers, restore OOT's
                    ComboUI_OnForegroundGame(0);
                current = GAME_OOT;
            } else {
                break;
            }
        }
    }

    // Teardown order matters: MM first (it holds a shared_ptr to the SHARED Context and BenGui::Destroy
    // needs the Context alive), then SOH — its DeinitOTR releases the LAST Context reference, so
    // ~Context runs here on the main thread: saves window geometry + config, destroys the window, and
    // shuts down logging. Everything thread-owning (audio threads, ResourceManager thread pools) must
    // be joined/destroyed before the FreeDll calls below — joining a thread during DLL unload runs
    // under the loader lock and deadlocks.
    // std::cerr (unbuffered) progress markers: spdlog is shut down by ~Context partway through this
    // sequence, and a late crash otherwise leaves no trace of how far teardown got.

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

    // Generate is now synchronous — no background thread to join before freeing DLLs.

    if (comboUIModule)
        FreeDll(comboUIModule);
    std::cerr << "[ComboShip] shutdown: comboui freed" << std::endl;
    FreeDll(mmModule);
    std::cerr << "[ComboShip] shutdown: 2ship freed" << std::endl;
    FreeDll(sohModule);
    std::cerr << "[ComboShip] shutdown: soh freed - exiting normally" << std::endl;
    return 0;
}
