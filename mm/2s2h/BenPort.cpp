#include "BenPort.h"
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <set>
#include <sstream>

#include <ship/resource/ResourceManager.h>
#include <fast/Fast3dWindow.h>
// ComboShip: upstream merge — our newer libultraship moved these; mm@develop assumed older paths/APIs.
#include <fast/debug/GfxDebugger.h>
#include <stb_image.h>
#include <ship/resource/File.h>
#include <ship/window/Window.h>

#include "z64animation.h"
#include "z64bgcheck.h"
#include <libultraship/libultra/gbi.h>
#include <ship/window/gui/Fonts.h>
#ifdef _WIN32
#include <Windows.h>
#else
#include <time.h>
#endif
#include <ship/audio/AudioPlayer.h>
#include "variables.h"
#include "z64.h"
#include "macros.h"
#include <ship/utils/StringHelper.h>
#include <nlohmann/json.hpp>
#include "build.h"

#include <fast/interpreter.h>
#include <fast/backends/gfx_rendering_api.h>

#ifdef __APPLE__
#include <SDL_scancode.h>
#else
#include <SDL2/SDL_scancode.h>
#endif
#include "Extractor/Extract.h"
// OTRTODO
// #include <functions.h>
#include "2s2h/Enhancements/FrameInterpolation/FrameInterpolation.h"

#ifdef ENABLE_CROWD_CONTROL
#include "Enhancements/crowd-control/CrowdControl.h"
CrowdControl* CrowdControl::Instance;
#endif

#include <libultraship/libultraship.h>
#include <libultraship/controller/controldeck/ControlDeck.h>
#include <fast/resource/ResourceType.h>
#include <BenGui/BenGui.hpp>
#include <BenGui/BenMenu.h>
#ifdef COMBO_BUILD
#include "ComboMenuSharedContext.h" // ComboShip: shared per-DLL ImGui context helper (combo-owned)
#endif

#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/Enhancements/Enhancements.h"
#include "2s2h/Enhancements/GfxPatcher/AuthenticGfxPatches.h"
#include "2s2h/Enhancements/GfxPatcher/PlayerCustomFlipbooks.h"
#include "2s2h/DeveloperTools/DebugConsole.h"
#include "2s2h/Rando/Rando.h"
#include "2s2h/Rando/Spoiler/Spoiler.h"
#include "2s2h/Rando/Logic/Logic.h"
#include "2s2h/Rando/MiscBehavior/ClockShuffle.h"
#include "2s2h/SaveManager/SaveManager.h"
#include "2s2h/CustomMessage/CustomMessage.h"
#include "2s2h/CustomItem/CustomItem.h"
#include "2s2h/BenGui/Notification.h"
#include "2s2h/ShipUtils.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h/PresetManager/PresetManager.h"
#include "2s2h/config/ConfigUpdaters.h"

// Resource Types/Factories
#include <ship/resource/type/Blob.h>
#include <fast/resource/type/DisplayList.h>
#include <fast/resource/type/Matrix.h>
#include <fast/resource/type/Texture.h>
#include <fast/resource/type/Vertex.h>
#include "2s2h/resource/type/2shResourceType.h"
#include "2s2h/resource/type/Animation.h"
#include "2s2h/resource/type/Array.h"
#include "2s2h/resource/type/AudioSample.h"
#include "2s2h/resource/type/AudioSequence.h"
#include "2s2h/resource/type/AudioSoundFont.h"
#include "2s2h/resource/type/CollisionHeader.h"
#include "2s2h/resource/type/Cutscene.h"
#include "2s2h/resource/type/Path.h"
#include "2s2h/resource/type/PlayerAnimation.h"
#include "2s2h/resource/type/Scene.h"
#include "2s2h/resource/type/Skeleton.h"
#include "2s2h/resource/type/SkeletonLimb.h"
#include <ship/resource/factory/BlobFactory.h>
#include <fast/resource/factory/DisplayListFactory.h>
#include <fast/resource/factory/MatrixFactory.h>
#include <fast/resource/factory/TextureFactory.h>
#include <fast/resource/factory/VertexFactory.h>
#include "2s2h/resource/importer/AnimationFactory.h"
#include "2s2h/resource/importer/ArrayFactory.h"
#include "2s2h/resource/importer/AudioSampleFactory.h"
#include "2s2h/resource/importer/AudioSequenceFactory.h"
#include "2s2h/resource/importer/AudioSoundFontFactory.h"
#include "2s2h/resource/importer/CollisionHeaderFactory.h"
#include "2s2h/resource/importer/CutsceneFactory.h"
#include "2s2h/resource/importer/PathFactory.h"
#include "2s2h/resource/importer/PlayerAnimationFactory.h"
#include "2s2h/resource/importer/SceneFactory.h"
#include "2s2h/resource/importer/SkeletonFactory.h"
#include "2s2h/resource/importer/SkeletonLimbFactory.h"
#include "2s2h/resource/importer/TextMMFactory.h"
#include "2s2h/resource/importer/BackgroundFactory.h"
#include "2s2h/resource/importer/TextureAnimationFactory.h"
#include "2s2h/resource/importer/KeyFrameFactory.h"
#include <ship/window/gui/resource/Font.h>
#include <ship/window/FileDropMgr.h>
#include <ship/window/gui/resource/FontFactory.h>
#include "2s2h/Enhancements/Audio/AudioCollection.h"
#include "BenGui/BenInputEditorWindow.h"

OTRGlobals* OTRGlobals::Instance;
GameInteractor* GameInteractor::Instance;
AudioCollection* AudioCollection::Instance;

#ifdef COMBO_BUILD
// Set by ComboShip before MM_RunGame to signal that the OOT context should be reused. With one
// shared libultraship.dll, Context::mContext is the same instance in all DLLs, so GetInstance()
// returns the OOT context — no injection needed.
static bool sComboTransitionActive = false;

extern "C"
#ifdef _WIN32
__declspec(dllexport)
#endif
void MM_NotifyComboTransition(void) {
    sComboTransitionActive = true;
}

extern "C" void (*gComboReturnCallback)(void) = nullptr;
extern "C" __declspec(dllexport) void MM_SetOnComboReturnCallback(void (*cb)(void)) {
    gComboReturnCallback = cb;
}
static bool sComboReturnPending = false;
// MM's own ResourceManager, created at first boot and kept alive for the whole process. A combo
// transition swaps the Context's active RM between MM's and OOT's, so each game keeps its archives +
// resource cache resident and nothing is ever unloaded (no dangling cached pointers). See MM_ResumeGame.
// (Upstream merge: the old RegisterMMResourceFactories factoring was dropped — Initialize() now owns
// the inline factory registration and it runs against whichever RM is active. See docs/UPSTREAM_MERGES.md.)
static std::shared_ptr<Ship::ResourceManager> sMMResourceManager;
#endif

extern "C" char** cameraStrings;
bool prevAltAssets = false;
std::vector<std::shared_ptr<std::string>> cameraStdStrings;

Color_RGB8 kokiriColor = { 0x1E, 0x69, 0x1B };
Color_RGB8 goronColor = { 0x64, 0x14, 0x00 };
Color_RGB8 zoraColor = { 0x00, 0xEC, 0x64 };

int32_t previousImGuiScaleIndex;
float previousImGuiScale;

typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} ArchiveVersion;

std::shared_ptr<Fast::Fast3dWindow> benFast3dWindow;
static ArchiveVersion DetectArchiveVersion(std::string path, bool isO2rType);
static bool VerifyArchiveVersion(ArchiveVersion version);
std::string portArchivePath = "";
static bool shipArchiveVersionMatch = false;

OTRGlobals::OTRGlobals() {
#ifdef COMBO_BUILD
    // Combo OOT->MM forward transition: reuse OOT's already-initialized shared context + window
    // instead of creating new ones (one shared libultraship.dll => GetInstance() is the OOT context).
    // SOH_PrepareForTransition() stopped OOT's audio first.
    bool usingExistingCtx = false;
    if (sComboTransitionActive) {
        auto existingCtx = Ship::Context::GetInstance();
        if (existingCtx != nullptr) {
            context = existingCtx;
            portArchivePath = Ship::Context::LocateFileAcrossAppDirs("2ship.o2r");
            shipArchiveVersionMatch = true; // 2ship.o2r already validated at OOT boot; enable font load below
            // MM's OWN ResourceManager: own archives + factories + resource cache. Make it active before
            // any GetResourceManager() lookup; OOT's RM stays alive (sOOTResourceManager) so nothing is
            // unloaded and no OOT cached pointer dangles. Initialize() adds mm.o2r + the factories onto it.
            auto mmResourceManager = std::make_shared<Ship::ResourceManager>();
            context->SetResourceManager(mmResourceManager);
            mmResourceManager->Init({ portArchivePath }, {}, 3);
            sMMResourceManager = mmResourceManager;
            // MM's fresh RM lacks the Gui-owned infra factories (Font, GuiTexture) the shared Gui
            // registered on OOT's RM at boot; register them so font/gui-texture loads work.
            context->GetWindow()->GetGui()->RegisterResourceFactories();
            // OOT closed the shared window backend on exit (mIsRunning=false); re-arm it so MM's
            // `while (WindowIsRunning())` loop runs instead of returning immediately.
            if (auto fast3d = std::dynamic_pointer_cast<Fast::Fast3dWindow>(context->GetWindow())) {
                fast3d->SetIsRunning(true);
            }
            usingExistingCtx = true;
        }
        sComboTransitionActive = false;
    }
    if (!usingExistingCtx) {
#endif
    context = Ship::Context::CreateUninitializedInstance("2 Ship 2 Harkinian", appShortName, "2ship2harkinian.json");

    portArchivePath = Ship::Context::LocateFileAcrossAppDirs("2ship.o2r");
    ArchiveVersion portArchiveVersion = DetectArchiveVersion("2ship.o2r", true);
    shipArchiveVersionMatch = portArchiveVersion.major == gBuildVersionMajor &&
                              portArchiveVersion.minor == gBuildVersionMinor &&
                              portArchiveVersion.patch == gBuildVersionPatch;

    context->InitConfiguration();
    context->InitConsoleVariables();

    auto controlDeck = std::make_shared<LUS::ControlDeck>(std::vector<CONTROLLERBUTTONS_T>({
        BTN_CUSTOM_MODIFIER1,
        BTN_CUSTOM_MODIFIER2,
        BTN_CUSTOM_OCARINA_NOTE_D4,
        BTN_CUSTOM_OCARINA_NOTE_F4,
        BTN_CUSTOM_OCARINA_NOTE_A4,
        BTN_CUSTOM_OCARINA_NOTE_B4,
        BTN_CUSTOM_OCARINA_NOTE_D5,
        BTN_CUSTOM_OCARINA_DISABLE_SONGS,
        BTN_CUSTOM_OCARINA_PITCH_UP,
        BTN_CUSTOM_OCARINA_PITCH_DOWN,
    }));
    context->InitControlDeck(controlDeck);
    context->InitResourceManager({ portArchivePath }, {}, 3, true);
    context->InitConsole();

    auto benInputEditorWindow = std::make_shared<BenInputEditorWindow>("gWindows.BenInputEditor", "2S2H Input Editor");
    benFast3dWindow =
        std::make_shared<Fast::Fast3dWindow>(std::vector<std::shared_ptr<Ship::GuiWindow>>({ benInputEditorWindow }));
    context->InitWindow(benFast3dWindow);

    BenGui::SetupMenu();
#ifdef COMBO_BUILD
    } // end if (!usingExistingCtx)
    // ImGui's current-context global (GImGui) is a per-module static; this 2ship.dll has its own,
    // separate from libultraship.dll where the context lives. Point it at the shared context (works
    // for both the reuse path and standalone window creation) before any ImGui use here.
    ImGui::SetCurrentContext(context->GetInstance()->GetWindow()->GetGui()->GetImGuiContext());
    // ComboShip: the reuse path above skipped BenGui::SetupMenu() (it only runs inside the
    // !usingExistingCtx block), so MM's BenMenu was never built and the shared Gui's single menu slot
    // still holds OOT's SohMenu. Build/install MM's menu now that the ImGui context is current
    // (widgets populate lazily via BenMenu::InitElement).
    if (usingExistingCtx) {
        BenGui::ActivateMenu(); // ComboShip: no-op under COMBO_BUILD (comboui owns the menu)
    }
#endif

    if (shipArchiveVersionMatch) {

        auto overlay = context->GetInstance()->GetWindow()->GetGui()->GetGameOverlay();
        overlay->LoadFont("Press Start 2P", 12.0f, "fonts/PressStart2P-Regular.ttf");
        overlay->LoadFont("Fipps", 32.0f, "fonts/Fipps-Regular.otf");
        overlay->SetCurrentFont(CVarGetString(CVAR_GAME_OVERLAY_FONT, "Press Start 2P"));

        fontMono = CreateFontWithSize(16.0f, "fonts/Inconsolata-Regular.ttf");
        fontMonoLarger = CreateFontWithSize(20.0f, "fonts/Inconsolata-Regular.ttf");
        fontMonoLargest = CreateFontWithSize(24.0f, "fonts/Inconsolata-Regular.ttf");
        fontStandard = CreateFontWithSize(16.0f, "fonts/Montserrat-Regular.ttf");
        fontStandardLarger = CreateFontWithSize(20.0f, "fonts/Montserrat-Regular.ttf");
        fontStandardLargest = CreateFontWithSize(24.0f, "fonts/Montserrat-Regular.ttf");
        ImGui::GetIO().FontDefault = fontStandardLarger;
    }

    previousImGuiScaleIndex = -1;
    previousImGuiScale = defaultImGuiScale;
    ScaleImGui();
#ifdef COMBO_BUILD
    if (usingExistingCtx) {
        // MM fonts were just added to the shared ImGui atlas (TexReady=false); the renderer backend
        // already built its font texture for OOT and won't rebuild on its own -> MM's first
        // ImGui::NewFrame() would assert "Font Atlas not built!". Invalidate so the next frame rebuilds.
        Ship::Context::GetInstance()->GetWindow()->GetGui()->RebuildFontTexture();
    }
#endif
}

typedef enum ExtractSteps {
    ES_PORT_ARCHIVE,
    ES_WINDOWS,
    ES_EXTRACT_ARGS,
    ES_EXTRACT,
    ES_VERIFY,
} ExtractSteps;

typedef enum PromptSteps {
    PS_FILE_CHECK,
    PS_LOCAL,
    PS_FIRST,
    PS_SECOND,
    PS_DUPE,
    PS_WAIT,
    PS_NONE,
} PromptSteps;

typedef enum WindowsSteps {
    WS_TEMP,
    WS_PERMS,
    WS_ONEDRIVE,
    WS_DONE,
} WindowsSteps;

bool IsSubpath(const std::filesystem::path& path, const std::filesystem::path& base) {
    auto rel = std::filesystem::relative(path, base);
    return !rel.empty() && rel.native()[0] != '.';
}

bool PathTestCleanup(FILE* tfile) {
    try {
        if (std::filesystem::exists("./text.txt"))
            std::filesystem::remove("./text.txt");
        if (std::filesystem::exists("./test/"))
            std::filesystem::remove("./test/");
    } catch (std::filesystem::filesystem_error const& ex) { return false; }
    return true;
}

void CheckAndCreateModFolder() {
    try {
        std::string modsPath = Ship::Context::LocateFileAcrossAppDirs("mods", appShortName);
        if (!std::filesystem::exists(modsPath)) {
            // Create mods folder relative to app dir
            modsPath = Ship::Context::GetPathRelativeToAppDirectory("mods", appShortName);
            std::string filePath = modsPath + "/custom_mod_files_go_here.txt";
            if (std::filesystem::create_directories(modsPath)) {
                std::ofstream(filePath).close();
            }
        }
    } catch (std::filesystem::filesystem_error const& ex) {
        // Couldn't make the folder, continue silently
        return;
    }
}

namespace BenGui {
extern std::shared_ptr<BenGui::BenMenu> mBenMenu;
}

void OTRGlobals::RunExtract(int argc, char* argv[]) {
    bool extractDone = false;
    ExtractSteps extractStep = ES_PORT_ARCHIVE;
    WindowsSteps windowsStep = WS_TEMP;
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(OTRGlobals::Instance->context->GetWindow());
    auto gui = wnd->GetGui();

    bool shouldRegen = VerifyArchiveVersion(DetectArchiveVersion("mm.o2r", true));

    std::filesystem::path ownPath;
    std::vector<std::string> args;
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            args.push_back(argv[i]);
        }
    }
    Extractor extract;
    PromptSteps promptStep = PS_FILE_CHECK;
    std::atomic<size_t> extractCount = 0, totalExtract = 0;

    std::string installPath = Ship::Context::GetAppBundlePath();
    std::string dataPath = Ship::Context::GetAppDirectoryPath(appShortName);
    std::string file;

#if defined(__SWITCH__)
    BenGui::RegisterPopup("Outdated ROM Archives",
                          "\x1b[2;2HYou've launched 2Ship with an old ROM O2R file."
                          "\x1b[4;2HPlease regenerate a new ROM O2R and relaunch."
                          "\x1b[6;2HPress the Home button to exit...",
                          "OK", "", [&]() { exit(1); });
#elif defined(__WIIU__)
    BenGui::RegisterPopup("Outdated ROM Archives",
                          "You've launched 2Ship with an old a ROM O2R file.\n\n"
                          "Please generate a ROM O2R and relaunch.\n\n"
                          "Press and hold the Power button to shutdown...",
                          "OK", "", [&]() { exit(1); });
    OSFatal();
#endif

    if (!std::filesystem::exists(installPath + "/assets")) {
        BenGui::RegisterPopup("Extractor assets not found",
                              "No O2R files found. Missing 'assets/' folder needed to generate OTR file.\nPlease "
                              "re-extract them from the download or.\n\nExiting...",
                              "OK", "", [&]() { exit(1); });
    } else if (shouldRegen) {
        BenGui::RegisterPopup("Outdated ROM Archives",
                              "Your mm.o2r was created with incompatible versions of 2Ship.\nYou will "
                              "now be redirected to re-extract them.");
        std::filesystem::remove("mm.o2r");
    }

    std::shared_ptr<BS::thread_pool> threadPool = std::make_shared<BS::thread_pool>(1);
    std::optional<std::future<void>> extractionTask;

#if not defined(__SWITCH__) && not defined(__WIIU__)
    CheckAndCreateModFolder();
#endif

    while (!extractDone) {
        if (BenGui::PopupsQueued() > 0 || extractionTask.has_value()) {
            goto render;
        }
        switch (extractStep) {
            case ES_PORT_ARCHIVE: {
                if (shipArchiveVersionMatch) {
#ifdef _WIN32
                    extractStep = ES_WINDOWS;
#elif (defined(__WIIU__) || defined(__SWITCH__))
                    extractStep = ES_VERIFY;
#else
                    extractStep = args.empty() ? ES_EXTRACT : ES_EXTRACT_ARGS;
#endif
                } else {
                    std::string msg;

#if defined(__SWITCH__)
                    msg = "\x1b[4;2HPlease re-extract it from the download.\n"
                          "\x1b[6;2HPress the Home button to exit...";
#elif defined(__WIIU__)
                    msg = "Please extract the 2ship.o2r from the 2 Ship 2 Harkinian download\nto your folder.\n\nPress "
                          "and hold the power\n"
                          "button to shutdown...";
#else
                    msg = "Please extract the 2ship.o2r from the 2 Ship 2 Harkinian download to your "
                          "folder.\n\nExiting...";
#endif
                    std::string title =
                        !std::filesystem::exists(portArchivePath) ? "Missing 2ship.o2r" : "2ship.o2r is outdated";
                    BenGui::RegisterPopup(title, msg, "OK", "", [&]() { exit(1); });
                }
                continue;
            }
            case ES_WINDOWS: {
                switch (windowsStep) {
                    case WS_TEMP: {
#ifdef _WIN32
                        char* tempVar = getenv("TEMP");
                        std::filesystem::path tempPath;
                        try {
                            tempPath = std::filesystem::canonical(tempVar);
                        } catch (std::filesystem::filesystem_error const& ex) {
                            std::string userPath = getenv("USERPROFILE");
                            userPath.append("\\AppData\\Local\\Temp");
                            tempPath = std::filesystem::canonical(userPath);
                        }
                        wchar_t buffer[MAX_PATH];
                        GetModuleFileName(NULL, buffer, _countof(buffer));
                        ownPath = std::filesystem::canonical(buffer).parent_path();
                        if (IsSubpath(ownPath, tempPath)) {
                            BenGui::RegisterPopup("2S2H Path Error",
                                                  "2S2H is running in a temp folder.\nExtract the .zip and run again.",
                                                  "OK", "", [&]() { exit(0); });
                        } else {
                            windowsStep = WS_PERMS;
                        }
#endif
                        continue;
                    }
                    case WS_PERMS: {
                        FILE* tfile = fopen("./text.txt", "w");
                        std::filesystem::path tfolder = std::filesystem::path("./test/");
                        bool error = false;
                        try {
                            create_directories(tfolder);
                        } catch (std::filesystem::filesystem_error const& ex) { error = true; }
                        if (tfile == NULL || error) {
                            BenGui::RegisterPopup("2S2H Permissions Error",
                                                  "2S2H does not have proper file permissions.\nPlease move it to a "
                                                  "folder that does and run again.",
                                                  "OK", "", [&]() {
                                                      fclose(tfile);
                                                      PathTestCleanup(tfile);
                                                      exit(0);
                                                  });
                        } else {
                            fclose(tfile);
                            if (!PathTestCleanup(tfile)) {
                                BenGui::RegisterPopup(
                                    "2S2H Permissions Error",
                                    "2S2H does not have proper file permissions.\nPlease move it to a "
                                    "folder that does and run again.",
                                    "OK", "", [&]() { exit(0); });
                            }
                            windowsStep = WS_ONEDRIVE;
                        }
                        continue;
                    }
                    case WS_ONEDRIVE: {
                        if (ownPath.string().find("OneDrive") != std::string::npos) {
                            BenGui::RegisterPopup("2S2H Path Error",
                                                  "2S2H appears to be in a OneDrive folder, which will cause issues.\n"
                                                  "Please move it to a folder outside of OneDrive, like the root of a\n"
                                                  "drive (e.g. \"C:\\Games\\2S2H\").",
                                                  "OK", "", [&]() { exit(0); });
                        } else {
                            windowsStep = WS_DONE;
                            extractStep = args.empty() ? ES_EXTRACT : ES_EXTRACT_ARGS;
                        }
                        continue;
                    }
                    default:
                        continue;
                }
                break;
            }
            case ES_EXTRACT_ARGS: {
#if !defined(__SWITCH__) && !defined(__WIIU__)
                if (args.empty()) {
                    BenGui::RegisterPopup(
                        "Run 2 Ship 2 Harkinian", "All files have been processed. Run 2S2H?", "Yes", "No",
                        [&]() {
                            if (!std::filesystem::exists(Ship::Context::GetAppDirectoryPath(appShortName) +
                                                         "/mm.o2r")) {
                                extractStep = ES_EXTRACT;
                                promptStep = PS_FILE_CHECK;
                            } else {
                                extractStep = ES_VERIFY;
                            }
                        },
                        [&]() { exit(0); });
                    break;
                }
                file = args.at(0);
                args.erase(args.begin());
                extract = Extractor();
                if (extract.RunFileStandalone(file)) {
                    bool doExtract = true;
                    if (std::filesystem::exists(Ship::Context::GetAppDirectoryPath(appShortName) + "/mm.o2r")) {
                        std::string msg = "Archive for current ROM, mm.o2r, already exists.\nExtract again?";
                        BenGui::RegisterPopup("Confirm Re-extract", msg.c_str(), "Yes", "No", [&]() {
                            extractionTask = threadPool->submit_task([&]() -> void {
                                extract.CallZapd(installPath, Ship::Context::GetAppDirectoryPath(appShortName),
                                                 &extractCount, &totalExtract);
                                extractCount = totalExtract = 0;
                            });
                        });
                    } else {
                        extractionTask = threadPool->submit_task([&]() -> void {
                            extract.CallZapd(installPath, Ship::Context::GetAppDirectoryPath(appShortName),
                                             &extractCount, &totalExtract);
                            extractCount = totalExtract = 0;
                        });
                    }
                } else {
                    bool open = true;
                    std::string msg = "File\n" + std::string(file) + "\nis not a ROM or does not match supported ROMs.";
                    BenGui::RegisterPopup("2S2H ROM Error", msg.c_str());
                }
#else
                extractStep = ES_VERIFY;
#endif
                break;
            }
            case ES_EXTRACT: {
                switch (promptStep) {
                    case PS_FILE_CHECK: {
                        if (!std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs("mm.o2r", appShortName))) {
                            BenGui::RegisterPopup(
                                "No O2R Files", "No O2R files found. Generate one now?", "Yes", "No",
                                [&]() { promptStep = PS_LOCAL; }, [&]() { exit(0); });
                        } else {
                            extractStep = ES_VERIFY;
                        }
                        continue;
                    }
                    case PS_LOCAL: {
                        extract = Extractor();
                        extract.SetSearchPath(installPath);
                        extract.GetRoms(args);
                        extract.SetSearchPath(dataPath);
                        extract.GetRoms(args);
                        if (!args.empty()) {
                            promptStep = PS_WAIT;
                            BenGui::RegisterPopup(
                                "ROMs found", "ROMs found in application directory. Would you like to process them?",
                                "Yes", "No", [&]() { extractStep = ES_EXTRACT_ARGS; },
                                [&]() { promptStep = PS_FIRST; });
                        } else {
                            promptStep = PS_FIRST;
                        }
                        continue;
                    }
                    case PS_FIRST: {
                        if (!extract.ManuallySearchForRomMatchingType(RomSearchMode::Both)) {
                            promptStep = PS_FILE_CHECK;
                            continue;
                        }
                        extractionTask = threadPool->submit_task([&]() -> void {
                            extract.CallZapd(installPath, Ship::Context::GetAppDirectoryPath(appShortName),
                                             &extractCount, &totalExtract);
                            promptStep = PS_SECOND;
                            extractCount = 0;
                            totalExtract = 0;
                        });
                        continue;
                    }
                    case PS_SECOND: {
                        BenGui::RegisterPopup(
                            "Extraction Complete", "ROM Extracted. Extract another?", "Yes", "No",
                            [&]() {
                                if (!extract.ManuallySearchForRomMatchingType(RomSearchMode::Vanilla)) {
                                    extractStep = ES_VERIFY;
                                } else {
                                    extractionTask = threadPool->submit_task([&]() -> void {
                                        extract.CallZapd(installPath, Ship::Context::GetAppDirectoryPath(appShortName),
                                                         &extractCount, &totalExtract);
                                        extractStep = ES_VERIFY;
                                        extractCount = 0;
                                        totalExtract = 0;
                                    });
                                }
                            },
                            [&]() { extractStep = ES_VERIFY; });
                        continue;
                    }
                    default:
                        break;
                }
                break;
            }
            case ES_VERIFY: {
                if (!std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs("mm.o2r", appShortName))) {
                    BenGui::RegisterPopup("No ROM Archives",
                                          "No ROM O2R files detected. Please generate a ROM O2R and relaunch.", "OK",
                                          "", [&]() { exit(0); });
                }
                extractDone = true;
                continue;
            }
            default:
                break;
        }

    render:
        if (!WindowIsRunning()) {
            exit(0);
        }
        // Process window events for resize, mouse, keyboard events
        wnd->HandleEvents();
        UIWidgets::Colors themeColor =
            static_cast<UIWidgets::Colors>(CVarGetInteger("gSettings.Menu.Theme", UIWidgets::Colors::LightBlue));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, UIWidgets::ColorValues.at(themeColor));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, UIWidgets::ColorValues.at(UIWidgets::Colors::DarkGray));

        // Skip dropped frames
        if (!wnd->IsFrameReady()) {
            continue;
        }
        gui->StartDraw();
        benFast3dWindow->StartFrame();
        benFast3dWindow->RunGuiOnly();
        if (extractionTask.has_value()) {
            auto status = extractionTask->wait_for(std::chrono::milliseconds(0));
            if (status == std::future_status::ready) {
                try {
                    extractionTask->get();
                } catch (const std::exception& e) {
                    BenGui::RegisterPopup("Extraction Crashed", e.what(), "Close", "", []() { exit(1); });
                }
                extractionTask.reset();
            } else {
                if (!ImGui::IsPopupOpen("ROM Extraction")) {
                    ImGui::OpenPopup("ROM Extraction");
                }
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
                auto color = UIWidgets::ColorValues.at(THEME_COLOR);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(color.x, color.y, color.z, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(color.x, color.y, color.z, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.3f));
                if (ImGui::BeginPopupModal("ROM Extraction", NULL,
                                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                               ImGuiWindowFlags_NoSavedSettings)) {
                    float progress = (totalExtract > 0.0f ? (float)extractCount / (float)totalExtract : 0) * 100.0f;
                    auto filename = std::filesystem::path(file).filename().string();
                    ImGui::Text("Extracting %s...%s", filename.c_str(),
                                roundf(progress) == 100.0f ? " Done. Finishing up." : "");
                    std::string overlay = extractCount > 0 ? fmt::format("{:.0f}%", progress) : "Starting Up";
                    ImGui::ProgressBar(progress / 100.0f, ImVec2(600.0f, 50.0f), overlay.c_str());
                    ImGui::EndPopup();
                }
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar(2);
            }
        }
        gui->EndDraw();
        benFast3dWindow->EndFrame();
        ImGui::PopStyleColor(2);
    }

#ifdef __SWITCH__
    Ship::Switch::Init(Ship::PreInitPhase);
#elif defined(__WIIU__)
    Ship::WiiU::Init(appShortName);
#endif
}

// ComboShip: our newer libultraship dropped Context::InitGfxDebugger; mirror soh's free helper
// (mm@develop's pin still called it as a Context method).
static void InitGfxDebugger() {
    auto dbg =
        std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow())->GetGfxDebugger();
    if (dbg != nullptr) {
        return;
    }
    dbg = std::make_shared<Fast::GfxDebugger>();
    if (dbg != nullptr) {
        SPDLOG_ERROR("Failed to initialize gfx debugger");
    }
}

void OTRGlobals::Initialize() {
    std::string mmPath = Ship::Context::LocateFileAcrossAppDirs("mm.o2r", appShortName);
    if (std::filesystem::exists(mmPath)) {
        context->GetResourceManager()->GetArchiveManager()->AddArchive(mmPath);
    }

    std::unordered_set<uint32_t> validHashes = { MM_NTSC_US_10, MM_NTSC_US_GC };

#if (_DEBUG)
    auto defaultLogLevel = spdlog::level::trace;
#else
    auto defaultLogLevel = spdlog::level::info;
#endif
    context->InitConfiguration();
    context->InitConsoleVariables();
    auto logLevel = static_cast<spdlog::level::level_enum>(CVarGetInteger("gDeveloperTools.LogLevel", defaultLogLevel));
    context->InitLogging(logLevel, logLevel);
    Ship::Context::GetInstance()->GetLogger()->set_pattern("[%H:%M:%S.%e] [%s:%#] [%l] %v");

    InitGfxDebugger();
    context->InitFileDropMgr();

    // tell LUS to reserve 3 2S2H specific threads (Game, Audio, Save)
    // ComboShip: default Alternate Assets OFF (same as soh side; upstream flipped it ON, combo ships
    // no HD/alt asset pack -> per-frame alt/ probe just spams the log). See docs/UPSTREAM_MERGES.md.
    prevAltAssets = CVarGetInteger("gEnhancements.Mods.AlternateAssets", 0);
    context->GetResourceManager()->SetAltAssetsEnabled(prevAltAssets);

    context->InitCrashHandler();

    context->GetWindow()->SetAutoCaptureMouse(CVarGetInteger("gSettings.EnableMouse", 0) &&
                                              CVarGetInteger("gSettings.AutoCaptureMouse", 1));
    context->GetWindow()->SetForceCursorVisibility(CVarGetInteger("gSettings.CursorVisibility", 0));

    context->InitAudio({ .SampleRate = 32000, .SampleLength = 1024, .DesiredBuffered = 1680 });

    SPDLOG_INFO("Starting 2 Ship 2 Harkinian version {} (Branch: {} | Commit: {})", (char*)gBuildVersion,
                (char*)gGitBranch, (char*)gGitCommitHash);

    auto loader = context->GetResourceManager()->GetResourceLoader();
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV0>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(Fast::ResourceType::Texture), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV1>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(Fast::ResourceType::Texture), 1);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryVertexV0>(), RESOURCE_FORMAT_BINARY,
                                    "Vertex", static_cast<uint32_t>(Fast::ResourceType::Vertex), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryXMLVertexV0>(), RESOURCE_FORMAT_XML, "Vertex",
                                    static_cast<uint32_t>(Fast::ResourceType::Vertex), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryDisplayListV0>(),
                                    RESOURCE_FORMAT_BINARY, "DisplayList",
                                    static_cast<uint32_t>(Fast::ResourceType::DisplayList), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryXMLDisplayListV0>(), RESOURCE_FORMAT_XML,
                                    "DisplayList", static_cast<uint32_t>(Fast::ResourceType::DisplayList), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryMatrixV0>(), RESOURCE_FORMAT_BINARY,
                                    "Matrix", static_cast<uint32_t>(Fast::ResourceType::Matrix), 0);
    loader->RegisterResourceFactory(std::make_shared<Ship::ResourceFactoryBinaryBlobV0>(), RESOURCE_FORMAT_BINARY,
                                    "Blob", static_cast<uint32_t>(Ship::ResourceType::Blob), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryArrayV0>(), RESOURCE_FORMAT_BINARY,
                                    "Array", static_cast<uint32_t>(SOH::ResourceType::SOH_Array), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryAnimationV0>(), RESOURCE_FORMAT_BINARY,
                                    "Animation", static_cast<uint32_t>(SOH::ResourceType::SOH_Animation), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryPlayerAnimationV0>(),
                                    RESOURCE_FORMAT_BINARY, "PlayerAnimation",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_PlayerAnimation), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinarySceneV0>(), RESOURCE_FORMAT_BINARY,
                                    "Room", static_cast<uint32_t>(SOH::ResourceType::SOH_Room), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryCollisionHeaderV0>(),
                                    RESOURCE_FORMAT_BINARY, "CollisionHeader",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_CollisionHeader), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinarySkeletonV0>(), RESOURCE_FORMAT_BINARY,
                                    "Skeleton", static_cast<uint32_t>(SOH::ResourceType::SOH_Skeleton), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinarySkeletonLimbV0>(),
                                    RESOURCE_FORMAT_BINARY, "SkeletonLimb",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_SkeletonLimb), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryPathMMV0>(), RESOURCE_FORMAT_BINARY,
                                    "Path", static_cast<uint32_t>(SOH::ResourceType::SOH_Path), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryCutsceneV0>(), RESOURCE_FORMAT_BINARY,
                                    "Cutscene", static_cast<uint32_t>(SOH::ResourceType::SOH_Cutscene), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryTextMMV0>(), RESOURCE_FORMAT_BINARY,
                                    "TextMM", static_cast<uint32_t>(SOH::ResourceType::TSH_TextMM), 0);

    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryAudioSampleV2>(), RESOURCE_FORMAT_BINARY,
                                    "AudioSample", static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSample), 2);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLAudioSampleV0>(), RESOURCE_FORMAT_XML,
                                    "Sample", static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSample), 0);

    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryAudioSoundFontV2>(),
                                    RESOURCE_FORMAT_BINARY, "AudioSoundFont",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSoundFont), 2);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLSoundFontV0>(), RESOURCE_FORMAT_XML,
                                    "SoundFont", static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSoundFont), 0);

    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryAudioSequenceV2>(),
                                    RESOURCE_FORMAT_BINARY, "AudioSequence",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSequence), 2);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLAudioSequenceV0>(), RESOURCE_FORMAT_XML,
                                    "Sequence", static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSequence), 0);

    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryBackgroundV0>(), RESOURCE_FORMAT_BINARY,
                                    "Background", static_cast<uint32_t>(SOH::ResourceType::SOH_Background), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryTextureAnimationV0>(),
                                    RESOURCE_FORMAT_BINARY, "TextureAnimation",
                                    static_cast<uint32_t>(SOH::ResourceType::TSH_TexAnim), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryKeyFrameAnim>(), RESOURCE_FORMAT_BINARY,
                                    "KeyFrameAnim", static_cast<uint32_t>(SOH::ResourceType::TSH_CKeyFrameAnim), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryKeyFrameSkel>(), RESOURCE_FORMAT_BINARY,
                                    "KeyFrameSkel", static_cast<uint32_t>(SOH::ResourceType::TSH_CKeyFrameSkel), 0);

    // gSaveStateMgr = std::make_shared<SaveStateMgr>();
    // gRandomizer = std::make_shared<Randomizer>();

    auto versions = context->GetResourceManager()->GetArchiveManager()->GetGameVersions();
    for (uint32_t version : versions) {
        if (!validHashes.contains(version)) {
#if defined(__SWITCH__)
            SPDLOG_ERROR("Invalid O2R File!");
#elif defined(__WIIU__)
            Ship::WiiU::ThrowInvalidOTR();
#else
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Invalid O2R File",
                                     "Attempted to load an invalid O2R file. Try regenerating.", nullptr);
            SPDLOG_ERROR("Invalid O2R File!");
#endif
            exit(1);
        }
    }
}

OTRGlobals::~OTRGlobals() {
}

extern "C" uint32_t Ship_GetInterpolationFPS() {
    return OTRGlobals::Instance->GetInterpolationFPS();
}

// Number of interpolated frames
extern "C" uint32_t Ship_GetInterpolationFrameCount() {
    return ceil((float)Ship_GetInterpolationFPS() / 20.0f);
}

struct ExtensionEntry {
    std::string path;
    std::string ext;
};

void OTRGlobals::ScaleImGui() {
    int32_t imGuiScaleIndex = CVarGetInteger("gSettings.ImGuiScale", defaultImGuiScale);
    if (imGuiScaleIndex == previousImGuiScaleIndex) {
        return;
    }

    float scale = imguiScaleOptionToValue[imGuiScaleIndex];
    float newScale = scale / previousImGuiScale;
    ImGui::GetStyle().ScaleAllSizes(newScale);
    ImGui::GetIO().FontGlobalScale = scale;
    previousImGuiScale = scale;
    previousImGuiScaleIndex = imGuiScaleIndex;
}

ImFont* OTRGlobals::CreateDefaultFontWithSize(float size) {
    auto mImGuiIo = &ImGui::GetIO();
    ImFontConfig fontCfg = ImFontConfig();
    fontCfg.OversampleH = fontCfg.OversampleV = 1;
    fontCfg.PixelSnapH = true;
    fontCfg.SizePixels = size;
    ImFont* font = mImGuiIo->Fonts->AddFontDefault(&fontCfg);
    // FontAwesome fonts need to have their sizes reduced by 2.0f/3.0f in order to align correctly
    float iconFontSize = size * 2.0f / 3.0f;
    static const ImWchar sIconsRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    ImFontConfig iconsConfig;
    iconsConfig.MergeMode = true;
    iconsConfig.PixelSnapH = true;
    iconsConfig.GlyphMinAdvanceX = iconFontSize;
    mImGuiIo->Fonts->AddFontFromMemoryCompressedBase85TTF(fontawesome_compressed_data_base85, iconFontSize,
                                                          &iconsConfig, sIconsRanges);
    return font;
}

uint32_t OTRGlobals::GetInterpolationFPS() {
    if (CVarGetInteger("gMatchRefreshRate", 0)) {
        return Ship::Context::GetInstance()->GetWindow()->GetCurrentRefreshRate();
    } else if (CVarGetInteger(CVAR_VSYNC_ENABLED, 1) ||
               !Ship::Context::GetInstance()->GetWindow()->CanDisableVerticalSync()) {
        return std::min<uint32_t>(Ship::Context::GetInstance()->GetWindow()->GetCurrentRefreshRate(),
                                  CVarGetInteger("gInterpolationFPS", 20));
    }
    return CVarGetInteger("gInterpolationFPS", 20);
}

extern "C" void OTRMessage_Init();
extern "C" void AudioMgr_CreateNextAudioBuffer(s16* samples, u32 num_samples);
extern "C" void AudioPlayer_Play(const uint8_t* buf, uint32_t len);
extern "C" int AudioPlayer_Buffered(void);
extern "C" int AudioPlayer_GetDesiredBuffered(void);
extern "C" void ResourceMgr_LoadDirectory(const char* resName);
std::unordered_map<std::string, ExtensionEntry> ExtensionCache;

static struct {
    std::thread thread;
    std::condition_variable cv_to_thread, cv_from_thread;
    std::mutex mutex;
    bool running;
    bool processing;
} audio;

void OTRAudio_Thread() {
    while (audio.running) {
        {
            std::unique_lock<std::mutex> Lock(audio.mutex);
            while (!audio.processing && audio.running) {
                audio.cv_to_thread.wait(Lock);
            }

            if (!audio.running) {
                break;
            }
        }
        std::unique_lock<std::mutex> Lock(audio.mutex);
// AudioMgr_ThreadEntry(&gAudioMgr);
//  528 and 544 relate to 60 fps at 32 kHz 32000/60 = 533.333..
//  in an ideal world, one third of the calls should use num_samples=544 and two thirds num_samples=528
#define SAMPLES_HIGH 560
#define SAMPLES_LOW 528

#define AUDIO_FRAMES_PER_UPDATE (R_UPDATE_RATE > 0 ? R_UPDATE_RATE : 1)
#define NUM_AUDIO_CHANNELS 2

        int samples_left = AudioPlayer_Buffered();
        u32 num_audio_samples = samples_left < AudioPlayer_GetDesiredBuffered() ? SAMPLES_HIGH : SAMPLES_LOW;

        // 3 is the maximum authentic frame divisor.
        s16 audio_buffer[SAMPLES_HIGH * NUM_AUDIO_CHANNELS * 3];
        for (int i = 0; i < AUDIO_FRAMES_PER_UPDATE; i++) {
            AudioMgr_CreateNextAudioBuffer(audio_buffer + i * (num_audio_samples * NUM_AUDIO_CHANNELS),
                                           num_audio_samples);
        }

        AudioPlayer_Play((u8*)audio_buffer,
                         num_audio_samples * (sizeof(int16_t) * NUM_AUDIO_CHANNELS * AUDIO_FRAMES_PER_UPDATE));

        audio.processing = false;
        audio.cv_from_thread.notify_one();
    }
}

// C->C++ Bridge
extern "C" void OTRAudio_Init() {
    // Precache all our samples, sequences, etc...
    ResourceMgr_LoadDirectory("audio");

    if (!audio.running) {
        audio.running = true;
        audio.thread = std::thread(OTRAudio_Thread);
    }
}

extern "C" char** gSequenceMap;
extern "C" size_t gSequenceMapSize;

extern "C" char** gFontMap;
extern "C" size_t gFontMapSize;

extern "C" void OTRAudio_Exit() {
    // Tell the audio thread to stop
    {
        std::unique_lock<std::mutex> Lock(audio.mutex);
        audio.running = false;
    }
    audio.cv_to_thread.notify_all();

    // Wait until the audio thread quit
    audio.thread.join();
#ifndef COMBO_BUILD
    // In a combo build OTRAudio_Exit runs on every OOT<->MM transition, not just at shutdown. These
    // maps (gFontMap/gSequenceMap) + load-status arrays are populated once by AudioLoad_Init at boot
    // and must stay resident so a later MM resume can use them (its OTRAudio_Init only restarts the
    // thread, it doesn't repopulate them). Freeing them left gFontMap[fontId] dangling ->
    // AudioHeap_Init/LoadPermanentSamples strlen(null) crash. Keep them (tiny leak only at real exit).
    for (size_t i = 0; i < gSequenceMapSize; i++) {
        free(gSequenceMap[i]);
    }
    free(gSequenceMap);

    for (size_t i = 0; i < gFontMapSize; i++) {
        free(gFontMap[i]);
    }
    free(gFontMap);
    free(gAudioCtx.seqLoadStatus);
    free(gAudioCtx.fontLoadStatus);
#endif
}

extern "C" void OTRExtScanner() {
    auto lst = *Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->ListFiles("*").get();

    for (auto& rPath : lst) {
        std::vector<std::string> raw = StringHelper::Split(rPath, ".");
        std::string ext = raw[raw.size() - 1];
        std::string nPath = rPath.substr(0, rPath.size() - (ext.size() + 1));
        replace(nPath.begin(), nPath.end(), '\\', '/');

        ExtensionCache[nPath] = { rPath, ext };
    }
}

// Read the port version from an archive file
ArchiveVersion ReadPortVersionFromArchive(std::string archivePath, bool isO2rType) {
    ArchiveVersion version = {};

    // Use a temporary archive instance to load the archive appropriately and read the version file
    std::shared_ptr<Ship::Archive> archive;
    if (isO2rType) {
        archive = make_shared<Ship::O2rArchive>(archivePath);
    } else {
#ifdef INCLUDE_MPQ_SUPPORT
        archive = make_shared<Ship::OtrArchive>(archivePath);
#else
        SPDLOG_ERROR("An OTR File, {}, was found but support for them is not included. File will be ignored.",
                     archivePath.c_str());
#endif
    }
    if (archive->Open()) {
        auto t = archive->LoadFile("portVersion");
        if (t != nullptr && t->IsLoaded) {
            auto stream = std::make_shared<Ship::MemoryStream>(t->Buffer->data(), t->Buffer->size());
            auto reader = std::make_shared<Ship::BinaryReader>(stream);
            Ship::Endianness endianness = (Ship::Endianness)reader->ReadUByte();
            reader->SetEndianness(endianness);
            version.major = reader->ReadUInt16();
            version.minor = reader->ReadUInt16();
            version.patch = reader->ReadUInt16();
        }
    }

    return version;
}

// Check that a 2ship.o2r exists and matches the version of 2ship running
// Otherwise show a message and exit
// For Windows/Mac/Linux if the version doesn't match, offer to regenerate it
ArchiveVersion DetectArchiveVersion(std::string fileName, bool isO2rType) {
    bool isArchiveOld = false;
    std::string archivePath = Ship::Context::LocateFileAcrossAppDirs(fileName, appShortName);

    // Doesn't exist so nothing to do here
    if (!std::filesystem::exists(archivePath)) {
        return { INT16_MAX, INT16_MAX, INT16_MAX };
    }

    return ReadPortVersionFromArchive(archivePath, isO2rType);
}

extern "C" void Messagebox_ShowErrorBox(char* title, char* body) {
    Extractor::ShowErrorBox(title, body);
}

bool VerifyArchiveVersion(ArchiveVersion version) {
    return version.major != INT16_MAX && version.major != gBuildVersionMajor;
}

extern "C" void InitOTR(int argc, char* argv[]) {
    OTRGlobals::Instance = new OTRGlobals();
    OTRGlobals::Instance->RunExtract(argc, argv);

    OTRGlobals::Instance->Initialize();

    std::shared_ptr<Ship::Config> conf = OTRGlobals::Instance->context->GetConfig();
    conf->RegisterVersionUpdater(std::make_shared<Ben::ConfigVersion1Updater>());
    conf->RunVersionUpdates();
    Ship::Context::GetInstance()->GetConsoleVariables()->Save();

    GameInteractor::Instance = new GameInteractor();
    AudioCollection::Instance = new AudioCollection();
    LoadGuiTextures();
    BenGui::SetupGuiElements();
    ShipInit::InitAll();
#ifdef COMBO_BUILD
    // Reverse MM->OOT trigger: entering the Clock Tower interior (SCENE_INSIDETOWER) flags a return;
    // acted on at the start of the next clean frame so the save + handoff happen outside scene init.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](s8 sceneId, s8 spawnNum) {
        if (sceneId == SCENE_INSIDETOWER) {
            sComboReturnPending = true;
        }
    });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateMainStart>([]() {
        if (!sComboReturnPending) return;
        sComboReturnPending = false;
        SaveManager_SaveCurrentForCombo();
        if (gComboReturnCallback) gComboReturnCallback();
        if (auto fast3d =
                std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow())) {
            fast3d->SetIsRunning(false);
        }
    });
#endif
    Rando::Init();
    GfxPatcher_ApplyNecessaryAuthenticPatches();
    DebugConsole_Init();
    GameInteractor::Instance->RegisterOwnHooks();
    CustomItem::RegisterHooks();
    CustomMessage::RegisterHooks();
    Rando::StaticData::PopulateCheckNames();

    OTRMessage_Init();
    OTRAudio_Init();
    OTRExtScanner();
    PlayerCustomFlipbooks_Patch();

    // Just came up with arbitrary numbers that seemed to work, this is
    // usually set once(?) in currently stubbed out areas of code.
    gIrqMgrRetraceTime = Ship_Random(700000, 850000);

    time_t now = time(NULL);
    tm* tm_now = localtime(&now);
    if (tm_now->tm_mon == 11 && tm_now->tm_mday >= 24 && tm_now->tm_mday <= 25) {
        CVarRegisterInteger("gLetItSnow", 1);
    } else {
        CVarClear("gLetItSnow");
    }

    srand(now);
#ifdef ENABLE_CROWD_CONTROL
    CrowdControl::Instance = new CrowdControl();
    CrowdControl::Instance->Init();
    if (CVarGetInteger("gCrowdControl", 0)) {
        CrowdControl::Instance->Enable();
    } else {
        CrowdControl::Instance->Disable();
    }
#endif

    Ship::Context::GetInstance()->GetFileDropMgr()->RegisterDropHandler(BinarySaveConverter_HandleFileDropped);
    Ship::Context::GetInstance()->GetFileDropMgr()->RegisterDropHandler(SaveManager_HandleFileDropped);
}

extern "C" void SaveManager_ThreadPoolWait() {
    // SaveManager::Instance->ThreadPoolWait();
}

extern "C" void DeinitOTR() {
    SaveManager_ThreadPoolWait();
    OTRAudio_Exit();
#ifdef ENABLE_CROWD_CONTROL
    CrowdControl::Instance->Disable();
    CrowdControl::Instance->Shutdown();
#endif

    // Destroying gui here because we have shared ptrs to LUS objects which output to SPDLOG which is destroyed before
    // these shared ptrs.
    BenGui::Destroy();
    benFast3dWindow = nullptr;

    OTRGlobals::Instance->context = nullptr;
    delete AudioCollection::Instance;
}

#ifdef _WIN32
extern "C" uint64_t GetFrequency() {
    LARGE_INTEGER nFreq;

    QueryPerformanceFrequency(&nFreq);

    return nFreq.QuadPart;
}

extern "C" uint64_t GetPerfCounter() {
    LARGE_INTEGER ticks;
    QueryPerformanceCounter(&ticks);

    return ticks.QuadPart;
}
#else
extern "C" uint64_t GetFrequency() {
    return 1000; // sec -> ms
}

extern "C" uint64_t GetPerfCounter() {
    struct timespec monotime;
    clock_gettime(CLOCK_MONOTONIC, &monotime);

    uint64_t remainingMs = (monotime.tv_nsec / 1000000);

    // in milliseconds
    return monotime.tv_sec * 1000 + remainingMs;
}
#endif

extern "C" uint64_t GetUnixTimestamp() {
    auto time = std::chrono::system_clock::now();
    auto since_epoch = time.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch);
    long now = millis.count();
    return now;
}

extern "C" void Graph_StartFrame() {
#ifndef __WIIU__
    using Ship::KbScancode;
    int32_t dwScancode = OTRGlobals::Instance->context->GetWindow()->GetLastScancode();
    OTRGlobals::Instance->context->GetWindow()->SetLastScancode(-1);

    switch (dwScancode) {
#if 0
        case KbScancode::LUS_KB_F5: {
            if (CVarGetInteger("gSaveStatesEnabled", 0) == 0) {
                Ship::Context::GetInstance()->GetWindow()->GetGui()->GetGameOverlay()->TextDrawNotification(
                    6.0f, true, "Save states not enabled. Check Cheats Menu.");
                return;
            }
            const unsigned int slot = OTRGlobals::Instance->gSaveStateMgr->GetCurrentSlot();
            const SaveStateReturn stateReturn =
                OTRGlobals::Instance->gSaveStateMgr->AddRequest({ slot, RequestType::SAVE });

            switch (stateReturn) {
                case SaveStateReturn::SUCCESS:
                    SPDLOG_INFO("[SOH] Saved state to slot {}", slot);
                    break;
                case SaveStateReturn::FAIL_WRONG_GAMESTATE:
                    SPDLOG_ERROR("[SOH] Can not save a state outside of \"GamePlay\"");
                    break;
                    [[unlikely]] default : break;
            }
            break;
        }
        case KbScancode::LUS_KB_F6: {
            if (CVarGetInteger("gSaveStatesEnabled", 0) == 0) {
                Ship::Context::GetInstance()->GetWindow()->GetGui()->GetGameOverlay()->TextDrawNotification(
                    6.0f, true, "Save states not enabled. Check Cheats Menu.");
                return;
            }
            unsigned int slot = OTRGlobals::Instance->gSaveStateMgr->GetCurrentSlot();
            slot++;
            if (slot > 5) {
                slot = 0;
            }
            OTRGlobals::Instance->gSaveStateMgr->SetCurrentSlot(slot);
            SPDLOG_INFO("Set SaveState slot to {}.", slot);
            break;
        }
        case KbScancode::LUS_KB_F7: {
            if (CVarGetInteger("gSaveStatesEnabled", 0) == 0) {
                Ship::Context::GetInstance()->GetWindow()->GetGui()->GetGameOverlay()->TextDrawNotification(
                    6.0f, true, "Save states not enabled. Check Cheats Menu.");
                return;
            }
            const unsigned int slot = OTRGlobals::Instance->gSaveStateMgr->GetCurrentSlot();
            const SaveStateReturn stateReturn =
                OTRGlobals::Instance->gSaveStateMgr->AddRequest({ slot, RequestType::LOAD });

            switch (stateReturn) {
                case SaveStateReturn::SUCCESS:
                    SPDLOG_INFO("[SOH] Loaded state from slot {}", slot);
                    break;
                case SaveStateReturn::FAIL_INVALID_SLOT:
                    SPDLOG_ERROR("[SOH] Invalid State Slot Number {}", slot);
                    break;
                case SaveStateReturn::FAIL_STATE_EMPTY:
                    SPDLOG_ERROR("[SOH] State Slot {} is empty", slot);
                    break;
                case SaveStateReturn::FAIL_WRONG_GAMESTATE:
                    SPDLOG_ERROR("[SOH] Can not load a state outside of \"GamePlay\"");
                    break;
                    [[unlikely]] default : break;
            }

            break;
        }
#endif
#if defined(_WIN32) || defined(__APPLE__)
        case KbScancode::LUS_KB_F9: {
            // Toggle TTS
            CVarSetInteger("gA11yTTS", !CVarGetInteger("gA11yTTS", 0));
            break;
        }
#endif
        case KbScancode::LUS_KB_TAB: {
            // Toggle HD Assets
            if (CVarGetInteger("gEnhancements.Mods.AlternateAssetsHotkey", 1)) {
                CVarSetInteger("gEnhancements.Mods.AlternateAssets",
                               !CVarGetInteger("gEnhancements.Mods.AlternateAssets", 0));
            }
            break;
        }
    }
#endif
}

void RunCommands(Gfx* Commands, const std::vector<std::unordered_map<Mtx*, MtxF>>& mtx_replacements) {
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(OTRGlobals::Instance->context->GetWindow());

    if (wnd == nullptr) {
        return;
    }

    // Process window events for resize, mouse, keyboard events
    wnd->HandleEvents();

    auto intp = wnd->GetInterpreterWeak().lock().get();
    intp->mInterpolationIndex = 0;

    UIWidgets::Colors themeColor =
        static_cast<UIWidgets::Colors>(CVarGetInteger("gSettings.Menu.Theme", UIWidgets::Colors::LightBlue));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, UIWidgets::ColorValues.at(themeColor));
    for (const auto& m : mtx_replacements) {
        wnd->DrawAndRunGraphicsCommands(Commands, m);
        intp->mInterpolationIndex++;
    }
    ImGui::PopStyleColor();
}

// C->C++ Bridge
extern "C" void Graph_ProcessGfxCommands(Gfx* commands) {
    {
        std::unique_lock<std::mutex> Lock(audio.mutex);
        audio.processing = true;
    }

    audio.cv_to_thread.notify_one();
    std::vector<std::unordered_map<Mtx*, MtxF>> mtx_replacements;
    int target_fps = OTRGlobals::Instance->GetInterpolationFPS();
    static int last_fps;
    static int last_update_rate;
    static int time;
    int fps = target_fps;
    int original_fps = 60 / R_UPDATE_RATE;
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());

    if (target_fps == 20 || original_fps > target_fps) {
        fps = original_fps;
    }

    if (last_fps != fps || last_update_rate != R_UPDATE_RATE) {
        time = 0;
    }

    // time_base = fps * original_fps (one second)
    int next_original_frame = fps;

    while (time + original_fps <= next_original_frame) {
        time += original_fps;
        if (time != next_original_frame) {
            mtx_replacements.push_back(FrameInterpolation_Interpolate((float)time / next_original_frame));
        } else {
            mtx_replacements.emplace_back();
        }
    }

    time -= fps;

    if (wnd != nullptr) {
        wnd->SetTargetFps(fps);
    }

    // When the gfx debugger is active, only run with the final mtx
    if (GfxDebuggerIsDebugging()) {
        mtx_replacements.clear();
        mtx_replacements.emplace_back();
    }

    RunCommands(commands, mtx_replacements);

    last_fps = fps;
    last_update_rate = R_UPDATE_RATE;

    {
        std::unique_lock<std::mutex> Lock(audio.mutex);
        while (audio.processing) {
            audio.cv_from_thread.wait(Lock);
        }
    }

    bool curAltAssets = CVarGetInteger("gEnhancements.Mods.AlternateAssets", 0);
    if (prevAltAssets != curAltAssets) {
        prevAltAssets = curAltAssets;
        Ship::Context::GetInstance()->GetResourceManager()->SetAltAssetsEnabled(curAltAssets);
        gfx_texture_cache_clear();
        PlayerCustomFlipbooks_Patch();
        SOH::SkeletonPatcher::UpdateSkeletons();
        // GameInteractor::Instance->ExecuteHooks<GameInteractor::OnAssetAltChange>();
    }

    // OTRTODO: FIGURE OUT END FRAME POINT
    /* if (OTRGlobals::Instance->context->GetWindow()->lastScancode != -1)
         OTRGlobals::Instance->context->GetWindow()->lastScancode = -1;*/
}

float divisor_num = 0.0f;

// Batch a coordinate to have its depth read later by OTRGetPixelDepth
extern "C" void OTRGetPixelDepthPrepare(float x, float y) {
    // Invert the Y value to match the origin values used in the renderer
    float adjustedY = SCREEN_HEIGHT - y;

    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());
    if (wnd == nullptr) {
        return;
    }

    wnd->GetPixelDepthPrepare(x, adjustedY);
}

extern "C" uint16_t OTRGetPixelDepth(float x, float y) {
    // Invert the Y value to match the origin values used in the renderer
    float adjustedY = SCREEN_HEIGHT - y;

    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());
    if (wnd == nullptr) {
        return 0;
    }

    return wnd->GetPixelDepth(x, adjustedY);
}

extern "C" bool ResourceMgr_IsAltAssetsEnabled() {
    return Ship::Context::GetInstance()->GetResourceManager()->IsAltAssetsEnabled();
}

extern "C" uint32_t ResourceMgr_GetNumGameVersions() {
    return Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->GetGameVersions().size();
}

extern "C" uint32_t ResourceMgr_GetGameVersion(int index) {
    return Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->GetGameVersions()[index];
}

extern "C" uint32_t ResourceMgr_GetGamePlatform(int index) {
    uint32_t version =
        Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->GetGameVersions()[index];

    switch (version) {
        case MM_NTSC_US_10:
            return GAME_PLATFORM_N64;
        case MM_NTSC_US_GC:
            return GAME_PLATFORM_GC;
    }
}

extern "C" uint32_t ResourceMgr_GetGameRegion(int index) {
    uint32_t version =
        Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->GetGameVersions()[index];

    switch (version) {
        case MM_NTSC_US_10:
        case MM_NTSC_US_GC:
            return GAME_REGION_NTSC;
    }
}

extern "C" void ResourceMgr_LoadDirectory(const char* resName) {
    Ship::Context::GetInstance()->GetResourceManager()->LoadResources(resName);
}
extern "C" void ResourceMgr_DirtyDirectory(const char* resName) {
    Ship::Context::GetInstance()->GetResourceManager()->DirtyResources(resName);
}

extern "C" void ResourceMgr_UnloadResource(const char* resName) {
    std::string path = resName;
    if (path.starts_with("__OTR__")) {
        path = path.substr(7);
    }
    Ship::Context::GetInstance()->GetResourceManager()->UnloadResource(path);
}

static void ResourceMgr_UnloadOriginalWhenAltExists(const char* resName) {
    std::string path = resName;
    if (path.starts_with("__OTR__")) {
        path = path.substr(7);
    }

    if (ResourceMgr_IsAltAssetsEnabled() && ExtensionCache.contains(Ship::IResource::gAltAssetPrefix + path)) {
        ResourceMgr_UnloadResource(path.c_str());
    }
}

// OTRTODO: There is probably a more elegant way to go about this...
// Kenix: This is definitely leaking memory when it's called.
extern "C" char** ResourceMgr_ListFiles(const char* searchMask, int* resultSize) {
    auto lst = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->ListFiles(searchMask);
    char** result = (char**)malloc(lst->size() * sizeof(char*));

    for (size_t i = 0; i < lst->size(); i++) {
        char* str = (char*)malloc(lst.get()[0][i].size() + 1);
        memcpy(str, lst.get()[0][i].data(), lst.get()[0][i].size());
        str[lst.get()[0][i].size()] = '\0';
        result[i] = str;
    }
    *resultSize = lst->size();

    return result;
}

extern "C" uint8_t ResourceMgr_FileExists(const char* filePath) {
    std::string path = filePath;
    if (path.substr(0, 7) == "__OTR__") {
        path = path.substr(7);
    }

    return ExtensionCache.contains(path);
}

extern "C" void ResourceMgr_LoadFile(const char* resName) {
    Ship::Context::GetInstance()->GetResourceManager()->LoadResource(resName);
}

std::shared_ptr<Ship::IResource> GetResourceByName(const char* path) {
    return Ship::Context::GetInstance()->GetResourceManager()->LoadResource(path);
}

extern "C" char* ResourceMgr_LoadFileFromDisk(const char* filePath) {
    FILE* file = fopen(filePath, "r");
    fseek(file, 0, SEEK_END);
    int fSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* data = (char*)malloc(fSize);
    fread(data, 1, fSize, file);

    fclose(file);

    return data;
}

extern "C" uint8_t ResourceMgr_ResourceIsBackground(char* texPath) {
    auto res = GetResourceByName(texPath);
    return res->GetInitData()->Type == static_cast<uint32_t>(SOH::ResourceType::SOH_Background);
}

extern "C" char* ResourceMgr_LoadJPEG(char* data, size_t dataSize) {
    static char* finalBuffer = 0;

    if (finalBuffer == 0)
        finalBuffer = (char*)malloc(dataSize);

    int w;
    int h;
    int comp;

    unsigned char* pixels =
        stbi_load_from_memory((const unsigned char*)data, 320 * 240 * 2, &w, &h, &comp, STBI_rgb_alpha);
    // unsigned char* pixels = stbi_load_from_memory((const unsigned char*)data, 480 * 240 * 2, &w, &h, &comp,
    // STBI_rgb_alpha);
    int idx = 0;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t* bufferTest = (uint16_t*)finalBuffer;
            int pixelIdx = ((y * w) + x) * 4;

            uint8_t r = pixels[pixelIdx + 0] / 8;
            uint8_t g = pixels[pixelIdx + 1] / 8;
            uint8_t b = pixels[pixelIdx + 2] / 8;

            uint8_t alphaBit = pixels[pixelIdx + 3] != 0;

            uint16_t data = (r << 11) + (g << 6) + (b << 1) + alphaBit;

            finalBuffer[idx++] = (data & 0xFF00) >> 8;
            finalBuffer[idx++] = (data & 0x00FF);
        }
    }

    return (char*)finalBuffer;
}

extern "C" uint16_t ResourceMgr_LoadTexWidthByName(char* texPath);

extern "C" uint16_t ResourceMgr_LoadTexHeightByName(char* texPath);

extern "C" char* ResourceMgr_LoadTexOrDListByName(const char* filePath) {
    auto res = GetResourceByName(filePath);

    if (res->GetInitData()->Type == static_cast<uint32_t>(Fast::ResourceType::DisplayList))
        return (char*)&((std::static_pointer_cast<Fast::DisplayList>(res))->Instructions[0]);
    else if (res->GetInitData()->Type == static_cast<uint32_t>(SOH::ResourceType::SOH_Array))
        return (char*)(std::static_pointer_cast<SOH::Array>(res))->Vertices.data();
    else {
        return (char*)ResourceGetDataByName(filePath);
    }
}

extern "C" char* ResourceMgr_LoadIfDListByName(const char* filePath) {
    auto res = GetResourceByName(filePath);

    if (res->GetInitData()->Type == static_cast<uint32_t>(Fast::ResourceType::DisplayList))
        return (char*)&((std::static_pointer_cast<Fast::DisplayList>(res))->Instructions[0]);

    return nullptr;
}

// extern "C" Sprite* GetSeedTexture(uint8_t index) {
//     return OTRGlobals::Instance->gRandomizer->GetSeedTexture(index);
// }

extern "C" char* ResourceMgr_LoadPlayerAnimByName(const char* animPath) {
    auto anim = std::static_pointer_cast<SOH::PlayerAnimation>(GetResourceByName(animPath));

    return (char*)&anim->limbRotData[0];
}

extern "C" void ResourceMgr_PushCurrentDirectory(char* path) {
    Fast::gfx_push_current_dir(path);
}

extern "C" Gfx* ResourceMgr_LoadGfxByName(const char* path) {
    ResourceMgr_UnloadOriginalWhenAltExists(path);

    auto res = std::static_pointer_cast<Fast::DisplayList>(GetResourceByName(path));
    return (Gfx*)&res->Instructions[0];
}

typedef struct {
    int index;
    Gfx instruction;
} GfxPatch;

std::unordered_map<std::string, std::unordered_map<std::string, GfxPatch>> originalGfx;

// Attention! This is primarily for cosmetics & bug fixes. For things like mods and model replacement you should be
// using OTRs instead (When that is available). Index can be found using the commented out section below.
extern "C" void ResourceMgr_PatchGfxByName(const char* path, const char* patchName, int index, Gfx instruction) {
    auto res = std::static_pointer_cast<Fast::DisplayList>(
        Ship::Context::GetInstance()->GetResourceManager()->LoadResource(path));

    // Leaving this here for people attempting to find the correct Dlist index to patch
    /*if (strcmp("__OTR__objects/object_gi_longsword/gGiBiggoronSwordDL", path) == 0) {
        for (int i = 0; i < res->instructions.size(); i++) {
            Gfx* gfx = (Gfx*)&res->instructions[i];
            // Log all commands
            // SPDLOG_INFO("index:{} command:{}", i, gfx->words.w0 >> 24);
            // Log only SetPrimColors
            if (gfx->words.w0 >> 24 == 250) {
                SPDLOG_INFO("index:{} r:{} g:{} b:{} a:{}", i, _SHIFTR(gfx->words.w1, 24, 8), _SHIFTR(gfx->words.w1, 16,
    8), _SHIFTR(gfx->words.w1, 8, 8), _SHIFTR(gfx->words.w1, 0, 8));
            }
        }
    }*/

    // Index refers to individual gfx words, which are half the size on 32-bit
    // if (sizeof(uintptr_t) < 8) {
    // index /= 2;
    // }

    // Do not patch custom assets as they most likely do not have the same instructions as authentic assets
    if (res->GetInitData()->IsCustom) {
        return;
    }

    Gfx* gfx = (Gfx*)&res->Instructions[index];

    if (!originalGfx.contains(path) || !originalGfx[path].contains(patchName)) {
        originalGfx[path][patchName] = { index, *gfx };
    }

    *gfx = instruction;
}

extern "C" void ResourceMgr_PatchGfxCopyCommandByName(const char* path, const char* patchName, int destinationIndex,
                                                      int sourceIndex) {
    auto res = std::static_pointer_cast<Fast::DisplayList>(
        Ship::Context::GetInstance()->GetResourceManager()->LoadResource(path));

    // Do not patch custom assets as they most likely do not have the same instructions as authentic assets
    if (res->GetInitData()->IsCustom) {
        return;
    }

    Gfx* destinationGfx = (Gfx*)&res->Instructions[destinationIndex];
    Gfx sourceGfx = *(Gfx*)&res->Instructions[sourceIndex];

    if (!originalGfx.contains(path) || !originalGfx[path].contains(patchName)) {
        originalGfx[path][patchName] = { destinationIndex, *destinationGfx };
    }

    *destinationGfx = sourceGfx;
}

extern "C" void ResourceMgr_UnpatchGfxByName(const char* path, const char* patchName) {
    if (originalGfx.contains(path) && originalGfx[path].contains(patchName)) {
        auto res = std::static_pointer_cast<Fast::DisplayList>(
            Ship::Context::GetInstance()->GetResourceManager()->LoadResource(path));

        Gfx* gfx = (Gfx*)&res->Instructions[originalGfx[path][patchName].index];
        *gfx = originalGfx[path][patchName].instruction;

        originalGfx[path].erase(patchName);
    }
}

extern "C" size_t ResourceMgr_GetPatchCountForDL(const char* path) {
    if (originalGfx.contains(path)) {
        return originalGfx[path].size();
    }
    return 0;
}

extern "C" void ResourceMgr_ResetAllPatchesForDL(const char* path) {
    if (!originalGfx.contains(path)) {
        return;
    }

    auto res = std::static_pointer_cast<Fast::DisplayList>(
        Ship::Context::GetInstance()->GetResourceManager()->LoadResource(path));

    // Iterate through all patches and restore original instructions
    auto& patches = originalGfx[path];
    for (auto it = patches.begin(); it != patches.end();) {
        Gfx* gfx = (Gfx*)&res->Instructions[it->second.index];
        *gfx = it->second.instruction;
        // erase() returns the next iterator, allowing safe iteration during removal
        it = patches.erase(it);
    }

    // Clean up empty map entry
    if (patches.empty()) {
        originalGfx.erase(path);
    }
}

extern "C" char* ResourceMgr_LoadVtxArrayByName(const char* path) {
    auto res = std::static_pointer_cast<SOH::Array>(GetResourceByName(path));

    return (char*)res->Vertices.data();
}

extern "C" size_t ResourceMgr_GetVtxArraySizeByName(const char* path) {
    auto res = std::static_pointer_cast<SOH::Array>(GetResourceByName(path));

    return res->Vertices.size();
}

extern "C" char* ResourceMgr_LoadArrayByName(const char* path) {
    auto res = std::static_pointer_cast<SOH::Array>(GetResourceByName(path));

    return (char*)res->Scalars.data();
}

extern "C" size_t ResourceMgr_GetArraySizeByName(const char* path) {
    auto res = std::static_pointer_cast<SOH::Array>(GetResourceByName(path));

    return res->Scalars.size();
}

// Loads U8 data from an Array resource into an externally managed buffer, or mallocs a new buffer
// if the passed in a nullptr. This malloced buffer must be freed by the caller.
extern "C" u8* ResourceMgr_LoadArrayByNameAsU8(const char* path, u8* buffer) {
    auto res = std::static_pointer_cast<SOH::Array>(GetResourceByName(path));

    if (buffer == nullptr) {
        buffer = (u8*)malloc(sizeof(u8) * res->Scalars.size());
    }

    for (size_t i = 0; i < res->Scalars.size(); i++) {
        buffer[i] = res->Scalars[i].u8;
    }

    return buffer;
}

// Loads Vec3s data from an Array resource.
// mallocs a new buffer that must be freed by the caller.
extern "C" char* ResourceMgr_LoadArrayByNameAsVec3s(const char* path) {
    auto res = std::static_pointer_cast<SOH::Array>(GetResourceByName(path));

    // if (res->CachedGameAsset != nullptr)
    //     return (char*)res->CachedGameAsset;
    // else
    // {
    Vec3s* data = (Vec3s*)malloc(sizeof(Vec3s) * res->Scalars.size());

    for (size_t i = 0; i < res->Scalars.size(); i += 3) {
        data[(i / 3)].x = res->Scalars[i + 0].s16;
        data[(i / 3)].y = res->Scalars[i + 1].s16;
        data[(i / 3)].z = res->Scalars[i + 2].s16;
    }

    // res->CachedGameAsset = data;

    return (char*)data;
    // }
}

extern "C" AnimatedMaterial* ResourceMgr_LoadAnimatedMatByName(const char* path) {
    return (AnimatedMaterial*)ResourceGetDataByName(path);
}

extern "C" CollisionHeader* ResourceMgr_LoadColByName(const char* path) {
    return (CollisionHeader*)ResourceGetDataByName(path);
}

extern "C" Vtx* ResourceMgr_LoadVtxByName(char* path) {
    return (Vtx*)ResourceGetDataByName(path);
}

extern "C" Mtx* ResourceMgr_LoadMtxByName(char* path) {
    return (Mtx*)ResourceGetDataByName(path);
}

extern "C" SequenceData ResourceMgr_LoadSeqByName(const char* path) {
    SequenceData* sequence = (SequenceData*)ResourceGetDataByName(path);
    return *sequence;
}
extern "C" SequenceData* ResourceMgr_LoadSeqPtrByName(const char* path) {
    SequenceData* sequence = (SequenceData*)ResourceGetDataByName(path);
    return sequence;
}
extern "C" KeyFrameSkeleton* ResourceMgr_LoadKeyFrameSkelByName(const char* path) {
    return (KeyFrameSkeleton*)ResourceGetDataByName(path);
}

extern "C" KeyFrameAnimation* ResourceMgr_LoadKeyFrameAnimByName(const char* path) {
    return (KeyFrameAnimation*)ResourceGetDataByName(path);
}
// std::map<std::string, SoundFontSample*> cachedCustomSFs;
#if 0
extern "C" SoundFontSample* ReadCustomSample(const char* path) {
    return nullptr;
    /*
        if (!ExtensionCache.contains(path))
            return nullptr;

        ExtensionEntry entry = ExtensionCache[path];

        auto sampleRaw = Ship::Context::GetInstance()->GetResourceManager()->LoadFile(entry.path);
        uint32_t* strem = (uint32_t*)sampleRaw->Buffer.get();
        uint8_t* strem2 = (uint8_t*)strem;

        SoundFontSample* sampleC = new SoundFontSample;

        if (entry.ext == "wav") {
            drwav_uint32 channels;
            drwav_uint32 sampleRate;
            drwav_uint64 totalPcm;
            drmp3_int16* pcmData =
                drwav_open_memory_and_read_pcm_frames_s16(strem2, sampleRaw->BufferSize, &channels, &sampleRate,
       &totalPcm, NULL); sampleC->size = totalPcm; sampleC->sampleAddr = (uint8_t*)pcmData; sampleC->codec = CODEC_S16;

            sampleC->loop = new AdpcmLoop;
            sampleC->loop->start = 0;
            sampleC->loop->end = sampleC->size - 1;
            sampleC->loop->count = 0;
            sampleC->sampleRateMagicValue = 'RIFF';
            sampleC->sampleRate = sampleRate;

            cachedCustomSFs[path] = sampleC;
            return sampleC;
        } else if (entry.ext == "mp3") {
            drmp3_config mp3Info;
            drmp3_uint64 totalPcm;
            drmp3_int16* pcmData =
                drmp3_open_memory_and_read_pcm_frames_s16(strem2, sampleRaw->BufferSize, &mp3Info, &totalPcm, NULL);

            sampleC->size = totalPcm * mp3Info.channels * sizeof(short);
            sampleC->sampleAddr = (uint8_t*)pcmData;
            sampleC->codec = CODEC_S16;

            sampleC->loop = new AdpcmLoop;
            sampleC->loop->start = 0;
            sampleC->loop->end = sampleC->size;
            sampleC->loop->count = 0;
            sampleC->sampleRateMagicValue = 'RIFF';
            sampleC->sampleRate = mp3Info.sampleRate;

            cachedCustomSFs[path] = sampleC;
            return sampleC;
        }

        return nullptr;
    */
}

extern "C" SoundFontSample* ResourceMgr_LoadAudioSample(const char* path) {
    return (SoundFontSample*)ResourceGetDataByName(path);
}
#endif

extern "C" SoundFont* ResourceMgr_LoadAudioSoundFontByName(const char* path) {
    return (SoundFont*)ResourceGetDataByName(path);
}

extern "C" SoundFont* ResourceMgr_LoadAudioSoundFontByCRC(uint64_t crc) {
    return (SoundFont*)ResourceGetDataByCrc(crc);
}

extern "C" int ResourceMgr_OTRSigCheck(char* imgData) {
    uintptr_t i = (uintptr_t)(imgData);

    // if (i == 0xD9000000 || i == 0xE7000000 || (i & 1) == 1)
    if ((i & 1) == 1)
        return 0;

    // if ((i & 0xFF000000) != 0xAB000000 && (i & 0xFF000000) != 0xCD000000 && i != 0) {
    if (i != 0) {
        if (imgData[0] == '_' && imgData[1] == '_' && imgData[2] == 'O' && imgData[3] == 'T' && imgData[4] == 'R' &&
            imgData[5] == '_' && imgData[6] == '_')
            return 1;
    }

    return 0;
}

// Load animation with explicit alt asset path checking.
// When Alt Assets is OFF: use original path directly (O2R or vanilla)
// When Alt Assets is ON: try alt/ prefix first, fall back to regular path if not found or invalid
extern "C" AnimationHeaderCommon* ResourceMgr_LoadAnimByName(const char* path) {
    bool isAlt = ResourceMgr_IsAltAssetsEnabled();

    if (isAlt) {
        std::string pathStr = std::string(path);
        static const std::string sOtr = "__OTR__";

        if (pathStr.starts_with(sOtr)) {
            pathStr = pathStr.substr(sOtr.length());
        }

        // Try alt/ first
        pathStr = Ship::IResource::gAltAssetPrefix + pathStr;
        AnimationHeaderCommon* animHeader = (AnimationHeaderCommon*)ResourceGetDataByName(pathStr.c_str());

        // If alt loaded successfully, verify it has valid data
        if (animHeader != NULL) {
            // Check for valid frame count (> 0)
            if (animHeader->frameCount > 0) {
                // For Normal animations: check frameData (comes after frameCount in AnimationHeader)
                // For Link animations: check segment (comes after frameCount in LinkAnimationHeader)
                // We check both to be safe - if either is valid, the animation is usable
                AnimationHeader* normalAnim = (AnimationHeader*)animHeader;
                PlayerAnimationHeader* playerAnim = (PlayerAnimationHeader*)animHeader;

                // Valid if Normal animation has frameData OR Link animation has segment
                if (normalAnim->frameData != NULL || playerAnim->segmentVoid != NULL) {
                    return animHeader;
                }
            }
            // Alt loaded but is invalid (broken), fall through to original path
        }

        // Fall back to original path
        return (AnimationHeaderCommon*)ResourceGetDataByName(path);
    }

    // Alt OFF: use original path directly
    return (AnimationHeaderCommon*)ResourceGetDataByName(path);
}

extern "C" SkeletonHeader* ResourceMgr_LoadSkeletonByName(const char* path, SkelAnime* skelAnime) {
    std::string pathStr = std::string(path);
    static const std::string sOtr = "__OTR__";

    if (pathStr.starts_with(sOtr)) {
        pathStr = pathStr.substr(sOtr.length());
    }

    bool isAlt = ResourceMgr_IsAltAssetsEnabled();

    if (isAlt) {
        pathStr = Ship::IResource::gAltAssetPrefix + pathStr;
    }

    SkeletonHeader* skelHeader = (SkeletonHeader*)ResourceGetDataByName(pathStr.c_str());

    // If there isn't an alternate model, load the regular one
    if (isAlt && skelHeader == NULL) {
        skelHeader = (SkeletonHeader*)ResourceGetDataByName(path);
    }

    // This function is only called when a skeleton is initialized.
    // Therefore we can take this opportunity to take note of the Skeleton that is created...
    if (skelAnime != nullptr) {
        auto stringPath = std::string(path);
        SOH::SkeletonPatcher::RegisterSkeleton(stringPath, skelAnime);
    }

    return skelHeader;
}

extern "C" void ResourceMgr_UnregisterSkeleton(SkelAnime* skelAnime) {
    if (skelAnime != nullptr)
        SOH::SkeletonPatcher::UnregisterSkeleton(skelAnime);
}

extern "C" void ResourceMgr_ClearSkeletons() {
    SOH::SkeletonPatcher::ClearSkeletons();
}

extern "C" s32* ResourceMgr_LoadCSByName(const char* path) {
    return (s32*)ResourceGetDataByName(path);
}

ImFont* OTRGlobals::CreateFontWithSize(float size, std::string fontPath) {
    auto mImGuiIo = &ImGui::GetIO();
    ImFont* font;
    if (fontPath == "") {
        ImFontConfig fontCfg = ImFontConfig();
        fontCfg.OversampleH = fontCfg.OversampleV = 1;
        fontCfg.PixelSnapH = true;
        fontCfg.SizePixels = size;
        font = mImGuiIo->Fonts->AddFontDefault(&fontCfg);
    } else {
        auto initData = std::make_shared<Ship::ResourceInitData>();
        initData->Format = RESOURCE_FORMAT_BINARY;
        initData->Type = static_cast<uint32_t>(RESOURCE_TYPE_FONT);
        initData->ResourceVersion = 0;
        initData->Path = fontPath;
        std::shared_ptr<Ship::Font> fontData = std::static_pointer_cast<Ship::Font>(
            Ship::Context::GetInstance()->GetResourceManager()->LoadResource(fontPath, false, initData));
        ImFontConfig fontConf;
        fontConf.FontDataOwnedByAtlas = false;
        font = mImGuiIo->Fonts->AddFontFromMemoryTTF(fontData->Data, fontData->DataSize, size, &fontConf, nullptr);
    }
    // FontAwesome fonts need to have their sizes reduced by 2.0f/3.0f in order to align correctly
    float iconFontSize = size * 2.0f / 3.0f;
    static const ImWchar sIconsRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    ImFontConfig iconsConfig;
    iconsConfig.MergeMode = true;
    iconsConfig.PixelSnapH = true;
    iconsConfig.GlyphMinAdvanceX = iconFontSize;
    mImGuiIo->Fonts->AddFontFromMemoryCompressedBase85TTF(fontawesome_compressed_data_base85, iconFontSize,
                                                          &iconsConfig, sIconsRanges);
    return font;
}

std::filesystem::path GetSaveFile(std::shared_ptr<Ship::Config> Conf) {
    const std::string fileName =
        Conf->GetString("Game.SaveName", Ship::Context::GetPathRelativeToAppDirectory("oot_save.sav"));
    std::filesystem::path saveFile = std::filesystem::absolute(fileName);

    if (!exists(saveFile.parent_path())) {
        create_directories(saveFile.parent_path());
    }

    return saveFile;
}

std::filesystem::path GetSaveFile() {
    const std::shared_ptr<Ship::Config> pConf = OTRGlobals::Instance->context->GetConfig();

    return GetSaveFile(pConf);
}

void OTRGlobals::CheckSaveFile(size_t sramSize) const {
    const std::shared_ptr<Ship::Config> pConf = Instance->context->GetConfig();

    std::filesystem::path savePath = GetSaveFile(pConf);
    std::fstream saveFile(savePath, std::fstream::in | std::fstream::out | std::fstream::binary);
    if (saveFile.fail()) {
        saveFile.open(savePath, std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::app);
        for (int i = 0; i < sramSize; ++i) {
            saveFile.write("\0", 1);
        }
    }
    saveFile.close();
}

// extern "C" void Ctx_ReadSaveFile(uintptr_t addr, void* dramAddr, size_t size) {
//     SaveManager::ReadSaveFile(GetSaveFile(), addr, dramAddr, size);
// }

// extern "C" void Ctx_WriteSaveFile(uintptr_t addr, void* dramAddr, size_t size) {
//     SaveManager::WriteSaveFile(GetSaveFile(), addr, dramAddr, size);
// }

std::wstring StringToU16(const std::string& s) {
    std::vector<unsigned long> result;
    size_t i = 0;
    while (i < s.size()) {
        unsigned long uni;
        size_t nbytes;
        bool error = false;
        unsigned char c = s[i++];
        if (c < 0x80) { // ascii
            uni = c;
            nbytes = 0;
        } else if (c <= 0xBF) { // assuming kata/hiragana delimiter
            nbytes = 0;
            uni = '\1';
        } else if (c <= 0xDF) {
            uni = c & 0x1F;
            nbytes = 1;
        } else if (c <= 0xEF) {
            uni = c & 0x0F;
            nbytes = 2;
        } else if (c <= 0xF7) {
            uni = c & 0x07;
            nbytes = 3;
        }
        for (size_t j = 0; j < nbytes; ++j) {
            unsigned char c = s[i++];
            uni <<= 6;
            uni += c & 0x3F;
        }
        if (uni != '\1')
            result.push_back(uni);
    }
    std::wstring utf16;
    for (size_t i = 0; i < result.size(); ++i) {
        unsigned long uni = result[i];
        if (uni <= 0xFFFF) {
            utf16 += (wchar_t)uni;
        } else {
            uni -= 0x10000;
            utf16 += (wchar_t)((uni >> 10) + 0xD800);
            utf16 += (wchar_t)((uni & 0x3FF) + 0xDC00);
        }
    }
    return utf16;
}

int CopyStringToCharBuffer(const std::string& inputStr, char* buffer, const int maxBufferSize) {
    if (!inputStr.empty()) {
        // Prevent potential horrible overflow due to implicit conversion of maxBufferSize to an unsigned. Prevents
        // negatives.
        memset(buffer, 0, std::max<int>(0, maxBufferSize));
        // Gaurentee that this value will be greater than 0, regardless of passed variables.
        const int copiedCharLen = std::min<int>(std::max<int>(0, maxBufferSize - 1), inputStr.length());
        memcpy(buffer, inputStr.c_str(), copiedCharLen);
        return copiedCharLen;
    }

    return 0;
}

extern "C" void OTRGfxPrint(const char* str, void* printer, void (*printImpl)(void*, char)) {
    const std::vector<uint32_t> hira1 = {
        u'を', u'ぁ', u'ぃ', u'ぅ', u'ぇ', u'ぉ', u'ゃ', u'ゅ', u'ょ', u'っ', u'-',  u'あ', u'い',
        u'う', u'え', u'お', u'か', u'き', u'く', u'け', u'こ', u'さ', u'し', u'す', u'せ', u'そ',
    };

    const std::vector<uint32_t> hira2 = {
        u'た', u'ち', u'つ', u'て', u'と', u'な', u'に', u'ぬ', u'ね', u'の', u'は', u'ひ', u'ふ', u'へ', u'ほ', u'ま',
        u'み', u'む', u'め', u'も', u'や', u'ゆ', u'よ', u'ら', u'り', u'る', u'れ', u'ろ', u'わ', u'ん', u'゛', u'゜',
    };

    std::wstring wstr = StringToU16(str);

    for (const auto& c : wstr) {
        if (c < 0x80) {
            printImpl(printer, c);
        } else if (c >= u'｡' && c <= u'ﾟ') { // katakana
            printImpl(printer, c - 0xFEC0);
        } else {
            auto it = std::find(hira1.begin(), hira1.end(), c);
            if (it != hira1.end()) { // hiragana block 1
                printImpl(printer, 0x88 + std::distance(hira1.begin(), it));
            }

            auto it2 = std::find(hira2.begin(), hira2.end(), c);
            if (it2 != hira2.end()) { // hiragana block 2
                printImpl(printer, 0xe0 + std::distance(hira2.begin(), it2));
            }
        }
    }
}

// Gets the width of the main ImGui window
extern "C" uint32_t OTRGetCurrentWidth() {
    return OTRGlobals::Instance->context->GetWindow()->GetWidth();
}

// Gets the height of the main ImGui window
extern "C" uint32_t OTRGetCurrentHeight() {
    return OTRGlobals::Instance->context->GetWindow()->GetHeight();
}

Color_RGB8 GetColorForControllerLED() {
#if 0
    auto brightness = CVarGetFloat("gLedBrightness", 1.0f) / 1.0f;
    Color_RGB8 color = { 0, 0, 0 };
    if (brightness > 0.0f) {
        LEDColorSource source =
            static_cast<LEDColorSource>(CVarGetInteger("gLedColorSource", LED_SOURCE_TUNIC_ORIGINAL));
        bool criticalOverride = CVarGetInteger("gLedCriticalOverride", 1);
        if (gPlayState && (source == LED_SOURCE_TUNIC_ORIGINAL || source == LED_SOURCE_TUNIC_COSMETICS)) {
            switch (CUR_EQUIP_VALUE(EQUIP_TUNIC) - 1) {
                case PLAYER_TUNIC_KOKIRI:
                    color = source == LED_SOURCE_TUNIC_COSMETICS
                                ? CVarGetColor24("gCosmetics.Link_KokiriTunic.Value", kokiriColor)
                                : kokiriColor;
                    break;
                case PLAYER_TUNIC_GORON:
                    color = source == LED_SOURCE_TUNIC_COSMETICS
                                ? CVarGetColor24("gCosmetics.Link_GoronTunic.Value", goronColor)
                                : goronColor;
                    break;
                case PLAYER_TUNIC_ZORA:
                    color = source == LED_SOURCE_TUNIC_COSMETICS
                                ? CVarGetColor24("gCosmetics.Link_ZoraTunic.Value", zoraColor)
                                : zoraColor;
                    break;
            }
        }
        if (source == LED_SOURCE_CUSTOM) {
            color = CVarGetColor24("gLedPort1Color", { 255, 255, 255 });
        }
        if (criticalOverride || source == LED_SOURCE_HEALTH) {
            if (HealthMeter_IsCritical()) {
                color = { 0xFF, 0, 0 };
            } else if (source == LED_SOURCE_HEALTH) {
                if (gSaveContext.health / gSaveContext.healthCapacity <= 0.4f) {
                    color = { 0xFF, 0xFF, 0 };
                } else {
                    color = { 0, 0xFF, 0 };
                }
            }
        }
        color.r = color.r * brightness;
        color.g = color.g * brightness;
        color.b = color.b * brightness;
    }
#endif
    return { 0, 0, 0 };
}

extern "C" void OTRControllerCallback(uint8_t rumble) {
    // We call this every tick, SDL accounts for this use and prevents driver spam
    // https://github.com/libsdl-org/SDL/blob/f17058b562c8a1090c0c996b42982721ace90903/src/joystick/SDL_joystick.c#L1114-L1144
    Ship::Context::GetInstance()->GetControlDeck()->GetControllerByPort(0)->GetLED()->SetLEDColor(
        GetColorForControllerLED());

    static std::shared_ptr<BenInputEditorWindow> controllerConfigWindow = nullptr;
    if (controllerConfigWindow == nullptr) {
        controllerConfigWindow = std::dynamic_pointer_cast<BenInputEditorWindow>(
            Ship::Context::GetInstance()->GetWindow()->GetGui()->GetGuiWindow("2S2H Input Editor"));
        // note: the current implementation may not be desired in LUS, as "true" rumble support
        //    using osMotor calls is planned: https://github.com/Kenix3/libultraship/issues/9
    }
    if (controllerConfigWindow->TestingRumble()) {
        return;
    }

    // TODO: other ports?
    if (rumble) {
        Ship::Context::GetInstance()->GetControlDeck()->GetControllerByPort(0)->GetRumble()->StartRumble();
    } else {
        Ship::Context::GetInstance()->GetControlDeck()->GetControllerByPort(0)->GetRumble()->StopRumble();
    }
}

extern "C" float OTRGetAspectRatio() {
    return Ship::Context::GetInstance()->GetWindow()->GetAspectRatio();
}

extern "C" float OTRGetDimensionFromLeftEdge(float v) {
    auto fastWnd = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());
    auto intP = fastWnd->GetInterpreterWeak().lock();

    if (!intP) {
        assert(false && "Lost reference to Fast::Interpreter");
        return v;
    }

    auto gfx_native_dimensions = intP->mNativeDimensions;

    return (gfx_native_dimensions.width / 2 - gfx_native_dimensions.height / 2 * OTRGetAspectRatio() + (v));
}

extern "C" float OTRGetDimensionFromRightEdge(float v) {
    auto fastWnd = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());
    auto intP = fastWnd->GetInterpreterWeak().lock();

    if (!intP) {
        assert(false && "Lost reference to Fast::Interpreter");
        return v;
    }

    auto gfx_native_dimensions = intP->mNativeDimensions;

    return (gfx_native_dimensions.width / 2 + gfx_native_dimensions.height / 2 * OTRGetAspectRatio() -
            (gfx_native_dimensions.width - v));
}

// Gets the width of the current render target area
extern "C" uint32_t OTRGetGameRenderWidth() {
    auto fastWnd = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());
    auto intP = fastWnd->GetInterpreterWeak().lock();

    if (!intP) {
        assert(false && "Lost reference to Fast::Interpreter");
        return 320;
    }

    uint32_t height, width;
    intP->GetCurDimensions(&width, &height);

    return width;
}

// Gets the height of the current render target area
extern "C" uint32_t OTRGetGameRenderHeight() {
    auto fastWnd = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());
    auto intP = fastWnd->GetInterpreterWeak().lock();

    if (!intP) {
        assert(false && "Lost reference to Fast::Interpreter");
        return 240;
    }

    uint32_t height, width;
    intP->GetCurDimensions(&width, &height);

    return height;
}

f32 floorf(f32 x);
f32 ceilf(f32 x);

extern "C" int16_t OTRGetRectDimensionFromLeftEdge(float v) {
    return ((int)floorf(OTRGetDimensionFromLeftEdge(v)));
}

extern "C" int16_t OTRGetRectDimensionFromRightEdge(float v) {
    return ((int)ceilf(OTRGetDimensionFromRightEdge(v)));
}

// Takes a HUD coordinate(320x240) and converts it to the game window pixel coordinates (any size, any aspect ratio)
// Though the HUD uses a 320x240 coordinates system, the size of the HUD box is scaled up to match the window height
// If the game window is 4:3, this will return the same value.

/*
Example, if the game window is 16:9 at twice the resolution of the HUD:
Calling with X (0,0) will return 8
Calling with Y (1,1) will return 10

. . . x _ _ _ _ _ _ _ . . .
. . . _ y _ _ _ _ _ _ . . .
. . . _ _ _ HUD _ _ _ . . .
. . . _ _ _ _ _ _ _ _ . . .
. . . _ _ _ _ _ _ _ _ . . .
*/
extern "C" int32_t OTRConvertHUDXToScreenX(int32_t v) {
    auto fastWnd = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());
    auto intP = fastWnd->GetInterpreterWeak().lock();

    if (!intP) {
        assert(false && "Lost reference to Fast::Interpreter");
        return v;
    }

    uint32_t gameHeight, gameWidth;
    float gameAspectRatio = fastWnd->GetAspectRatio();
    intP->GetCurDimensions(&gameWidth, &gameHeight);
    float hudAspectRatio = 4.0f / 3.0f;
    int32_t hudHeight = gameHeight;
    int32_t hudWidth = hudHeight * hudAspectRatio;

    float hudScreenRatio = (hudWidth / 320.0f);
    float hudCoord = v * hudScreenRatio;
    float gameOffset = (gameWidth - hudWidth) / 2;
    float gameCoord = hudCoord + gameOffset;
    float gameScreenRatio = (320.0f / gameWidth);
    float screenScaledCoord = gameCoord * gameScreenRatio;
    int32_t screenScaledCoordInt = screenScaledCoord;

    return screenScaledCoordInt;
}

extern "C" void Gfx_RegisterBlendedTexture(const char* name, u8* mask, u8* replacement) {
    if (auto intP = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow())
                        ->GetInterpreterWeak()
                        .lock()) {
        intP->RegisterBlendedTexture(name, mask, replacement);
    } else {
        assert(false && "Lost reference to Fast::Interpreter");
    }
}

extern "C" void Gfx_UnregisterBlendedTexture(const char* name) {
    if (auto intP = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow())
                        ->GetInterpreterWeak()
                        .lock()) {
        intP->UnregisterBlendedTexture(name);
    } else {
        assert(false && "Lost reference to Fast::Interpreter");
    }
}

extern "C" void Gfx_TextureCacheDelete(const uint8_t* texAddr) {
    char* imgName = (char*)texAddr;

    if (texAddr == nullptr) {
        return;
    }

    if (ResourceMgr_OTRSigCheck(imgName)) {
        texAddr = (const uint8_t*)ResourceGetDataByName(imgName);
    }

    if (auto intP = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow())
                        ->GetInterpreterWeak()
                        .lock()) {
        intP->TextureCacheDelete(texAddr);
    } else {
        assert(false && "Lost reference to Fast::Interpreter");
    }
}

extern "C" int AudioPlayer_Buffered(void) {
    return AudioPlayerBuffered();
}

extern "C" int AudioPlayer_GetDesiredBuffered(void) {
    return AudioPlayerGetDesiredBuffered();
}

extern "C" void AudioPlayer_Play(const uint8_t* buf, uint32_t len) {
    AudioPlayerPlayFrame(buf, len);
}

extern "C" int Controller_ShouldRumble(size_t slot) {
    // don't rumble if we don't have rumble mappings
    if (Ship::Context::GetInstance()
            ->GetControlDeck()
            ->GetControllerByPort(static_cast<uint8_t>(slot))
            ->GetRumble()
            ->GetAllRumbleMappings()
            .empty()) {
        return 0;
    }

    // don't rumble if we don't have connected gamepads
    if (Ship::Context::GetInstance()
            ->GetControlDeck()
            ->GetConnectedPhysicalDeviceManager()
            ->GetConnectedSDLGamepadsForPort(slot)
            .empty()) {
        return 0;
    }

    // rumble
    return 1;
}

// ============================================================
// ComboShip exports — 2ship.dll side
// ============================================================

static std::unique_ptr<Ship::ArchiveManager> gMMArchiveManager;

// Opens mm.o2r + 2ship.o2r into a MM-private ArchiveManager (no context, no window).
// This is the "dormant" MM state: archives open, no game loop running.
extern "C" __declspec(dllexport) void MM_InitArchives() {
    std::vector<std::string> archivePaths;

    std::string mmPathO2R = Ship::Context::LocateFileAcrossAppDirs("mm.o2r", appShortName);
    std::string mmPathZIP = Ship::Context::LocateFileAcrossAppDirs("mm.zip", appShortName);
    if (std::filesystem::exists(mmPathO2R)) {
        archivePaths.push_back(mmPathO2R);
    } else if (std::filesystem::exists(mmPathZIP)) {
        archivePaths.push_back(mmPathZIP);
    } else {
        std::string mmPathOtr = Ship::Context::LocateFileAcrossAppDirs("mm.otr", appShortName);
        if (std::filesystem::exists(mmPathOtr)) {
            archivePaths.push_back(mmPathOtr);
        }
    }

    std::string shipO2R = Ship::Context::GetPathRelativeToAppBundle("2ship.o2r");
    if (std::filesystem::exists(shipO2R)) {
        archivePaths.push_back(shipO2R);
    }

    if (!archivePaths.empty()) {
        printf("[2ship] MM_InitArchives: opening %zu archive(s):\n", archivePaths.size());
        for (const auto& p : archivePaths) {
            printf("[2ship]   %s\n", p.c_str());
        }
        gMMArchiveManager = std::make_unique<Ship::ArchiveManager>();
        gMMArchiveManager->Init(archivePaths);
        printf("[2ship] MM archives loaded (dormant).\n");
    } else {
        printf("[2ship] MM_InitArchives: no archives found — MM will remain unloaded.\n");
    }
}

// -1 = normal MM boot; >= 0 = ComboShip game-switch: skip title/file-select and load this slot.
// extern "C" so title_setup.c (a C file) can link to it without name mangling.
extern "C" int gComboStartFileNum = -1;

// C-callable wrapper used by title_setup.c (which is a C file) to load a MM save from disk.
extern "C" void Combo_LoadMMSaveFile(int mmFileNum) {
    SaveManager_LoadSaveFile(mmFileNum);
}

extern "C" void MM_RunMain(void);

// Full MM initialization + game loop, entered after OOT has exited.
// fileNum is the OOT 0-indexed slot; we map it to the same MM slot.
extern "C" __declspec(dllexport) void MM_RunGame(int fileNum) {
    gComboStartFileNum = fileNum;
    MM_RunMain();
}

// ComboShip: read by mm/src/code/main.c's MM_RunMain to skip the blocking game loop. Set only for
// the duration of MM_BootForCombo.
extern "C" int gComboBootOnly = 0;

// ComboShip: eagerly boot MM at OOT startup (called once after SOH_Init) so the cross-world rando
// oracle runs against a real, fully-initialized MM (region graph built by the real ShipInit::InitAll,
// real GameInteractor/AudioCollection/RM) instead of a fragile headless fake. Reuses OOT's shared
// Context (sComboTransitionActive) and runs MM_RunMain's full init while skipping its game loop
// (gComboBootOnly). The caller (ComboShip main) brackets this with SOH_PrepareForTransition (before)
// and MM_PrepareForTransition + SOH_ResumeForeground (after) to hand the foreground back to OOT.
extern "C" __declspec(dllexport) void MM_BootForCombo(void) {
    gComboStartFileNum = -1;        // boot only — no save load / Play jump
    sComboTransitionActive = true;  // OTRGlobals ctor reuses OOT's Context + creates MM's own RM
    gComboBootOnly = 1;
    MM_RunMain();                   // full init; main.c skips Graph_ThreadEntry due to gComboBootOnly
    gComboBootOnly = 0;
}

#ifdef COMBO_BUILD
// Defined in mm/src/code/main.c: re-enters ONLY MM's game loop (no heap/thread re-init).
extern "C" void MM_RunGameLoop(void);
// Defined in mm/src/code/graph.c: resets the frame state machine so MM_RunGameLoop restarts the
// gamestate sequence from Setup instead of resuming into the destroyed post-handoff gamestate.
extern "C" void MM_ResetFrameLoopForResume(void);
// Defined in mm/src/code/main.c: resets MM's system arena so RunFrame's SysCfb_Init + SystemArena_Malloc
// have a fresh arena on resume.
extern "C" void MM_ResetSystemHeapForResume(void);
// Defined in mm/2s2h/z_message_OTR.cpp: rebuilds the message tables whose backing resources were
// freed by the forward (MM->OOT) transition's UnloadResources.
extern "C" void OTRMessage_ResetForResume(void);

// ComboShip OOT->MM forward transition: stop MM audio and tear down MM gui WITHOUT destroying the
// shared context/window/resource-manager (OOT will reuse them). Mirrors SOH_PrepareForTransition.
extern "C" __declspec(dllexport) void MM_PrepareForTransition(void) {
    SaveManager_ThreadPoolWait();
    OTRAudio_Exit();
    // NOTE: do NOT BenGui::Destroy() here. The Gui is a single shared libultraship instance; tearing
    // down its windows forces the resuming game's SetupGuiElements to RE-CREATE them, which re-registers
    // SaveManager load functions and asserts (AddLoadFunction: duplicate). The shared Gui persists across
    // transitions; each game's windows are set up once at its first boot.
    // Context, window, and resource manager are intentionally kept alive for OOT to reuse.
}

// ComboShip OOT->MM return: re-enter MM's game loop on the SAME shared context/window, swap archives
// back to MM, reload resources, and jump straight to Play in South Clock Town for the given slot.
// Counterpart to OOT's SOH_ResumeGame in soh/soh/OTRGlobals.cpp.
extern "C" __declspec(dllexport) void MM_ResumeGame(int fileNum) {
    auto ctx = Ship::Context::GetInstance();
    ctx->GetLogger()->flush_on(spdlog::level::trace);
    SPDLOG_INFO("[ComboShip] MM_ResumeGame: begin (fileNum={})", fileNum);

    // 1. Re-activate MM's own ResourceManager (created at MM's first boot, kept resident the whole
    //    time OOT was running). Its archives + factories + resource cache never went away, so there's
    //    nothing to swap, unload, re-register, or reset (message tables/audio samples stay valid).
    ctx->SetResourceManager(sMMResourceManager);

    // 2. Restart MM's audio thread (MM_PrepareForTransition stopped it). Soundfonts/samples are still
    //    resident in MM's RM, so it resumes against valid data with no reload/heap reset.
    OTRAudio_Init(); // counterpart to OTRAudio_Exit() in MM_PrepareForTransition

    // 4. Re-sync this DLL's ImGui current-context (GImGui is a per-module static). Do NOT re-run
    //    BenGui::SetupGuiElements() — the shared Gui's windows persist from MM's first boot;
    //    re-creating them would re-register SaveManager load functions and assert.
    ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());

    // ComboShip: re-activate MM's menu in the shared Gui's single menu slot (OOT set it back to its
    // SohMenu while it was the active game). BenMenu persists (BenGui::Destroy isn't called in combo).
    BenGui::ActivateMenu(); // ComboShip: no-op under COMBO_BUILD (comboui owns the menu)

    // 5. Re-arm the shared window so MM's `while (WindowIsRunning())` loop runs instead of returning
    //    immediately (OOT cleared mIsRunning when its loop exited).
    if (auto fast3d = std::dynamic_pointer_cast<Fast::Fast3dWindow>(ctx->GetWindow())) {
        fast3d->SetIsRunning(true);
    }

    // 6. Hand off to MM's boot path: title_setup.c's Setup_InitImpl loads the save, sets the South
    //    Clock Town entrance, and jumps straight to Play when gComboStartFileNum >= 0.
    gComboStartFileNum = fileNum;
    // Reset MM's system arena: RunFrame's state-0 path re-runs SysCfb_Init + SystemArena_Malloc, which
    // need a fresh arena (MM_RunGameLoop skips MM_RunMain's SystemHeap_Init). Without this,
    // SystemArena_Malloc returns a bad pointer and RunFrame crashes in memset.
    MM_ResetSystemHeapForResume();
    MM_ResetFrameLoopForResume();
    SPDLOG_INFO("[ComboShip] MM_ResumeGame: entering MM loop (gComboStartFileNum={})", gComboStartFileNum);

    // 7. Re-run MM's game loop (returns when the shared window's running flag is cleared again).
    MM_RunGameLoop();
    SPDLOG_INFO("[ComboShip] MM_ResumeGame: MM loop RETURNED");
}
#endif

// Initializes a default MM save for the given OOT file slot (0-indexed) and writes it to disk.
// Called by ComboShip when OOT creates a new save, so MM has a matching save ready for the transition.
extern "C" __declspec(dllexport) void MM_InitSaveFile(int fileNum) {
    // fileNum is OOT's 0-indexed slot; MM save files are 1-indexed (file1.json, file2.json, file3.json)
    SaveManager_InitNewSaveForSlot(fileNum + 1);
}

// ComboShip Inc2 (Task 4): create a RANDO MM save for the given OOT slot from a combo placement slice.
// placementJson is the "mm" object of the combined spoiler: { "<RC_name>": "<itemSpoilerName>", ... }.
// We do NOT run MM's own generator (GeneratePools/logic) — the combo layer owns placement. Instead we
// build the playable combo baseline (SaveManager_InitNewSaveForSlot, post-first-cycle Human Link in
// South Clock Town), mark the save SAVETYPE_RANDO, and feed the placement through the existing
// Rando::Spoiler::ApplyToSaveContext path (which writes randoSaveChecks). Headless-safe: never calls
// GrantStartingItems / Item_Give (those need gPlayState). Falls back to a vanilla save on any error.
extern "C" __declspec(dllexport) void MM_InitRandoSaveFile(int fileNum, const char* placementJson) {
    // Playable combo baseline first (Human Link, South Clock Town, ocarina/songs, etc.).
    SaveManager_InitNewSaveForSlot(fileNum + 1);
    // Sram_InitNewSave (inside the call above) resets fileNum; restore it so SaveManager_SaveCurrentForCombo
    // re-writes the correct slot (it targets gSaveContext.fileNum + 1).
    gSaveContext.fileNum = (s16)fileNum;

    if (!placementJson || placementJson[0] == '\0') {
        SPDLOG_WARN("[ComboShip] MM_InitRandoSaveFile: no placement for slot {}; left vanilla MM save", fileNum);
        return;
    }

    // Mark the save as rando and zero the rando struct (mirrors Rando::MiscBehavior::OnFileCreate).
    gSaveContext.save.shipSaveInfo.saveType = SAVETYPE_RANDO;
    memset(&gSaveContext.save.shipSaveInfo.rando, 0, sizeof(gSaveContext.save.shipSaveInfo.rando));
    memcpy(&gSaveContext.save.shipSaveInfo.rando.foundDungeonKeys,
           &gSaveContext.save.saveInfo.inventory.dungeonKeys,
           sizeof(gSaveContext.save.saveInfo.inventory.dungeonKeys));

    try {
        // ApplyToSaveContext consumes a full MM spoiler: it requires finalSeed, options, startingItems
        // and checks keys (startingItems and finalSeed throw if absent). For the no-logic native phase
        // we supply empty options/startingItems (defaults) and feed the combo placement as "checks".
        nlohmann::json spoiler;
        spoiler["finalSeed"] = (uint32_t)0; // Phase 1: not used for runtime delivery (driven by randoSaveChecks)
        // ComboShip: persist the player's chosen MM options into the save (mirrors OnFileCreate) so MM
        // honors its toggles at runtime; an empty options object would make ApplyToSaveContext default
        // everything (the analog of OOT's SetAllToContext fix).
        nlohmann::json options = nlohmann::json::object();
        for (auto& [id, opt] : Rando::StaticData::Options) {
            options[opt.name] = (uint32_t)CVarGetInteger(opt.cvar, opt.defaultValue);
        }
        spoiler["options"] = options;
        spoiler["startingItems"] = nlohmann::json::array();
        spoiler["checks"] = nlohmann::json::parse(placementJson); // { "RC_*": "<spoilerName>", ... }

        Rando::Spoiler::ApplyToSaveContext(spoiler);

        // ComboShip: record the chosen starting items in the save (mirrors OnFileCreate). NOTE: we do
        // NOT call Rando::GrantStartingItems() — it needs gPlayState (Item_Give) and this runs headless,
        // so starting items are stored in the rando struct but not pushed into inventory here.
        {
            auto startingItems = Rando::GetStartingItemsFromConfig();
            Rando::SetStartingItemsInSave(gSaveContext.save.shipSaveInfo.rando, startingItems);
        }

        // The two always-eligible starting checks (mirrors OnFileCreate tail).
        RANDO_SAVE_CHECKS[RC_STARTING_ITEM_DEKU_MASK].eligible = true;
        RANDO_SAVE_CHECKS[RC_STARTING_ITEM_SONG_OF_HEALING].eligible = true;

        SPDLOG_INFO("[ComboShip] MM_InitRandoSaveFile: applied {} placements for slot {}",
                    spoiler["checks"].size(), fileNum);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[ComboShip] MM_InitRandoSaveFile: {} — falling back to vanilla save for slot {}",
                     e.what(), fileNum);
        gSaveContext.save.shipSaveInfo.saveType = SAVETYPE_VANILLA;
    }

    // Persist the (rando) save to the slot file.
    SaveManager_SaveCurrentForCombo();
}

// Returns the number of archives open in the MM-private ArchiveManager.
// 0 means MM_InitArchives was not called or found no files.
extern "C" __declspec(dllexport) int MM_ArchiveCount() {
    if (!gMMArchiveManager) return 0;
    auto archives = gMMArchiveManager->GetArchives();
    return archives ? static_cast<int>(archives->size()) : 0;
}

#if not defined(__SWITCH__) && not defined(__WIIU__)
extern "C" __declspec(dllexport) bool MM_Extract(const char* searchPath) {
    std::string path = searchPath ? searchPath : std::filesystem::current_path().string();
    std::string installPath = Ship::Context::GetAppBundlePath();

    // Guard: check assets folder exists before attempting extraction
    if (!std::filesystem::exists(installPath + "/assets")) {
        Extractor::ShowErrorBox(
            "Extractor assets not found",
            "No game O2R file found. Missing assets folder needed to generate O2R file.\n\nExiting...");
        return false;
    }

    Extractor extract;
    if (!extract.Run(path)) {
        return false;
    }
    // Upstream merge: CallZapd gained two atomic progress counters (extracted / total).
    std::atomic<size_t> extractCount = 0, totalExtract = 0;
    if (!extract.CallZapd(installPath, path, &extractCount, &totalExtract)) {
        Extractor::ShowErrorBox("Extraction failed",
                                "ROM extraction failed. Check the console window for details.\n\nExiting...");
        return false;
    }
    return true;
}
#endif

// ComboShip Inc2: headless dump of MM rando tables (checks + items).
// ComboShip Inc7: scoped to the current settings via Rando::Logic::GeneratePools — mirrors
// RefreshMetrics() in Rando/Menu.cpp. Only checks that the current CVars actually shuffle
// are emitted, so the cross-world fill sees the same pool as MM's own generator would use.
// Cache removed (was static/permanent): result now depends on live CVar state so it must
// recompute every call.
// Caller MUST invoke this AFTER SOH_Init() returns (so the Context + logger exist
// in the shared libultraship.dll, and CVars are loaded).
extern "C" __declspec(dllexport) const char* MM_DumpRandoStaticData(void) {
    static std::string cached;

    nlohmann::json checks = nlohmann::json::array();
    nlohmann::json items  = nlohmann::json::array();

    // Build a RandoSaveInfo from current CVars — same pattern as Menu.cpp RefreshMetrics().
    RandoSaveInfo saveInfo;
    for (auto& [id, opt] : Rando::StaticData::Options) {
        saveInfo.randoSaveOptions[id] = (uint32_t)CVarGetInteger(opt.cvar, opt.defaultValue);
    }
    auto startingItems = Rando::GetStartingItemsFromConfig();
    Rando::SetStartingItemsInSave(saveInfo, startingItems);

    std::vector<RandoCheckId> checkPool;
    std::vector<RandoItemId> itemPool;
    Rando::Logic::GeneratePools(saveInfo, checkPool, itemPool);

    // ComboShip canary: count every reason a pool check fails to emit (see debug-mmdump.json
    // write below). An empty/near-empty pool here silently kills cross-game placement — that
    // exact failure hid for weeks behind the fill's place-anywhere fallback (see UPSTREAM_MERGES.md
    // "eager-boot export" entry), so keep this cheap per-Generate diagnostic.
    int skippedNoStatic = 0, skippedNoName = 0, noVanillaItem = 0;

    // Emit only the checks in the settings-scoped pool.
    for (RandoCheckId id : checkPool) {
        auto chkIt = Rando::StaticData::Checks.find(id);
        if (chkIt == Rando::StaticData::Checks.end()) { skippedNoStatic++; continue; }
        const auto& chk = chkIt->second;
        if (!chk.name || chk.name[0] == '\0') { skippedNoName++; continue; }
        nlohmann::json entry = { {"name", chk.name} };

        // MM stores vanilla item per check via randoItemId.
        if (chk.randoItemId != RI_UNKNOWN) {
            auto it = Rando::StaticData::Items.find(chk.randoItemId);
            if (it != Rando::StaticData::Items.end() &&
                it->second.spoilerName && it->second.spoilerName[0] != '\0') {
                entry["vanillaItem"] = it->second.spoilerName;
            }
        }
        if (!entry.contains("vanillaItem")) noVanillaItem++;
        checks.push_back(std::move(entry));
    }

    // ComboShip canary: written to a file because 2ship.dll's spdlog default logger is never
    // configured in combo (shared context owns logging in soh's module), so SPDLOG_* here goes
    // nowhere. regions==0 here means MM's eager boot / ShipInit::InitAll didn't run.
    try {
        std::error_code ec;
        std::filesystem::create_directories("saves/combo", ec);
        nlohmann::json diag = {
            { "regions", Rando::Logic::Regions.size() },
            { "staticChecks", Rando::StaticData::Checks.size() },
            { "options", Rando::StaticData::Options.size() },
            { "logicOpt", (uint32_t)saveInfo.randoSaveOptions[RO_LOGIC] },
            { "checkPool", checkPool.size() },
            { "itemPool", itemPool.size() },
            { "emitted", checks.size() },
            { "skippedNoStatic", skippedNoStatic },
            { "skippedNoName", skippedNoName },
            { "noVanillaItem", noVanillaItem },
        };
        std::ofstream("saves/combo/debug-mmdump.json", std::ios::trunc) << diag.dump(2);
    } catch (...) {}

    for (auto& [id, item] : Rando::StaticData::Items) {
        if (!item.spoilerName || item.spoilerName[0] == '\0') continue;
        // ComboShip: "name" MUST stay spoilerName (RI_*) — grant lookup keys on it. "displayName"
        // is the human string (StaticData's unused .name field) for toasts/shops in the OTHER game.
        nlohmann::json entry = { {"name", item.spoilerName} };
        if (item.name && item.name[0] != '\0') {
            entry["displayName"] = item.name;
        }
        items.push_back(std::move(entry));
    }

    cached = nlohmann::json{ {"checks", std::move(checks)}, {"items", std::move(items)} }.dump();
    return cached.c_str();
}

// ComboShip Inc4: MM reachability oracle — headless logic engine wrappers.
// The combined fill drives these to query "given owned items, which checks are reachable?"

static SaveContext sMM_OracleSavedContext;
static uint64_t sMM_OracleSavedRegionTime;
using Rando::Logic::gCurrentRegionTime;

// Headless item-give: sets gSaveContext fields without ever touching gPlayState.
// Covers the save-context mutations that logic conditions read (INV_CONTENT, equipment,
// quest items, rando flags, dungeon items, week event regs). Derived from GiveItem.cpp.
static void GiveItemForOracle(RandoItemId ri) {
    switch (ri) {
        case RI_JUNK:
        case RI_NONE:
        case RI_TRAP:
            break;

        // Magic
        case RI_SINGLE_MAGIC:
            gSaveContext.save.saveInfo.playerData.isMagicAcquired = true;
            gSaveContext.save.saveInfo.playerData.magic = MAGIC_NORMAL_METER;
            SET_WEEKEVENTREG(WEEKEVENTREG_12_80);
            break;
        case RI_DOUBLE_MAGIC:
            gSaveContext.save.saveInfo.playerData.isMagicAcquired = true;
            gSaveContext.save.saveInfo.playerData.isDoubleMagicAcquired = true;
            gSaveContext.save.saveInfo.playerData.magic = MAGIC_DOUBLE_METER;
            SET_WEEKEVENTREG(WEEKEVENTREG_12_80);
            break;
        case RI_DOUBLE_DEFENSE:
            gSaveContext.save.saveInfo.playerData.doubleDefense = true;
            gSaveContext.save.saveInfo.inventory.defenseHearts = 20;
            break;

        // Swords — set equipment value (logic checks GET_CUR_EQUIP_VALUE)
        case RI_SWORD_KOKIRI:
            SET_EQUIP_VALUE(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_KOKIRI);
            break;
        case RI_SWORD_RAZOR:
            SET_EQUIP_VALUE(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_RAZOR);
            break;
        case RI_SWORD_GILDED:
            SET_EQUIP_VALUE(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_GILDED);
            break;

        // Bomb bags — set upgrade + inventory
        case RI_BOMB_BAG_20:
            Inventory_ChangeUpgrade(UPG_BOMB_BAG, 1);
            INV_CONTENT(ITEM_BOMB) = ITEM_BOMB;
            INV_CONTENT(ITEM_BOMBCHU) = ITEM_BOMBCHU;
            AMMO(ITEM_BOMB) = 20;
            break;
        case RI_BOMB_BAG_30:
            Inventory_ChangeUpgrade(UPG_BOMB_BAG, 2);
            INV_CONTENT(ITEM_BOMB) = ITEM_BOMB;
            INV_CONTENT(ITEM_BOMBCHU) = ITEM_BOMBCHU;
            AMMO(ITEM_BOMB) = 30;
            break;
        case RI_BOMB_BAG_40:
            Inventory_ChangeUpgrade(UPG_BOMB_BAG, 3);
            INV_CONTENT(ITEM_BOMB) = ITEM_BOMB;
            INV_CONTENT(ITEM_BOMBCHU) = ITEM_BOMBCHU;
            AMMO(ITEM_BOMB) = 40;
            break;

        // Wallets
        case RI_WALLET_ADULT:
            Inventory_ChangeUpgrade(UPG_WALLET, 1);
            break;
        case RI_WALLET_GIANT:
            Inventory_ChangeUpgrade(UPG_WALLET, 2);
            break;
        case RI_WALLET_TYCOON:
            Inventory_ChangeUpgrade(UPG_WALLET, 3);
            break;

        // Heart pieces/containers — logic doesn't check these for reachability, but include for completeness
        case RI_HEART_CONTAINER:
            gSaveContext.save.saveInfo.playerData.healthCapacity += 0x10;
            break;
        case RI_HEART_PIECE:
            gSaveContext.save.saveInfo.playerData.healthCapacity += 0x10;
            break;

        // Bottle
        case RI_BOTTLE_RED_POTION: {
            for (int i = SLOT(ITEM_BOTTLE); i < SLOT(ITEM_BOTTLE) + 6; i++) {
                if (gSaveContext.save.saveInfo.inventory.items[i] == ITEM_NONE) {
                    gSaveContext.save.saveInfo.inventory.items[i] = ITEM_POTION_RED;
                    break;
                }
            }
            break;
        }

        // Dungeon items
        case RI_WOODFALL_BOSS_KEY: case RI_WOODFALL_MAP: case RI_WOODFALL_COMPASS:
            SET_DUNGEON_ITEM(Rando::StaticData::Items[ri].itemId - ITEM_KEY_BOSS, DUNGEON_SCENE_INDEX_WOODFALL_TEMPLE);
            break;
        case RI_SNOWHEAD_BOSS_KEY: case RI_SNOWHEAD_MAP: case RI_SNOWHEAD_COMPASS:
            SET_DUNGEON_ITEM(Rando::StaticData::Items[ri].itemId - ITEM_KEY_BOSS, DUNGEON_SCENE_INDEX_SNOWHEAD_TEMPLE);
            break;
        case RI_GREAT_BAY_BOSS_KEY: case RI_GREAT_BAY_MAP: case RI_GREAT_BAY_COMPASS:
            SET_DUNGEON_ITEM(Rando::StaticData::Items[ri].itemId - ITEM_KEY_BOSS, DUNGEON_SCENE_INDEX_GREAT_BAY_TEMPLE);
            break;
        case RI_STONE_TOWER_BOSS_KEY: case RI_STONE_TOWER_MAP: case RI_STONE_TOWER_COMPASS:
            SET_DUNGEON_ITEM(Rando::StaticData::Items[ri].itemId - ITEM_KEY_BOSS, DUNGEON_SCENE_INDEX_STONE_TOWER_TEMPLE);
            break;

        // Small keys
        case RI_WOODFALL_SMALL_KEY:
            DUNGEON_KEY_COUNT(DUNGEON_SCENE_INDEX_WOODFALL_TEMPLE) = std::max(0, (int)DUNGEON_KEY_COUNT(DUNGEON_SCENE_INDEX_WOODFALL_TEMPLE)) + 1;
            break;
        case RI_SNOWHEAD_SMALL_KEY:
            DUNGEON_KEY_COUNT(DUNGEON_SCENE_INDEX_SNOWHEAD_TEMPLE) = std::max(0, (int)DUNGEON_KEY_COUNT(DUNGEON_SCENE_INDEX_SNOWHEAD_TEMPLE)) + 1;
            break;
        case RI_GREAT_BAY_SMALL_KEY:
            DUNGEON_KEY_COUNT(DUNGEON_SCENE_INDEX_GREAT_BAY_TEMPLE) = std::max(0, (int)DUNGEON_KEY_COUNT(DUNGEON_SCENE_INDEX_GREAT_BAY_TEMPLE)) + 1;
            break;
        case RI_STONE_TOWER_SMALL_KEY:
            DUNGEON_KEY_COUNT(DUNGEON_SCENE_INDEX_STONE_TOWER_TEMPLE) = std::max(0, (int)DUNGEON_KEY_COUNT(DUNGEON_SCENE_INDEX_STONE_TOWER_TEMPLE)) + 1;
            break;

        // Stray fairies
        case RI_CLOCK_TOWN_STRAY_FAIRY: SET_WEEKEVENTREG(WEEKEVENTREG_08_80); break;
        case RI_WOODFALL_STRAY_FAIRY: gSaveContext.save.saveInfo.inventory.strayFairies[DUNGEON_SCENE_INDEX_WOODFALL_TEMPLE]++; break;
        case RI_SNOWHEAD_STRAY_FAIRY: gSaveContext.save.saveInfo.inventory.strayFairies[DUNGEON_SCENE_INDEX_SNOWHEAD_TEMPLE]++; break;
        case RI_GREAT_BAY_STRAY_FAIRY: gSaveContext.save.saveInfo.inventory.strayFairies[DUNGEON_SCENE_INDEX_GREAT_BAY_TEMPLE]++; break;
        case RI_STONE_TOWER_STRAY_FAIRY: gSaveContext.save.saveInfo.inventory.strayFairies[DUNGEON_SCENE_INDEX_STONE_TOWER_TEMPLE]++; break;

        // Rando-flag items (deeds, keys, letters, etc.)
        case RI_MOONS_TEAR: Flags_SetRandoInf(RANDO_INF_OBTAINED_MOONS_TEAR); break;
        case RI_DEED_LAND: Flags_SetRandoInf(RANDO_INF_OBTAINED_DEED_LAND); break;
        case RI_DEED_SWAMP: Flags_SetRandoInf(RANDO_INF_OBTAINED_DEED_SWAMP); break;
        case RI_DEED_MOUNTAIN: Flags_SetRandoInf(RANDO_INF_OBTAINED_DEED_MOUNTAIN); break;
        case RI_DEED_OCEAN: Flags_SetRandoInf(RANDO_INF_OBTAINED_DEED_OCEAN); break;
        case RI_ROOM_KEY: Flags_SetRandoInf(RANDO_INF_OBTAINED_ROOM_KEY); break;
        case RI_LETTER_TO_MAMA: Flags_SetRandoInf(RANDO_INF_OBTAINED_LETTER_TO_MAMA); break;
        case RI_LETTER_TO_KAFEI: Flags_SetRandoInf(RANDO_INF_OBTAINED_LETTER_TO_KAFEI); break;
        case RI_PENDANT_OF_MEMORIES: Flags_SetRandoInf(RANDO_INF_OBTAINED_PENDANT_OF_MEMORIES); break;
        case RI_POWDER_KEG: Flags_SetWeekEventReg(WEEKEVENTREG_HAS_POWDERKEG_PRIVILEGES); break;
        case RI_GREAT_SPIN_ATTACK: SET_WEEKEVENTREG(WEEKEVENTREG_RECEIVED_GREAT_SPIN_ATTACK); break;
        case RI_ABILITY_SWIM: Flags_SetRandoInf(RANDO_INF_OBTAINED_SWIM); break;

        // Ocarina buttons
        case RI_OCARINA_BUTTON_A:
        case RI_OCARINA_BUTTON_C_DOWN:
        case RI_OCARINA_BUTTON_C_LEFT:
        case RI_OCARINA_BUTTON_C_RIGHT:
        case RI_OCARINA_BUTTON_C_UP:
            Flags_SetRandoInf(RANDO_INF_OBTAINED_OCARINA_BUTTON_A + (ri - RI_OCARINA_BUTTON_A));
            break;

        // Songs (song double/inverted time)
        case RI_SONG_DOUBLE_TIME: Flags_SetRandoInf(RANDO_INF_OBTAINED_SONG_DOUBLE_TIME); break;
        case RI_SONG_INVERTED_TIME: Flags_SetRandoInf(RANDO_INF_OBTAINED_SONG_INVERTED_TIME); break;

        // Clock items
        case RI_TIME_DAY_1: case RI_TIME_NIGHT_1:
        case RI_TIME_DAY_2: case RI_TIME_NIGHT_2:
        case RI_TIME_DAY_3: case RI_TIME_NIGHT_3: {
            int index = Rando::ClockItems::GetHalfDayIndexFromClockItem(ri);
            if (index != Rando::ClockItems::INVALID) {
                Flags_SetRandoInf(static_cast<RandoInf>(RANDO_INF_OBTAINED_CLOCK_DAY_1 + index));
            }
            break;
        }
        case RI_TIME_PROGRESSIVE: {
            RandoItemId concrete = Rando::ConvertItem(RI_TIME_PROGRESSIVE);
            if (concrete != RI_JUNK) GiveItemForOracle(concrete);
            break;
        }

        // Souls
        case RI_SOUL_BOSS_GOHT: case RI_SOUL_BOSS_GYORG: case RI_SOUL_BOSS_MAJORA:
        case RI_SOUL_BOSS_ODOLWA: case RI_SOUL_BOSS_TWINMOLD:
            Flags_SetRandoInf(SOUL_RI_TO_RANDO_INF(ri));
            break;

        // Progressive items — convert then recurse
        case RI_PROGRESSIVE_MAGIC:
        case RI_PROGRESSIVE_BOW:
        case RI_PROGRESSIVE_BOMB_BAG:
        case RI_PROGRESSIVE_LULLABY:
        case RI_PROGRESSIVE_SWORD:
        case RI_PROGRESSIVE_WALLET:
            GiveItemForOracle(Rando::ConvertItem(ri));
            break;

        // Frogs
        case RI_FROG_BLUE: SET_WEEKEVENTREG(WEEKEVENTREG_33_01); break;
        case RI_FROG_CYAN: SET_WEEKEVENTREG(WEEKEVENTREG_32_40); break;
        case RI_FROG_PINK: SET_WEEKEVENTREG(WEEKEVENTREG_32_80); break;
        case RI_FROG_WHITE: SET_WEEKEVENTREG(WEEKEVENTREG_33_02); break;

        // GS tokens
        case RI_GS_TOKEN_SWAMP:
            SET_QUEST_ITEM(QUEST_QUIVER);
            Inventory_IncrementSkullTokenCount(SCENE_KINSTA1);
            break;
        case RI_GS_TOKEN_OCEAN:
            SET_QUEST_ITEM(QUEST_QUIVER);
            Inventory_IncrementSkullTokenCount(SCENE_KINDAN2);
            break;

        default: {
            // Standard items: set inventory slot directly (what HAS_ITEM checks).
            auto it = Rando::StaticData::Items.find(ri);
            if (it != Rando::StaticData::Items.end()) {
                u8 itemId = it->second.itemId;
                if (itemId != ITEM_NONE && itemId < ITEM_FD) {
                    INV_CONTENT(itemId) = itemId;
                    // Songs go in quest items too
                    if (itemId >= ITEM_SONG_SONATA && itemId <= ITEM_SONG_SUN) {
                        SET_QUEST_ITEM(QUEST_SONG_SONATA + (itemId - ITEM_SONG_SONATA));
                    }
                }
            }
            break;
        }
    }
}

extern "C" __declspec(dllexport) void Combo_MM_Rando_Reset(void) {
    // ComboShip: MM's region graph + static data are now built by the real eager boot
    // (MM_BootForCombo -> ShipInit::InitAll), replacing the deleted headless MM_InitRandoLogic
    // warm-up; the oracle no longer needs a lazy init here.
    memcpy(&sMM_OracleSavedContext, &gSaveContext, sizeof(SaveContext));
    sMM_OracleSavedRegionTime = gCurrentRegionTime;
    memset(&gSaveContext, 0, sizeof(SaveContext));
}

extern "C" __declspec(dllexport) void Combo_MM_Rando_SetOwnedItems(const char* itemNamesJson) {
    if (!itemNamesJson) return;
    try {
        auto items = nlohmann::json::parse(itemNamesJson);
        for (const auto& name : items) {
            std::string itemStr = name.get<std::string>();
            for (auto& [id, item] : Rando::StaticData::Items) {
                if (item.spoilerName && itemStr == item.spoilerName) {
                    GiveItemForOracle(id);
                    break;
                }
            }
        }
    } catch (...) {}
}

extern "C" __declspec(dllexport) const char* Combo_MM_Rando_GetReachableChecks(void) {
    static std::string buf;

    std::set<RandoRegionId> reachable = { RR_MAX };
    auto timeStates = Rando::Logic::InitializeRegionTimeStates(RR_MAX);

    bool changed = true;
    while (changed) {
        size_t prevSize = reachable.size();
        for (auto regionId : std::set<RandoRegionId>(reachable)) {
            Rando::Logic::FindReachableRegions(regionId, reachable, timeStates);
        }
        changed = (reachable.size() != prevSize);
    }

    nlohmann::json out = nlohmann::json::array();
    for (RandoRegionId regionId : reachable) {
        auto regIt = Rando::Logic::Regions.find(regionId);
        if (regIt == Rando::Logic::Regions.end()) continue;
        auto& region = regIt->second;

        auto tsIt = timeStates.find(regionId);
        if (tsIt != timeStates.end()) {
            gCurrentRegionTime = tsIt->second.timeSlices;
        }

        for (auto& [checkId, checkLogic] : region.checks) {
            if (checkLogic.first()) {
                auto chkIt = Rando::StaticData::Checks.find(checkId);
                if (chkIt != Rando::StaticData::Checks.end() && chkIt->second.name) {
                    out.push_back(chkIt->second.name);
                }
            }
        }
    }

    buf = out.dump();
    return buf.c_str();
}

extern "C" __declspec(dllexport) void Combo_MM_Rando_PlaceItem(
    const char* checkName, const char* itemName) {
    if (!checkName || !itemName) return;
    for (auto& [id, chk] : Rando::StaticData::Checks) {
        if (chk.name && std::string(chk.name) == checkName) {
            for (auto& [ri, item] : Rando::StaticData::Items) {
                if (item.spoilerName && std::string(item.spoilerName) == itemName) {
                    RANDO_SAVE_CHECKS[id].randoItemId = ri;
                    RANDO_SAVE_CHECKS[id].shuffled = true;
                    return;
                }
            }
            return;
        }
    }
}

extern "C" __declspec(dllexport) void Combo_MM_Rando_Restore(void) {
    memcpy(&gSaveContext, &sMM_OracleSavedContext, sizeof(SaveContext));
    gCurrentRegionTime = sMM_OracleSavedRegionTime;
}

#ifdef COMBO_BUILD
// ComboShip: MM analog of SOH_ExportMenu et al. (soh/soh/OTRGlobals.cpp). comboui resolves these by
// GetProcAddress and ingests the CwMenu (combo/menu/ComboMenuABI.h), then invokes back by index.
namespace {
// Ensure mBenMenu exists; returns it (or nullptr if it couldn't be built).
std::shared_ptr<BenGui::BenMenu> Combo_EnsureBenMenu() {
    auto menu = BenGui::GetBenMenu();
    if (!menu) {
        BenGui::ActivateMenu();
        menu = BenGui::GetBenMenu();
    }
    // MM populates its menu tree (AddSettings/AddEnhancements/AddDevTools) and disabledMap in
    // BenMenu::InitElement() — NOT in the constructor. comboui owns the menu slot so the Gui loop
    // never Init()s this menu, leaving menuEntries empty and ExportComboMenu walking nothing (empty
    // MM tab). Init() here (idempotent) before any export/walk. OOT differs: it calls
    // AddMenuElements() explicitly at boot, so SOH_ExportMenu needs no Init.
    if (menu) {
        menu->Init();
    }
    // ComboShip: MM's rando Seed combobox reads Rando::Spoiler::spoilerOptions, which is empty if the
    // rando subsystem's RefreshOptions hasn't populated it in this (combo/backgrounded) context. Populate
    // it on demand so the always-available rando menu renders. RefreshOptions is idempotent (clears+repopulates).
    if (Rando::Spoiler::spoilerOptions.empty()) {
        Rando::Spoiler::RefreshOptions();
    }
    return menu;
}
} // namespace

// 2ship.dll has its own per-module ImGui GImGui — see combo/menu/ComboMenuSharedContext.h.

extern "C" __declspec(dllexport) const CwMenu* MM_ExportMenu(void) {
    ComboMenuContext::UseSharedImGuiContext();
    auto menu = Combo_EnsureBenMenu();
    return menu ? menu->ExportComboMenu() : nullptr;
}

extern "C" __declspec(dllexport) void MM_MenuInvokeCallback(int32_t i) {
    ComboMenuContext::UseSharedImGuiContext();
    if (auto menu = Combo_EnsureBenMenu()) {
        menu->InvokeCallbackByIndex(i);
    }
}

extern "C" __declspec(dllexport) int32_t MM_MenuEvalDisabled(int32_t i, const char** outReason) {
    ComboMenuContext::UseSharedImGuiContext();
    auto menu = Combo_EnsureBenMenu();
    return menu ? menu->EvalDisabledByIndex(i, outReason) : 0;
}

extern "C" __declspec(dllexport) void MM_MenuDrawCustom(int32_t i) {
    // comboui owns the active menu slot, so the Gui loop never drives MM's menu. A custom widget may read
    // THEME_COLOR (menuThemeIndex), which is set in UpdateElement(); skipping Update() makes ColorValues.at()
    // throw out_of_range (proven by the Phase 0 spike). So Init()+Update() before any custom draw.
    ComboMenuContext::UseSharedImGuiContext();
    auto menu = Combo_EnsureBenMenu();
    if (menu) {
        menu->Init();
        menu->Update();
        menu->DrawCustomByIndex(i);
    }
}
#endif

// Helper to redirect the user to the boot screen in place of known console crash scenarios, and emits a notification
extern "C" bool Ship_HandleConsoleCrashAsReset() {
    // If fix crashes is on, return false and let fallback handling process in source
    if (CVarGetInteger("gEnhancements.Fixes.ConsoleCrashes", 1)) {
        return false;
    }

    std::reinterpret_pointer_cast<Ship::ConsoleWindow>(
        Ship::Context::GetInstance()->GetWindow()->GetGui()->GetGuiWindow("Console"))
        ->Dispatch("reset");

    Notification::Emit({
        .itemIcon = "__OTR__icon_item_24_static_yar/gQuestIconGoldSkulltulaTex",
        .message = "Crash prevented!",
        .remainingTime = 10.0f,
    });

    return true;
}
