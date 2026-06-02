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

static int g_PendingMMFileNum = -1;

static void Combo_OnOOTSaveInit(int fileNum) {
    if (MM_InitSaveFile) {
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

    // --- 5. Register OOT callbacks ---
    // Note: MM_InitArchives (dormant archive pre-load) is skipped — Ship::ArchiveManager::Init
    // requires a live context which doesn't exist until MM_RunMain runs InitOTR().
    // Archives are loaded correctly when MM_RunGame is called after OOT exits.

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
    bool ootBooted = false, mmBooted = false;
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
