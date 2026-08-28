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

// SDL_net.h pulls in SDL.h -> SDL_main.h, which #defines main -> SDL_main unless we opt out.
// ComboShip provides its own main(), so suppress SDL's entry-point hijack.
#define SDL_MAIN_HANDLED
#include <SDL2/SDL_net.h>

#include "rando/CrossForeign.h"
#include "rando/CrossWorldRando.h"
#include "rando/ComboPlaythrough.h"
#include "rando/CrossHints.h"
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
// Last-chance crash capture for the post-teardown window (FreeLibrary / CRT exit / static dtors),
// which libultraship's Context-dependent CrashHandler can't cover. Installed after deinit; writes
// module+symbol frames to combo_late_crash.txt. See docs/deviations/boot-shutdown.md.
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
typedef void (*FnGetPlayerName)(unsigned char*);
static FnGetPlayerName SOH_GetCurrentPlayerName = nullptr;
// Nonzero = the slot's MM half is missing/broken; nothing was loaded (see SaveManager_LoadSaveFile).
typedef int (*FnMMLoadSave)(int);
static FnMMLoadSave MM_LoadSaveForCombo = nullptr;
// ComboShip (#182): MM caches which slot's owl save its gSaveContext came from; tell it when the
// launcher replaces a slot's mm section underneath it.
typedef void (*FnMMInvalidateOwlBlob)(void);
static FnMMInvalidateOwlBlob MM_InvalidateOwlBlobSlot = nullptr;
// #89 resume-into-MM: drop out of OOT's loop before it runs a Play frame / tell MM how it was entered.
// SOH_IsOnFileSelect distinguishes a real file-select load from the OnLoadGame that TitleSetup fires
// on the MM->OOT return (which must not bounce the player straight back into MM).
typedef unsigned char (*FnIsOnFileSelect)();
static FnVoid SOH_ParkForComboMMResume = nullptr;
static FnMMInitSave MM_SetComboEntryIsResume = nullptr;
static FnIsOnFileSelect SOH_IsOnFileSelect = nullptr;
// OOT slot whose MM save is live in MM's dormant memory (-1 = none). Guards Combo_OnOOTSaveLoad
// against reloading stale disk state over MM's in-memory progress after round trips.
static int g_MmSaveInMemorySlot = -1;
static FnSetSceneSwitchCallback SOH_SetOnSceneSwitchCallback = nullptr;
static FnMMRunGame MM_RunGame = nullptr;
static FnSOHDeinit SOH_Deinit = nullptr;
static FnSOHPrepare SOH_PrepareForTransition = nullptr;
static FnMMNotify MM_NotifyComboTransition = nullptr;

typedef void (*FnMMSetReturnCb)(void (*)(int));
static FnMMSetReturnCb MM_SetOnComboReturnCallback = nullptr;
static bool g_pendingOOTReturn = false;

typedef void (*FnVoidArgless)(void);
static FnVoidArgless SOH_ResumeGame = nullptr;
static FnVoidArgless SOH_NotifyComboReturn = nullptr;

typedef void (*FnMMResume)(int);
static FnMMResume MM_ResumeGame = nullptr;
static FnVoidArgless MM_PrepareForTransition = nullptr;

// ComboShip: headless static-data dump exports
typedef const char* (*FnDumpData)(void);
static FnDumpData SOH_DumpRandoStaticData = nullptr;
static FnDumpData MM_DumpRandoStaticData = nullptr;
static FnDumpData SOH_DumpRandoSettings = nullptr; // {cvar:value} OOT rando settings snapshot
static FnDumpData SOH_DumpEnabledTricks = nullptr; // [NameTag,...] the player's enabled OOT tricks
static FnDumpData MM_DumpRandoSettings = nullptr;  // {cvar:value} MM rando settings snapshot
static FnDumpData SOH_DumpRandoHintData = nullptr; // OOT hint text/options schema (cross-hint Phase 2)
// ComboShip: cross-hint Phase 3 — apply combo-generated hints + tell OOT whether this seed has any.
typedef void (*FnApplyHints)(const char*);
typedef void (*FnSetHintsPresent)(int);
static FnApplyHints SOH_ApplyComboHints = nullptr;
static FnSetHintsPresent SOH_SetComboHintsPresent = nullptr;
// #169: OOT's generation-completion hooks (cosmetics/audio randomize-on-gen) — combo's fill never
// reaches the vanilla fire site, so the launcher fires them itself (see Combo_FireGenRollHooksOnce).
static FnVoidArgless SOH_FireGenerationCompleteHooks = nullptr;
// Reload/remember-seed: restore settings + run the pool prep before re-applying saved placements.
typedef void (*FnVoidV)(void);
typedef void (*FnTakeStr)(const char*);
typedef void (*FnSetReloadCb)(int (*)(const char*));
static FnVoidV SOH_PrepRandoContext = nullptr;
static FnTakeStr SOH_RestoreRandoSettings = nullptr;
static FnTakeStr MM_RestoreRandoSettings = nullptr;
static FnTakeStr SOH_SetCheckPrices = nullptr;
static FnTakeStr MM_SetCheckPrices = nullptr;
static FnSetReloadCb SOH_SetOnComboReloadCallback = nullptr;
// Remembered spoiler path (CVAR_GENERAL("ComboSpoiler")) — soh owns the config, so the launcher goes
// through it rather than parsing comboship.json itself.
static FnTakeStr SOH_SetComboSpoilerPath = nullptr;
static FnDumpData SOH_GetComboSpoilerPath = nullptr;

// ComboShip merged per-slot save container: setters that push the launcher's save-IO callbacks into
// each DLL, plus the once-per-load push of the baked combo rando (foreign map + cross-hints).
typedef void (*FnSetComboSaveIO)(ComboRando::FnComboReadSave, ComboRando::FnComboWriteSave);
static FnSetComboSaveIO SOH_SetComboSaveIO = nullptr;
static FnSetComboSaveIO MM_SetComboSaveIO = nullptr;
static FnTakeStr SOH_LoadComboRando = nullptr;
static FnTakeStr MM_LoadComboRando = nullptr;
// OOT file-select "copy file": whole-container copy (both games + rando) through the launcher.
typedef void (*FnSetCopyContainer)(void (*)(int, int));
static FnSetCopyContainer SOH_SetCopyContainer = nullptr;
// OOT polls this each frame (main thread) for slots whose container was backed up for a release mismatch.
typedef void (*FnSetOutdatedSaveNotice)(int (*)());
static FnSetOutdatedSaveNotice SOH_SetOutdatedSaveNotice = nullptr;

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

// ComboShip: hand comboui the launcher-owned Anchor roster getter (the room window reads it).
typedef void (*FnComboUISetRosterProvider)(const char* (*)());
static FnComboUISetRosterProvider ComboUI_SetAnchorRosterProvider = nullptr;

// ComboShip (#165): hand comboui the launcher-owned per-slot notes accessors (combo.notes).
typedef void (*FnComboUISetNotesStore)(const char* (*)(int), void (*)(int, const char*));
static FnComboUISetNotesStore ComboUI_SetNotesStore = nullptr;

// ComboShip (#164): combo Hint Tracker — push the slot's hints slice + read state into comboui, and
// receive reveal reports back from both game DLLs.
typedef void (*FnComboUISetHintTrackerData)(int, const char*, const char*);
static FnComboUISetHintTrackerData ComboUI_SetHintTrackerData = nullptr;
typedef void (*FnSetHintRevealOot)(void (*)(int, const char*));
static FnSetHintRevealOot SOH_SetComboHintRevealCb = nullptr;
typedef void (*FnSetHintRevealMm)(void (*)(int, int, int, const char*, const char*));
static FnSetHintRevealMm MM_SetComboHintRevealCb = nullptr;

// ComboShip (#173): combo-owned overlay timers. MM's play time is wall clock between flushes, so it
// must be paused/resumed across every game swap or the time spent in OOT lands in MM's save.
typedef void (*FnComboUISetInt)(int);
static FnComboUISetInt ComboUI_SetComboComplete = nullptr;
static FnVoidArgless MM_ComboPausePlaytime = nullptr;
static FnVoidArgless MM_ComboResumePlaytime = nullptr;

// ComboShip (#169): combo-owned OOT->MM cosmetic color sync (combo/gui/ComboCosmeticsSync.cpp). The
// gate predicate is exported too, so the launcher never duplicates the CVar reads.
static FnVoidArgless ComboUI_SyncRandomizedCosmetics = nullptr;
typedef int (*FnComboUIGate)(void);
static FnComboUIGate ComboUI_CosmeticsSyncGateEnabled = nullptr;
// Per-seed latch for the gen-roll hooks (comboui owns it — the exe has no CVar API). 1 = not rolled yet.
typedef int (*FnClaimGenRollSeed)(unsigned long long);
static FnClaimGenRollSeed ComboUI_ClaimGenRollSeed = nullptr;

// ComboShip (#169): fire OOT's generation-completion hooks at most once per seed per machine, so the
// silent auto-load on every boot cannot re-roll over cosmetic/audio edits the user made by hand.
// force = fresh generation: still claim the seed (so later loads of it leave manual edits alone) but
// roll regardless, keeping vanilla's unconditional gen-only semantics.
static void Combo_FireGenRollHooksOnce(uint64_t masterSeed, bool force = false) {
    if (!SOH_FireGenerationCompleteHooks)
        return;
    const bool claimed = ComboUI_ClaimGenRollSeed && ComboUI_ClaimGenRollSeed(masterSeed);
    if (claimed || force)
        SOH_FireGenerationCompleteHooks();
}

// ComboShip-owned unified ROM extraction (see ComboExtract.h). The split init lets us create the
// shared window from soh.o2r before any ROM exists, run the extraction screen, then finish.
static FnVoid SOH_InitWindowOnly = nullptr;
static FnVoid SOH_FinishInit = nullptr;
static ComboFnValidateRom SOH_ValidateRom = nullptr;
static ComboFnValidateRom SOH_ClassifyRom = nullptr;
static ComboFnStartExtraction SOH_StartExtraction = nullptr;
static ComboFnGetProgress SOH_GetExtractionProgress = nullptr;
static ComboFnValidateRom MM_ValidateRom = nullptr;
static ComboFnValidateRom MM_ClassifyRom = nullptr;
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
typedef uint8_t (*FnOracleGetPortalOpen)(void);

static FnOracleVoid Combo_SOH_Rando_Reset = nullptr;
static FnOracleSetItems Combo_SOH_Rando_SetOwnedItems = nullptr;
static FnOracleGetChecks Combo_SOH_Rando_GetReachableChecks = nullptr;
static FnOraclePlaceItem Combo_SOH_Rando_PlaceItem = nullptr;
// OOT->MM portal gate (Happy Mask Shop region access) — see CrossWorldRando.h.
static FnOracleGetPortalOpen Combo_SOH_Rando_GetPortalOpen = nullptr;

static FnOracleVoid Combo_MM_Rando_Reset = nullptr;
static FnOracleSetItems Combo_MM_Rando_SetOwnedItems = nullptr;
static FnOracleGetChecks Combo_MM_Rando_GetReachableChecks = nullptr;
static FnOraclePlaceItem Combo_MM_Rando_PlaceItem = nullptr;
static FnOracleVoid Combo_MM_Rando_Restore = nullptr;

// ComboShip (#90): OOT entrance shuffle — the combo generator never runs native Fill(), so the
// entrance options need this explicit headless shuffle + an informational spoiler dump.
typedef int (*FnShuffleEntrances)(uint64_t);
static FnShuffleEntrances SOH_ShuffleEntrancesForCombo = nullptr;
static FnDumpData SOH_DumpEntranceOverrides = nullptr;

// ---------- ComboShip merged per-slot save container (Save/file{N+1}.combosav) ----------
// One JSON file per slot holds both games' saves verbatim + combo metadata (completion + baked rando).
// The launcher mediates every per-slot read/write through Combo_ReadGameSave/Combo_WriteGameSave
// (pushed into each DLL at boot), so the in-process cache stays authoritative. Single mutex serializes
// OOT's thread-pool writes, MM's synchronous writes, and Anchor cross-writes. Write = temp+rename,
// never torn on crash. Each container carries "comboRelease" (COMBO_RELEASE_VERSION); a container from a
// different major.minor is backed up to .bak and recreated — the launcher is the sole save-compat gate.
// See docs/deviations/boot-shutdown.md.
static std::mutex g_containerMutex;
static std::map<int, nlohmann::json> g_containerCache;
// Slots whose container was backed up for a COMBO_RELEASE_VERSION mismatch; guarded by g_containerMutex.
// OOT drains it on the main thread (Combo_TakeEvictionNotice) to surface a popup.
static std::vector<int> g_evictedSlots;

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

static std::filesystem::path ComboContainerPath(int fileNum) {
    return std::filesystem::path("Save") / ("file" + std::to_string(fileNum + 1) + ".combosav");
}

// Only the three real save slots have a container. Callbacks reached from a game's gSaveContext.fileNum
// can carry 0xFF (no save loaded) or 0xFE (Boss Rush) — those must never create a phantom container.
static bool ComboIsValidSlot(int fileNum) {
    return fileNum >= 0 && fileNum <= 2;
}

// Save compat is gated on major.minor only: patch releases must never change save-affecting behavior.
static std::string ComboReleaseMajorMinor(const std::string& v) {
    size_t dot = v.find('.');
    return v.substr(0, dot == std::string::npos ? std::string::npos : v.find('.', dot + 1));
}

// Hold g_containerMutex. Returns a ref into the cache; loads from disk or creates a fresh container.
static nlohmann::json& LoadOrCreateContainer(int fileNum) {
    auto it = g_containerCache.find(fileNum);
    if (it != g_containerCache.end())
        return it->second;
    nlohmann::json j;
    auto path = ComboContainerPath(fileNum);
    std::error_code ec;
    bool existed = std::filesystem::exists(path, ec);
    std::ifstream in(path);
    bool parsed = false;
    if (in.is_open()) {
        try {
            in >> j;
            parsed = j.is_object();
        } catch (...) { parsed = false; }
    }
    in.close();
    // Never silently overwrite an existing-but-unreadable container: a fresh cache entry would drop
    // the other game's section on the next write. Preserve the file aside, then start fresh.
    if (existed && !parsed) {
        std::filesystem::rename(path, path.string() + ".corrupt-" + std::to_string(std::time(nullptr)), ec);
    }
    // COMBO_RELEASE_VERSION gate (major.minor only; patch releases keep saves): a container from a
    // different release is outdated. Back it up aside and start fresh; record the slot so OOT can
    // surface a popup on its main thread.
    if (parsed) {
        auto rel = j.find("comboRelease");
        if (rel == j.end() || !rel->is_string() ||
            ComboReleaseMajorMinor(rel->get<std::string>()) != ComboReleaseMajorMinor(COMBO_RELEASE_VERSION)) {
            std::filesystem::rename(path, path.string() + "-" + std::to_string(std::time(nullptr)) + ".bak", ec);
            g_evictedSlots.push_back(fileNum); // caller holds g_containerMutex
            parsed = false;
        }
    }
    // ComboShip (#165): one-time migrate SoH's per-save notes into combo.notes. Absent-key gate only,
    // so a deliberately cleared combo note is never re-migrated.
    if (parsed && !(j.contains("combo") && j["combo"].is_object() && j["combo"].contains("notes"))) {
        try {
            auto& pn = j.at("oot").at("sections").at("itemTrackerData").at("data").at("personalNotes");
            if (pn.is_string() && !pn.get<std::string>().empty())
                j["combo"]["notes"] = pn;
        } catch (...) {} // oot section absent/null — nothing to migrate
    }
    if (!parsed)
        j = nlohmann::json{ { "comboVersion", 1 }, { "comboRelease", COMBO_RELEASE_VERSION },
                            { "slot", fileNum },   { "oot", nullptr },
                            { "mm", nullptr },     { "combo", nlohmann::json::object() } };
    return g_containerCache.emplace(fileNum, std::move(j)).first->second;
}

// Hold g_containerMutex. Serialize the cached container to a temp file, then atomic rename over it.
static void FlushContainer(int fileNum) {
    auto it = g_containerCache.find(fileNum);
    if (it == g_containerCache.end())
        return;
    std::error_code ec;
    auto path = ComboContainerPath(fileNum);
    std::filesystem::create_directories(path.parent_path(), ec);
    auto tmp = path;
    tmp += ".temp";
    {
        std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
        if (!out.is_open())
            return;
        it->second["comboRelease"] = COMBO_RELEASE_VERSION; // every write carries the current release
        out << it->second.dump();
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) { // some filesystems won't replace-on-rename — remove then retry
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
}

// OOT (main thread) polls this each frame via SOH_SetOutdatedSaveNotice: pops the next slot whose
// container was backed up for a release mismatch, or -1 if none. Mirrors the SOH_SetCopyContainer wiring.
static int Combo_TakeEvictionNotice() {
    std::lock_guard<std::mutex> lk(g_containerMutex);
    if (g_evictedSlots.empty())
        return -1;
    int slot = g_evictedSlots.front();
    g_evictedSlots.erase(g_evictedSlots.begin());
    return slot;
}

static void EraseComboContainer(int slot) {
    {
        std::lock_guard<std::mutex> lk(g_containerMutex);
        g_containerCache.erase(slot);
        // MM's dormant save is now stale: leave it marked resident and the next load skips re-reading it,
        // and any dormant MM write would put the erased save back into the slot.
        if (g_MmSaveInMemorySlot == slot)
            g_MmSaveInMemorySlot = -1;
        std::error_code ec;
        std::filesystem::remove(ComboContainerPath(slot), ec);
    }
    // ComboShip (#182): MM caches which slot's owl save its gSaveContext came from; the section it
    // named is gone. Outside the lock — the DLL must never re-enter the container.
    if (MM_InvalidateOwlBlobSlot)
        MM_InvalidateOwlBlobSlot();
    // ComboShip (#164): clear the Hint Tracker outside the lock — its push path re-takes the mutex, and
    // the window would otherwise keep showing the deleted slot's hints on the file-select screen.
    if (ComboUI_SetHintTrackerData)
        ComboUI_SetHintTrackerData(-1, "", "");
}

// Copy a whole slot (both games + baked rando) — OOT file-select "copy file". Registered into OOT
// via SOH_SetCopyContainer; the .combosav has no per-game file to copy, so the launcher owns it.
static void Combo_CopyContainer(int from, int to) {
    {
        std::lock_guard<std::mutex> lk(g_containerMutex);
        nlohmann::json copy = LoadOrCreateContainer(from); // deep copy of the source container
        copy["slot"] = to;
        g_containerCache[to] = std::move(copy);
        if (g_MmSaveInMemorySlot == to)
            g_MmSaveInMemorySlot = -1; // the destination's MM save just changed under us
        FlushContainer(to);
    }
    // ComboShip (#182): the destination's owl save came from the donor, so MM's descent cache is wrong.
    if (MM_InvalidateOwlBlobSlot)
        MM_InvalidateOwlBlobSlot();
}

// Launcher-provided save IO, pushed into each DLL. game: 0=OOT,1=MM (GameId); fileNum 0-based.
// Returns the section JSON in a thread_local buffer (OOT may read off the main thread), "" if absent.
static const char* Combo_ReadGameSave(int game, int fileNum) {
    // No container exists for a sentinel fileNum - see ComboIsValidSlot.
    if (!ComboIsValidSlot(fileNum))
        return "";
    thread_local std::string buf;
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(fileNum);
    const char* key = (game == ComboRando::GAME_OOT) ? "oot" : "mm";
    auto it = c.find(key);
    buf = (it == c.end() || it->is_null()) ? std::string() : it->dump();
    return buf.c_str();
}

// Read-modify-write the FULL container (never re-derived) so the other game's section stays intact
// when e.g. an Anchor MM write lands during OOT play. Malformed inbound leaves the section untouched.
static void Combo_WriteGameSave(int game, int fileNum, const char* json) {
    if (!json)
        return;
    // Same guard as the read: a sentinel fileNum must never create a phantom container. Say so once —
    // the session this fires in drops every save, and the load failure may be hours back in the log.
    if (!ComboIsValidSlot(fileNum)) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::cerr << "[ComboShip] dropping save write for game " << game << ": no slot loaded (fileNum " << fileNum
                      << ")" << std::endl;
        }
        return;
    }
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(fileNum);
    const char* key = (game == ComboRando::GAME_OOT) ? "oot" : "mm";
    try {
        c[key] = nlohmann::json::parse(json);
    } catch (...) { return; }
    FlushContainer(fileNum);
}

// Record which game the player is now in, so a quit-and-reload resumes there. Set at the two
// transitions only — NOT from save writes: loading an OOT save itself writes sections (rando and
// check-tracker OnLoadGame handlers), which would stamp OOT over the MM the player actually left in.
static void Combo_SetLastGame(int fileNum, int game) {
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(fileNum);
    if (c.value("combo", nlohmann::json::object()).value("lastGame", -1) == game)
        return; // already correct — don't rewrite the container for nothing
    std::cout << "[ComboShip] lastGame <- " << game << " (slot " << fileNum << ")" << std::endl;
    c["combo"]["lastGame"] = game;
    FlushContainer(fileNum);
}

// Which game a slot was last saved in (GameId). Absent => OOT, so pre-lastGame saves resume as before.
static int Combo_GetLastGame(int fileNum) {
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(fileNum);
    int g = c.value("combo", nlohmann::json::object()).value("lastGame", (int)ComboRando::GAME_OOT);
    return (g == ComboRando::GAME_MM) ? ComboRando::GAME_MM : ComboRando::GAME_OOT;
}

// ComboShip (#165): the slot's cross-game personal notes. One note per slot, editable from either
// game. Returned in a thread_local buffer (same lifetime contract as Combo_ReadGameSave).
static const char* Combo_GetNotes(int fileNum) {
    thread_local std::string buf;
    if (!ComboIsValidSlot(fileNum)) {
        buf.clear();
        return buf.c_str();
    }
    std::lock_guard<std::mutex> lk(g_containerMutex);
    buf.clear();
    // find()-based: never throws (comboui calls these by raw fn-ptr) and never copies combo.rando.
    auto& c = LoadOrCreateContainer(fileNum);
    auto combo = c.find("combo");
    if (combo != c.end() && combo->is_object()) {
        auto n = combo->find("notes");
        if (n != combo->end() && n->is_string())
            buf = n->get<std::string>();
    }
    return buf.c_str();
}

static void Combo_SetNotes(int fileNum, const char* text) {
    if (!text || !ComboIsValidSlot(fileNum))
        return;
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(fileNum);
    const std::string* cur = nullptr;
    auto combo = c.find("combo");
    if (combo != c.end() && combo->is_object()) {
        auto n = combo->find("notes");
        if (n != combo->end() && n->is_string())
            cur = &n->get_ref<const std::string&>();
    }
    // Unchanged — don't rewrite the container on every debounce (absent/non-string compares as "").
    if (cur ? *cur == text : !*text)
        return;
    c["combo"]["notes"] = text;
    FlushContainer(fileNum);
}

// ComboShip (issue #1): erasing a slot from either game's file-select wipes BOTH saves — each game
// fires its Set*-registered callback with the slot, the launcher routes it to the other game's
// save-only delete export (never re-entering a menu erase path). See docs/deviations/boot-shutdown.md.
typedef void (*FnSetDeleteForeignSave)(void (*)(int));
typedef void (*FnDeleteSaveFile)(int);
static FnSetDeleteForeignSave SOH_SetDeleteForeignSave = nullptr;
static FnSetDeleteForeignSave MM_SetDeleteForeignSave = nullptr;
static FnDeleteSaveFile SOH_DeleteSaveFile = nullptr;
static FnDeleteSaveFile MM_DeleteSaveFile = nullptr;

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

// ComboShip: placement injection exports
typedef void (*FnSetGenerateCb)(void (*)(int));
typedef void (*FnApplyPlacements)(const char*);
// Returns 0 on success; nonzero means the placement apply failed and the slot has no MM placements.
typedef int (*FnMMInitRandoSave)(int, const char*, const unsigned char*);
typedef void (*FnSetComboRandoSeed)(uint64_t);
typedef void (*FnSetComboSeedHash)(uint32_t);
static FnSetGenerateCb SOH_SetOnComboGenerateCallback = nullptr;
static FnApplyPlacements SOH_ApplyRandoPlacements = nullptr;
static FnMMInitRandoSave MM_InitRandoSaveFile = nullptr;
static FnSetComboRandoSeed SOH_SetComboRandoSeed = nullptr;
static FnSetComboRandoSeed MM_SetComboRandoSeed = nullptr;
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
static std::filesystem::path g_FinalizeSpoilerPath;
static uint32_t g_FinalizeDisplaySeed = 0;
static uint32_t g_FinalizeMasterSeed = 0; // #169: the gen-roll latch keys off the master seed, not the display hash
// Consolidated spoiler JSON for the just-generated seed. The worker writes the pending file from it;
// Combo_OnOOTSaveInit bakes it into the slot's container and pushes it into both DLLs at Start.
static std::string g_ConsolidatedJson;

// ---------- ComboShip-owned Anchor connection (Phase 1) ----------
// The persistent socket + receive thread live HERE (launcher) so the connection survives OOT<->MM
// transitions. soh's Anchor keeps its packet/handler/menu logic but redirects transport through the
// callbacks registered below and receives inbound via SOH_Anchor_RecvJson. See docs/deviations/anchor.md.
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
// Bug 2: launcher-orchestrated resync, dormant-safe (see ComboAnchor::RequestFullResync below).
static FnVoidArgless SOH_Anchor_RequestResync = nullptr;

// MM Anchor adapter exports (Phase 2). MM piggybacks on the same launcher-owned connection; it is
// activated/deactivated on transitions and receives inbound packets when it is the active game.
static FnSetAnchorSend MM_SetAnchorSend = nullptr;
static FnAnchorRecv MM_Anchor_RecvJson = nullptr;
static FnVoidArgless MM_Anchor_Activate = nullptr;
static FnVoidArgless MM_Anchor_Deactivate = nullptr;
static FnVoidArgless MM_Anchor_RequestResync = nullptr;

// A6: live dormant-game co-op sync. The launcher feeds every inbound packet to BOTH games; the active
// game calls the registered pump each frame so the dormant sibling applies save-affecting packets on
// the game thread (never the receive thread — that would race the active game's save writes).
typedef void (*FnSetPumpDormant)(void (*)());
static FnSetPumpDormant SOH_SetPumpDormant = nullptr;
static FnSetPumpDormant MM_SetPumpDormant = nullptr;
static FnVoidArgless SOH_Anchor_PumpDormant = nullptr;
static FnVoidArgless MM_Anchor_PumpDormant = nullptr;

// Cross-game item delivery seam (issue #3). Each game's foreign-check detection (and the Anchor
// receive path) routes an item to the OTHER game through one launcher-owned dispatcher, which calls
// the target DLL's save-only grant export. The same dispatcher serves the single-player and
// networked paths. targetGame/srcGame use the GameId convention 0 = OOT, 1 = MM (== sActiveGame).
typedef void (*FnSetCrossRoute)(void (*)(int, const char*));
// Deliver callback carries srcCheckName too (bug 3: keys the launcher-side receive dedup below).
typedef void (*FnSetCrossDeliver)(void (*)(int, const char*, const char*));
typedef void (*FnGrantCrossItem)(const char*);
static FnSetCrossDeliver SOH_SetCrossDeliver = nullptr;
static FnSetCrossDeliver MM_SetCrossDeliver = nullptr;
static FnGrantCrossItem SOH_GrantCrossItem = nullptr;
static FnGrantCrossItem MM_GrantCrossItem = nullptr;
static FnSetCrossRoute SOH_SetMarkForeignObtained = nullptr;
static FnSetCrossRoute MM_SetMarkForeignObtained = nullptr;
static FnGrantCrossItem SOH_MarkForeignObtained = nullptr;
static FnGrantCrossItem MM_MarkForeignObtained = nullptr;

// ComboShip: gate the ending on BOTH final bosses. Each game calls the registered callback when its
// final boss dies (OOT Ganon / MM Majora): it records the kill in the per-slot completion sidecar and
// returns 1 iff both are now dead. The game then plays its native ending (finale) or warps the player
// back to the cross-game portal to finish the other game. See docs/UPSTREAM_MERGES.md.
typedef void (*FnSetBossDefeatedCb)(int (*)(int, int));
static FnSetBossDefeatedCb SOH_SetFinalBossDefeatedCb = nullptr;
static FnSetBossDefeatedCb MM_SetFinalBossDefeatedCb = nullptr;
static bool g_comboCompletion[2] = { false, false };
static int g_comboCompletionSlot = -1;

// ComboShip (#136): Triforce Hunt is ONE combined goal — the launcher pushes it into both DLLs, sums
// both counters on every piece grant/merge, and dispatches the ending itself.
typedef void (*FnSetComboGoal)(int hunt, int required, int pieces);
typedef int (*FnReadComboGoalCVars)(int* required, int* total);
typedef int (*FnGetTriforceCount)(void);
typedef void (*FnTriggerTriforceCredits)(int dormant);
typedef void (*FnSetTriforceProgressCb)(void (*)(int, int));
typedef void (*FnSetOtherTriforceCountCb)(int (*)(void));
static FnSetComboGoal SOH_SetComboGoal = nullptr;
static FnSetComboGoal MM_SetComboGoal = nullptr;
static FnReadComboGoalCVars SOH_ReadComboGoalCVars = nullptr;
static FnGetTriforceCount SOH_GetTriforcePieceCount = nullptr;
static FnGetTriforceCount MM_GetTriforcePieceCount = nullptr;
static FnTriggerTriforceCredits SOH_TriggerTriforceCredits = nullptr;
static FnTriggerTriforceCredits MM_TriggerTriforceCredits = nullptr;
static FnSetTriforceProgressCb SOH_SetTriforceProgressCb = nullptr;
static FnSetTriforceProgressCb MM_SetTriforceProgressCb = nullptr;
static FnSetOtherTriforceCountCb SOH_SetOtherTriforceCountCb = nullptr;
static FnSetOtherTriforceCountCb MM_SetOtherTriforceCountCb = nullptr;
// Active goal for the loaded slot (0 required = the both-bosses goal) + the one-shot completion latch.
static bool g_goalHunt = false;
static int g_goalRequired = 0;
static int g_goalTotal = -1; // combined pieces the seed places; -1 = seed predates the combo-owned total
static bool g_comboTriforceDone = false;

// ComboShip (#135): starting game. The menu CVar may say Random; the launcher resolves it per seed and
// pushes the concrete value, which soh's FinalizeSettings turns into forced age/forest/exclusions.
typedef void (*FnSetComboStartingGame)(int mmStart);
typedef int (*FnReadComboStartingGameCVar)(void);
static FnSetComboStartingGame SOH_SetComboStartingGame = nullptr;
static FnReadComboStartingGameCVar SOH_ReadComboStartingGameCVar = nullptr;
// Starting game of the LOADED slot (seed-bound, like g_goalHunt).
static bool g_startingGameMM = false;

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
// Finding 4: on-connect resync must run on the game thread (RequestResyncDormantSafe touches
// gPlayState/isDormantApply, which PumpDormant also mutates there). Set here, drained by PumpDormant.
static std::atomic<bool> sResyncPending{ false };

// Combo-owned Anchor roster/presence. A game's Anchor only drains packets while it's the foreground
// game, so its per-game roster goes stale when dormant. The launcher sees every packet on this
// never-dormant thread, so it keeps ONE always-live roster the room window reads (display-only; games
// keep their own roster for puppets/save-apply).
struct ClientInfo {
    uint32_t clientId = 0;
    std::string name = "???";
    uint8_t r = 255, g = 255, b = 255;
    std::string teamId = "default";
    bool online = false;
    uint32_t seed = 0;
    std::string clientVersion;
    bool isSaveLoaded = false;
    bool isGameComplete = false;
    int16_t rawScene = 0;
    int game = 0; // 0 = OOT, 1 = MM
};
struct RoomState {
    uint32_t ownerClientId = 0;
    int pvpMode = 1;
    int showLocationsMode = 1;
    int teleportMode = 1;
    int syncItemsAndFlags = 1;
};
static std::mutex sRosterMutex;
static std::map<uint32_t, ClientInfo> sRoster;
static RoomState sRoomState;
static uint32_t sOwnClientId = 0;

// Fill a ClientInfo from a client JSON object (schema mirrors soh PrepClientState / JsonConversions).
// MM namespaces its sceneNum (>= 1000) and tags packets originGame=="mm"; either flags the MM side.
static void ParseClient(const nlohmann::json& c, const std::string& originGame, ClientInfo& info) {
    info.clientId = c.value("clientId", (uint32_t)0);
    info.name = c.value("name", std::string("???"));
    if (c.contains("color") && c["color"].is_object()) {
        info.r = c["color"].value("r", 255);
        info.g = c["color"].value("g", 255);
        info.b = c["color"].value("b", 255);
    }
    info.clientVersion = c.value("clientVersion", std::string());
    info.teamId = c.value("teamId", std::string("default"));
    info.online = c.value("online", false);
    info.seed = c.value("seed", (uint32_t)0);
    info.isSaveLoaded = c.value("isSaveLoaded", false);
    info.isGameComplete = c.value("isGameComplete", false);
    int32_t sceneNum = c.value("sceneNum", 0);
    bool mm = originGame == "mm" || sceneNum >= 1000;
    info.game = mm ? 1 : 0;
    info.rawScene = (int16_t)(mm ? sceneNum - 1000 : sceneNum);
}

// Parse the presence/room packets into the roster (called on the receive thread, before forwarding).
static void UpdateRosterFromPacket(const std::string& packet) {
    try {
        auto pj = nlohmann::json::parse(packet);
        std::string type = pj.value("type", std::string());
        std::string origin = pj.value("originGame", std::string());
        if (type == "ALL_CLIENT_STATE") {
            std::lock_guard<std::mutex> lk(sRosterMutex);
            sRoster.clear();
            for (auto& c : pj.value("state", nlohmann::json::array())) {
                ClientInfo info;
                ParseClient(c, "", info); // array entries carry no originGame; sceneNum tags MM
                if (c.value("self", false))
                    sOwnClientId = info.clientId;
                sRoster[info.clientId] = info;
            }
        } else if (type == "UPDATE_CLIENT_STATE") {
            uint32_t cid = pj.value("clientId", (uint32_t)0);
            if (cid != 0 && pj.contains("state")) {
                std::lock_guard<std::mutex> lk(sRosterMutex);
                ClientInfo info;
                ParseClient(pj["state"], origin, info);
                info.clientId = cid;
                sRoster[cid] = info;
            }
        } else if (type == "UPDATE_ROOM_STATE" && pj.contains("state")) {
            auto s = pj["state"];
            std::lock_guard<std::mutex> lk(sRosterMutex);
            sRoomState.ownerClientId = s.value("ownerClientId", (uint32_t)0);
            sRoomState.pvpMode = s.value("pvpMode", 1);
            sRoomState.showLocationsMode = s.value("showLocationsMode", 1);
            sRoomState.teleportMode = s.value("teleportMode", 1);
            sRoomState.syncItemsAndFlags = s.value("syncItemsAndFlags", 1);
        }
        // UPDATE_TEAM_STATE (save blob) + PLAYER_UPDATE (high-rate puppet coords) are display-irrelevant.
    } catch (...) {}
}

// Roster snapshot for comboui's room window: { ownClientId, room{...}, clients[...] }. areaVisible +
// isOwner + self are resolved here (the launcher owns room-state); comboui resolves scene NAMES and
// version/seed mismatch itself. ownTeam comes from the self entry's teamId (== gRemote.Anchor.TeamId).
static const char* Combo_Anchor_GetRoster() {
    static std::string cached;
    try {
        std::lock_guard<std::mutex> lk(sRosterMutex);
        std::string ownTeam = "default";
        auto selfIt = sRoster.find(sOwnClientId);
        if (selfIt != sRoster.end())
            ownTeam = selfIt->second.teamId;
        int showLoc = sRoomState.showLocationsMode;

        nlohmann::json clients = nlohmann::json::array();
        for (auto& [cid, c] : sRoster) {
            bool isOwnTeam = c.teamId == ownTeam;
            bool areaVisible = c.isSaveLoaded && (showLoc == 2 || (showLoc == 1 && isOwnTeam));
            nlohmann::json e;
            e["clientId"] = cid;
            e["name"] = c.name;
            e["color"] = { { "r", c.r }, { "g", c.g }, { "b", c.b } };
            e["teamId"] = c.teamId;
            e["online"] = c.online;
            e["self"] = (cid == sOwnClientId);
            e["game"] = c.game == 1 ? "mm" : "oot";
            e["rawScene"] = c.rawScene;
            e["isOwner"] = (cid == sRoomState.ownerClientId);
            e["isSaveLoaded"] = c.isSaveLoaded;
            e["isGameComplete"] = c.isGameComplete;
            e["areaVisible"] = areaVisible;
            e["clientVersion"] = c.clientVersion;
            e["seed"] = c.seed;
            clients.push_back(e);
        }
        nlohmann::json out;
        out["ownClientId"] = sOwnClientId;
        out["room"] = { { "ownerClientId", sRoomState.ownerClientId },
                        { "pvpMode", sRoomState.pvpMode },
                        { "showLocationsMode", showLoc },
                        { "teleportMode", sRoomState.teleportMode },
                        { "syncItemsAndFlags", sRoomState.syncItemsAndFlags } };
        out["clients"] = clients;
        cached = out.dump();
    } catch (...) { cached = "{}"; }
    return cached.c_str();
}

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
        // Bug 2: full both-games resync on every (re)connect. Both requests go out unconditionally
        // (dormant-safe), so a late-joiner/reconnect pulls the peer's OOT AND MM progress, and this
        // client's own dormant sibling gets asked too. Deferred to the game thread (finding 4).
        sResyncPending.store(true);

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
                // Keep the always-live combo roster in sync before forwarding (fixes dormant staleness).
                UpdateRosterFromPacket(packet);
                // A6: feed every packet to BOTH games. Each RecvJson only enqueues (thread-safe). The
                // active game drains+handles it on its tick; the dormant game applies its save-affecting
                // subset via PumpDormant (driven by the active game's per-frame pump call), so a
                // teammate's progression registers in the dormant game's save live.
                if (SOH_Anchor_RecvJson)
                    SOH_Anchor_RecvJson(packet.c_str());
                if (MM_Anchor_RecvJson)
                    MM_Anchor_RecvJson(packet.c_str());
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
    // Our own scene/room-state is broadcast OUTBOUND (never echoed inbound), so parse it here too or the
    // self roster row would freeze at its join value.
    UpdateRosterFromPacket(json);
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

// Cross-game delivery dispatcher (issue #3). Registered into BOTH DLLs; invoked by the collector
// game's foreign-check detection (local) and the active game's Anchor receive handler (network).
// Grants into the TARGET game's resident save via its save-only export (target is usually the
// dormant game, so its save isn't mutating underneath us). See docs/deviations/rando.md.
static std::set<std::string> sAppliedCrossChecks; // dedup: the same wire packet can reach both DLLs
static uint32_t sCrossItemDedupSeed = 0;          // scoped per-seed (ResetCrossItemDedupForSeed)
// Reset runs on the generation worker; deliver runs on the game thread — both mutate the set.
static std::mutex sAppliedCrossChecksMutex;

// Clears the dedup set whenever the active seed changes (regen/new-file), so a check name reused
// across seeds isn't silently dropped as a stale "already delivered" duplicate.
static void ResetCrossItemDedupForSeed(uint32_t seed) {
    std::lock_guard<std::mutex> lock(sAppliedCrossChecksMutex);
    if (seed != sCrossItemDedupSeed) {
        sAppliedCrossChecks.clear();
        sCrossItemDedupSeed = seed;
    }
}

// ComboShip (#136): defined below; the cross-grant re-evaluates the combined goal (see DeliverCrossItem).
static void Combo_OnTriforceProgress(int game, int fileNum);

static void DeliverCrossItem(int targetGame, const char* itemName, const char* srcCheckName) {
    if (srcCheckName && srcCheckName[0] != '\0') {
        std::lock_guard<std::mutex> lock(sAppliedCrossChecksMutex);
        if (!sAppliedCrossChecks.insert(srcCheckName).second) {
            return; // already delivered for this check
        }
    }
    if (targetGame == 1) {
        if (MM_GrantCrossItem)
            MM_GrantCrossItem(itemName);
    } else {
        if (SOH_GrantCrossItem)
            SOH_GrantCrossItem(itemName);
    }
    // ComboShip (#136): the grant's own poke carries the TARGET game's fileNum, which is unbound (0xFF)
    // whenever that game is dormant, so it gets dropped. Re-poke here — the single choke point every
    // cross-grant (local collection in either game, Anchor receive, resync backfill) passes through —
    // with the launcher's loaded slot. Latched in Combo_OnTriforceProgress, so extra pokes are free.
    if (itemName && ComboRando::CwIsTriforcePiece((ComboRando::GameId)targetGame, itemName)) {
        Combo_OnTriforceProgress(targetGame, g_comboCompletionSlot);
    }
}

// A6: invoked each frame BY the active game (via the registered pump seam). Applies queued
// save-affecting co-op packets to the DORMANT game on the caller's (game) thread, so a teammate's
// collection registers in the dormant game's save live instead of only on next entry.
static void PumpDormant() {
    // Finding 4: drain the on-connect resync here (game thread), once per connect.
    if (ComboAnchor::sResyncPending.exchange(false)) {
        if (SOH_Anchor_RequestResync)
            SOH_Anchor_RequestResync();
        if (MM_Anchor_RequestResync)
            MM_Anchor_RequestResync();
    }
    if (ComboAnchor::sActiveGame.load() == 1) {
        if (SOH_Anchor_PumpDormant)
            SOH_Anchor_PumpDormant(); // MM foreground -> apply to dormant OOT
    } else {
        if (MM_Anchor_PumpDormant)
            MM_Anchor_PumpDormant(); // OOT foreground -> apply to dormant MM
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

// Per-slot completion lives in the merged container's combo.completion object. Works in non-rando
// play too. Read on save-load, rewritten on each final-boss kill.
static void LoadComboCompletion(int slot) {
    g_comboCompletion[0] = g_comboCompletion[1] = false;
    g_comboCompletionSlot = slot;
    g_comboTriforceDone = false;
    g_goalHunt = false;
    g_goalRequired = 0;
    g_goalTotal = -1;
    g_startingGameMM = false;
    {
        std::lock_guard<std::mutex> lk(g_containerMutex);
        auto& c = LoadOrCreateContainer(slot);
        auto combo = c.value("combo", nlohmann::json::object());
        auto comp = combo.value("completion", nlohmann::json::object());
        g_comboCompletion[0] = comp.value("oot", false);
        g_comboCompletion[1] = comp.value("mm", false);
        g_comboTriforceDone = comp.value("triforce", false);
        // The goal is seed-bound: it rides the slot's baked combo.rando, not the live menu CVars.
        auto rando = combo.value("rando", nlohmann::json::object());
        auto goal = rando.value("goal", nlohmann::json::object());
        g_goalHunt = goal.value("type", std::string("bosses")) == "triforceHunt";
        g_goalRequired = g_goalHunt ? goal.value("requiredPieces", 0) : 0;
        g_goalTotal = goal.value("totalPieces", -1); // absent on seeds made before the combined total
        // Same for the starting game (#135) — old seeds have no field and started in OOT.
        g_startingGameMM = rando.value("startingGame", std::string("OOT")) == "MM";
    }
    // Push outside the container lock — the DLL setters must never re-enter the sidecar.
    if (SOH_SetComboGoal)
        SOH_SetComboGoal(g_goalHunt ? 1 : 0, g_goalRequired, ComboRando::CwOotPieces(g_goalTotal));
    if (MM_SetComboGoal)
        MM_SetComboGoal(g_goalHunt ? 1 : 0, g_goalRequired, ComboRando::CwMmPieces(g_goalTotal));
    if (SOH_SetComboStartingGame)
        SOH_SetComboStartingGame(g_startingGameMM ? 1 : 0);
    if (ComboUI_SetComboComplete)
        ComboUI_SetComboComplete((g_comboCompletion[0] && g_comboCompletion[1]) ? 1 : 0);
}

// Generation pushes the MENU goal into both DLLs. If a slot is loaded, put its own (seed-bound) goal
// back afterwards so the DLL-side globals keep describing the loaded seed.
static void RestoreLoadedSlotGoal() {
    if (g_comboCompletionSlot < 0)
        return;
    if (SOH_SetComboGoal)
        SOH_SetComboGoal(g_goalHunt ? 1 : 0, g_goalRequired, ComboRando::CwOotPieces(g_goalTotal));
    if (MM_SetComboGoal)
        MM_SetComboGoal(g_goalHunt ? 1 : 0, g_goalRequired, ComboRando::CwMmPieces(g_goalTotal));
    if (SOH_SetComboStartingGame)
        SOH_SetComboStartingGame(g_startingGameMM ? 1 : 0);
}

static void SaveComboCompletion(int slot) {
    {
        std::lock_guard<std::mutex> lk(g_containerMutex);
        auto& c = LoadOrCreateContainer(slot);
        c["combo"]["completion"]["oot"] = g_comboCompletion[0];
        c["combo"]["completion"]["mm"] = g_comboCompletion[1];
        c["combo"]["completion"]["triforce"] = g_comboTriforceDone;
        FlushContainer(slot);
    }
    // #173: tints the timer overlay's total green. Pushed outside the container lock — comboui must
    // never re-enter the sidecar.
    if (ComboUI_SetComboComplete)
        ComboUI_SetComboComplete((g_comboCompletion[0] && g_comboCompletion[1]) ? 1 : 0);
}

// ComboShip (#164): push the slot's hints slice + read state into comboui's Hint Tracker. Reads the
// container under the mutex, then calls out with it released (the DLL setters must never re-enter it).
static void Combo_PushHintTrackerData(int slot) {
    if (!ComboUI_SetHintTrackerData)
        return;
    if (!ComboIsValidSlot(slot)) {
        ComboUI_SetHintTrackerData(-1, "", "");
        return;
    }
    std::string hints, read;
    {
        std::lock_guard<std::mutex> lk(g_containerMutex);
        auto& c = LoadOrCreateContainer(slot);
        const auto combo = c.value("combo", nlohmann::json::object());
        hints = combo.value("rando", nlohmann::json::object()).value("hints", nlohmann::json::object()).dump();
        read = combo.value("hintsRead", nlohmann::json::object()).dump();
    }
    ComboUI_SetHintTrackerData(slot, hints.c_str(), read.c_str());
}

// Set-semantics insert into the container's combo.hintsRead[bucket]. Caller holds g_containerMutex.
// matchField (object values only) compares just that member, so a varying sibling — an MM trap check's
// re-rolled disguise text — can't add a duplicate entry per talk. First write wins.
static bool ComboHintsReadInsert(nlohmann::json& c, const char* bucket, const nlohmann::json& value,
                                 const char* matchField) {
    nlohmann::json& arr = c["combo"]["hintsRead"][bucket];
    if (!arr.is_array())
        arr = nlohmann::json::array();
    for (auto& e : arr) {
        if (matchField ? e.value(matchField, std::string()) == value.value(matchField, std::string()) : e == value)
            return false;
    }
    arr.push_back(value);
    return true;
}

// Persist one reveal and re-push the read state. Runs on the reporting game's thread, so the container
// write is mutex-guarded and the comboui push happens after the lock is released.
static void Combo_RecordHintRead(int fileNum, const char* bucket, const nlohmann::json& value,
                                 const char* matchField = nullptr) {
    bool inserted = false;
    {
        std::lock_guard<std::mutex> lk(g_containerMutex);
        auto& c = LoadOrCreateContainer(fileNum);
        inserted = ComboHintsReadInsert(c, bucket, value, matchField);
        if (inserted)
            FlushContainer(fileNum);
    }
    if (inserted)
        Combo_PushHintTrackerData(fileNum);
}

// OOT reported a revealed hint (keyed by the combo checkName it was applied from). OnRandoHintRevealed
// can fire repeatedly per textbox — the set-semantics insert makes that free.
static void Combo_OnOotHintRevealed(int fileNum, const char* comboKey) try {
    if (!ComboIsValidSlot(fileNum) || !comboKey || comboKey[0] == '\0')
        return;
    Combo_RecordHintRead(fileNum, "oot", std::string(comboKey));
} catch (const std::exception& e) {
    std::cerr << "[ComboShip] Combo_OnOotHintRevealed threw: " << e.what() << std::endl;
} catch (...) { std::cerr << "[ComboShip] Combo_OnOotHintRevealed threw a non-std exception" << std::endl; }

// MM reported a revealed hint. kind: 0 = cross gossipPool pick (poolIndex), 1 = native MM stone hint
// (no upfront list, so the tracker shows these as a revealed-only group), 2 = NPC itemLocations hint.
static void Combo_OnMmHintRevealed(int fileNum, int kind, int poolIndex, const char* key, const char* text) try {
    if (!ComboIsValidSlot(fileNum))
        return;
    switch (kind) {
        case 0:
            if (poolIndex >= 0)
                Combo_RecordHintRead(fileNum, "mmPool", poolIndex);
            return;
        case 1:
            if (key && key[0] != '\0')
                Combo_RecordHintRead(fileNum, "mmNative",
                                     nlohmann::json{ { "check", key }, { "text", text ? text : "" } }, "check");
            return;
        case 2:
            if (key && key[0] != '\0')
                Combo_RecordHintRead(fileNum, "mmNpc", std::string(key));
            return;
        default:
            return;
    }
} catch (const std::exception& e) {
    std::cerr << "[ComboShip] Combo_OnMmHintRevealed threw: " << e.what() << std::endl;
} catch (...) { std::cerr << "[ComboShip] Combo_OnMmHintRevealed threw a non-std exception" << std::endl; }

// Registered into both games: record THIS game's final-boss kill for its slot and return 1 iff BOTH
// games' bosses are now dead. game/fileNum use the GameId convention (0=OOT, 1=MM).
static int Combo_OnFinalBossDefeated(int game, int fileNum) {
    if ((game != 0 && game != 1) || !ComboIsValidSlot(fileNum))
        return 0;
    if (fileNum != g_comboCompletionSlot)
        LoadComboCompletion(fileNum);
    // The OOT death cutscene re-enters this every frame during the fade; persist + log only on the
    // first report for this slot so we don't thrash the sidecar. Repeats just return the cached answer.
    if (!g_comboCompletion[game]) {
        g_comboCompletion[game] = true;
        SaveComboCompletion(fileNum);
        std::cout << "[ComboShip] Final boss defeated: game=" << game << " slot=" << fileNum
                  << " both=" << (g_comboCompletion[0] && g_comboCompletion[1]) << std::endl;
    }
    return (g_comboCompletion[0] && g_comboCompletion[1]) ? 1 : 0;
}

// ComboShip (#136): each game's piece-count getter, handed to the OTHER game so its pickup messages
// and hints can show combined progress.
static int Combo_GetOotTriforceCount() {
    return SOH_GetTriforcePieceCount ? SOH_GetTriforcePieceCount() : 0;
}
static int Combo_GetMmTriforceCount() {
    return MM_GetTriforcePieceCount ? MM_GetTriforcePieceCount() : 0;
}

// Poked after every Triforce Piece grant (own or dormant) and every Anchor team-state merge: sums both
// games' counters and, on the first crossing, latches completion and rolls the ending. game/fileNum use
// the GameId convention (0 = OOT, 1 = MM). No exception may cross the C-ABI boundary.
static void Combo_OnTriforceProgress(int game, int fileNum) try {
    if ((game != 0 && game != 1) || !ComboIsValidSlot(fileNum))
        return; // Anchor pokes carry fileNum 0xFF at the file-select — no slot, nothing to evaluate
    if (fileNum != g_comboCompletionSlot)
        LoadComboCompletion(fileNum);
    if (!g_goalHunt || g_goalRequired <= 0 || g_comboTriforceDone)
        return;
    const int total = Combo_GetOotTriforceCount() + Combo_GetMmTriforceCount();
    if (total < g_goalRequired)
        return;
    g_comboTriforceDone = true;
    g_comboCompletion[0] = g_comboCompletion[1] = true;
    SaveComboCompletion(fileNum);
    const bool mmActive = ComboAnchor::sActiveGame.load() == 1;
    std::cout << "[ComboShip] Triforce Hunt complete: " << total << "/" << g_goalRequired << " slot=" << fileNum
              << " active=" << (mmActive ? "mm" : "oot") << std::endl;
    if (SOH_TriggerTriforceCredits)
        SOH_TriggerTriforceCredits(mmActive ? 1 : 0);
    if (MM_TriggerTriforceCredits)
        MM_TriggerTriforceCredits(mmActive ? 0 : 1);
} catch (const std::exception& e) {
    std::cerr << "[ComboShip] Combo_OnTriforceProgress threw: " << e.what() << std::endl;
} catch (...) { std::cerr << "[ComboShip] Combo_OnTriforceProgress threw a non-std exception" << std::endl; }

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
// ComboShip (#135): resolve the starting-game CVar (0=OOT, 1=MM, 2=Random 50-50 off the master seed).
// ComboRandoHeadless.cpp duplicates this; the derivation string must stay byte-identical.
static bool ResolveStartingGameMM(int cfg, uint32_t masterSeed) {
    if (cfg == 2)
        return (ComboHash(("startingGame:" + std::to_string(masterSeed)).c_str()) & 1u) != 0;
    return cfg == 1;
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

// ComboShip: write a seed's spoiler under its own hash-icon name. Returns the path (empty on failure).
// Worker-safe: pointing the CVar at it is a separate main-thread step (RememberComboSpoiler).
static std::filesystem::path WriteComboSpoiler(const nlohmann::json& fileHash, const std::string& json) {
    std::error_code ec;
    std::filesystem::create_directories(ComboRando::ConsolidatedDir(), ec);
    auto path = ComboRando::ComboSpoilerPath(fileHash);
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "[ComboShip] could not write spoiler " << path.string() << std::endl;
        return {};
    }
    out << json;
    return path;
}

// ComboShip: mark a spoiler as the one a restart reloads. MAIN THREAD ONLY — this writes a CVar and
// saves the config, and libultraship's ConsoleVariable map is unlocked (same race as the apply below).
static void RememberComboSpoiler(const std::filesystem::path& path) {
    if (!SOH_SetComboSpoilerPath || path.empty())
        return;
    // Absolute: WriteComboSpoiler returns a CWD-relative path, which stops resolving if the game is
    // ever launched from another directory (shortcut, launcher, debugger).
    std::error_code ec;
    auto abs = std::filesystem::absolute(path, ec);
    SOH_SetComboSpoilerPath((ec ? path : abs).string().c_str());
}

// ComboShip: a silent auto-reload must not let the pending seed's settings overwrite the user's
// gRando.* CVars on disk. MM reads gRando.* only at slot-bind, so its restore is deferred there
// (not inline in Combo_OnReloadRequest like OOT's). See docs/deviations/rando.md.
static std::string g_PendingMMSettingsJson;     // seed's MM settings, applied right before slot-bind
static std::string g_UserMMSettingsSnapshot;    // user's MM settings, restored right after slot-bind
static bool g_ComboReloadRestoreUserMM = false; // false for an explicit drop (seed settings stick)

// Forward decl: defined later, called from RunComboFill on every successful in-game generation.
// playthroughOut (optional) receives the structured sphere playthrough for the consolidated file.
static void WriteComboPlaythrough(const std::string& spoilerJson, const ComboRando::OracleFns& ootOracle,
                                  const ComboRando::OracleFns& mmOracle, const std::string& seedLabel,
                                  nlohmann::json* playthroughOut = nullptr, const std::string& sohDump = "",
                                  const std::string& mmDump = "", ComboRando::CwGoal goal = {}, bool mmStart = false);

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
        RestoreLoadedSlotGoal(); // a bailed generation must not leave the menu goal in the DLLs
        g_GenerateBusy.store(false);
    };

    if (!SOH_DumpRandoStaticData || !MM_DumpRandoStaticData) {
        fail("dump functions not resolved");
        return;
    }

    if (inputSeed.empty())
        inputSeed = std::to_string(ComboRandRange(0, 1000000));
    const uint32_t baseSeed = ComboHash(inputSeed.c_str());
    uint32_t masterSeed = baseSeed;

    // ComboShip (#136): the combo-owned goal. Read via soh.dll — the launcher has no CVar access.
    ComboRando::CwGoal goal;
    if (SOH_ReadComboGoalCVars) {
        int required = 0, total = -1;
        goal.hunt = SOH_ReadComboGoalCVars(&required, &total) != 0;
        goal.required = goal.hunt ? required : 0;
        goal.total = total; // kept even for bosses, so each game's own piece slider is forced to 0
    }
    if (goal.hunt && goal.required < 1) {
        fail("Triforce Hunt needs at least 1 required piece");
        return;
    }
    // ComboShip (#135): 0 = OOT, 1 = MM, 2 = Random (resolved per attempt below).
    const int startCfg = SOH_ReadComboStartingGameCVar ? SOH_ReadComboStartingGameCVar() : 0;
    bool pinStartOot = false; // Random: after an MM-start attempt fails, fall back silently to OOT
    bool resolvedMmStart = false;

    std::string sohDump, mmDump, spoiler, lastFillError, sohHintDump;
    bool usedCombinedFill = false;
    nlohmann::json playthroughJson = nlohmann::json::array(); // structured sphere playthrough (combined-fill only)
    ComboRando::RequirednessResult pareDownResult;            // cross-hint Phase 3 WotH/foolish classification
    // ComboShip: checkName -> OOT area, computed once from sohHintDump right after the winning attempt's
    // dump (below) for the pare-down call.
    std::unordered_map<std::string, std::string> ootCheckAreasCache;

    // ComboShip: checkName -> area/region string, from each game's own dump, for the pare-down
    // (foolish-area rollup).
    auto buildOotCheckAreas = [](const std::string& hintDumpJson) {
        std::unordered_map<std::string, std::string> out;
        try {
            auto hd = nlohmann::json::parse(hintDumpJson.empty() ? "{}" : hintDumpJson);
            for (auto& c : hd.value("checks", nlohmann::json::array())) {
                std::string name = c.value("name", ""), area = c.value("area", "");
                if (!name.empty() && !area.empty())
                    out.emplace(std::move(name), std::move(area));
            }
        } catch (...) {}
        return out;
    };
    auto buildMmCheckAreas = [](const std::string& dumpJson) {
        std::unordered_map<std::string, std::string> out;
        try {
            auto d = nlohmann::json::parse(dumpJson.empty() ? "{}" : dumpJson);
            const auto locHints = d.value("locationHints", nlohmann::json::object());
            for (auto& [chk, region] : locHints.items())
                out.emplace(chk, region.get<std::string>());
        } catch (...) {}
        return out;
    };

    const bool haveOracles = Combo_SOH_Rando_Reset && Combo_SOH_Rando_SetOwnedItems &&
                             Combo_SOH_Rando_GetReachableChecks && Combo_SOH_Rando_PlaceItem && Combo_MM_Rando_Reset &&
                             Combo_MM_Rando_SetOwnedItems && Combo_MM_Rando_GetReachableChecks &&
                             Combo_MM_Rando_PlaceItem && Combo_MM_Rando_Restore;
    // Stale soh.dll: refuse rather than fall back to an ungated fill, which would silently reintroduce
    // portal-unreachable (softlockable) seeds. See docs/deviations/rando.md.
    if (haveOracles && !Combo_SOH_Rando_GetPortalOpen) {
        fail("stale soh.dll: Combo_SOH_Rando_GetPortalOpen missing, portal gate unavailable");
        return;
    }

    // Whole-fill retries (GAP-4): each attempt re-derives the master seed, so dumps, confined placement,
    // and prices re-roll deterministically per attempt. Budget lives in CrossWorldRando.h.
    const int kFillAttempts = ComboRando::kFillAttempts;
    for (int attempt = 0; attempt < kFillAttempts && !usedCombinedFill; ++attempt) {
        // Space each retry's seed far apart (golden-ratio step) so attempts don't correlate.
        masterSeed = baseSeed + attempt * 0x9E3779B9u;
        ResetCrossItemDedupForSeed(masterSeed);
        // Random's LAST attempt always resolves OOT, so Random can never hard-fail on an MM-start attempt.
        const bool mmStart = !pinStartOot && !(startCfg == 2 && attempt + 1 == kFillAttempts) &&
                             ResolveStartingGameMM(startCfg, masterSeed);

        // ComboShip: seed OOT's rando RNG BEFORE the dump so its shop/scrub/merchant setup (which runs
        // both inside the dump and again at SOH_ApplyRandoPlacements) makes identical choices each time.
        if (SOH_SetComboRandoSeed)
            SOH_SetComboRandoSeed(masterSeed);
        if (MM_SetComboRandoSeed)
            MM_SetComboRandoSeed(masterSeed);
        // Must precede the dumps: OOT's FinalizeSettings and MM's GeneratePools shape their pools
        // (wincon, Majora soul removal, piece count) from the goal.
        if (SOH_SetComboGoal)
            SOH_SetComboGoal(goal.hunt ? 1 : 0, goal.required, ComboRando::CwOotPieces(goal.total));
        if (MM_SetComboGoal)
            MM_SetComboGoal(goal.hunt ? 1 : 0, goal.required, ComboRando::CwMmPieces(goal.total));
        // Same reason (#135): an MM start forces OOT's age/forest/exclusions, which shape its pool.
        if (SOH_SetComboStartingGame)
            SOH_SetComboStartingGame(mmStart ? 1 : 0);

        sohDump = SOH_DumpRandoStaticData();
        mmDump = MM_DumpRandoStaticData();
        if (sohDump.empty() || mmDump.empty()) {
            fail("empty static-data dump");
            return;
        }
        resolvedMmStart = mmStart; // these dumps used it, so whatever spoiler they end up in must record it
        if (goal.hunt) {
            const int pieces = ComboRando::CountPoolTriforcePieces(sohDump, mmDump);
            if (pieces < goal.required) {
                fail((std::string("Triforce Hunt needs ") + std::to_string(goal.required) + " pieces but only " +
                      std::to_string(pieces) + " are in the combined pool — raise the Combo menu's pool total")
                         .c_str());
                return;
            }
        }
        // ComboShip: OOT forced placements (Link's Pocket) the static dump can't carry. The fill
        // reserves these out of the cross pool and commits them so the check isn't left unplaced.
        // Read BEFORE the entrance shuffle: it reads the live placement ComboFillConfined just made,
        // and the shuffle's ItemReset would wipe it.
        std::string forcedOot;
        if (SOH_GetForcedPlacements)
            forcedOot = SOH_GetForcedPlacements(masterSeed);

        // ComboShip (#90): OOT entrance shuffle. Runs after the dump (settings finalized) and before the
        // fill, so logic validates the shuffled world. No-op when the entrance options are off.
        if (SOH_ShuffleEntrancesForCombo && !SOH_ShuffleEntrancesForCombo(masterSeed)) {
            // A different masterSeed yields a different layout, so reroll rather than sending the user
            // to the settings; the post-loop check reports it if every attempt fails. Mirrors the loop
            // tail's MM restore, which this skips.
            lastFillError = "OOT entrance shuffle found no valid layout — relax the entrance settings";
            std::cout << "[ComboShip] RunComboFill: attempt " << (attempt + 1) << "/" << kFillAttempts
                      << " failed: " << lastFillError << "\n";
            if (mmStart && startCfg == 2)
                pinStartOot = true;
            if (Combo_MM_Rando_Restore)
                Combo_MM_Rando_Restore();
            continue;
        }
        if (!haveOracles)
            break; // no oracles -> no-logic fallback below; the dumps are still needed

        ComboRando::OracleFns ootOracle = { Combo_SOH_Rando_Reset, Combo_SOH_Rando_SetOwnedItems,
                                            Combo_SOH_Rando_GetReachableChecks, Combo_SOH_Rando_PlaceItem,
                                            Combo_SOH_Rando_GetPortalOpen };
        ComboRando::OracleFns mmOracle = { Combo_MM_Rando_Reset, Combo_MM_Rando_SetOwnedItems,
                                           Combo_MM_Rando_GetReachableChecks, Combo_MM_Rando_PlaceItem };

        // ComboShip: honor OOT's logic/ALR settings per-game (MM stays all-reachable). The fill gates MM
        // on the portal region via ootOracle.GetPortalOpen; NO_LOGIC bypasses it.
        ComboRando::OotAccess ootAccess = ComboRando::OotAccessFromDump(sohDump);
        auto result =
            ComboRando::CrossWorldCombinedFill(sohDump, mmDump, masterSeed, ootOracle, mmOracle, progress, forcedOot,
                                               ootAccess, goal, mmStart ? ComboRando::GAME_MM : ComboRando::GAME_OOT);

        if (result.success) {
            spoiler = result.spoilerJson;
            usedCombinedFill = true;
            std::cout << "[ComboShip] RunComboFill: combined-logic fill succeeded (seed=" << masterSeed << ", attempt "
                      << (attempt + 1) << ")\n";
            // ComboShip: cross-hint schema dump (Phase 2) — must run on THIS attempt's still-live OOT
            // Context, before anything re-runs FinalizeSettings (which would re-roll RNG-derived state
            // like trial selection) or the reload-path force-off touches the hint options.
            if (SOH_DumpRandoHintData) {
                sohHintDump = SOH_DumpRandoHintData();
            }
            ootCheckAreasCache = buildOotCheckAreas(sohHintDump); // parsed once, reused below and after the loop
            // ComboShip: requiredness pare-down (Phase 3) — needs the STILL-LIVE oracle session, so it
            // runs before WriteComboPlaythrough (which restores MM internally). Doesn't restore itself;
            // the WriteComboPlaythrough call below (or the loop's own restore) does that once. Skipped
            // entirely when no enabled hint surface consumes requiredness (empty result = all non-required).
            const bool noLogic = ootAccess == ComboRando::OotAccess::NO_LOGIC;
            if (goal.hunt && noLogic) {
                // Any piece substitutes for any other and OOT may be unbeatable by design, so nothing
                // is meaningfully "required" — same compromise as MmOnlyMajoraGoal.
                std::cout << "[ComboShip] RunComboFill: pare-down skipped (No Logic + Triforce Hunt)\n";
            } else if (ComboRando::NeedsRequirednessPareDown(sohHintDump, mmDump)) {
                // NO_LOGIC: gate requiredness on MM only (OOT may be unbeatable by design).
                pareDownResult =
                    ComboRando::PareDownPlaythrough(result.spoilerJson, ootOracle, mmOracle, nullptr, sohDump, mmDump,
                                                    ootCheckAreasCache, buildMmCheckAreas(mmDump),
                                                    goal.hunt ? ComboRando::MakeTriforceHuntGoal(goal.required)
                                                    : noLogic ? ComboRando::MmOnlyMajoraGoal
                                                              : ComboRando::DefaultGanonMajoraGoal,
                                                    !noLogic, mmStart, progress);
            } else {
                std::cout << "[ComboShip] RunComboFill: pare-down skipped (no enabled hint surface needs "
                             "requiredness)\n";
            }
            // ComboShip: write the sphere-by-sphere playthrough log. Replays reachability via the
            // oracles BEFORE SOH_ApplyRandoPlacements restores the live OOT context, so it can't
            // corrupt the generated seed. Restores MM itself.
            WriteComboPlaythrough(result.spoilerJson, ootOracle, mmOracle, inputSeed, &playthroughJson, sohDump, mmDump,
                                  goal, mmStart);
        } else {
            lastFillError = result.error;
            std::cout << "[ComboShip] RunComboFill: attempt " << (attempt + 1) << "/" << kFillAttempts
                      << " failed: " << lastFillError << "\n";
            if (mmStart && startCfg == 2)
                pinStartOot = true;
        }
        Combo_MM_Rando_Restore();
    }

    if (haveOracles && !usedCombinedFill) {
        std::string msg =
            std::string("combined fill failed after ") + std::to_string(kFillAttempts) + " attempts — " + lastFillError;
        // #135: explicit MM start hard-fails (Random would have fallen back to OOT by now), and the
        // usual cause is an OOT that an itemless strayed player cannot walk back out of.
        if (startCfg == 1)
            msg += " | Starting Game is Majora's Mask, which also requires the OOT->MM portal to stay "
                   "re-openable from an empty OOT start — loosen the OOT access settings (Closed Forest, "
                   "Door of Time, Lock Overworld Doors) or set Starting Game back to Ocarina of Time";
        fail(msg.c_str());
        return;
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
        // Also carries each item's advancement flag (name -> is-progression) so the collecting game
        // knows whether a foreign item should play the held-up pickup animation.
        auto buildNameMap = [](const std::string& dump, std::unordered_map<std::string, bool>& advOut) {
            std::unordered_map<std::string, std::string> m;
            try {
                auto d = nlohmann::json::parse(dump);
                for (auto& it : d.value("items", nlohmann::json::array())) {
                    std::string n = it.value("name", "");
                    std::string dn = it.value("displayName", "");
                    if (n.empty())
                        continue;
                    advOut[n] = it.value("advancement", false);
                    if (!dn.empty())
                        m.emplace(std::move(n), std::move(dn));
                }
            } catch (...) {}
            return m;
        };
        std::unordered_map<std::string, bool> ootAdv, mmAdv;
        auto ootNames = buildNameMap(sohDump, ootAdv);
        auto mmNames = buildNameMap(mmDump, mmAdv);

        // ComboShip: OOT's curated ice-trap disguise set — carried into the apply payload (below) and
        // the consolidated spoiler so a reload restores it instead of deriving one from placements.
        nlohmann::json ootIceTrapModels = nlohmann::json::array();
        try {
            ootIceTrapModels = nlohmann::json::parse(sohDump).value("iceTrapModels", nlohmann::json::array());
        } catch (...) {}
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
            const auto& adv = (itemGame == "mm") ? mmAdv : ootAdv;
            auto ait = adv.find(itemName);
            if (ait != adv.end()) {
                fm["advancement"] = ait->second;
            }
        }

        // ComboShip: disguise cross-placed traps as a plausible progression item of their own game.
        ComboRando::AssignTrapDisguises(foreignArr, j.value("oot", nlohmann::json::object()),
                                        j.value("mm", nlohmann::json::object()), sohDump, mmDump, masterSeed);

        // The apply payloads (fed to each game's placement injection) hold the SENTINEL for foreign
        // checks — the check's own game places the sentinel and diverts the real item cross-game. The
        // consolidated spoiler placements (below) instead show the real item name for readability (#1).
        // Only OOT's is built here: OOT applies right after generation, whereas MM applies at slot-bind
        // and re-derives its payload from the consolidated seed (ApplyPayloadFromConsolidated).
        nlohmann::json ootApply = j.value("oot", nlohmann::json::object());
        nlohmann::json ootSpoiler = ootApply; // copy real-name placements before sentinel overwrite
        nlohmann::json mmSpoiler = j.value("mm", nlohmann::json::object());
        for (const auto& fm : foreignArr) {
            std::string cg = fm.value("checkGame", "");
            std::string cn = fm.value("checkName", "");
            if (cn.empty())
                continue;
            std::string dn = fm.value("displayName", fm.value("itemName", ""));
            if (cg == "oot") {
                ootApply[cn] = ComboRando::kForeignSentinelNameOOT;
                if (!dn.empty())
                    ootSpoiler[cn] = dn;
            } else if (cg == "mm" && !dn.empty()) {
                mmSpoiler[cn] = dn;
            }
        }
        // Reserved apply-only key (ootSpoiler was copied above, so it stays a pure placement map).
        ootApply["__iceTrapModels"] = ootIceTrapModels;

        // ComboShip: the gSaveContext-mutating apply (SOH_ApplyRandoPlacements) and the seed-hash set
        // MUST run on the main thread — the worker only computes. Stash their inputs for
        // Combo_FinalizeGenerate, which the main-thread file-select poll runs once it sees done.
        // The OOT seed-hash folds in input-seed + both settings dumps so the icons identify seed and
        // settings (same seed+settings -> matching icons across players).
        uint32_t displaySeed = ComboHash((inputSeed + sohDump + mmDump).c_str());
        g_FinalizeOotApply = ootApply.dump();
        g_FinalizeDisplaySeed = displaySeed;
        g_FinalizeMasterSeed = masterSeed;

        // ComboShip: file_hash = the 5 icon indexes the file-select shows, derived from displaySeed
        // exactly as OOT's GenerateHash (decimal padded to 10, five 2-digit pairs).
        std::string seedDigits = std::to_string(displaySeed);
        while (seedDigits.size() < 10)
            seedDigits = "0" + seedDigits;
        nlohmann::json fileHashArr = nlohmann::json::array();
        for (int i = 0; i < 5; ++i)
            fileHashArr.push_back(std::stoi(seedDigits.substr(i * 2, 2)));

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
        // ComboShip: suffix cross-game item-name collisions (e.g. "Mirror Shield") in the human-readable
        // placements so the consolidated file / plandomizer read unambiguously; each game strips its own
        // "(OOT)"/"(MM)" on apply. Foreign checks are skipped (carried by foreign[]).
        ComboRando::SuffixCrossGameItems(ootSpoiler, mmSpoiler, foreignArr, sohDump, mmDump);

        nlohmann::json consolidated;
        consolidated["fileType"] = "ComboShipRandomizer";
        consolidated["version"] = 1;
        consolidated["seed"] = inputSeed;
        consolidated["masterSeed"] = masterSeed;
        consolidated["displaySeed"] = displaySeed;
        consolidated["file_hash"] = fileHashArr;
        // Rolled shop/scrub/merchant prices (from the dumps) travel in the spoiler so the validator
        // and the reload path never guess them — unknown price is never treated as buyable.
        auto pricesOf = [](const std::string& dump) -> nlohmann::json {
            try {
                return nlohmann::json::parse(dump).value("prices", nlohmann::json::object());
            } catch (...) { return nlohmann::json::object(); }
        };
        consolidated["oot"] = { { "settings", parseOrEmpty(SOH_DumpRandoSettings) },
                                { "enabledTricks", parseOrEmpty(SOH_DumpEnabledTricks) },
                                { "placements", ootSpoiler },
                                { "prices", pricesOf(sohDump) },
                                { "iceTrapModels", ootIceTrapModels } };
        consolidated["mm"] = { { "settings", parseOrEmpty(MM_DumpRandoSettings) },
                               { "placements", mmSpoiler },
                               { "prices", pricesOf(mmDump) } };
        auto foreignEnriched = ComboRando::BuildForeignArray(foreignArr);
        consolidated["foreign"] = foreignEnriched;
        consolidated["playthrough"] = ComboRando::PlaythroughLines(playthroughJson);
        // ComboShip (#136): the goal is seed-bound — the runtime latch reads it back from the slot's
        // baked combo.rando, never from the live menu CVars.
        consolidated["goal"] = { { "type", goal.hunt ? "triforceHunt" : "bosses" },
                                 { "requiredPieces", goal.required },
                                 { "totalPieces", goal.total } };
        // ComboShip (#135): the resolved starting game — seed-bound like the goal, never re-rolled.
        consolidated["startingGame"] = resolvedMmStart ? "MM" : "OOT";
        // ComboShip (#90): OOT entrance layout — informational, reload re-derives it from masterSeed.
        {
            nlohmann::json ootEnt = nlohmann::json::array();
            if (SOH_DumpEntranceOverrides) {
                try {
                    ootEnt = nlohmann::json::parse(SOH_DumpEntranceOverrides());
                } catch (...) {}
            }
            consolidated["entrances"] = { { "oot", std::move(ootEnt) } };
        }
        // ComboShip: cross-game hint generation (Phase 3) — real per-seed hint assignments, from the
        // pare-down computed above. usedCombinedFill guards the no-logic fallback path (no oracles/
        // pare-down data there): that path ships with an empty hints payload, same as before Phase 3.
        consolidated["hints"] = usedCombinedFill ? ComboRando::Generate(masterSeed, sohDump, sohHintDump, mmDump,
                                                                        foreignEnriched, spoiler, pareDownResult)
                                                 : nlohmann::json{ { "version", 1 } };
        g_ConsolidatedJson = consolidated.dump(2);

        // This seed's own spoiler, so earlier seeds survive instead of being overwritten. The CVar that
        // makes it the remembered one is set by Combo_FinalizeGenerate (main thread).
        g_FinalizeSpoilerPath = WriteComboSpoiler(consolidated["file_hash"], g_ConsolidatedJson);
        std::cout << "[ComboShip] RunComboFill: placements computed; spoiler written to "
                  << g_FinalizeSpoilerPath.string() << "\n";

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
    RestoreLoadedSlotGoal();
    g_GenerateBusy.store(false);
}

// ComboShip: headless cross-world generation TEST (COMBO_GENTEST=<count>). Runs the combined fill
// over a seed range; a seed "succeeds" only if every advancement check in both games is reachable
// from an empty start (honoring the OOT->MM portal gate) — i.e. provably 100%-completable. Uses the
// same oracles as the real generator under the current CVars. Returns the FAILED seed count.
static int RunComboGenTest(int numSeeds, uint32_t seedBase) {
    if (!(Combo_SOH_Rando_Reset && Combo_SOH_Rando_SetOwnedItems && Combo_SOH_Rando_GetReachableChecks &&
          Combo_SOH_Rando_PlaceItem && Combo_SOH_Rando_GetPortalOpen && Combo_MM_Rando_Reset &&
          Combo_MM_Rando_SetOwnedItems && Combo_MM_Rando_GetReachableChecks && Combo_MM_Rando_PlaceItem &&
          Combo_MM_Rando_Restore)) {
        std::cerr << "[GENTEST] oracle exports unavailable — cannot run\n";
        return -1;
    }
    if (!SOH_DumpRandoStaticData || !MM_DumpRandoStaticData) {
        std::cerr << "[GENTEST] dump functions not resolved — cannot run\n";
        return -1;
    }
    ComboRando::OracleFns ootOracle = { Combo_SOH_Rando_Reset, Combo_SOH_Rando_SetOwnedItems,
                                        Combo_SOH_Rando_GetReachableChecks, Combo_SOH_Rando_PlaceItem,
                                        Combo_SOH_Rando_GetPortalOpen };
    ComboRando::OracleFns mmOracle = { Combo_MM_Rando_Reset, Combo_MM_Rando_SetOwnedItems,
                                       Combo_MM_Rando_GetReachableChecks, Combo_MM_Rando_PlaceItem };

    std::cout << "[GENTEST] running " << numSeeds << " cross-world generations (seedBase=" << seedBase
              << ") — asserting every advancement item is reachable from an empty start in both games\n";
    const int startCfg = SOH_ReadComboStartingGameCVar ? SOH_ReadComboStartingGameCVar() : 0;
    int failures = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < numSeeds; ++i) {
        const uint32_t baseSeed = seedBase + static_cast<uint32_t>(i);
        ComboRando::CombinedFillResult result{};
        bool pinStartOot = false; // #135: Random falls back to OOT after a failed MM-start attempt
        // Mirror RunComboFill's whole-fill retries: a seed rejected on attempt 0 is a PASS in-game
        // after one reroll, so counting it FAIL here would inflate the failure rate.
        for (int attempt = 0; attempt < ComboRando::kFillAttempts && !result.success; ++attempt) {
            const uint32_t seed = baseSeed + attempt * 0x9E3779B9u;
            const bool mmStart = !pinStartOot && !(startCfg == 2 && attempt + 1 == ComboRando::kFillAttempts) &&
                                 ResolveStartingGameMM(startCfg, seed);
            // Seeds before the dump (shop/scrub choices are seed-derived and made inside it); forced
            // placements before the shuffle, whose ItemReset wipes the placement they read.
            if (SOH_SetComboRandoSeed)
                SOH_SetComboRandoSeed(seed);
            if (MM_SetComboRandoSeed)
                MM_SetComboRandoSeed(seed);
            if (SOH_SetComboStartingGame)
                SOH_SetComboStartingGame(mmStart ? 1 : 0);
            std::string sohDump = SOH_DumpRandoStaticData();
            std::string mmDump = MM_DumpRandoStaticData();
            if (sohDump.empty() || mmDump.empty()) {
                std::cerr << "[GENTEST] empty dump — cannot run\n";
                return -1;
            }
            std::string forcedOot;
            if (SOH_GetForcedPlacements)
                forcedOot = SOH_GetForcedPlacements(seed);
            // Per-seed OOT entrance layout, exactly like the real generator (no-op when the options are off).
            if (SOH_ShuffleEntrancesForCombo && !SOH_ShuffleEntrancesForCombo(seed)) {
                result.error = "OOT entrance shuffle found no valid layout";
                if (mmStart && startCfg == 2)
                    pinStartOot = true;
                Combo_MM_Rando_Restore();
                continue; // a different masterSeed yields a different layout — reroll, like RunComboFill
            }
            result = ComboRando::CrossWorldCombinedFill(sohDump, mmDump, seed, ootOracle, mmOracle, nullptr, forcedOot,
                                                        ComboRando::OotAccessFromDump(sohDump), {},
                                                        mmStart ? ComboRando::GAME_MM : ComboRando::GAME_OOT);
            if (!result.success && mmStart && startCfg == 2)
                pinStartOot = true;
            Combo_MM_Rando_Restore(); // reset the MM oracle's snapshot guard for the next fill
        }
        if (result.success) {
            std::cout << "[GENTEST]   seed " << baseSeed << " PASS\n";
        } else {
            std::cerr << "[GENTEST]   seed " << baseSeed << " FAIL: " << result.error << "\n";
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

// ComboShip: headless cross-world PLAYTHROUGH log (COMBO_PLAYTHROUGH=<seed>). Replays an
// already-generated spoiler sphere by sphere, listing each item in obtainable order across both
// games (OOT->MM portal honored) until BEATABLE (Ganon goal + Majora's Lair both reachable). Full
// log to saves/combo/slot0.playthrough.txt. Restores the MM oracle snapshot at the end (drives MM
// here). Called from the env-gated entry and from RunComboFill. See docs/deviations/rando.md.
static void WriteComboPlaythrough(const std::string& spoilerJson, const ComboRando::OracleFns& ootOracle,
                                  const ComboRando::OracleFns& mmOracle, const std::string& seedLabel,
                                  nlohmann::json* playthroughOut, const std::string& sohDump, const std::string& mmDump,
                                  ComboRando::CwGoal goal, bool mmStart) {
    // Thin wrapper over the shared traversal (combo/rando/ComboPlaythrough.h); passes this build's
    // MM oracle-restore pointer. A playthroughOut here means the spoiler's playthrough section, which
    // lists progression only; the text-log path and the headless validator keep every step.
    ComboRando::RunPlaythrough(spoilerJson, ootOracle, mmOracle, seedLabel, Combo_MM_Rando_Restore, playthroughOut,
                               sohDump, mmDump,
                               ComboRando::OotAccessFromDump(sohDump) != ComboRando::OotAccess::NO_LOGIC,
                               /*progressionOnly*/ playthroughOut != nullptr, goal, mmStart);
}

// Env-gated entry: COMBO_PLAYTHROUGH=<seed> generates that seed headless, then writes its log.
static void RunComboPlaythrough(const std::string& inputSeed) {
    if (!(Combo_SOH_Rando_Reset && Combo_SOH_Rando_SetOwnedItems && Combo_SOH_Rando_GetReachableChecks &&
          Combo_SOH_Rando_PlaceItem && Combo_SOH_Rando_GetPortalOpen && Combo_MM_Rando_Reset &&
          Combo_MM_Rando_SetOwnedItems && Combo_MM_Rando_GetReachableChecks && Combo_MM_Rando_PlaceItem &&
          Combo_MM_Rando_Restore)) {
        std::cerr << "[PLAYTHROUGH] oracle exports unavailable\n";
        return;
    }
    if (!SOH_DumpRandoStaticData || !MM_DumpRandoStaticData) {
        std::cerr << "[PLAYTHROUGH] dump functions not resolved\n";
        return;
    }
    ComboRando::OracleFns ootOracle = { Combo_SOH_Rando_Reset, Combo_SOH_Rando_SetOwnedItems,
                                        Combo_SOH_Rando_GetReachableChecks, Combo_SOH_Rando_PlaceItem,
                                        Combo_SOH_Rando_GetPortalOpen };
    ComboRando::OracleFns mmOracle = { Combo_MM_Rando_Reset, Combo_MM_Rando_SetOwnedItems,
                                       Combo_MM_Rando_GetReachableChecks, Combo_MM_Rando_PlaceItem };
    std::string seedStr = inputSeed.empty() ? "1" : inputSeed;
    const uint32_t baseSeed = ComboHash(seedStr.c_str());
    std::string sohDump, mmDump;
    ComboRando::CombinedFillResult fill{};
    const int startCfg = SOH_ReadComboStartingGameCVar ? SOH_ReadComboStartingGameCVar() : 0;
    bool pinStartOot = false, resolvedMmStart = false; // #135, same fallback as RunComboFill
    // Mirror RunComboFill including its retries — the player's seed may have come from attempt 1, and
    // validating only attempt 0 would either report "did not generate" or log a world they never got.
    for (int attempt = 0; attempt < ComboRando::kFillAttempts && !fill.success; ++attempt) {
        const uint32_t masterSeed = baseSeed + attempt * 0x9E3779B9u;
        const bool mmStart = !pinStartOot && !(startCfg == 2 && attempt + 1 == ComboRando::kFillAttempts) &&
                             ResolveStartingGameMM(startCfg, masterSeed);
        if (SOH_SetComboRandoSeed)
            SOH_SetComboRandoSeed(masterSeed);
        if (MM_SetComboRandoSeed)
            MM_SetComboRandoSeed(masterSeed);
        if (SOH_SetComboStartingGame)
            SOH_SetComboStartingGame(mmStart ? 1 : 0);
        sohDump = SOH_DumpRandoStaticData();
        mmDump = MM_DumpRandoStaticData();
        if (sohDump.empty() || mmDump.empty()) {
            std::cerr << "[PLAYTHROUGH] empty static-data dump\n";
            return;
        }
        // Read before the entrance shuffle: its ItemReset wipes the placement this reads.
        std::string forcedOot;
        if (SOH_GetForcedPlacements)
            forcedOot = SOH_GetForcedPlacements(masterSeed);
        if (SOH_ShuffleEntrancesForCombo && !SOH_ShuffleEntrancesForCombo(masterSeed)) {
            fill.error = "OOT entrance shuffle found no valid layout";
            if (mmStart && startCfg == 2)
                pinStartOot = true;
            Combo_MM_Rando_Restore();
            continue;
        }
        fill = ComboRando::CrossWorldCombinedFill(sohDump, mmDump, masterSeed, ootOracle, mmOracle, nullptr, forcedOot,
                                                  ComboRando::OotAccessFromDump(sohDump), {},
                                                  mmStart ? ComboRando::GAME_MM : ComboRando::GAME_OOT);
        if (!fill.success) {
            if (mmStart && startCfg == 2)
                pinStartOot = true;
            Combo_MM_Rando_Restore();
        } else {
            resolvedMmStart = mmStart;
        }
    }
    if (!fill.success) {
        std::cerr << "[PLAYTHROUGH] seed '" << seedStr << "' did not generate: " << fill.error << "\n";
        return;
    }
    // restores MM at the end
    WriteComboPlaythrough(fill.spoilerJson, ootOracle, mmOracle, seedStr, nullptr, sohDump, mmDump, {},
                          resolvedMmStart);
}

// ComboShip: synchronous generate — used only by the headless COMBO_AUTOGEN_SEED path. The UI
// registers Combo_OnGenerateThreaded instead. Reentrancy-guarded via g_GenerateBusy.
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

// ComboShip: cross-hint Phase 3 — "hints" only contains "oot" for a seed CrossHints::Generate actually
// ran on; older/no-logic-fallback seeds keep the Phase 2 {"version":1} scaffold and fall back to the
// pre-Phase-3 force-off behavior (back-compat).
// ComboShip: slices the "hints" sub-object out of the consolidated spoiler once (parse-once — this
// used to be two separate re-parses of the whole consolidated blob just to check/extract one field).
static nlohmann::json ComboHintsJsonFrom(const std::string& consolidatedJson) {
    try {
        return nlohmann::json::parse(consolidatedJson).value("hints", nlohmann::json::object());
    } catch (...) { return nlohmann::json::object(); }
}

// ComboShip: main-thread finalize — the gSaveContext-mutating apply + seed-hash set. Runs from
// Combo_PollFinalize on the main thread once the worker has stashed its result. NEVER call from the
// worker thread (that race crashed the prior threaded attempt).
static void Combo_FinalizeGenerate() {
    nlohmann::json hints = ComboHintsJsonFrom(g_ConsolidatedJson);
    bool hintsPresent = hints.contains("oot");
    if (SOH_SetComboHintsPresent)
        SOH_SetComboHintsPresent(hintsPresent ? 1 : 0);
    if (SOH_ApplyRandoPlacements) {
        SOH_ApplyRandoPlacements(g_FinalizeOotApply.c_str());
        std::cout << "[ComboShip] Combo_FinalizeGenerate: OOT placements applied\n";
    } else if (SOH_SetSeedGenerated) {
        SOH_SetSeedGenerated(1);
    }
    if (SOH_SetComboSeedHash)
        SOH_SetComboSeedHash(g_FinalizeDisplaySeed);
    RememberComboSpoiler(g_FinalizeSpoilerPath); // worker wrote the file; the CVar is ours to set
    g_FinalizeSpoilerPath.clear();
    if (hintsPresent && SOH_ApplyComboHints)
        SOH_ApplyComboHints(hints.dump().c_str());
    // #169: a fresh generation always re-rolls (vanilla gen-only semantics), and claims the seed on
    // the way through so later loads of THIS seed leave the user's manual edits alone.
    Combo_FireGenRollHooksOnce(g_FinalizeMasterSeed, /*force=*/true);
    // A fresh generation's live MM CVars already ARE this seed's settings, so slot-bind must fall
    // through to reading them directly — clear any stale reload-restore state left by an unstarted
    // pending seed (else it would apply THAT seed's MM settings over this generation's placements).
    g_PendingMMSettingsJson.clear();
    g_UserMMSettingsSnapshot.clear();
    g_ComboReloadRestoreUserMM = false;
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

// ComboShip: read a candidate consolidated seed file. True only if it opens, parses and is ours.
static bool TryLoadComboSeedFile(const std::filesystem::path& p, nlohmann::json& out) {
    std::ifstream in(p);
    if (!in.is_open())
        return false;
    try {
        nlohmann::json j;
        in >> j;
        if (j.value("fileType", std::string()) != "ComboShipRandomizer") {
            std::cerr << "[ComboShip] reload: " << p.string() << " is not a ComboShip seed file\n";
            return false;
        }
        out = std::move(j);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ComboShip] reload: could not parse " << p.string() << ": " << e.what() << "\n";
        return false;
    }
}

// ComboShip: a stored-relative path (or one from a different CWD) also gets tried next to the exe.
static std::filesystem::path ResolveComboSeedPath(const std::string& file) {
    std::filesystem::path p(file);
    std::error_code ec;
    if (std::filesystem::exists(p, ec))
        return p;
#ifdef _WIN32
    // Wide API: the ANSI variant mangles non-ASCII install paths (e.g. accented user names) to '?'.
    wchar_t exe[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
        const auto dir = std::filesystem::path(exe).parent_path();
        // A relative path re-rooted at the exe; a moved absolute one by name under the seed dir.
        for (const auto& alt : { dir / p.relative_path(), dir / ComboRando::ConsolidatedDir() / p.filename() })
            if (std::filesystem::exists(alt, ec))
                return alt;
    }
#endif
    return p;
}

// ComboShip: newest readable combo seed in the Randomizer dir — recovers an auto-load when the
// remembered path is lost (e.g. a wiped CVar) while the seed files are still there.
static std::filesystem::path FindNewestComboSeed(nlohmann::json& out) {
    std::error_code ec;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> found;
    for (const auto& dir :
         { ComboRando::ConsolidatedDir(), ResolveComboSeedPath(ComboRando::ConsolidatedDir().string()) }) {
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != ".json")
                continue;
            found.emplace_back(std::filesystem::last_write_time(it->path(), ec), it->path());
        }
        if (!found.empty())
            break;
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [t, p] : found)
        if (TryLoadComboSeedFile(p, out))
            return p;
    return {};
}

// ComboShip: reload a consolidated seed file (the remembered pending file when path is null/empty, or
// a dropped file) and make it playable WITHOUT regenerating. Runs synchronously on the MAIN thread
// (called from the file-select), so the gSaveContext-mutating apply is safe. Restores both games'
// settings, runs the pool prep, re-applies the saved OOT placements + seed-hash, stashes the MM
// placements, and keeps the consolidated JSON in memory so "Start Randomizer" writes the per-slot
// file. Returns 1 on success. The hash string is recomputed from displaySeed for the per-slot name.
static int Combo_OnReloadRequest(const char* path) {
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

static void Combo_OnOOTSaveInit(int fileNum) {
    // A new file starts in OOT. Explicit because not every delete path clears the container
    // (DeleteFileOnDeath calls DeleteZeldaFile directly), so a stale MM could otherwise survive here.
    Combo_SetLastGame(fileNum, ComboRando::GAME_OOT);
    // ComboShip (#165/#164): a fresh file starts with an empty personal note (written, never erased —
    // an absent key is the "never migrated" sentinel) and must not inherit the hint read state.
    {
        std::lock_guard<std::mutex> lk(g_containerMutex);
        auto& c = LoadOrCreateContainer(fileNum);
        c["combo"]["notes"] = "";
        if (c["combo"].contains("hintsRead"))
            c["combo"].erase("hintsRead");
        FlushContainer(fileNum);
    }
    // ComboShip: bind the pending consolidated seed to this slot — bake it into the container's
    // combo.rando (self-contained), then push it into both DLLs so foreign data is live immediately.
    nlohmann::json seed;
    if (!g_ConsolidatedJson.empty()) {
        try {
            seed = nlohmann::json::parse(g_ConsolidatedJson);
        } catch (...) {}
        {
            std::lock_guard<std::mutex> lk(g_containerMutex);
            auto& c = LoadOrCreateContainer(fileNum);
            if (!seed.is_null())
                c["combo"]["rando"] = seed;
            // A rebaked slot is a NEW seed: a completed prior one must not instantly finish this hunt
            // (or report both bosses already dead). Hint read state is already cleared above.
            if (c.contains("combo") && c["combo"].is_object())
                c["combo"].erase("completion");
            FlushContainer(fileNum);
        }
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
        std::string rando;
        {
            std::lock_guard<std::mutex> lk(g_containerMutex);
            auto& c = LoadOrCreateContainer(fileNum);
            auto r = c.value("combo", nlohmann::json::object()).value("rando", nlohmann::json());
            if (!r.is_null())
                rando = r.dump();
        }
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
    SOH_GetCurrentPlayerName = (FnGetPlayerName)GetSym(sohModule, "SOH_GetCurrentPlayerName");
    MM_LoadSaveForCombo = (FnMMLoadSave)GetSym(mmModule, "MM_LoadSaveForCombo");
    MM_InvalidateOwlBlobSlot = (FnMMInvalidateOwlBlob)GetSym(mmModule, "MM_InvalidateOwlBlobSlot");
    SOH_ParkForComboMMResume = (FnVoid)GetSym(sohModule, "SOH_ParkForComboMMResume");
    MM_SetComboEntryIsResume = (FnMMInitSave)GetSym(mmModule, "MM_SetComboEntryIsResume");
    SOH_IsOnFileSelect = (FnIsOnFileSelect)GetSym(sohModule, "SOH_IsOnFileSelect");
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
    SOH_DumpRandoStaticData = (FnDumpData)GetSym(sohModule, "SOH_DumpRandoStaticData");
    MM_DumpRandoStaticData = (FnDumpData)GetSym(mmModule, "MM_DumpRandoStaticData");
    SOH_DumpRandoSettings = (FnDumpData)GetSym(sohModule, "SOH_DumpRandoSettings");
    SOH_DumpEnabledTricks = (FnDumpData)GetSym(sohModule, "SOH_DumpEnabledTricks");
    MM_DumpRandoSettings = (FnDumpData)GetSym(mmModule, "MM_DumpRandoSettings");
    SOH_DumpRandoHintData = (FnDumpData)GetSym(sohModule, "SOH_DumpRandoHintData");
    SOH_ApplyComboHints = (FnApplyHints)GetSym(sohModule, "SOH_ApplyComboHints");
    SOH_SetComboHintsPresent = (FnSetHintsPresent)GetSym(sohModule, "SOH_SetComboHintsPresent");
    SOH_FireGenerationCompleteHooks = (FnVoidArgless)GetSym(sohModule, "SOH_FireGenerationCompleteHooks");
    // #164: combo Hint Tracker reveal reporting.
    SOH_SetComboHintRevealCb = (FnSetHintRevealOot)GetSym(sohModule, "SOH_SetComboHintRevealCb");
    MM_SetComboHintRevealCb = (FnSetHintRevealMm)GetSym(mmModule, "MM_SetComboHintRevealCb");
    SOH_PrepRandoContext = (FnVoidV)GetSym(sohModule, "SOH_PrepRandoContext");
    SOH_RestoreRandoSettings = (FnTakeStr)GetSym(sohModule, "SOH_RestoreRandoSettings");
    MM_RestoreRandoSettings = (FnTakeStr)GetSym(mmModule, "MM_RestoreRandoSettings");
    SOH_SetCheckPrices = (FnTakeStr)GetSym(sohModule, "SOH_SetCheckPrices");
    MM_SetCheckPrices = (FnTakeStr)GetSym(mmModule, "MM_SetCheckPrices");
    SOH_SetOnComboReloadCallback = (FnSetReloadCb)GetSym(sohModule, "SOH_SetOnComboReloadCallback");
    SOH_SetComboSpoilerPath = (FnTakeStr)GetSym(sohModule, "SOH_SetComboSpoilerPath");
    SOH_GetComboSpoilerPath = (FnDumpData)GetSym(sohModule, "SOH_GetComboSpoilerPath");
    MM_InitRandoSaveFile = (FnMMInitRandoSave)GetSym(mmModule, "MM_InitRandoSaveFile");
    SOH_SetOnComboGenerateCallback = (FnSetGenerateCb)GetSym(sohModule, "SOH_SetOnComboGenerateCallback");
    SOH_ApplyRandoPlacements = (FnApplyPlacements)GetSym(sohModule, "SOH_ApplyRandoPlacements");
    SOH_GetForcedPlacements = (FnGetForced)GetSym(sohModule, "SOH_GetForcedPlacements");
    SOH_SetComboRandoSeed = (FnSetComboRandoSeed)GetSym(sohModule, "SOH_SetComboRandoSeed");
    MM_SetComboRandoSeed = (FnSetComboRandoSeed)GetSym(mmModule, "MM_SetComboRandoSeed");
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
    SOH_ClassifyRom = (ComboFnValidateRom)GetSym(sohModule, "SOH_ClassifyRom");
    SOH_StartExtraction = (ComboFnStartExtraction)GetSym(sohModule, "SOH_StartExtraction");
    SOH_GetExtractionProgress = (ComboFnGetProgress)GetSym(sohModule, "SOH_GetExtractionProgress");
    MM_ValidateRom = (ComboFnValidateRom)GetSym(mmModule, "MM_ValidateRom");
    MM_ClassifyRom = (ComboFnValidateRom)GetSym(mmModule, "MM_ClassifyRom");
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
    SOH_Anchor_RequestResync = (FnVoidArgless)GetSym(sohModule, "SOH_Anchor_RequestResync");
    MM_Anchor_RequestResync = (FnVoidArgless)GetSym(mmModule, "MM_Anchor_RequestResync");
    SOH_SetPumpDormant = (FnSetPumpDormant)GetSym(sohModule, "SOH_SetPumpDormant");
    MM_SetPumpDormant = (FnSetPumpDormant)GetSym(mmModule, "MM_SetPumpDormant");
    SOH_Anchor_PumpDormant = (FnVoidArgless)GetSym(sohModule, "SOH_Anchor_PumpDormant");
    MM_Anchor_PumpDormant = (FnVoidArgless)GetSym(mmModule, "MM_Anchor_PumpDormant");

    // Cross-game item delivery seam (issue #3)
    SOH_SetCrossDeliver = (FnSetCrossDeliver)GetSym(sohModule, "SOH_SetCrossDeliver");
    MM_SetCrossDeliver = (FnSetCrossDeliver)GetSym(mmModule, "MM_SetCrossDeliver");
    SOH_GrantCrossItem = (FnGrantCrossItem)GetSym(sohModule, "SOH_GrantCrossItem");
    MM_GrantCrossItem = (FnGrantCrossItem)GetSym(mmModule, "MM_GrantCrossItem");
    SOH_SetMarkForeignObtained = (FnSetCrossRoute)GetSym(sohModule, "SOH_SetMarkForeignObtained");
    MM_SetMarkForeignObtained = (FnSetCrossRoute)GetSym(mmModule, "MM_SetMarkForeignObtained");
    SOH_MarkForeignObtained = (FnGrantCrossItem)GetSym(sohModule, "SOH_MarkForeignObtained");
    MM_MarkForeignObtained = (FnGrantCrossItem)GetSym(mmModule, "MM_MarkForeignObtained");
    SOH_SetFinalBossDefeatedCb = (FnSetBossDefeatedCb)GetSym(sohModule, "SOH_SetFinalBossDefeatedCb");
    MM_SetFinalBossDefeatedCb = (FnSetBossDefeatedCb)GetSym(mmModule, "MM_SetFinalBossDefeatedCb");

    // Combined Triforce Hunt goal seam (#136)
    SOH_SetComboGoal = (FnSetComboGoal)GetSym(sohModule, "SOH_SetComboGoal");
    MM_SetComboGoal = (FnSetComboGoal)GetSym(mmModule, "MM_SetComboGoal");
    SOH_ReadComboGoalCVars = (FnReadComboGoalCVars)GetSym(sohModule, "SOH_ReadComboGoalCVars");
    SOH_GetTriforcePieceCount = (FnGetTriforceCount)GetSym(sohModule, "SOH_GetTriforcePieceCount");
    MM_GetTriforcePieceCount = (FnGetTriforceCount)GetSym(mmModule, "MM_GetTriforcePieceCount");
    MM_ComboPausePlaytime = (FnVoidArgless)GetSym(mmModule, "MM_ComboPausePlaytime");
    MM_ComboResumePlaytime = (FnVoidArgless)GetSym(mmModule, "MM_ComboResumePlaytime");
    SOH_TriggerTriforceCredits = (FnTriggerTriforceCredits)GetSym(sohModule, "SOH_TriggerTriforceCredits");
    MM_TriggerTriforceCredits = (FnTriggerTriforceCredits)GetSym(mmModule, "MM_TriggerTriforceCredits");
    SOH_SetTriforceProgressCb = (FnSetTriforceProgressCb)GetSym(sohModule, "SOH_SetTriforceProgressCb");
    MM_SetTriforceProgressCb = (FnSetTriforceProgressCb)GetSym(mmModule, "MM_SetTriforceProgressCb");
    SOH_SetOtherTriforceCountCb = (FnSetOtherTriforceCountCb)GetSym(sohModule, "SOH_SetOtherTriforceCountCb");
    MM_SetOtherTriforceCountCb = (FnSetOtherTriforceCountCb)GetSym(mmModule, "MM_SetOtherTriforceCountCb");

    // Starting game seam (#135) — OOT-side only; MM needs no setter (nothing there reads it).
    SOH_SetComboStartingGame = (FnSetComboStartingGame)GetSym(sohModule, "SOH_SetComboStartingGame");
    SOH_ReadComboStartingGameCVar = (FnReadComboStartingGameCVar)GetSym(sohModule, "SOH_ReadComboStartingGameCVar");

    // Oracle exports
    Combo_SOH_Rando_Reset = (FnOracleVoid)GetSym(sohModule, "Combo_SOH_Rando_Reset");
    Combo_SOH_Rando_SetOwnedItems = (FnOracleSetItems)GetSym(sohModule, "Combo_SOH_Rando_SetOwnedItems");
    Combo_SOH_Rando_GetReachableChecks = (FnOracleGetChecks)GetSym(sohModule, "Combo_SOH_Rando_GetReachableChecks");
    Combo_SOH_Rando_PlaceItem = (FnOraclePlaceItem)GetSym(sohModule, "Combo_SOH_Rando_PlaceItem");
    Combo_SOH_Rando_GetPortalOpen = (FnOracleGetPortalOpen)GetSym(sohModule, "Combo_SOH_Rando_GetPortalOpen");
    Combo_MM_Rando_Reset = (FnOracleVoid)GetSym(mmModule, "Combo_MM_Rando_Reset");
    Combo_MM_Rando_SetOwnedItems = (FnOracleSetItems)GetSym(mmModule, "Combo_MM_Rando_SetOwnedItems");
    Combo_MM_Rando_GetReachableChecks = (FnOracleGetChecks)GetSym(mmModule, "Combo_MM_Rando_GetReachableChecks");
    Combo_MM_Rando_PlaceItem = (FnOraclePlaceItem)GetSym(mmModule, "Combo_MM_Rando_PlaceItem");
    Combo_MM_Rando_Restore = (FnOracleVoid)GetSym(mmModule, "Combo_MM_Rando_Restore");

    // OOT entrance-shuffle wiring (#90)
    SOH_ShuffleEntrancesForCombo = (FnShuffleEntrances)GetSym(sohModule, "SOH_ShuffleEntrancesForCombo");
    SOH_DumpEntranceOverrides = (FnDumpData)GetSym(sohModule, "SOH_DumpEntranceOverrides");

    // Cross-game erase seam (issue #1)
    SOH_SetComboSaveIO = (FnSetComboSaveIO)GetSym(sohModule, "SOH_SetComboSaveIO");
    MM_SetComboSaveIO = (FnSetComboSaveIO)GetSym(mmModule, "MM_SetComboSaveIO");
    SOH_LoadComboRando = (FnTakeStr)GetSym(sohModule, "SOH_LoadComboRando");
    MM_LoadComboRando = (FnTakeStr)GetSym(mmModule, "MM_LoadComboRando");
    SOH_SetCopyContainer = (FnSetCopyContainer)GetSym(sohModule, "SOH_SetCopyContainer");
    SOH_SetOutdatedSaveNotice = (FnSetOutdatedSaveNotice)GetSym(sohModule, "SOH_SetOutdatedSaveNotice");
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
        ComboUI_Register = (FnComboUIRegister)GetSym(comboUIModule, "ComboUI_Register");
        ComboUI_OnForegroundGame = (FnComboUIForeground)GetSym(comboUIModule, "ComboUI_OnForegroundGame");
        ComboUI_RestoreTrackerIntent = (FnComboUIRegister)GetSym(comboUIModule, "ComboUI_RestoreTrackerIntent");
        ComboUI_SetAnchorRosterProvider =
            (FnComboUISetRosterProvider)GetSym(comboUIModule, "ComboUI_SetAnchorRosterProvider");
        ComboUI_SetHintTrackerData = (FnComboUISetHintTrackerData)GetSym(comboUIModule, "ComboUI_SetHintTrackerData");
        ComboUI_SetComboComplete = (FnComboUISetInt)GetSym(comboUIModule, "ComboUI_SetComboComplete");
        if (ComboUI_SetAnchorRosterProvider)
            ComboUI_SetAnchorRosterProvider(&ComboAnchor::Combo_Anchor_GetRoster);
        ComboUI_SetNotesStore = (FnComboUISetNotesStore)GetSym(comboUIModule, "ComboUI_SetNotesStore");
        if (ComboUI_SetNotesStore)
            ComboUI_SetNotesStore(&Combo_GetNotes, &Combo_SetNotes);
        ComboUI_SyncRandomizedCosmetics = (FnVoidArgless)GetSym(comboUIModule, "ComboUI_SyncRandomizedCosmetics");
        ComboUI_CosmeticsSyncGateEnabled = (FnComboUIGate)GetSym(comboUIModule, "ComboUI_CosmeticsSyncGateEnabled");
        ComboUI_ClaimGenRollSeed = (FnClaimGenRollSeed)GetSym(comboUIModule, "ComboUI_ClaimGenRollSeed");
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
