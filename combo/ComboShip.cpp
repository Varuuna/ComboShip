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
#include <string>
#include <exception>
#include <cstdlib>

#include "rando/CrossMailbox.h"
#include "rando/CrossForeign.h"
#include "rando/CrossWorldRando.h"

// Surfaces the real exception behind a silent terminate()/exit(3). With the shared dynamic
// CRT, exceptions thrown in soh.dll/2ship.dll propagate across the DLL boundary to here.
static void ComboTerminateHandler() {
    std::cerr << "[ComboShip] std::terminate";
    if (auto ep = std::current_exception()) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            std::cerr << " — uncaught std::exception: " << e.what();
        } catch (...) {
            std::cerr << " — uncaught non-std exception";
        }
    }
    std::cerr << std::endl;
    std::abort();
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

// ---------- DLL helpers ----------

#ifdef _WIN32
typedef HMODULE DllHandle;
static DllHandle LoadDll(const char* name) { return LoadLibraryA(name); }
static void* GetSym(DllHandle h, const char* sym) { return (void*)GetProcAddress(h, sym); }
static void FreeDll(DllHandle h) { FreeLibrary(h); }
static std::string DllError() { return std::to_string(GetLastError()); }
#else
typedef void* DllHandle;
static DllHandle LoadDll(const char* name) { return dlopen(name, RTLD_NOW | RTLD_GLOBAL); }
static void* GetSym(DllHandle h, const char* sym) { return dlsym(h, sym); }
static void FreeDll(DllHandle h) { dlclose(h); }
static std::string DllError() { return dlerror(); }
#endif

// ---------- Function pointer types ----------

typedef void  (*FnVoid)();
typedef bool  (*FnExtract)(const char*);
typedef void  (*FnRunMain)(int, char**);
typedef int   (*FnInt)();
typedef void  (*FnSetSaveCallback)(void (*)(int));
typedef void  (*FnMMInitSave)(int);
typedef void  (*FnSetSceneSwitchCallback)(void (*)(int));
typedef void  (*FnMMRunGame)(int);
typedef void  (*FnSOHDeinit)();
typedef void  (*FnSOHPrepare)();
typedef void  (*FnMMNotify)();
static FnVoid                    SOH_Init                    = nullptr;
static FnExtract                 SOH_Extract                 = nullptr;
static FnRunMain                 SOH_RunMain                 = nullptr;
static FnVoid                    MM_InitArchives             = nullptr;
static FnExtract                 MM_Extract                  = nullptr;
static FnInt                     MM_ArchiveCount             = nullptr;
static FnSetSaveCallback         SOH_SetOnNewSaveCallback    = nullptr;
static FnMMInitSave              MM_InitSaveFile             = nullptr;
static FnSetSceneSwitchCallback  SOH_SetOnSceneSwitchCallback = nullptr;
static FnMMRunGame               MM_RunGame                  = nullptr;
static FnSOHDeinit               SOH_Deinit                  = nullptr;
static FnSOHPrepare              SOH_PrepareForTransition    = nullptr;
static FnMMNotify                MM_NotifyComboTransition    = nullptr;

typedef void (*FnMMSetReturnCb)(void (*)(void));
static FnMMSetReturnCb MM_SetOnComboReturnCallback = nullptr;
static bool g_pendingOOTReturn = false;

typedef void (*FnVoidArgless)(void);
static FnVoidArgless SOH_ResumeGame      = nullptr;
static FnVoidArgless SOH_NotifyComboReturn = nullptr;

typedef void (*FnMMResume)(int);
static FnMMResume    MM_ResumeGame          = nullptr;
static FnVoidArgless MM_PrepareForTransition = nullptr;

// ComboShip Inc2: headless static-data dump exports
typedef const char* (*FnDumpData)(void);
static FnDumpData SOH_DumpRandoStaticData = nullptr;
static FnDumpData MM_DumpRandoStaticData  = nullptr;

// ComboShip: eager MM boot at startup (replaces the headless MM_InitRandoLogic warm-up).
static FnVoidArgless MM_BootForCombo      = nullptr;
static FnVoidArgless SOH_ResumeForeground = nullptr;

// ComboShip Inc4: per-game reachability oracle exports
typedef void        (*FnOracleVoid)(void);
typedef void        (*FnOracleSetItems)(const char*);
typedef const char* (*FnOracleGetChecks)(void);
typedef void        (*FnOraclePlaceItem)(const char*, const char*);

static FnOracleVoid      Combo_SOH_Rando_Reset             = nullptr;
static FnOracleSetItems  Combo_SOH_Rando_SetOwnedItems     = nullptr;
static FnOracleGetChecks Combo_SOH_Rando_GetReachableChecks = nullptr;
static FnOraclePlaceItem Combo_SOH_Rando_PlaceItem          = nullptr;

static FnOracleVoid      Combo_MM_Rando_Reset              = nullptr;
static FnOracleSetItems  Combo_MM_Rando_SetOwnedItems      = nullptr;
static FnOracleGetChecks Combo_MM_Rando_GetReachableChecks  = nullptr;
static FnOraclePlaceItem Combo_MM_Rando_PlaceItem           = nullptr;
static FnOracleVoid      Combo_MM_Rando_Restore             = nullptr;

// ComboShip Inc2 (Task 3): placement injection exports
typedef void (*FnSetGenerateCb)(void (*)(int));
typedef void (*FnApplyPlacements)(const char*);
typedef void (*FnMMInitRandoSave)(int, const char*);
static FnSetGenerateCb    SOH_SetOnComboGenerateCallback = nullptr;
static FnApplyPlacements  SOH_ApplyRandoPlacements       = nullptr;
static FnMMInitRandoSave  MM_InitRandoSaveFile           = nullptr;

static int g_PendingMMFileNum = -1;

// ComboShip Inc2 (Task 4): the "mm" placement slice from the most recent Combo_OnGenerate run, stashed
// so the (later) Combo_OnOOTSaveInit callback can hand it to MM_InitRandoSaveFile. Generate fires before
// save-init in the same new-save flow, so this is populated by the time the MM save is created.
static std::string g_PendingMMPlacements;

// ComboShip: generate callback — called by Sram_InitSave (via gComboGenerateCallback)
// before save creation. Runs the combined-logic fill (or no-logic fallback) and applies placements.
static void Combo_OnGenerate(int fileNum) {
    if (!SOH_DumpRandoStaticData || !MM_DumpRandoStaticData) {
        std::cerr << "[ComboShip] Combo_OnGenerate: dump functions not resolved, cannot generate\n";
        return;
    }

    std::string sohDump = SOH_DumpRandoStaticData();
    std::string mmDump  = MM_DumpRandoStaticData();

    if (sohDump.empty() || mmDump.empty()) {
        std::cerr << "[ComboShip] Combo_OnGenerate: empty dump, cannot generate\n";
        return;
    }

    std::string spoiler;
    bool usedCombinedFill = false;

    // Use the combined-logic assumed fill when both oracles are available
    if (Combo_SOH_Rando_Reset && Combo_SOH_Rando_SetOwnedItems &&
        Combo_SOH_Rando_GetReachableChecks && Combo_SOH_Rando_PlaceItem &&
        Combo_MM_Rando_Reset && Combo_MM_Rando_SetOwnedItems &&
        Combo_MM_Rando_GetReachableChecks && Combo_MM_Rando_PlaceItem &&
        Combo_MM_Rando_Restore) {

        ComboRando::OracleFns ootOracle = {
            Combo_SOH_Rando_Reset, Combo_SOH_Rando_SetOwnedItems,
            Combo_SOH_Rando_GetReachableChecks, Combo_SOH_Rando_PlaceItem
        };
        ComboRando::OracleFns mmOracle = {
            Combo_MM_Rando_Reset, Combo_MM_Rando_SetOwnedItems,
            Combo_MM_Rando_GetReachableChecks, Combo_MM_Rando_PlaceItem
        };

        auto result = ComboRando::CrossWorldCombinedFill(
            sohDump, mmDump, 12345u, ootOracle, mmOracle);

        if (result.success) {
            spoiler = result.spoilerJson;
            usedCombinedFill = true;
            std::cout << "[ComboShip] Combo_OnGenerate: combined-logic fill succeeded\n";
        } else {
            std::cerr << "[ComboShip] Combo_OnGenerate: combined fill failed ("
                      << result.error << "), falling back to no-logic\n";
        }

        Combo_MM_Rando_Restore();
    }

    if (!usedCombinedFill) {
        spoiler = ComboRando::CrossWorldGenerateSpoiler(sohDump, mmDump, 12345u);
        std::cout << "[ComboShip] Combo_OnGenerate: using no-logic fallback\n";
    }

    // Write per-slot spoiler for debugging.
    {
        std::error_code ec;
        std::filesystem::create_directories("saves/combo", ec);
        std::string path = "saves/combo/slot" + std::to_string(fileNum) + ".spoiler.json";
        std::ofstream f(path, std::ios::trunc);
        f << spoiler;
        std::cout << "[ComboShip] Combo_OnGenerate: spoiler written to " << path << "\n";
    }

    auto j = nlohmann::json::parse(spoiler);

    // Write the cross-world foreign-item marker map, then build the per-game apply payloads with the
    // foreign-check slots overwritten by each game's sentinel item. The spoiler keeps the real foreign
    // item names (human-readable); the save only ever sees the sentinel for a foreign check, so the
    // check's own game places it and the pickup code diverts the real item through the mailbox.
    auto foreignArr = j.value("foreign", nlohmann::json::array());
    ComboRando::WriteForeignFromAnnotations(fileNum, foreignArr);

    nlohmann::json ootApply = j.value("oot", nlohmann::json::object());
    nlohmann::json mmApply  = j.value("mm",  nlohmann::json::object());
    for (const auto& fm : foreignArr) {
        std::string cg = fm.value("checkGame", "");
        std::string cn = fm.value("checkName", "");
        if (cn.empty()) continue;
        if (cg == "oot")     ootApply[cn] = ComboRando::kForeignSentinelNameOOT;
        else if (cg == "mm") mmApply[cn]  = ComboRando::kForeignSentinelNameMM;
    }

    if (SOH_ApplyRandoPlacements) {
        SOH_ApplyRandoPlacements(ootApply.dump().c_str());
        std::cout << "[ComboShip] Combo_OnGenerate: OOT placements applied for slot " << fileNum
                  << " (" << foreignArr.size() << " foreign markers)\n";
    }

    // Stash the MM slice so Combo_OnOOTSaveInit (fires next in the new-save flow) can create a rando
    // MM save. Cleared first so a generator failure can't leave a stale slice for the wrong slot.
    g_PendingMMPlacements.clear();
    g_PendingMMPlacements = mmApply.dump();
    std::cout << "[ComboShip] Combo_OnGenerate: MM placements stashed for slot " << fileNum << "\n";
}

static void Combo_OnOOTSaveInit(int fileNum) {
    if (MM_InitRandoSaveFile && !g_PendingMMPlacements.empty()) {
        std::cout << "[ComboShip] Creating RANDO MM save for OOT slot " << fileNum << std::endl;
        MM_InitRandoSaveFile(fileNum, g_PendingMMPlacements.c_str());
        g_PendingMMPlacements.clear();
    } else if (MM_InitSaveFile) {
        // No placement available (e.g. generation was skipped) — fall back to a vanilla MM save.
        std::cout << "[ComboShip] Creating MM save for OOT slot " << fileNum << std::endl;
        MM_InitSaveFile(fileNum);
    }
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
    return std::filesystem::exists("soh.o2r")  ||
           std::filesystem::exists("oot-mq.o2r") ||
           std::filesystem::exists("oot.o2r");
}

// ROM-derived archive (must be extracted from the player's MM ROM)
static bool MMRomArchiveExists() {
    return std::filesystem::exists("mm.o2r") ||
           std::filesystem::exists("mm.zip") ||
           std::filesystem::exists("mm.otr");
}

// Any MM archive at all (used for general "is MM set up" check)
static bool MMArchivesExist() {
    return MMRomArchiveExists() || std::filesystem::exists("2ship.o2r");
}

// ---------- Entry point ----------

int main(int argc, char** argv) {
    std::cout << "ComboShip Launcher - Starting..." << std::endl;

    std::set_terminate(ComboTerminateHandler);

    std::string workDir = std::filesystem::current_path().string();

    // ComboShip: surface any mailbox left from a previous session (debug aid; harmless if absent).
    {
        // Slot 0 only for now; expand when multi-slot save is wired.
        auto leftover = ComboRando::LoadAll(0);
        if (!leftover.empty()) {
            std::cout << "[ComboShip] mailbox slot0 has " << leftover.size()
                      << " entr" << (leftover.size() == 1 ? "y" : "ies") << " on startup\n";
        }
    }

    // --- 1. Load DLLs ---

#ifdef _WIN32
    const char* sohDll    = "soh.dll";
    const char* twoShipDll = "2ship.dll";
#elif defined(__APPLE__)
    const char* sohDll    = "libsoh.dylib";
    const char* twoShipDll = "lib2ship.dylib";
#else
    const char* sohDll    = "libsoh.so";
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
    SOH_Init         = (FnVoid)        GetSym(sohModule, "SOH_Init");
    SOH_RunMain      = (FnRunMain)     GetSym(sohModule, "SOH_RunMain");
    SOH_Extract      = (FnExtract)     GetSym(sohModule, "SOH_Extract");

    if (!SOH_Init || !SOH_RunMain) {
        std::cerr << "ERROR: soh.dll is missing required ComboShip exports (SOH_Init / SOH_RunMain)." << std::endl;
        std::cerr << "       Rebuild soh.dll from this ComboShip branch." << std::endl;
        FreeDll(mmModule);
        FreeDll(sohModule);
        return 1;
    }

    // Resolve 2ship.dll exports
    MM_InitArchives          = (FnVoid)           GetSym(mmModule,  "MM_InitArchives");
    MM_Extract               = (FnExtract)        GetSym(mmModule,  "MM_Extract");
    MM_ArchiveCount          = (FnInt)            GetSym(mmModule,  "MM_ArchiveCount");
    SOH_SetOnNewSaveCallback     = (FnSetSaveCallback)        GetSym(sohModule, "SOH_SetOnNewSaveCallback");
    MM_InitSaveFile              = (FnMMInitSave)             GetSym(mmModule,  "MM_InitSaveFile");
    SOH_SetOnSceneSwitchCallback = (FnSetSceneSwitchCallback) GetSym(sohModule, "SOH_SetOnSceneSwitchCallback");
    MM_RunGame                   = (FnMMRunGame)              GetSym(mmModule,  "MM_RunGame");
    SOH_Deinit                   = (FnSOHDeinit)              GetSym(sohModule, "SOH_Deinit");
    SOH_PrepareForTransition     = (FnSOHPrepare)             GetSym(sohModule, "SOH_PrepareForTransition");
    MM_NotifyComboTransition     = (FnMMNotify)               GetSym(mmModule,  "MM_NotifyComboTransition");
    MM_SetOnComboReturnCallback  = (FnMMSetReturnCb)          GetSym(mmModule,  "MM_SetOnComboReturnCallback");
    SOH_ResumeGame               = (FnVoidArgless)            GetSym(sohModule, "SOH_ResumeGame");
    SOH_NotifyComboReturn        = (FnVoidArgless)            GetSym(sohModule, "SOH_NotifyComboReturn");
    MM_ResumeGame                = (FnMMResume)               GetSym(mmModule,  "MM_ResumeGame");
    MM_PrepareForTransition      = (FnVoidArgless)            GetSym(mmModule,  "MM_PrepareForTransition");
    SOH_DumpRandoStaticData          = (FnDumpData)           GetSym(sohModule, "SOH_DumpRandoStaticData");
    MM_DumpRandoStaticData           = (FnDumpData)           GetSym(mmModule,  "MM_DumpRandoStaticData");
    MM_InitRandoSaveFile             = (FnMMInitRandoSave)    GetSym(mmModule,  "MM_InitRandoSaveFile");
    SOH_SetOnComboGenerateCallback   = (FnSetGenerateCb)      GetSym(sohModule, "SOH_SetOnComboGenerateCallback");
    SOH_ApplyRandoPlacements         = (FnApplyPlacements)    GetSym(sohModule, "SOH_ApplyRandoPlacements");
    MM_BootForCombo                  = (FnVoidArgless)        GetSym(mmModule,  "MM_BootForCombo");
    SOH_ResumeForeground             = (FnVoidArgless)        GetSym(sohModule, "SOH_ResumeForeground");

    // Inc4: oracle exports
    Combo_SOH_Rando_Reset             = (FnOracleVoid)      GetSym(sohModule, "Combo_SOH_Rando_Reset");
    Combo_SOH_Rando_SetOwnedItems     = (FnOracleSetItems)  GetSym(sohModule, "Combo_SOH_Rando_SetOwnedItems");
    Combo_SOH_Rando_GetReachableChecks = (FnOracleGetChecks) GetSym(sohModule, "Combo_SOH_Rando_GetReachableChecks");
    Combo_SOH_Rando_PlaceItem          = (FnOraclePlaceItem) GetSym(sohModule, "Combo_SOH_Rando_PlaceItem");
    Combo_MM_Rando_Reset              = (FnOracleVoid)      GetSym(mmModule,  "Combo_MM_Rando_Reset");
    Combo_MM_Rando_SetOwnedItems      = (FnOracleSetItems)  GetSym(mmModule,  "Combo_MM_Rando_SetOwnedItems");
    Combo_MM_Rando_GetReachableChecks  = (FnOracleGetChecks) GetSym(mmModule,  "Combo_MM_Rando_GetReachableChecks");
    Combo_MM_Rando_PlaceItem           = (FnOraclePlaceItem) GetSym(mmModule,  "Combo_MM_Rando_PlaceItem");
    Combo_MM_Rando_Restore             = (FnOracleVoid)      GetSym(mmModule,  "Combo_MM_Rando_Restore");

    if (!MM_InitArchives) {
        std::cerr << "ERROR: 2ship.dll is missing required ComboShip exports (MM_InitArchives)." << std::endl;
        std::cerr << "       Rebuild 2ship.dll from this ComboShip branch." << std::endl;
        FreeDll(mmModule);
        FreeDll(sohModule);
        return 1;
    }

    // --- 2. Ensure OOT archives exist ---

    if (!OOTArchivesExist()) {
        if (SOH_Extract) {
            std::cout << "OOT archives not found — launching OOT extractor..." << std::endl;
            if (!SOH_Extract(workDir.c_str())) {
                std::cerr << "ERROR: OOT extraction failed or was cancelled." << std::endl;
                FreeDll(mmModule);
                FreeDll(sohModule);
                return 1;
            }
        } else {
            std::cerr << "ERROR: OOT archives not found and SOH_Extract is unavailable." << std::endl;
            FreeDll(mmModule);
            FreeDll(sohModule);
            return 1;
        }

        if (!OOTArchivesExist()) {
            std::cerr << "ERROR: OOT archives still missing after extraction." << std::endl;
            FreeDll(mmModule);
            FreeDll(sohModule);
            return 1;
        }
    }

    // --- 3. Ensure MM ROM archive exists (extract from player's ROM if missing) ---

    if (!MMRomArchiveExists()) {
        if (MM_Extract) {
            std::cout << "MM ROM archive not found — launching MM extractor..." << std::endl;
            if (!MM_Extract(workDir.c_str())) {
                std::cerr << "ERROR: MM extraction failed or was cancelled." << std::endl;
                FreeDll(mmModule);
                FreeDll(sohModule);
                return 1;
            }
        } else {
            std::cerr << "ERROR: MM ROM archive not found and MM_Extract is unavailable." << std::endl;
            FreeDll(mmModule);
            FreeDll(sohModule);
            return 1;
        }

        if (!MMRomArchiveExists()) {
            std::cerr << "ERROR: MM ROM archive still missing after extraction." << std::endl;
            FreeDll(mmModule);
            FreeDll(sohModule);
            return 1;
        }
    }

    // --- 4. Initialize OOT game ---

    std::cout << "[ComboShip] Initializing Ship of Harkinian (OOT)..." << std::endl;
    try {
        SOH_Init();
    } catch (const std::exception& e) {
        std::cerr << "[ComboShip] SOH_Init threw std::exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "[ComboShip] SOH_Init threw a non-std exception" << std::endl;
        return 1;
    }
    std::cout << "[ComboShip] OOT initialized." << std::endl;

    // ComboShip: eagerly boot MM now (after OOT init) so the cross-world rando oracle runs against a
    // real, fully-initialized MM. This performs an OOT->MM->OOT transition once, with MM's game loop
    // skipped: hand the foreground from OOT to MM (SOH_PrepareForTransition), boot MM without its loop
    // (MM_BootForCombo), then hand the foreground back to OOT (MM_PrepareForTransition stops MM's
    // audio; SOH_ResumeForeground re-activates OOT's RM/audio/GUI). MM stays booted + resident, so the
    // first portal transition is a normal MM_ResumeGame.
    bool mmEagerBooted = false;
    if (MM_BootForCombo && SOH_PrepareForTransition && MM_PrepareForTransition && SOH_ResumeForeground) {
        std::cout << "[ComboShip] Eager MM boot: begin" << std::endl;
        SOH_PrepareForTransition();   // stop OOT audio + tear down OOT GUI (Context/RM kept alive)
        MM_BootForCombo();            // full MM init on the shared Context, MM's RM active, no loop
        MM_PrepareForTransition();    // stop MM's audio (MM started it during InitOTR)
        SOH_ResumeForeground();       // re-activate OOT's RM/audio/GUI as the foreground game
        mmEagerBooted = true;
        std::cout << "[ComboShip] Eager MM boot: complete" << std::endl;
    } else {
        std::cerr << "[ComboShip] Eager MM boot: required exports missing — oracle will be unavailable"
                  << std::endl;
    }

    // ComboShip Inc2 de-risk: dump OOT static rando data (headless, safe AFTER SOH_Init).
    // Write to saves/combo/oot_dump.json so the coherent check set is verifiable.
    // Spoiler generation now happens per-save in Combo_OnGenerate (not at startup).
    {
        std::error_code ec;
        std::filesystem::create_directories("saves/combo", ec);

        if (SOH_DumpRandoStaticData) {
            std::string sohDump = SOH_DumpRandoStaticData();
            { std::ofstream f("saves/combo/oot_dump.json", std::ios::trunc); f << sohDump; }
            auto j = nlohmann::json::parse(sohDump);
            std::cout << "[ComboShip] OOT coherent dump: " << j["checks"].size() << " checks, "
                      << j["items"].size() << " items -> saves/combo/oot_dump.json\n";
        }
        if (MM_DumpRandoStaticData) {
            std::string mmDump = MM_DumpRandoStaticData();
            { std::ofstream f("saves/combo/mm_dump.json", std::ios::trunc); f << mmDump; }
            auto j = nlohmann::json::parse(mmDump);
            std::cout << "[ComboShip] MM static dump: " << j["checks"].size() << " checks, "
                      << j["items"].size() << " items -> saves/combo/mm_dump.json\n";
        }
    }

    // --- 5. Register OOT callbacks ---
    // Note: MM_InitArchives (dormant archive pre-load) is skipped — Ship::ArchiveManager::Init
    // requires a live context which doesn't exist until MM_RunMain runs InitOTR().
    // Archives are loaded correctly when MM_RunGame is called after OOT exits.

    // ComboShip Inc2 (Task 3): register the generate callback BEFORE the save-init callback.
    // Fired by Sram_InitSave before questType is read; generates the seed and applies OOT placements.
    if (SOH_SetOnComboGenerateCallback) {
        SOH_SetOnComboGenerateCallback(Combo_OnGenerate);
        std::cout << "[ComboShip] OOT generate callback registered." << std::endl;
    }

    if (SOH_SetOnNewSaveCallback && MM_InitSaveFile) {
        SOH_SetOnNewSaveCallback(Combo_OnOOTSaveInit);
        std::cout << "[ComboShip] OOT new-save callback registered." << std::endl;
    }

    if (SOH_SetOnSceneSwitchCallback) {
        SOH_SetOnSceneSwitchCallback(Combo_OnOOTSceneSwitch);
        std::cout << "[ComboShip] OOT scene-switch callback registered." << std::endl;
    }

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
                if (SOH_ResumeGame) SOH_ResumeGame();
            }
            if (g_PendingMMFileNum >= 0 && MM_RunGame) {
                if (SOH_PrepareForTransition) SOH_PrepareForTransition();
                if (MM_NotifyComboTransition) MM_NotifyComboTransition();
                if (MM_SetOnComboReturnCallback) MM_SetOnComboReturnCallback(Combo_OnMMReturn);
                current = GAME_MM;
            } else {
                break;
            }
        } else {
            g_pendingOOTReturn = false;
            if (!mmBooted) {
                std::cout << "[ComboShip] MM boot\n";
                MM_RunGame(g_PendingMMFileNum);
                mmBooted = true;
            } else {
                std::cout << "[ComboShip] MM resume\n";
                if (MM_ResumeGame) MM_ResumeGame(g_PendingMMFileNum);
            }
            if (g_pendingOOTReturn) {
                if (MM_PrepareForTransition) MM_PrepareForTransition();
                if (SOH_NotifyComboReturn) SOH_NotifyComboReturn();
                current = GAME_OOT;
            } else {
                break;
            }
        }
    }

    // SOH context outlived MM (COMBO_BUILD keeps it alive across transition) — now fully tear it down.
    if (SOH_Deinit) {
        SOH_Deinit();
    }

    // --- 7. Cleanup ---

    FreeDll(mmModule);
    FreeDll(sohModule);
    return 0;
}
