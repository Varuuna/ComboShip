#include "OTRGlobals.h"
#include "OTRAudio.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>
#include <chrono>
#include <optional>
#include <imgui.h>

#include "ResourceManagerHelpers.h"
#include <fast/Fast3dWindow.h>
#include <ship/Context.h>
#include <ship/resource/CrossRMRegistry.h>
#include <ship/resource/File.h>
#include <ship/window/Window.h>
#include <soh/GameVersions.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include "Enhancements/gameconsole.h"
#ifdef _WIN32
#include <Windows.h>
#else
#include <time.h>
#endif
#include <ship/audio/AudioPlayer.h>
#include "Enhancements/speechsynthesizer/SpeechSynthesizer.h"
#include "Enhancements/controls/SohInputEditorWindow.h"
#include "Enhancements/audio/AudioCollection.h"
#include "Enhancements/debugconsole.h"
#include "Enhancements/randomizer/randomizer.h"
#include "Enhancements/randomizer/3drando/spoiler_log.hpp" // ComboShip: GenerateHash() for seed-hash icons
#include "Enhancements/randomizer/randomizer_entrance_tracker.h"
#include "Enhancements/randomizer/randomizer_check_tracker.h"
#include "Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/randomizer/settings.h"
#include "soh/Enhancements/randomizer/logic.h"
#include "soh/Enhancements/randomizer/3drando/fill.hpp"
#include "soh/Enhancements/randomizer/3drando/shops.hpp"
#include "soh/Enhancements/randomizer/3drando/random.hpp"
#include "soh/Enhancements/randomizer/location_access.h"
#include "soh/Enhancements/randomizer/3drando/item_pool.hpp"
#include "soh/Enhancements/randomizer/3drando/starting_inventory.hpp"
#include "Enhancements/gameplaystats.h"
#include "soh/Enhancements/savestates.h"
#include "frame_interpolation.h"
#include "SohGui/SohMenu.h"
#include "SohGui/SohGui.hpp"
#include "variables.h"
#include "z64.h"
#include "macros.h"
#include <ship/window/gui/Fonts.h>
#include <ship/window/FileDropMgr.h>
#include <ship/window/gui/resource/Font.h>
#include <ship/utils/StringHelper.h>
#include "Enhancements/custom-message/CustomMessageManager.h"
#include "util.h"

#include <fast/Fast3dGui.h>
#include <fast/debug/GfxDebugger.h>

#if not defined(__SWITCH__) && not defined(__WIIU__)
#include "Extractor/Extract.h"
#endif

#include <fast/interpreter.h>

#ifdef __APPLE__
#include <SDL_scancode.h>
#else
#include <SDL2/SDL_scancode.h>
#endif

#ifdef __SWITCH__
#include <port/switch/SwitchImpl.h>
#elif defined(__WIIU__)
#include <port/wiiu/WiiUImpl.h>
#include <coreinit/debug.h> // OSFatal
#endif

#include <functions.h>
#include "Enhancements/item-tables/ItemTableManager.h"
#include "Enhancements/Lang/Lang.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/ImGuiUtils.h"
#include "ActorDB.h"
#include "SaveManager.h"
#include "soh/Network/CrowdControl/CrowdControl.h"
#include "soh/Network/Sail/Sail.h"
#include "soh/Network/Anchor/Anchor.h"
#include "Enhancements/game-interactor/GameInteractor.h"
#include "Enhancements/randomizer/draw.h"
#include <libultraship/libultraship.h>
#include <libultraship/controller/controldeck/ControlDeck.h>
#include <fast/resource/ResourceType.h>

// Resource Types/Factories
#include "soh/resource/type/Array.h"
#include <ship/resource/type/Blob.h>
#include <fast/resource/type/DisplayList.h>
#include <fast/resource/type/Matrix.h>
#include <fast/resource/type/Texture.h>
#include <fast/resource/type/Vertex.h>
#include "soh/resource/type/SohResourceType.h"
#include "soh/resource/type/Animation.h"
#include "soh/resource/type/AudioSample.h"
#include "soh/resource/type/AudioSequence.h"
#include "soh/resource/type/AudioSoundFont.h"
#include "soh/resource/type/CollisionHeader.h"
#include "soh/resource/type/Cutscene.h"
#include "soh/resource/type/Path.h"
#include "soh/resource/type/PlayerAnimation.h"
#include "soh/resource/type/Scene.h"
#include "soh/resource/type/Skeleton.h"
#include "soh/resource/type/SkeletonLimb.h"
#include "soh/resource/type/Text.h"
#include <ship/resource/factory/BlobFactory.h>
#include <fast/resource/factory/DisplayListFactory.h>
#include <fast/resource/factory/MatrixFactory.h>
#include <fast/resource/factory/TextureFactory.h>
#include <fast/resource/factory/VertexFactory.h>
#include "soh/resource/importer/ArrayFactory.h"
#include "soh/resource/importer/AnimationFactory.h"
#include "soh/resource/importer/AudioSampleFactory.h"
#include "soh/resource/importer/AudioSequenceFactory.h"
#include "soh/resource/importer/AudioSoundFontFactory.h"
#include "soh/resource/importer/CollisionHeaderFactory.h"
#include "soh/resource/importer/CutsceneFactory.h"
#include "soh/resource/importer/PathFactory.h"
#include "soh/resource/importer/PlayerAnimationFactory.h"
#include "soh/resource/importer/SceneFactory.h"
#include "soh/resource/importer/SkeletonFactory.h"
#include "soh/resource/importer/SkeletonLimbFactory.h"
#include "soh/resource/importer/TextFactory.h"
#include "soh/resource/importer/BackgroundFactory.h"

#include "soh/config/ConfigUpdaters.h"
#include "soh/ShipInit.hpp"
#ifdef COMBO_BUILD
#include "ComboMenuSharedContext.h" // ComboShip: shared per-DLL ImGui context helper (combo-owned)
#endif

#ifdef _MSC_VER
#define strdup _strdup
#endif

#ifdef _MSC_VER
#define strdup _strdup
#endif

#ifdef __WIIU__
const uint32_t defaultImGuiScale = 3;
#else
const uint32_t defaultImGuiScale = 1;
#endif

const float imguiScaleOptionToValue[4] = { 0.75f, 1.0f, 1.5f, 2.0f };

bool SoH_HandleConfigDrop(char* filePath);

OTRGlobals* OTRGlobals::Instance;
SaveManager* SaveManager::Instance;
CustomMessageManager* CustomMessageManager::Instance;
ItemTableManager* ItemTableManager::Instance;
GameInteractor* GameInteractor::Instance;
AudioCollection* AudioCollection::Instance;
SpeechSynthesizer* SpeechSynthesizer::Instance;
CrowdControl* CrowdControl::Instance;
Sail* Sail::Instance;
Anchor* Anchor::Instance;

extern "C" char** cameraStrings;

extern "C" void PadMgr_ThreadEntry(PadMgr* padMgr);
std::vector<std::shared_ptr<std::string>> cameraStdStrings;

Color_RGB8 kokiriColor = { 0x1E, 0x69, 0x1B };
Color_RGB8 goronColor = { 0x64, 0x14, 0x00 };
Color_RGB8 zoraColor = { 0x00, 0xEC, 0x64 };

int32_t previousImGuiScaleIndex;
float previousImGuiScale;

bool prevAltAssets = false;

// Same as NaviColor type from OoT src (z_actor.c), but modified to be sans alpha channel for Controller LED.
typedef struct {
    Color_RGB8 inner;
    Color_RGB8 outer;
} NaviColor_RGB8;

static NaviColor_RGB8 defaultIdleColor = { { 255, 255, 255 }, { 0, 0, 255 } };
static NaviColor_RGB8 defaultNPCColor = { { 150, 150, 255 }, { 150, 150, 255 } };
static NaviColor_RGB8 defaultEnemyColor = { { 255, 255, 0 }, { 200, 155, 0 } };
static NaviColor_RGB8 defaultPropsColor = { { 0, 255, 0 }, { 0, 255, 0 } };

// Labeled according to ActorCategory (included through ActorDB.h)
const NaviColor_RGB8 LEDColorDefaultNaviColorList[] = {
    defaultPropsColor, // ACTORCAT_SWITCH       Switch
    defaultPropsColor, // ACTORCAT_BG           Background (Prop type 1)
    defaultIdleColor,  // ACTORCAT_PLAYER       Player
    defaultPropsColor, // ACTORCAT_EXPLOSIVE    Bomb
    defaultNPCColor,   // ACTORCAT_NPC          NPC
    defaultEnemyColor, // ACTORCAT_ENEMY        Enemy
    defaultPropsColor, // ACTORCAT_PROP         Prop type 2
    defaultPropsColor, // ACTORCAT_ITEMACTION   Item/Action
    defaultPropsColor, // ACTORCAT_MISC         Misc.
    defaultEnemyColor, // ACTORCAT_BOSS         Boss
    defaultPropsColor, // ACTORCAT_DOOR         Door
    defaultPropsColor, // ACTORCAT_CHEST        Chest
    defaultPropsColor, // ACTORCAT_MAX
};

// OTRTODO: A lot of these left in Japanese are used by the mempak manager. LUS does not currently support mempaks.
// Ignore unused ones.
const char* constCameraStrings[] = {
    "INSUFFICIENT",
    "KEYFRAMES",
    "YOU CAN ADD MORE",
    "FINISHED",
    "PLAYING",
    "DEMO CAMERA TOOL",
    "CANNOT PLAY",
    "KEYFRAME   ",
    "PNT   /      ",
    ">            >",
    "<            <",
    "<          >",
    GFXP_KATAKANA "*ﾌﾟﾚｲﾔ-*",
    "E MODE FIX",
    "E MODE ABS",
    GFXP_HIRAGANA "ｶﾞﾒﾝ" GFXP_KATAKANA "   ﾃﾞﾓ", // OTRTODO: Unused, get a translation! Number 15
    GFXP_HIRAGANA "ｶﾞﾒﾝ   ﾌﾂｳ",                  // OTRTODO: Unused, get a translation! Number 16
    "P TIME  MAX",
    GFXP_KATAKANA "ﾘﾝｸ" GFXP_HIRAGANA "    ｷｵｸ", // OTRTODO: Unused, get a translation! Number 18
    GFXP_KATAKANA "ﾘﾝｸ" GFXP_HIRAGANA "     ﾑｼ", // OTRTODO: Unused, get a translation! Number 19
    "*VIEWPT*",
    "*CAMPOS*",
    "DEBUG CAMERA",
    "CENTER/LOCK",
    "CENTER/FREE",
    "DEMO CONTROL",
    GFXP_KATAKANA "ﾒﾓﾘ" GFXP_HIRAGANA "ｶﾞﾀﾘﾏｾﾝ",
    "p",
    "e",
    "s",
    "l",
    "c",
    GFXP_KATAKANA "ﾒﾓﾘﾊﾟｯｸ",
    GFXP_KATAKANA "ｾｰﾌﾞ",
    GFXP_KATAKANA "ﾛｰﾄﾞ",
    GFXP_KATAKANA "ｸﾘｱ-",
    GFXP_HIRAGANA "ｦﾇｶﾅｲﾃﾞﾈ",
    "FREE      BYTE",
    "NEED      BYTE",
    GFXP_KATAKANA "*ﾒﾓﾘ-ﾊﾟｯｸ*",
    GFXP_HIRAGANA "ｦﾐﾂｹﾗﾚﾏｾﾝ",
    GFXP_KATAKANA "ﾌｧｲﾙ " GFXP_HIRAGANA "ｦ",
    GFXP_HIRAGANA "ｼﾃﾓｲｲﾃﾞｽｶ?",
    GFXP_HIRAGANA "ｹﾞﾝｻﾞｲﾍﾝｼｭｳﾁｭｳﾉ",              // OTRTODO: Unused, get a translation! Number 43
    GFXP_KATAKANA "ﾌｧｲﾙ" GFXP_HIRAGANA "ﾊﾊｷｻﾚﾏｽ", // OTRTODO: Unused, get a translation! Number 44
    GFXP_HIRAGANA "ﾊｲ",
    GFXP_HIRAGANA "ｲｲｴ",
    GFXP_HIRAGANA "ｼﾃｲﾏｽ",
    GFXP_HIRAGANA "ｳﾜｶﾞｷ", // OTRTODO: Unused, get a translation! Number 48
    GFXP_HIRAGANA "ｼﾏｼﾀ",
    "USE       BYTE",
    GFXP_HIRAGANA "ﾆｼｯﾊﾟｲ",
    "E MODE REL",
    "FRAME       ",
    "KEY   /       ",
    "(CENTER)",
    "(ORIG)",
    "(PLAYER)",
    "(ALIGN)",
    "(SET)",
    "(OBJECT)",
    GFXP_KATAKANA "ﾎﾟｲﾝﾄNo.     ", // OTRTODO: Unused, need translation. Number 62
    "FOV              ",
    "N FRAME          ",
    "Z ROT            ",
    GFXP_KATAKANA "ﾓ-ﾄﾞ        ", // OTRTODO: Unused, need translation. Number 65
    "  R FOCUS   ",
    "PMAX              ",
    "DEPTH             ",
    "XROT              ",
    "YROT              ",
    GFXP_KATAKANA "ﾌﾚ-ﾑ         ",
    GFXP_KATAKANA "ﾄ-ﾀﾙ         ",
    GFXP_KATAKANA "ｷ-     /   ",
};

typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} OTRVersion;

std::shared_ptr<Fast::Fast3dWindow> sohFast3dWindow;
static OTRVersion DetectOTRVersion(std::string path, bool isMq);
static bool VerifyArchiveVersion(OTRVersion version);
std::string portArchivePath = "";
static bool sohArchiveVersionMatch = false;

#ifdef COMBO_BUILD
// OOT's own ResourceManager, created at first boot and kept alive for the whole process. A combo
// transition swaps the Context's active RM between OOT's and MM's (each game keeps its own archives +
// resource cache resident), so nothing is unloaded and no cached resource pointer ever dangles.
// (Upstream merge note: the old RegisterOOTResourceFactories factoring was dropped — per-game RMs
// mean SOH_ResumeGame never needs to re-register factories, and Initialize was its only caller.
// See docs/UPSTREAM_MERGES.md.)
static std::shared_ptr<Ship::ResourceManager> sOOTResourceManager;
#endif

OTRGlobals::OTRGlobals() {
    context = Ship::Context::CreateUninitializedInstance("Ship of Harkinian", appShortName, "shipofharkinian.json");

    portArchivePath = Ship::Context::LocateFileAcrossAppDirs("soh.o2r");
    OTRVersion portArchiveVersion = DetectOTRVersion("soh.o2r", false);
    sohArchiveVersionMatch = portArchiveVersion.major == gBuildVersionMajor &&
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
#ifdef COMBO_BUILD
    // Keep a reference to OOT's ResourceManager so a combo MM->OOT return can re-activate it (its
    // archives + resource cache stay resident the whole time — see SOH_ResumeGame). Upstream moved
    // RM creation into this constructor, so the capture moved here too (same RM object as before).
    sOOTResourceManager = context->GetResourceManager();
    Ship::CrossRMRegistry::Register("oot", sOOTResourceManager); // ComboShip: cross-game rendering
#endif
    context->InitConsole();

    auto sohInputEditorWindow =
        std::make_shared<SohInputEditorWindow>(CVAR_WINDOW("ControllerConfiguration"), "Configure Controller");
    sohFast3dWindow =
        std::make_shared<Fast::Fast3dWindow>(std::vector<std::shared_ptr<Ship::GuiWindow>>({ sohInputEditorWindow }));
    context->InitWindow(sohFast3dWindow);
#ifdef COMBO_BUILD
    // ImGui's current-context global (GImGui) is a per-module static. libultraship.dll created
    // the context inside InitWindow; point this DLL's GImGui at it before any ImGui use here
    // (e.g. CreateFontWithSize below), or ImGui::GetIO() asserts on a null context.
    ImGui::SetCurrentContext(context->GetInstance()->GetWindow()->GetGui()->GetImGuiContext());
#endif

    SohGui::SetupMenu();

    if (sohArchiveVersionMatch) {

        auto overlay = context->GetRawInstance()->GetWindow()->GetGui()->GetGameOverlay();
        overlay->LoadFont("Press Start 2P", 12.0f, "fonts/PressStart2P-Regular.ttf");
        overlay->LoadFont("Fipps", 32.0f, "fonts/Fipps-Regular.otf");
        overlay->SetCurrentFont(CVarGetString(CVAR_GAME_OVERLAY_FONT, "Press Start 2P"));

        fontMonoSmall = CreateFontWithSize(14.0f, "fonts/Inconsolata-Regular.ttf");
        fontMono = CreateFontWithSize(16.0f, "fonts/Inconsolata-Regular.ttf");
        fontMonoLarger = CreateFontWithSize(20.0f, "fonts/Inconsolata-Regular.ttf");
        fontMonoLargest = CreateFontWithSize(24.0f, "fonts/Inconsolata-Regular.ttf");
        fontStandard = CreateFontWithSize(16.0f, "fonts/Montserrat-Regular.ttf");
        fontStandardLarger = CreateFontWithSize(20.0f, "fonts/Montserrat-Regular.ttf");
        fontStandardLargest = CreateFontWithSize(24.0f, "fonts/Montserrat-Regular.ttf");
        fontJapanese = CreateFontWithSize(24.0f, "fonts/NotoSansJP-Regular.ttf", true);
        ImGui::GetIO().FontDefault = fontStandardLarger;
    }

    previousImGuiScaleIndex = -1;
    previousImGuiScale = defaultImGuiScale;
    ScaleImGui();
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
    } catch ([[maybe_unused]] std::filesystem::filesystem_error const& ex) { return false; }
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
    } catch ([[maybe_unused]] std::filesystem::filesystem_error const& ex) {
        // Couldn't make the folder, continue silently
        return;
    }
}

namespace SohGui {
extern std::shared_ptr<SohGui::SohMenu> mSohMenu;
}

void OTRGlobals::RunExtract(int argc, char* argv[]) {
    bool extractDone = false;
    ExtractSteps extractStep = ES_PORT_ARCHIVE;
    WindowsSteps windowsStep = WS_TEMP;
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(OTRGlobals::Instance->context->GetWindow());
    auto gui = wnd->GetGui();

    OTRVersion vanillaVersion = DetectOTRVersion("oot.o2r", false);
    OTRVersion mqVersion = DetectOTRVersion("oot-mq.o2r", true);

    bool shouldRegen = VerifyArchiveVersion(vanillaVersion) || VerifyArchiveVersion(mqVersion);

    std::filesystem::path ownPath;
    std::vector<std::string> args;
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            args.push_back(argv[i]);
        }
    }
    Extractor extract;
    PromptSteps promptStep = PS_FILE_CHECK;
    bool generatedIsMQ = false;
    std::atomic<size_t> extractCount = 0, totalExtract = 0;

    std::string installPath = Ship::Context::GetAppBundlePath();
    std::string dataPath = Ship::Context::GetAppDirectoryPath(appShortName);
    std::string file;

#if defined(__SWITCH__)
    SohGui::RegisterPopup("Outdated ROM Archives",
                          "\x1b[2;2HYou've launched the Ship with an old ROM O2R file."
                          "\x1b[4;2HPlease regenerate a new ROM O2R and relaunch."
                          "\x1b[6;2HPress the Home button to exit...",
                          "OK", "", [&]() { exit(1); });
#elif defined(__WIIU__)
    SohGui::RegisterPopup("Outdated ROM Archives",
                          "You've launched the Ship with an old a ROM O2R file.\n\n"
                          "Please generate a ROM O2R and relaunch.\n\n"
                          "Press and hold the Power button to shutdown...",
                          "OK", "", [&]() { exit(1); });
    OSFatal();
#endif

    if (!std::filesystem::exists(installPath + "/assets")) {
        SohGui::RegisterPopup("Extractor assets not found",
                              "No O2R files found. Missing 'assets/' folder needed to generate OTR file.\nPlease "
                              "re-extract them from the download or.\n\nExiting...",
                              "OK", "", [&]() { exit(1); });
    } else if (shouldRegen) {
        SohGui::RegisterPopup("Outdated ROM Archives",
                              "Your oot.o2r or oot-mq.o2r were created with incompatible versions of SoH.\nYou will "
                              "now be redirected to re-extract them.");
        std::filesystem::remove("oot.o2r");
        std::filesystem::remove("oot-mq.o2r");
    }

    std::shared_ptr<BS::thread_pool> threadPool = std::make_shared<BS::thread_pool>(1);
    std::optional<std::future<void>> extractionTask;

#if not defined(__SWITCH__) && not defined(__WIIU__)
    CheckAndCreateModFolder();
#endif

    while (!extractDone) {
        if (SohGui::PopupsQueued() > 0 || extractionTask.has_value()) {
            goto render;
        }
        switch (extractStep) {
            case ES_PORT_ARCHIVE: {
                if (sohArchiveVersionMatch) {
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
                    msg = "Please extract the soh.o2r from the Ship of Harkinian download\nto your folder.\n\nPress "
                          "and hold the power\n"
                          "button to shutdown...";
#else
                    msg =
                        "Please extract the soh.o2r from the Ship of Harkinian download to your folder.\n\nExiting...";
#endif
                    std::string title =
                        !std::filesystem::exists(portArchivePath) ? "Missing soh.o2r" : "soh.o2r is outdated";
                    SohGui::RegisterPopup(title, msg, "OK", "", [&]() { exit(1); });
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
                        } catch ([[maybe_unused]] std::filesystem::filesystem_error const& ex) {
                            std::string userPath = getenv("USERPROFILE");
                            userPath.append("\\AppData\\Local\\Temp");
                            tempPath = std::filesystem::canonical(userPath);
                        }
                        wchar_t buffer[MAX_PATH];
                        GetModuleFileName(NULL, buffer, _countof(buffer));
                        ownPath = std::filesystem::canonical(buffer).parent_path();
                        if (IsSubpath(ownPath, tempPath)) {
                            SohGui::RegisterPopup("SoH Path Error",
                                                  "SoH is running in a temp folder.\nExtract the .zip and run again.",
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
                        } catch ([[maybe_unused]] std::filesystem::filesystem_error const& ex) { error = true; }
                        if (tfile == NULL || error) {
                            SohGui::RegisterPopup("SoH Permissions Error",
                                                  "SoH does not have proper file permissions.\nPlease move it to a "
                                                  "folder that does and run again.",
                                                  "OK", "", [&]() {
                                                      fclose(tfile);
                                                      PathTestCleanup(tfile);
                                                      exit(0);
                                                  });
                        } else {
                            fclose(tfile);
                            if (!PathTestCleanup(tfile)) {
                                SohGui::RegisterPopup("SoH Permissions Error",
                                                      "SoH does not have proper file permissions.\nPlease move it to a "
                                                      "folder that does and run again.",
                                                      "OK", "", [&]() { exit(0); });
                            }
                            windowsStep = WS_ONEDRIVE;
                        }
                        continue;
                    }
                    case WS_ONEDRIVE: {
                        if (ownPath.string().find("OneDrive") != std::string::npos) {
                            SohGui::RegisterPopup("SoH Path Error",
                                                  "SoH appears to be in a OneDrive folder, which will cause issues.\n"
                                                  "Please move it to a folder outside of OneDrive, like the root of a\n"
                                                  "drive (e.g. \"C:\\Games\\SoH\").",
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
                    SohGui::RegisterPopup(
                        "Run Ship of Harkinian", "All files have been processed. Run SoH?", "Yes", "No",
                        [&]() {
                            if (!std::filesystem::exists(Ship::Context::GetAppDirectoryPath(appShortName) +
                                                         "/oot.o2r") &&
                                !std::filesystem::exists(Ship::Context::GetAppDirectoryPath(appShortName) +
                                                         "/oot-mq.o2r")) {
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
                    std::string archive = (extract.IsMasterQuest() ? "oot-mq.o2r" : "oot.o2r");
                    if (std::filesystem::exists(Ship::Context::GetAppDirectoryPath(appShortName) + "/" + archive)) {
                        std::string msg = "Archive for current ROM, " + archive + ", already exists.\nExtract again?";
                        SohGui::RegisterPopup("Confirm Re-extract", msg.c_str(), "Yes", "No", [&]() {
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
                    SohGui::RegisterPopup("SoH ROM Error", msg.c_str());
                }
#else
                extractStep = ES_VERIFY;
#endif
                break;
            }
            case ES_EXTRACT: {
                switch (promptStep) {
                    case PS_FILE_CHECK: {
                        const bool ootO2RExists =
                            std::filesystem::exists(
                                Ship::Context::LocateFileAcrossAppDirs("oot-mq.o2r", appShortName)) ||
                            std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs("oot.o2r", appShortName));

                        if (!ootO2RExists) {
                            SohGui::RegisterPopup(
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
                            SohGui::RegisterPopup(
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
                            generatedIsMQ = extract.IsMasterQuest();
                            promptStep = PS_SECOND;
                            extractCount = 0;
                            totalExtract = 0;
                        });
                        continue;
                    }
                    case PS_SECOND: {
                        SohGui::RegisterPopup(
                            "Extraction Complete", "ROM Extracted. Extract another?", "Yes", "No",
                            [&]() {
                                if (!extract.ManuallySearchForRomMatchingType(generatedIsMQ ? RomSearchMode::Vanilla
                                                                                            : RomSearchMode::MQ)) {
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
                const bool ootO2RExists =
                    std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs("oot-mq.o2r", appShortName)) ||
                    std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs("oot.o2r", appShortName));

                if (!ootO2RExists) {
                    SohGui::RegisterPopup("No ROM Archives",
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
            static_cast<UIWidgets::Colors>(CVarGetInteger(CVAR_SETTING("Menu.Theme"), UIWidgets::Colors::LightBlue));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, UIWidgets::ColorValues.at(themeColor));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, UIWidgets::ColorValues.at(UIWidgets::Colors::DarkGray));

        // Skip dropped frames
        if (!wnd->IsFrameReady()) {
            continue;
        }
        gui->StartDraw();
        sohFast3dWindow->StartFrame();
        sohFast3dWindow->RunGuiOnly();
        if (extractionTask.has_value()) {
            auto status = extractionTask->wait_for(std::chrono::milliseconds(0));
            if (status == std::future_status::ready) {
                try {
                    extractionTask->get();
                } catch (const std::exception& e) {
                    SohGui::RegisterPopup("Extraction Crashed", e.what(), "Close", "", []() { exit(1); });
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
        sohFast3dWindow->EndFrame();
        ImGui::PopStyleColor(2);
    }

#ifdef __SWITCH__
    Ship::Switch::Init(Ship::PreInitPhase);
#elif defined(__WIIU__)
    Ship::WiiU::Init(appShortName);
#endif
}

void InitGfxDebugger() {
    auto dbg =
        std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow())->GetGfxDebugger();

    if (dbg != nullptr) {
        return;
    }

    dbg = std::make_shared<Fast::GfxDebugger>();

    if (dbg != nullptr) {
        SPDLOG_ERROR("Failed to initialize gfx debugger");
    }
}

void OTRGlobals::Initialize() {
    std::string mqPath = Ship::Context::LocateFileAcrossAppDirs("oot-mq.o2r", appShortName);
    if (std::filesystem::exists(mqPath)) {
        context->GetResourceManager()->GetArchiveManager()->AddArchive(mqPath);
    }
    std::string ootPath = Ship::Context::LocateFileAcrossAppDirs("oot.o2r", appShortName);
    if (std::filesystem::exists(ootPath)) {
        context->GetResourceManager()->GetArchiveManager()->AddArchive(ootPath);
    }

    std::unordered_set<uint32_t> ValidHashes = {
        OOT_PAL_MQ,     OOT_NTSC_JP_MQ, OOT_NTSC_US_MQ, OOT_PAL_GC_MQ_DBG, OOT_NTSC_US_10,
        OOT_NTSC_US_11, OOT_NTSC_US_12, OOT_PAL_10,     OOT_PAL_11,        OOT_NTSC_JP_GC_CE,
        OOT_NTSC_JP_GC, OOT_NTSC_US_GC, OOT_PAL_GC,     OOT_PAL_GC_DBG1,   OOT_PAL_GC_DBG2,
    };

#if (_DEBUG)
    auto defaultLogLevel = spdlog::level::trace;
#else
    auto defaultLogLevel = spdlog::level::info;
#endif
    context->InitConfiguration();
    context->InitConsoleVariables();
    auto logLevel =
        static_cast<spdlog::level::level_enum>(CVarGetInteger(CVAR_DEVELOPER_TOOLS("LogLevel"), defaultLogLevel));
    context->InitLogging(logLevel, logLevel);
    Ship::Context::GetRawInstance()->GetLogger()->set_pattern("[%H:%M:%S.%e] [%s:%#] [%l] %v");

    InitGfxDebugger();
    context->InitFileDropMgr();

    // tell LUS to reserve 3 SoH specific threads (Game, Audio, Save)
    // ComboShip: default Alternate Assets OFF (upstream defaults ON). We ship no HD/alt asset pack,
    // so the per-frame alt/ resource probe just spams the log with misses. See docs/UPSTREAM_MERGES.md.
    prevAltAssets = CVarGetInteger(CVAR_SETTING("AltAssets"), 0);
    context->GetResourceManager()->SetAltAssetsEnabled(prevAltAssets);

    context->InitCrashHandler();

    context->GetWindow()->SetAutoCaptureMouse(CVarGetInteger(CVAR_SETTING("EnableMouse"), 0) &&
                                              CVarGetInteger(CVAR_SETTING("AutoCaptureMouse"), 1));
    context->GetWindow()->SetForceCursorVisibility(CVarGetInteger(CVAR_SETTING("CursorVisibility"), 0));

    context->InitAudio({ .SampleRate = 32000,
                         .SampleLength = 1024,
                         // 4096 frames at 32 kHz (~128 ms) gives enough reservoir for frame
                         // jitter and slow-frame spikes without perceptible audio latency.
                         .DesiredBuffered = 4096 });

    // The menu is set up before audio is initialized, so its list of available audio backends has to be
    // populated here rather than in Menu::InitElement (where the window backends are handled).
    SohGui::GetSohMenu()->UpdateAudioBackendObjects();

    SPDLOG_INFO("Starting Ship of Harkinian version {} (Branch: {} | Commit: {})", (char*)gBuildVersion,
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
                                    "Room", static_cast<uint32_t>(SOH::ResourceType::SOH_Room),
                                    0); // Is room scene? maybe?
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLSceneV0>(), RESOURCE_FORMAT_XML, "Room",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_Room), 0); // Is room scene? maybe?
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryCollisionHeaderV0>(),
                                    RESOURCE_FORMAT_BINARY, "CollisionHeader",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_CollisionHeader), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLCollisionHeaderV0>(), RESOURCE_FORMAT_XML,
                                    "CollisionHeader", static_cast<uint32_t>(SOH::ResourceType::SOH_CollisionHeader),
                                    0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinarySkeletonV0>(), RESOURCE_FORMAT_BINARY,
                                    "Skeleton", static_cast<uint32_t>(SOH::ResourceType::SOH_Skeleton), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLSkeletonV0>(), RESOURCE_FORMAT_XML,
                                    "Skeleton", static_cast<uint32_t>(SOH::ResourceType::SOH_Skeleton), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinarySkeletonLimbV0>(),
                                    RESOURCE_FORMAT_BINARY, "SkeletonLimb",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_SkeletonLimb), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLSkeletonLimbV0>(), RESOURCE_FORMAT_XML,
                                    "SkeletonLimb", static_cast<uint32_t>(SOH::ResourceType::SOH_SkeletonLimb), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryPathV0>(), RESOURCE_FORMAT_BINARY,
                                    "Path", static_cast<uint32_t>(SOH::ResourceType::SOH_Path), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLPathV0>(), RESOURCE_FORMAT_XML, "Path",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_Path), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryCutsceneV0>(), RESOURCE_FORMAT_BINARY,
                                    "Cutscene", static_cast<uint32_t>(SOH::ResourceType::SOH_Cutscene), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryTextV0>(), RESOURCE_FORMAT_BINARY,
                                    "Text", static_cast<uint32_t>(SOH::ResourceType::SOH_Text), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLTextV0>(), RESOURCE_FORMAT_XML, "Text",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_Text), 0);
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

    Lang::LoadLangs();

    gSaveStateMgr = std::make_shared<SaveStateMgr>();
    gRandoContext->InitStaticData();
    gRandoContext = Rando::Context::CreateInstance();
    Rando::Settings::GetInstance()->AssignContext(gRandoContext);
    Rando::StaticData::InitItemTable(); // RANDOTODO make this not rely on context's logic so it can be initialised in
                                        // InitStaticData
    gRandomizer = std::make_shared<Randomizer>();

    hasMasterQuest = hasOriginal = false;

    // Move the camera strings from read only memory onto the heap (writable memory)
    // This is in OTRGlobals right now because this is a place that will only ever be run once at the beginning of
    // startup. We should probably find some code in db_camera that does initialization and only run once, and then
    // dealloc on deinitialization.
    cameraStrings = (char**)malloc(sizeof(constCameraStrings));
    for (size_t i = 0; i < sizeof(constCameraStrings) / sizeof(char*); i++) {
        // OTRTODO: never deallocated...
        cameraStrings[i] = strdup(constCameraStrings[i]);
    }

    auto versions = context->GetResourceManager()->GetArchiveManager()->GetGameVersions();

    for (uint32_t version : versions) {
        if (!ValidHashes.contains(version)) {
#if defined(__SWITCH__)
            SPDLOG_ERROR("Invalid OTR File!");
#elif defined(__WIIU__)
            Ship::WiiU::ThrowInvalidOTR();
#else
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Invalid OTR File",
                                     "Attempted to load an invalid OTR file. Try regenerating.", nullptr);
            SPDLOG_ERROR("Invalid OTR File!");
#endif
            exit(1);
        }
        switch (version) {
            case OOT_PAL_MQ:
            case OOT_NTSC_JP_MQ:
            case OOT_NTSC_US_MQ:
            case OOT_PAL_GC_MQ_DBG:
                hasMasterQuest = true;
                break;
            case OOT_NTSC_US_10:
            case OOT_NTSC_US_11:
            case OOT_NTSC_US_12:
            case OOT_PAL_10:
            case OOT_PAL_11:
            case OOT_NTSC_JP_GC_CE:
            case OOT_NTSC_JP_GC:
            case OOT_NTSC_US_GC:
            case OOT_PAL_GC:
            case OOT_PAL_GC_DBG1:
            case OOT_PAL_GC_DBG2:
                hasOriginal = true;
                break;
            default:
                break;
        }
    }
}

OTRGlobals::~OTRGlobals() {
}

void OTRGlobals::ScaleImGui() {
    int32_t imGuiScaleIndex = CVarGetInteger(CVAR_SETTING("ImGuiScale"), defaultImGuiScale);
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

bool OTRGlobals::HasMasterQuest() {
    return hasMasterQuest;
}

bool OTRGlobals::HasOriginal() {
    return hasOriginal;
}

uint32_t OTRGlobals::GetInterpolationFPS() {
    if (CVarGetInteger(CVAR_SETTING("MatchRefreshRate"), 0)) {
        return Ship::Context::GetRawInstance()->GetWindow()->GetCurrentRefreshRate();
    } else if (CVarGetInteger(CVAR_VSYNC_ENABLED, 1) ||
               !Ship::Context::GetRawInstance()->GetWindow()->CanDisableVerticalSync()) {
        return std::min<uint32_t>(Ship::Context::GetRawInstance()->GetWindow()->GetCurrentRefreshRate(),
                                  CVarGetInteger(CVAR_SETTING("InterpolationFPS"), 20));
    }
    return CVarGetInteger(CVAR_SETTING("InterpolationFPS"), 20);
}

extern "C" void OTRMessage_Init();
extern "C" void AudioMgr_CreateNextAudioBuffer(s16* samples, u32 num_samples);
extern "C" void AudioPlayer_Play(const uint8_t* buf, uint32_t len);
int AudioPlayer_Buffered(void);
extern "C" int AudioPlayer_GetDesiredBuffered(void);
std::unordered_map<std::string, ExtensionEntry> ExtensionCache;

void OTRAudio_Thread() {
#define SAMPLES_HIGH 560
#define SAMPLES_LOW 528
#define AUDIO_FRAMES_PER_UPDATE (R_UPDATE_RATE > 0 ? R_UPDATE_RATE : 1)
#define NUM_AUDIO_CHANNELS 2

    // Single producer routine used by both the wake-driven and pre-buffer
    // loops. Captures the per-iteration sample count from the caller.
    auto produce_and_play = [&](u32 num_audio_samples) {
        const u32 total_frames = num_audio_samples * AUDIO_FRAMES_PER_UPDATE;
        const u32 total_samples = total_frames * NUM_AUDIO_CHANNELS;

        // 3 is the maximum authentic frame divisor.
        static thread_local s16 audio_buffer[SAMPLES_HIGH * NUM_AUDIO_CHANNELS * 3];

        for (int i = 0; i < AUDIO_FRAMES_PER_UPDATE; i++) {
            AudioMgr_CreateNextAudioBuffer(audio_buffer + i * (num_audio_samples * NUM_AUDIO_CHANNELS),
                                           num_audio_samples);
        }

        AudioPlayer_Play(reinterpret_cast<u8*>(audio_buffer), total_samples * sizeof(int16_t));
    };

    // Self-pump cadence. The gfx thread wakes us once per rendered frame
    // (Graph_ProcessGfxCommands sets audio.processing), but a single long
    // frame leave us asleep while the backend's queue drains to silence.
    // So we also wake on a short timeout, independent of the gfx frame rate.
    // Doing so is in fact closer to the console, where the audio task ran
    // off the scheduler rather than gated on rendering..
    constexpr auto kSelfPumpInterval = std::chrono::milliseconds(5);

    // The self-pump timeout must wait that the game has reached its render
    // loop, to avoid accessing uninitialized variables.
    bool primed = false;

    while (audio.running) {
        {
            std::unique_lock<std::mutex> Lock(audio.mutex);
            if (!primed) {
                // Pre-init: block until the gfx thread drives the first buffer
                // (engine guaranteed ready by then), exactly as before.
                while (!audio.processing && audio.running) {
                    audio.cv_to_thread.wait(Lock);
                }
                primed = true;
            } else if (!audio.processing && audio.running) {
                // Primed: wait for the next gfx wake, but no longer than
                // kSelfPumpInterval so a stalled gfx thread can't starve the
                // backend queue. A pending wake falls straight through.
                audio.cv_to_thread.wait_for(Lock, kSelfPumpInterval);
            }

            if (!audio.running) {
                break;
            }
        }

        {
            std::unique_lock<std::mutex> Lock(audio.mutex);
            int samples_left = AudioPlayer_Buffered();
            u32 num_audio_samples = samples_left < AudioPlayer_GetDesiredBuffered() ? SAMPLES_HIGH : SAMPLES_LOW;

            // Producer guard (banteg/Shipwright#6594): skip advancing the audio
            // engine if the backend ring cannot accept the smallest next burst.
            // Generating PCM that DoPlay() would refuse creates a discontinuity
            // audible as a click. The pre-buffer loop below will catch up once
            // the backend drains enough.
            if (AudioPlayer_Buffered() + SAMPLES_LOW * AUDIO_FRAMES_PER_UPDATE > AudioPlayer_GetDesiredBuffered()) {
                audio.processing = false;
            } else {
                produce_and_play(num_audio_samples);
                audio.processing = false;
            }
        }

        // Pre-buffer: fill the reservoir while the backend can accept more,
        // without waiting for the next frame signal. This absorbs load spikes.
        // Safe for BGM — the N64 sequencer advances independently of gameplay.
        // The producer guard (same as above) prevents advancing the audio engine
        // when the backend ring is already at capacity.
        while (audio.running && AudioPlayer_Buffered() < AudioPlayer_GetDesiredBuffered()) {
            if (AudioPlayer_Buffered() + SAMPLES_LOW * AUDIO_FRAMES_PER_UPDATE > AudioPlayer_GetDesiredBuffered()) {
                break;
            }
            int samples_left = AudioPlayer_Buffered();
            u32 num_audio_samples = samples_left < AudioPlayer_GetDesiredBuffered() ? SAMPLES_HIGH : SAMPLES_LOW;
            produce_and_play(num_audio_samples);
        }
    }
}

void OTRAudio_Init() {
    // Precache all our samples, sequences, etc...
    ResourceMgr_LoadDirectory("audio");

    if (!audio.running) {
        audio.running = true;
        audio.thread = std::thread(OTRAudio_Thread);
    }
}

// C->C++ Bridge
extern "C" char** sequenceMap;
extern "C" size_t sequenceMapSize;

extern "C" char** fontMap;
extern "C" size_t fontMapSize;

extern "C" void OTRAudio_Exit() {
    // Tell the audio thread to stop
    {
        std::unique_lock<std::mutex> Lock(audio.mutex);
        audio.running = false;
    }
    audio.cv_to_thread.notify_all();

    // Wait until the audio thread quit
    // ComboShip: guard the join. At shutdown OTRAudio_Exit can run again after
    // SOH_PrepareForTransition already joined the thread; joining a non-joinable thread
    // throws std::system_error -> terminate.
    if (audio.thread.joinable()) {
        audio.thread.join();
    }
#if 0
    for (size_t i = 0; i < sequenceMapSize; i++) {
        free(sequenceMap[i]);
    }
    free(sequenceMap);

    for (size_t i = 0; i < fontMapSize; i++) {
        free(fontMap[i]);
    }
    free(fontMap);
    free(gAudioContext.seqLoadStatus);
    free(gAudioContext.fontLoadStatus);
#endif
}

void VanillaItemTable_Init() {
    static GetItemEntry getItemTable[] = {
        // clang-format off
        GET_ITEM(ITEM_BOMBS_5,          OBJECT_GI_BOMB_1,        GID_BOMB,             0x32, 0x59, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_BOMBS_5),
        GET_ITEM(ITEM_NUTS_5,           OBJECT_GI_NUTS,          GID_NUTS,             0x34, 0x0C, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_NUTS_5),
        GET_ITEM(ITEM_BOMBCHU,          OBJECT_GI_BOMB_2,        GID_BOMBCHU,          0x33, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOMBCHUS_10),
        GET_ITEM(ITEM_BOW,              OBJECT_GI_BOW,           GID_BOW,              0x31, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOW),
        GET_ITEM(ITEM_SLINGSHOT,        OBJECT_GI_PACHINKO,      GID_SLINGSHOT,        0x30, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SLINGSHOT),
        GET_ITEM(ITEM_BOOMERANG,        OBJECT_GI_BOOMERANG,     GID_BOOMERANG,        0x35, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOOMERANG),
        GET_ITEM(ITEM_STICK,            OBJECT_GI_STICK,         GID_STICK,            0x37, 0x0D, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_STICKS_1),
        GET_ITEM(ITEM_HOOKSHOT,         OBJECT_GI_HOOKSHOT,      GID_HOOKSHOT,         0x36, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_HOOKSHOT),
        GET_ITEM(ITEM_LONGSHOT,         OBJECT_GI_HOOKSHOT,      GID_LONGSHOT,         0x4F, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_LONGSHOT),
        GET_ITEM(ITEM_LENS,             OBJECT_GI_GLASSES,       GID_LENS,             0x39, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_LENS),
        GET_ITEM(ITEM_LETTER_ZELDA,     OBJECT_GI_LETTER,        GID_LETTER_ZELDA,     0x69, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_LETTER_ZELDA),
        GET_ITEM(ITEM_OCARINA_TIME,     OBJECT_GI_OCARINA,       GID_OCARINA_TIME,     0x3A, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_OCARINA_OOT),
        GET_ITEM(ITEM_HAMMER,           OBJECT_GI_HAMMER,        GID_HAMMER,           0x38, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_HAMMER),
        GET_ITEM(ITEM_COJIRO,           OBJECT_GI_NIWATORI,      GID_COJIRO,           0x02, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_COJIRO),
        GET_ITEM(ITEM_BOTTLE,           OBJECT_GI_BOTTLE,        GID_BOTTLE,           0x42, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOTTLE),
        GET_ITEM(ITEM_POTION_RED,       OBJECT_GI_LIQUID,        GID_POTION_RED,       0x43, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_POTION_RED),
        GET_ITEM(ITEM_POTION_GREEN,     OBJECT_GI_LIQUID,        GID_POTION_GREEN,     0x44, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_POTION_GREEN),
        GET_ITEM(ITEM_POTION_BLUE,      OBJECT_GI_LIQUID,        GID_POTION_BLUE,      0x45, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_POTION_BLUE),
        GET_ITEM(ITEM_FAIRY,            OBJECT_GI_BOTTLE,        GID_BOTTLE,           0x46, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_FAIRY),
        GET_ITEM(ITEM_MILK_BOTTLE,      OBJECT_GI_MILK,          GID_MILK,             0x98, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MILK_BOTTLE),
        GET_ITEM(ITEM_LETTER_RUTO,      OBJECT_GI_BOTTLE_LETTER, GID_LETTER_RUTO,      0x99, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_LETTER_RUTO),
        GET_ITEM(ITEM_BEAN,             OBJECT_GI_BEAN,          GID_BEAN,             0x48, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BEAN),
        GET_ITEM(ITEM_MASK_SKULL,       OBJECT_GI_SKJ_MASK,      GID_MASK_SKULL,       0x10, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MASK_SKULL),
        GET_ITEM(ITEM_MASK_SPOOKY,      OBJECT_GI_REDEAD_MASK,   GID_MASK_SPOOKY,      0x11, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MASK_SPOOKY),
        GET_ITEM(ITEM_CHICKEN,          OBJECT_GI_NIWATORI,      GID_CHICKEN,          0x48, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_CHICKEN),
        GET_ITEM(ITEM_MASK_KEATON,      OBJECT_GI_KI_TAN_MASK,   GID_MASK_KEATON,      0x12, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MASK_KEATON),
        GET_ITEM(ITEM_MASK_BUNNY,       OBJECT_GI_RABIT_MASK,    GID_MASK_BUNNY,       0x13, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MASK_BUNNY),
        GET_ITEM(ITEM_MASK_TRUTH,       OBJECT_GI_TRUTH_MASK,    GID_MASK_TRUTH,       0x17, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MASK_TRUTH),
        GET_ITEM(ITEM_POCKET_EGG,       OBJECT_GI_EGG,           GID_EGG,              0x01, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_POCKET_EGG),
        GET_ITEM(ITEM_POCKET_CUCCO,     OBJECT_GI_NIWATORI,      GID_CHICKEN,          0x48, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_POCKET_CUCCO),
        GET_ITEM(ITEM_ODD_MUSHROOM,     OBJECT_GI_MUSHROOM,      GID_ODD_MUSHROOM,     0x03, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_ODD_MUSHROOM),
        GET_ITEM(ITEM_ODD_POTION,       OBJECT_GI_POWDER,        GID_ODD_POTION,       0x04, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_ODD_POTION),
        GET_ITEM(ITEM_SAW,              OBJECT_GI_SAW,           GID_SAW,              0x05, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SAW),
        GET_ITEM(ITEM_SWORD_BROKEN,     OBJECT_GI_BROKENSWORD,   GID_SWORD_BROKEN,     0x08, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SWORD_BROKEN),
        GET_ITEM(ITEM_PRESCRIPTION,     OBJECT_GI_PRESCRIPTION,  GID_PRESCRIPTION,     0x09, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_PRESCRIPTION),
        GET_ITEM(ITEM_FROG,             OBJECT_GI_FROG,          GID_FROG,             0x0D, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_FROG),
        GET_ITEM(ITEM_EYEDROPS,         OBJECT_GI_EYE_LOTION,    GID_EYEDROPS,         0x0E, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_EYEDROPS),
        GET_ITEM(ITEM_CLAIM_CHECK,      OBJECT_GI_TICKETSTONE,   GID_CLAIM_CHECK,      0x0A, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_CLAIM_CHECK),
        GET_ITEM(ITEM_SWORD_KOKIRI,     OBJECT_GI_SWORD_1,       GID_SWORD_KOKIRI,     0xA4, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SWORD_KOKIRI),
        GET_ITEM(ITEM_SWORD_BGS,        OBJECT_GI_LONGSWORD,     GID_SWORD_BGS,        0x4B, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SWORD_KNIFE),
        GET_ITEM(ITEM_SHIELD_DEKU,      OBJECT_GI_SHIELD_1,      GID_SHIELD_DEKU,      0x4C, 0xA0, CHEST_ANIM_SHORT, ITEM_CATEGORY_LESSER,          MOD_NONE, GI_SHIELD_DEKU),
        GET_ITEM(ITEM_SHIELD_HYLIAN,    OBJECT_GI_SHIELD_2,      GID_SHIELD_HYLIAN,    0x4D, 0xA0, CHEST_ANIM_SHORT, ITEM_CATEGORY_LESSER,          MOD_NONE, GI_SHIELD_HYLIAN),
        GET_ITEM(ITEM_SHIELD_MIRROR,    OBJECT_GI_SHIELD_3,      GID_SHIELD_MIRROR,    0x4E, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SHIELD_MIRROR),
        GET_ITEM(ITEM_TUNIC_GORON,      OBJECT_GI_CLOTHES,       GID_TUNIC_GORON,      0x50, 0xA0, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_TUNIC_GORON),
        GET_ITEM(ITEM_TUNIC_ZORA,       OBJECT_GI_CLOTHES,       GID_TUNIC_ZORA,       0x51, 0xA0, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_TUNIC_ZORA),
        GET_ITEM(ITEM_BOOTS_IRON,       OBJECT_GI_BOOTS_2,       GID_BOOTS_IRON,       0x53, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOOTS_IRON),
        GET_ITEM(ITEM_BOOTS_HOVER,      OBJECT_GI_HOVERBOOTS,    GID_BOOTS_HOVER,      0x54, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOOTS_HOVER),
        GET_ITEM(ITEM_QUIVER_40,        OBJECT_GI_ARROWCASE,     GID_QUIVER_40,        0x56, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_QUIVER_40),
        GET_ITEM(ITEM_QUIVER_50,        OBJECT_GI_ARROWCASE,     GID_QUIVER_50,        0x57, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_QUIVER_50),
        GET_ITEM(ITEM_BOMB_BAG_20,      OBJECT_GI_BOMBPOUCH,     GID_BOMB_BAG_20,      0x58, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOMB_BAG_20),
        GET_ITEM(ITEM_BOMB_BAG_30,      OBJECT_GI_BOMBPOUCH,     GID_BOMB_BAG_30,      0x59, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_BOMB_BAG_30),
        GET_ITEM(ITEM_BOMB_BAG_40,      OBJECT_GI_BOMBPOUCH,     GID_BOMB_BAG_40,      0x5A, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_BOMB_BAG_40),
        GET_ITEM(ITEM_GAUNTLETS_SILVER, OBJECT_GI_GLOVES,        GID_GAUNTLETS_SILVER, 0x5B, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_GAUNTLETS_SILVER),
        GET_ITEM(ITEM_GAUNTLETS_GOLD,   OBJECT_GI_GLOVES,        GID_GAUNTLETS_GOLD,   0x5C, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_GAUNTLETS_GOLD),
        GET_ITEM(ITEM_SCALE_SILVER,     OBJECT_GI_SCALE,         GID_SCALE_SILVER,     0xCD, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SCALE_SILVER),
        GET_ITEM(ITEM_SCALE_GOLDEN,     OBJECT_GI_SCALE,         GID_SCALE_GOLDEN,     0xCE, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SCALE_GOLDEN),
        GET_ITEM(ITEM_STONE_OF_AGONY,   OBJECT_GI_MAP,           GID_STONE_OF_AGONY,   0x68, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_STONE_OF_AGONY),
        GET_ITEM(ITEM_GERUDO_CARD,      OBJECT_GI_GERUDO,        GID_GERUDO_CARD,      0x7B, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_GERUDO_CARD),
        GET_ITEM(ITEM_OCARINA_FAIRY,    OBJECT_GI_OCARINA_0,     GID_OCARINA_FAIRY,    0x4A, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_OCARINA_FAIRY),
        GET_ITEM(ITEM_SEEDS,            OBJECT_GI_SEED,          GID_SEEDS,            0xDC, 0x50, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_SEEDS_5),
        GET_ITEM(ITEM_HEART_CONTAINER,  OBJECT_GI_HEARTS,        GID_HEART_CONTAINER,  0xC6, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_HEALTH,          MOD_NONE, GI_HEART_CONTAINER),
        GET_ITEM(ITEM_HEART_PIECE_2,    OBJECT_GI_HEARTS,        GID_HEART_PIECE,      0xC2, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_HEALTH,          MOD_NONE, GI_HEART_PIECE),
        GET_ITEM(ITEM_KEY_BOSS,         OBJECT_GI_BOSSKEY,       GID_KEY_BOSS,         0xC7, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_BOSS_KEY,        MOD_NONE, GI_KEY_BOSS),
        GET_ITEM(ITEM_COMPASS,          OBJECT_GI_COMPASS,       GID_COMPASS,          0x67, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_COMPASS),
        GET_ITEM(ITEM_DUNGEON_MAP,      OBJECT_GI_MAP,           GID_DUNGEON_MAP,      0x66, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_MAP),
        GET_ITEM(ITEM_KEY_SMALL,        OBJECT_GI_KEY,           GID_KEY_SMALL,        0x60, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_SMALL_KEY,       MOD_NONE, GI_KEY_SMALL),
        GET_ITEM(ITEM_MAGIC_SMALL,      OBJECT_GI_MAGICPOT,      GID_MAGIC_SMALL,      0x52, 0x6F, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_MAGIC_SMALL),
        GET_ITEM(ITEM_MAGIC_LARGE,      OBJECT_GI_MAGICPOT,      GID_MAGIC_LARGE,      0x52, 0x6E, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_MAGIC_LARGE),
        GET_ITEM(ITEM_WALLET_ADULT,     OBJECT_GI_PURSE,         GID_WALLET_ADULT,     0x5E, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_WALLET_ADULT),
        GET_ITEM(ITEM_WALLET_GIANT,     OBJECT_GI_PURSE,         GID_WALLET_GIANT,     0x5F, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_WALLET_GIANT),
        GET_ITEM(ITEM_WEIRD_EGG,        OBJECT_GI_EGG,           GID_EGG,              0x9A, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_WEIRD_EGG),
        GET_ITEM(ITEM_HEART,            OBJECT_GI_HEART,         GID_HEART,            0x55, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_HEART),
        GET_ITEM(ITEM_ARROWS_SMALL,     OBJECT_GI_ARROW,         GID_ARROWS_SMALL,     0xE6, 0x48, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_ARROWS_SMALL),
        GET_ITEM(ITEM_ARROWS_MEDIUM,    OBJECT_GI_ARROW,         GID_ARROWS_MEDIUM,    0xE6, 0x49, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_ARROWS_MEDIUM),
        GET_ITEM(ITEM_ARROWS_LARGE,     OBJECT_GI_ARROW,         GID_ARROWS_LARGE,     0xE6, 0x4A, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_ARROWS_LARGE),
        GET_ITEM(ITEM_RUPEE_GREEN,      OBJECT_GI_RUPY,          GID_RUPEE_GREEN,      0x6F, 0x00, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_RUPEE_GREEN),
        GET_ITEM(ITEM_RUPEE_BLUE,       OBJECT_GI_RUPY,          GID_RUPEE_BLUE,       0xCC, 0x01, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_RUPEE_BLUE),
        GET_ITEM(ITEM_RUPEE_RED,        OBJECT_GI_RUPY,          GID_RUPEE_RED,        0xF0, 0x02, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_RUPEE_RED),
        GET_ITEM(ITEM_HEART_CONTAINER,  OBJECT_GI_HEARTS,        GID_HEART_CONTAINER,  0xC6, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_HEALTH,          MOD_NONE, GI_HEART_CONTAINER_2),
        GET_ITEM(ITEM_MILK,             OBJECT_GI_MILK,          GID_MILK,             0x98, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_MILK),
        GET_ITEM(ITEM_MASK_GORON,       OBJECT_GI_GOLONMASK,     GID_MASK_GORON,       0x14, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MASK_GORON),
        GET_ITEM(ITEM_MASK_ZORA,        OBJECT_GI_ZORAMASK,      GID_MASK_ZORA,        0x15, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MASK_ZORA),
        GET_ITEM(ITEM_MASK_GERUDO,      OBJECT_GI_GERUDOMASK,    GID_MASK_GERUDO,      0x16, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MASK_GERUDO),
        GET_ITEM(ITEM_BRACELET,         OBJECT_GI_BRACELET,      GID_BRACELET,         0x79, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BRACELET),
        GET_ITEM(ITEM_RUPEE_PURPLE,     OBJECT_GI_RUPY,          GID_RUPEE_PURPLE,     0xF1, 0x14, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_RUPEE_PURPLE),
        GET_ITEM(ITEM_RUPEE_GOLD,       OBJECT_GI_RUPY,          GID_RUPEE_GOLD,       0xF2, 0x13, CHEST_ANIM_SHORT, ITEM_CATEGORY_LESSER,          MOD_NONE, GI_RUPEE_GOLD),
        GET_ITEM(ITEM_SWORD_BGS,        OBJECT_GI_LONGSWORD,     GID_SWORD_BGS,        0x0C, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SWORD_BGS),
        GET_ITEM(ITEM_ARROW_FIRE,       OBJECT_GI_M_ARROW,       GID_ARROW_FIRE,       0x70, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_ARROW_FIRE),
        GET_ITEM(ITEM_ARROW_ICE,        OBJECT_GI_M_ARROW,       GID_ARROW_ICE,        0x71, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_ARROW_ICE),
        GET_ITEM(ITEM_ARROW_LIGHT,      OBJECT_GI_M_ARROW,       GID_ARROW_LIGHT,      0x72, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_ARROW_LIGHT),
        GET_ITEM(ITEM_SKULL_TOKEN,      OBJECT_GI_SUTARU,        GID_SKULL_TOKEN,      0xB4, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_SKULLTULA_TOKEN, MOD_NONE, GI_SKULL_TOKEN),
        GET_ITEM(ITEM_DINS_FIRE,        OBJECT_GI_GODDESS,       GID_DINS_FIRE,        0xAD, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_DINS_FIRE),
        GET_ITEM(ITEM_FARORES_WIND,     OBJECT_GI_GODDESS,       GID_FARORES_WIND,     0xAE, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_FARORES_WIND),
        GET_ITEM(ITEM_NAYRUS_LOVE,      OBJECT_GI_GODDESS,       GID_NAYRUS_LOVE,      0xAF, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_NAYRUS_LOVE),
        GET_ITEM(ITEM_BULLET_BAG_30,    OBJECT_GI_DEKUPOUCH,     GID_BULLET_BAG,       0x07, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_BULLET_BAG_30),
        GET_ITEM(ITEM_BULLET_BAG_40,    OBJECT_GI_DEKUPOUCH,     GID_BULLET_BAG,       0x07, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_BULLET_BAG_40),
        GET_ITEM(ITEM_STICKS_5,         OBJECT_GI_STICK,         GID_STICK,            0x37, 0x0D, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_STICKS_5),
        GET_ITEM(ITEM_STICKS_10,        OBJECT_GI_STICK,         GID_STICK,            0x37, 0x0D, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_STICKS_10),
        GET_ITEM(ITEM_NUTS_5,           OBJECT_GI_NUTS,          GID_NUTS,             0x34, 0x0C, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_NUTS_5_2),
        GET_ITEM(ITEM_NUTS_10,          OBJECT_GI_NUTS,          GID_NUTS,             0x34, 0x0C, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_NUTS_10),
        GET_ITEM(ITEM_BOMB,             OBJECT_GI_BOMB_1,        GID_BOMB,             0x32, 0x59, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_BOMBS_1),
        GET_ITEM(ITEM_BOMBS_10,         OBJECT_GI_BOMB_1,        GID_BOMB,             0x32, 0x59, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_BOMBS_10),
        GET_ITEM(ITEM_BOMBS_20,         OBJECT_GI_BOMB_1,        GID_BOMB,             0x32, 0x59, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_BOMBS_20),
        GET_ITEM(ITEM_BOMBS_30,         OBJECT_GI_BOMB_1,        GID_BOMB,             0x32, 0x59, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_BOMBS_30),
        GET_ITEM(ITEM_SEEDS_30,         OBJECT_GI_SEED,          GID_SEEDS,            0xDC, 0x50, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_SEEDS_30),
        GET_ITEM(ITEM_BOMBCHUS_5,       OBJECT_GI_BOMB_2,        GID_BOMBCHU,          0x33, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOMBCHUS_5),
        GET_ITEM(ITEM_BOMBCHUS_20,      OBJECT_GI_BOMB_2,        GID_BOMBCHU,          0x33, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOMBCHUS_20),
        GET_ITEM(ITEM_FISH,             OBJECT_GI_FISH,          GID_FISH,             0x47, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_FISH),
        GET_ITEM(ITEM_BUG,              OBJECT_GI_INSECT,        GID_BUG,              0x7A, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_BUGS),
        GET_ITEM(ITEM_BLUE_FIRE,        OBJECT_GI_FIRE,          GID_BLUE_FIRE,        0x5D, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_BLUE_FIRE),
        GET_ITEM(ITEM_POE,              OBJECT_GI_GHOST,         GID_POE,              0x97, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_POE),
        GET_ITEM(ITEM_BIG_POE,          OBJECT_GI_GHOST,         GID_BIG_POE,          0xF9, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_BIG_POE),
        GET_ITEM(ITEM_KEY_SMALL,        OBJECT_GI_KEY,           GID_KEY_SMALL,        0xF3, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_SMALL_KEY,       MOD_NONE, GI_DOOR_KEY),
        GET_ITEM(ITEM_RUPEE_GREEN,      OBJECT_GI_RUPY,          GID_RUPEE_GREEN,      0xF4, 0x00, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_RUPEE_GREEN_LOSE),
        GET_ITEM(ITEM_RUPEE_BLUE,       OBJECT_GI_RUPY,          GID_RUPEE_BLUE,       0xF5, 0x01, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_RUPEE_BLUE_LOSE),
        GET_ITEM(ITEM_RUPEE_RED,        OBJECT_GI_RUPY,          GID_RUPEE_RED,        0xF6, 0x02, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_RUPEE_RED_LOSE),
        GET_ITEM(ITEM_RUPEE_PURPLE,     OBJECT_GI_RUPY,          GID_RUPEE_PURPLE,     0xF7, 0x14, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_RUPEE_PURPLE_LOSE),
        GET_ITEM(ITEM_HEART_PIECE_2,    OBJECT_GI_HEARTS,        GID_HEART_PIECE,      0xFA, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_HEALTH,          MOD_NONE, GI_HEART_PIECE_WIN),
        GET_ITEM(ITEM_STICK_UPGRADE_20, OBJECT_GI_STICK,         GID_STICK,            0x90, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_LESSER,          MOD_NONE, GI_STICK_UPGRADE_20),
        GET_ITEM(ITEM_STICK_UPGRADE_30, OBJECT_GI_STICK,         GID_STICK,            0x91, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_LESSER,          MOD_NONE, GI_STICK_UPGRADE_30),
        GET_ITEM(ITEM_NUT_UPGRADE_30,   OBJECT_GI_NUTS,          GID_NUTS,             0xA7, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_LESSER,          MOD_NONE, GI_NUT_UPGRADE_30),
        GET_ITEM(ITEM_NUT_UPGRADE_40,   OBJECT_GI_NUTS,          GID_NUTS,             0xA8, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_LESSER,          MOD_NONE, GI_NUT_UPGRADE_40),
        GET_ITEM(ITEM_BULLET_BAG_50,    OBJECT_GI_DEKUPOUCH,     GID_BULLET_BAG_50,    0x6C, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_BULLET_BAG_50),
        GET_ITEM_NONE,
        GET_ITEM_NONE,
        GET_ITEM_NONE // GI_MAX - if you need to add to this table insert it before this entry.
        // clang-format on
    };
    ItemTableManager::Instance->AddItemTable(MOD_NONE);
    for (uint8_t i = 0; i < ARRAY_COUNT(getItemTable); i++) {
        // The vanilla item table array started with ITEM_BOMBS_5,
        // but the GetItemID enum started with GI_NONE. Then everywhere
        // that table was accessed used `GetItemID - 1`. This allows the
        // "first" item of the new map to start at 1, syncing it up with
        // the GetItemID values and removing the need for the `- 1`
        ItemTableManager::Instance->AddItemEntry(MOD_NONE, i + 1, getItemTable[i]);
    }
}

std::unordered_map<ItemID, GetItemID> ItemIDtoGetItemIDMap{
    { ITEM_ARROWS_LARGE, GI_ARROWS_LARGE },
    { ITEM_ARROWS_MEDIUM, GI_ARROWS_MEDIUM },
    { ITEM_ARROWS_SMALL, GI_ARROWS_SMALL },
    { ITEM_ARROW_FIRE, GI_ARROW_FIRE },
    { ITEM_ARROW_ICE, GI_ARROW_ICE },
    { ITEM_ARROW_LIGHT, GI_ARROW_LIGHT },
    { ITEM_BEAN, GI_BEAN },
    { ITEM_BIG_POE, GI_BIG_POE },
    { ITEM_BLUE_FIRE, GI_BLUE_FIRE },
    { ITEM_BOMB, GI_BOMBS_1 },
    { ITEM_BOMBCHU, GI_BOMBCHUS_10 },
    { ITEM_BOMBCHUS_20, GI_BOMBCHUS_20 },
    { ITEM_BOMBCHUS_5, GI_BOMBCHUS_5 },
    { ITEM_BOMBS_10, GI_BOMBS_10 },
    { ITEM_BOMBS_20, GI_BOMBS_20 },
    { ITEM_BOMBS_30, GI_BOMBS_30 },
    { ITEM_BOMBS_5, GI_BOMBS_5 },
    { ITEM_BOMB_BAG_20, GI_BOMB_BAG_20 },
    { ITEM_BOMB_BAG_30, GI_BOMB_BAG_30 },
    { ITEM_BOMB_BAG_40, GI_BOMB_BAG_40 },
    { ITEM_BOOMERANG, GI_BOOMERANG },
    { ITEM_BOOTS_HOVER, GI_BOOTS_HOVER },
    { ITEM_BOOTS_IRON, GI_BOOTS_IRON },
    { ITEM_BOTTLE, GI_BOTTLE },
    { ITEM_BOW, GI_BOW },
    { ITEM_BRACELET, GI_BRACELET },
    { ITEM_BUG, GI_BUGS },
    { ITEM_BULLET_BAG_30, GI_BULLET_BAG_30 },
    { ITEM_BULLET_BAG_40, GI_BULLET_BAG_40 },
    { ITEM_BULLET_BAG_50, GI_BULLET_BAG_50 },
    { ITEM_CHICKEN, GI_CHICKEN },
    { ITEM_CLAIM_CHECK, GI_CLAIM_CHECK },
    { ITEM_COJIRO, GI_COJIRO },
    { ITEM_COMPASS, GI_COMPASS },
    { ITEM_DINS_FIRE, GI_DINS_FIRE },
    { ITEM_DUNGEON_MAP, GI_MAP },
    { ITEM_EYEDROPS, GI_EYEDROPS },
    { ITEM_FAIRY, GI_FAIRY },
    { ITEM_FARORES_WIND, GI_FARORES_WIND },
    { ITEM_FISH, GI_FISH },
    { ITEM_FROG, GI_FROG },
    { ITEM_GAUNTLETS_GOLD, GI_GAUNTLETS_GOLD },
    { ITEM_GAUNTLETS_SILVER, GI_GAUNTLETS_SILVER },
    { ITEM_GERUDO_CARD, GI_GERUDO_CARD },
    { ITEM_HAMMER, GI_HAMMER },
    { ITEM_HEART, GI_HEART },
    { ITEM_HEART_CONTAINER, GI_HEART_CONTAINER },
    { ITEM_HEART_CONTAINER, GI_HEART_CONTAINER_2 },
    { ITEM_HEART_PIECE_2, GI_HEART_PIECE },
    { ITEM_HEART_PIECE_2, GI_HEART_PIECE_WIN },
    { ITEM_HOOKSHOT, GI_HOOKSHOT },
    { ITEM_KEY_BOSS, GI_KEY_BOSS },
    { ITEM_KEY_SMALL, GI_DOOR_KEY },
    { ITEM_KEY_SMALL, GI_KEY_SMALL },
    { ITEM_LENS, GI_LENS },
    { ITEM_LETTER_RUTO, GI_LETTER_RUTO },
    { ITEM_LETTER_ZELDA, GI_LETTER_ZELDA },
    { ITEM_LONGSHOT, GI_LONGSHOT },
    { ITEM_MAGIC_LARGE, GI_MAGIC_LARGE },
    { ITEM_MAGIC_SMALL, GI_MAGIC_SMALL },
    { ITEM_MASK_BUNNY, GI_MASK_BUNNY },
    { ITEM_MASK_GERUDO, GI_MASK_GERUDO },
    { ITEM_MASK_GORON, GI_MASK_GORON },
    { ITEM_MASK_KEATON, GI_MASK_KEATON },
    { ITEM_MASK_SKULL, GI_MASK_SKULL },
    { ITEM_MASK_SPOOKY, GI_MASK_SPOOKY },
    { ITEM_MASK_TRUTH, GI_MASK_TRUTH },
    { ITEM_MASK_ZORA, GI_MASK_ZORA },
    { ITEM_MILK, GI_MILK },
    { ITEM_MILK_BOTTLE, GI_MILK_BOTTLE },
    { ITEM_NAYRUS_LOVE, GI_NAYRUS_LOVE },
    { ITEM_NUT, GI_NUTS_5 },
    { ITEM_NUTS_10, GI_NUTS_10 },
    { ITEM_NUTS_5, GI_NUTS_5 },
    { ITEM_NUTS_5, GI_NUTS_5_2 },
    { ITEM_NUT_UPGRADE_30, GI_NUT_UPGRADE_30 },
    { ITEM_NUT_UPGRADE_40, GI_NUT_UPGRADE_40 },
    { ITEM_OCARINA_FAIRY, GI_OCARINA_FAIRY },
    { ITEM_OCARINA_TIME, GI_OCARINA_OOT },
    { ITEM_ODD_MUSHROOM, GI_ODD_MUSHROOM },
    { ITEM_ODD_POTION, GI_ODD_POTION },
    { ITEM_POCKET_CUCCO, GI_POCKET_CUCCO },
    { ITEM_POCKET_EGG, GI_POCKET_EGG },
    { ITEM_POE, GI_POE },
    { ITEM_POTION_BLUE, GI_POTION_BLUE },
    { ITEM_POTION_GREEN, GI_POTION_GREEN },
    { ITEM_POTION_RED, GI_POTION_RED },
    { ITEM_PRESCRIPTION, GI_PRESCRIPTION },
    { ITEM_QUIVER_40, GI_QUIVER_40 },
    { ITEM_QUIVER_50, GI_QUIVER_50 },
    { ITEM_RUPEE_BLUE, GI_RUPEE_BLUE },
    { ITEM_RUPEE_BLUE, GI_RUPEE_BLUE_LOSE },
    { ITEM_RUPEE_GOLD, GI_RUPEE_GOLD },
    { ITEM_RUPEE_GREEN, GI_RUPEE_GREEN },
    { ITEM_RUPEE_GREEN, GI_RUPEE_GREEN_LOSE },
    { ITEM_RUPEE_PURPLE, GI_RUPEE_PURPLE },
    { ITEM_RUPEE_PURPLE, GI_RUPEE_PURPLE_LOSE },
    { ITEM_RUPEE_RED, GI_RUPEE_RED },
    { ITEM_RUPEE_RED, GI_RUPEE_RED_LOSE },
    { ITEM_SAW, GI_SAW },
    { ITEM_SCALE_GOLDEN, GI_SCALE_GOLDEN },
    { ITEM_SCALE_SILVER, GI_SCALE_SILVER },
    { ITEM_SEEDS, GI_SEEDS_5 },
    { ITEM_SEEDS_30, GI_SEEDS_30 },
    { ITEM_SHIELD_DEKU, GI_SHIELD_DEKU },
    { ITEM_SHIELD_HYLIAN, GI_SHIELD_HYLIAN },
    { ITEM_SHIELD_MIRROR, GI_SHIELD_MIRROR },
    { ITEM_SKULL_TOKEN, GI_SKULL_TOKEN },
    { ITEM_SLINGSHOT, GI_SLINGSHOT },
    { ITEM_STICK, GI_STICKS_1 },
    { ITEM_STICKS_10, GI_STICKS_10 },
    { ITEM_STICKS_5, GI_STICKS_5 },
    { ITEM_STICK_UPGRADE_20, GI_STICK_UPGRADE_20 },
    { ITEM_STICK_UPGRADE_30, GI_STICK_UPGRADE_30 },
    { ITEM_STONE_OF_AGONY, GI_STONE_OF_AGONY },
    { ITEM_SWORD_BGS, GI_SWORD_BGS },
    { ITEM_SWORD_BGS, GI_SWORD_KNIFE },
    { ITEM_SWORD_BROKEN, GI_SWORD_BROKEN },
    { ITEM_SWORD_KOKIRI, GI_SWORD_KOKIRI },
    { ITEM_TUNIC_GORON, GI_TUNIC_GORON },
    { ITEM_TUNIC_ZORA, GI_TUNIC_ZORA },
    { ITEM_WALLET_ADULT, GI_WALLET_ADULT },
    { ITEM_WALLET_GIANT, GI_WALLET_GIANT },
    { ITEM_WEIRD_EGG, GI_WEIRD_EGG },
};

extern "C" GetItemID RetrieveGetItemIDFromItemID(ItemID itemID) {
    if (ItemIDtoGetItemIDMap.contains(itemID)) {
        return ItemIDtoGetItemIDMap.at(itemID);
    }
    return GI_MAX;
}

std::unordered_map<ItemID, RandomizerGet> ItemIDtoRandomizerGetMap{
    { ITEM_SONG_MINUET, RG_MINUET_OF_FOREST },
    { ITEM_SONG_BOLERO, RG_BOLERO_OF_FIRE },
    { ITEM_SONG_SERENADE, RG_SERENADE_OF_WATER },
    { ITEM_SONG_REQUIEM, RG_REQUIEM_OF_SPIRIT },
    { ITEM_SONG_NOCTURNE, RG_NOCTURNE_OF_SHADOW },
    { ITEM_SONG_PRELUDE, RG_PRELUDE_OF_LIGHT },
    { ITEM_SONG_LULLABY, RG_ZELDAS_LULLABY },
    { ITEM_SONG_EPONA, RG_EPONAS_SONG },
    { ITEM_SONG_SARIA, RG_SARIAS_SONG },
    { ITEM_SONG_SUN, RG_SUNS_SONG },
    { ITEM_SONG_TIME, RG_SONG_OF_TIME },
    { ITEM_SONG_STORMS, RG_SONG_OF_STORMS },
    { ITEM_MEDALLION_FOREST, RG_FOREST_MEDALLION },
    { ITEM_MEDALLION_FIRE, RG_FIRE_MEDALLION },
    { ITEM_MEDALLION_WATER, RG_WATER_MEDALLION },
    { ITEM_MEDALLION_SPIRIT, RG_SPIRIT_MEDALLION },
    { ITEM_MEDALLION_SHADOW, RG_SHADOW_MEDALLION },
    { ITEM_MEDALLION_LIGHT, RG_LIGHT_MEDALLION },
    { ITEM_KOKIRI_EMERALD, RG_KOKIRI_EMERALD },
    { ITEM_GORON_RUBY, RG_GORON_RUBY },
    { ITEM_ZORA_SAPPHIRE, RG_ZORA_SAPPHIRE },
    { ITEM_SWORD_MASTER, RG_MASTER_SWORD },
};

extern "C" RandomizerGet RetrieveRandomizerGetFromItemID(ItemID itemID) {
    if (ItemIDtoRandomizerGetMap.contains(itemID)) {
        return ItemIDtoRandomizerGetMap.at(itemID);
    }
    return RG_MAX;
}

extern "C" void OTRExtScanner() {
    auto lst = *Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->ListFiles().get();

    for (auto& rPath : lst) {
        std::vector<std::string> raw = StringHelper::Split(rPath, ".");
        std::string ext = raw[raw.size() - 1];
        std::string nPath = rPath.substr(0, rPath.size() - (ext.size() + 1));
        replace(nPath.begin(), nPath.end(), '\\', '/');

        ExtensionCache[nPath] = { rPath, ext };
    }
}

// Read the port version from an OTR file
OTRVersion ReadPortVersionFromOTR(std::string otrPath) {
    OTRVersion version = {};

    // Use a temporary archive instance to load the otr and read the version file
    auto archive = std::make_shared<Ship::O2rArchive>(otrPath);
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

// Checks the program version stored in the otr and compares the major value to soh
// For Windows/Mac/Linux if the version doesn't match, offer to
OTRVersion DetectOTRVersion(std::string fileName, bool isMQ) {
    bool isOtrOld = false;
    std::string otrPath = Ship::Context::LocateFileAcrossAppDirs(fileName, appShortName);

    // Doesn't exist so nothing to do here
    if (!std::filesystem::exists(otrPath)) {
        return { INT16_MAX, INT16_MAX, INT16_MAX };
    }

    return ReadPortVersionFromOTR(otrPath);
}

extern "C" void Messagebox_ShowErrorBox(char* title, char* body) {
    Extractor::ShowErrorBox(title, body);
}

bool VerifyArchiveVersion(OTRVersion version) {
    return version.major != INT16_MAX && version.major != gBuildVersionMajor;
}

// ComboShip: forward declaration — defined further down with the combo exports.
extern "C" void (*gComboSceneSwitchCallback)(int fileNum);

// ComboShip: InitOTR is split so the launcher can create the shared window (which needs only the
// bundled soh.o2r, not a ROM) BEFORE the ROM archives exist, run its own unified extraction screen,
// then finish init once oot.o2r is present. SOH_InitWindowOnly() = the OTRGlobals ctor (window +
// ImGui + SohGui::SetupMenu). Combo_FinishInit() = everything that needs the ROM archives. The
// non-combo InitOTR keeps the original ctor -> RunExtract -> finish ordering. See docs/UPSTREAM_MERGES.md.
static void Combo_FinishInit();

extern "C" void InitOTR(int argc, char* argv[]) {
    OTRGlobals::Instance = new OTRGlobals();
    OTRGlobals::Instance->RunExtract(argc, argv);
    Combo_FinishInit();
}

#ifdef COMBO_BUILD
extern "C" __declspec(dllexport) void SOH_InitWindowOnly() {
    OTRGlobals::Instance = new OTRGlobals();
}
extern "C" __declspec(dllexport) void SOH_FinishInit() {
    Combo_FinishInit();
}
#endif

static void Combo_FinishInit() {
    OTRGlobals::Instance->Initialize();
    CustomMessageManager::Instance = new CustomMessageManager();
    ItemTableManager::Instance = new ItemTableManager();
    GameInteractor::Instance = new GameInteractor();
    SaveManager::Instance = new SaveManager();

    std::shared_ptr<Ship::Config> conf = OTRGlobals::Instance->context->GetConfig();
    conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion1Updater>());
    conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion2Updater>());
    conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion3Updater>());
    conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion4Updater>());
    conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion5Updater>());
    conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion6Updater>());
    conf->RunVersionUpdates();

    SohGui::SetupGuiElements();
    SohGui::SetupMenuElements();

    AudioCollection::Instance = new AudioCollection();
    ActorDB::Instance = new ActorDB();
#ifdef __APPLE__
    SpeechSynthesizer::Instance = new DarwinSpeechSynthesizer();
#elif defined(_WIN32)
    SpeechSynthesizer::Instance = new SAPISpeechSynthesizer();
#elif ESPEAK
    SpeechSynthesizer::Instance = new ESpeakSpeechSynthesizer();
#else
    SpeechSynthesizer::Instance = new SpeechLogger();
#endif
    SpeechSynthesizer::Instance->Init();

    CrowdControl::Instance = new CrowdControl();
    Sail::Instance = new Sail();
    Anchor::Instance = new Anchor();

    OTRMessage_Init();
    OTRAudio_Init();
    OTRExtScanner();
    VanillaItemTable_Init();
    DebugConsole_Init();

    ActorDB::AddBuiltInCustomActors();
    // #region SOH [Randomizer] TODO: Remove these and refactor spoiler file handling for randomizer
    CVarClear(CVAR_GENERAL("RandomizerNewFileDropped"));
    CVarClear(CVAR_GENERAL("RandomizerDroppedFile"));
    // #endregion

    Ship::Context::GetRawInstance()->GetFileDropMgr()->RegisterDropHandler(SoH_HandleConfigDrop);

#ifdef COMBO_BUILD
    // Flag set when we want to switch to MM. Acted on at the start of the next clean frame.
    static bool sComboSwitchPending = false;
    static int sComboSwitchFileNum = -1;

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t sceneNum) {
        // Cross-game OOT->MM trigger: entering the Happy Mask Shop.
        if (sceneNum == SCENE_HAPPY_MASK_SHOP) {
            sComboSwitchFileNum = (int)gSaveContext.fileNum;
            sComboSwitchPending = true;
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if (!sComboSwitchPending)
            return;
        sComboSwitchPending = false;
        SaveManager::Instance->SaveFile(sComboSwitchFileNum);
        SaveManager::Instance->ThreadPoolWait();
        if (gComboSceneSwitchCallback) {
            gComboSceneSwitchCallback(sComboSwitchFileNum);
        }
        if (gGameState) {
            gGameState->running = false;
        }
    });
#endif

    RegisterImGuiItemIcons();

    time_t now = time(NULL);
    tm* tm_now = localtime(&now);
    if (tm_now->tm_mon == 11 && tm_now->tm_mday >= 24 && tm_now->tm_mday <= 25) {
        CVarRegisterInteger(CVAR_GENERAL("LetItSnow"), 1);
    } else {
        CVarClear(CVAR_GENERAL("LetItSnow"));
    }

    srand(static_cast<unsigned int>(now));
    SDLNet_Init();
    if (CVarGetInteger(CVAR_REMOTE_CROWD_CONTROL("Enabled"), 0)) {
        CrowdControl::Instance->Enable();
    }
    if (CVarGetInteger(CVAR_REMOTE_SAIL("Enabled"), 0)) {
        Sail::Instance->Enable();
    }
    if (CVarGetInteger(CVAR_REMOTE_ANCHOR("Enabled"), 0)) {
        Anchor::Instance->Enable();
    }
    ShipInit::InitAll();
    Rando::StaticData::InitHashMaps();
    OTRGlobals::Instance->gRandoContext->AddExcludedOptions();
}

extern "C" void SaveManager_ThreadPoolWait() {
    SaveManager::Instance->ThreadPoolWait();
}

extern "C" void DeinitOTR() {
    SaveManager_ThreadPoolWait();
    OTRAudio_Exit();
    if (CVarGetInteger(CVAR_REMOTE_CROWD_CONTROL("Enabled"), 0)) {
        CrowdControl::Instance->Disable();
    }
    if (CVarGetInteger(CVAR_REMOTE_SAIL("Enabled"), 0)) {
        Sail::Instance->Disable();
    }
    if (CVarGetInteger(CVAR_REMOTE_ANCHOR("Enabled"), 0)) {
        Anchor::Instance->Disable();
    }
    SDLNet_Quit();

    // Destroying gui here because we have shared ptrs to LUS objects which output to SPDLOG which is destroyed before
    // these shared ptrs.
    SohGui::Destroy();
    sohFast3dWindow = nullptr;

#ifdef COMBO_BUILD
    // ComboShip: drop the resident-RM refs now so the ResourceManager is destroyed here on the
    // main thread. Left in these statics / the registry, it would die during DLL-unload static
    // destructors, where its thread pool joins workers under the loader lock and deadlocks.
    Ship::CrossRMRegistry::Unregister("oot");
    sOOTResourceManager = nullptr;
#endif

    OTRGlobals::Instance->context = nullptr;
#ifdef COMBO_BUILD
    // ComboShip: ~Context ran ImGui::DestroyContext, but that only nulls libultraship's GImGui —
    // this DLL's module-local GImGui still points at the freed context. soh statics destroyed at
    // process exit call ImGui::MemFree, which derefs GImGui -> crash after main returns. Null it now.
    ImGui::SetCurrentContext(nullptr);
#endif
}

#ifdef COMBO_BUILD
// ComboShip: stop OOT audio and wait for pending saves WITHOUT destroying the context or window.
// Called before launching MM so archives can be safely swapped.
// declspec must follow the extern "C" specifier: the split form (`__declspec(dllexport)` on its
// own line BEFORE `extern "C"`) is silently ignored by MSVC (C4091), so this function would not be
// exported and the MM boot gate would fail.
extern "C" __declspec(dllexport) void SOH_PrepareForTransition(void) {
    SaveManager_ThreadPoolWait();
    OTRAudio_Exit();
    // ComboShip: do NOT SohGui::Destroy() here. The Gui is a single shared libultraship instance that
    // persists across transitions; OOT's windows are set up once at first boot and stay resident (same
    // model MM uses — see MM_PrepareForTransition). Tearing them down would force a rebuild on resume,
    // but the shared Gui still holds the old windows so AddGuiWindow rejects the duplicates; the
    // rejected windows never get InitElement'd, and freeing their uninitialized buffers crashes
    // (0xCD heap marker). Context, window, and resource manager are kept alive for MM.
}
#endif

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
    return (uint64_t)millis.count();
}

extern "C" void Graph_StartFrame() {
#ifndef __WIIU__
    using Ship::KbScancode;
    int32_t dwScancode = OTRGlobals::Instance->context->GetWindow()->GetLastScancode();
    OTRGlobals::Instance->context->GetWindow()->SetLastScancode(-1);

    switch (dwScancode) {
        case KbScancode::LUS_KB_F1: {
            std::shared_ptr<SohModalWindow> modal = static_pointer_cast<SohModalWindow>(
                std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                    ->GetGuiWindow("Modal Window"));
            if (modal->IsPopupOpen("Menu Moved")) {
                modal->DismissPopup();
            } else {
                modal->RegisterPopup("Menu Moved",
                                     "The menubar, accessed by hitting F1, no longer exists.\nThe new menu can be "
                                     "accessed by hitting the Esc button instead.",
                                     "OK");
            }
            break;
        }
        case KbScancode::LUS_KB_F5: {
            if (CVarGetInteger(CVAR_CHEAT("SaveStatesEnabled"), 0) == 0) {
                std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                    ->GetGameOverlay()
                    ->TextDrawNotification(6.0f, true, "Save states not enabled. Check Cheats Menu.");
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
            if (CVarGetInteger(CVAR_CHEAT("SaveStatesEnabled"), 0) == 0) {
                std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                    ->GetGameOverlay()
                    ->TextDrawNotification(6.0f, true, "Save states not enabled. Check Cheats Menu.");
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
            if (CVarGetInteger(CVAR_CHEAT("SaveStatesEnabled"), 0) == 0) {
                std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                    ->GetGameOverlay()
                    ->TextDrawNotification(6.0f, true, "Save states not enabled. Check Cheats Menu.");
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
#if defined(_WIN32) || defined(__APPLE__)
        case KbScancode::LUS_KB_F9: {
            // Toggle TTS
            CVarSetInteger(CVAR_SETTING("A11yTTS"), !CVarGetInteger(CVAR_SETTING("A11yTTS"), 0));
            break;
        }
#endif
        case KbScancode::LUS_KB_TAB: {
            if (CVarGetInteger(CVAR_SETTING("Mods.AlternateAssetsHotkey"), 1)) {
                CVarSetInteger(CVAR_SETTING("AltAssets"), !CVarGetInteger(CVAR_SETTING("AltAssets"), 0));
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
        static_cast<UIWidgets::Colors>(CVarGetInteger(CVAR_SETTING("Menu.Theme"), UIWidgets::Colors::LightBlue));
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
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());

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

    // ComboShip: AltAssets default OFF (upstream defaults ON). We ship no HD/alt asset pack, so ON
    // makes the ResourceManager probe alt/<path> for every resource every frame.
    bool curAltAssets = CVarGetInteger(CVAR_SETTING("AltAssets"), 0);
    if (prevAltAssets != curAltAssets) {
        prevAltAssets = curAltAssets;
        Ship::Context::GetRawInstance()->GetResourceManager()->SetAltAssetsEnabled(curAltAssets);
        gfx_texture_cache_clear();
        SOH::SkeletonPatcher::UpdateSkeletons();
        GameInteractor::Instance->ExecuteHooks<GameInteractor::OnAssetAltChange>();
    }

    // OTRTODO: FIGURE OUT END FRAME POINT
    /* if (OTRGlobals::Instance->context->lastScancode != -1)
         OTRGlobals::Instance->context->lastScancode = -1;*/
}

extern "C" void OTRGetPixelDepthPrepare(float x, float y) {
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    if (wnd == nullptr) {
        return;
    }

    wnd->GetPixelDepthPrepare(x, y);
}

extern "C" uint16_t OTRGetPixelDepth(float x, float y) {
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    if (wnd == nullptr) {
        return 0;
    }

    return wnd->GetPixelDepth(x, y);
}

extern "C" Sprite* GetSeedTexture(uint8_t index) {
    return OTRGlobals::Instance->gRandoContext->GetSeedTexture(index);
}

extern "C" uint8_t GetSeedIconIndex(uint8_t index) {
    return OTRGlobals::Instance->gRandoContext->hashIconIndexes[index];
}

std::map<std::string, SoundFontSample*> cachedCustomSFs;

ImFont* OTRGlobals::CreateFontWithSize(float size, std::string fontPath, bool isJapaneseFont) {
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
            Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(fontPath, false, initData));
        ImFontConfig fontConf;
        fontConf.FontDataOwnedByAtlas = false;
        const ImWchar* glyph_ranges = isJapaneseFont ? mImGuiIo->Fonts->GetGlyphRangesJapanese() : nullptr;
        font = mImGuiIo->Fonts->AddFontFromMemoryTTF(fontData->Data, static_cast<int>(fontData->DataSize), size,
                                                     &fontConf, glyph_ranges);
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

extern "C" void Ctx_ReadSaveFile(uintptr_t addr, void* dramAddr, size_t size) {
    SaveManager::ReadSaveFile(GetSaveFile(), addr, dramAddr, size);
}

extern "C" void Ctx_WriteSaveFile(uintptr_t addr, void* dramAddr, size_t size) {
    SaveManager::WriteSaveFile(GetSaveFile(), addr, dramAddr, size);
}

std::wstring StringToU16(const std::string& s) {
    std::vector<unsigned long> result;
    size_t i = 0;

    while (i < s.size()) {
        unsigned long uni;
        size_t nbytes = 0;
        bool error = false;
        unsigned char c = s[i++];
        if (c < 0x80) { // ascii
            uni = c;
            nbytes = 0;
        } else if (c == GFXP_HIRAGANA_CHAR) { // Start Hiragana Mode
            uni = c;
            nbytes = 0;
        } else if (c == GFXP_KATAKANA_CHAR) { // Start Katakana Mode
            uni = c;
            nbytes = 0;
        } else if (c <= 0xBF) { // Invalid Characters (Skipped)
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

extern "C" void OTRGfxPrint(const char* str, void* printer, void (*printImpl)(void*, char)) {
    const std::vector<uint32_t> hira1 = {
        u'を', u'ぁ', u'ぃ', u'ぅ', u'ぇ', u'ぉ', u'ゃ', u'ゅ', u'ょ', u'っ', u'-',  u'あ', u'い',
        u'う', u'え', u'お', u'か', u'き', u'く', u'け', u'こ', u'さ', u'し', u'す', u'せ', u'そ',
    };

    const std::vector<uint32_t> hira2 = {
        u'た', u'ち', u'つ', u'て', u'と', u'な', u'に', u'ぬ', u'ね', u'の', u'は', u'ひ', u'ふ', u'へ', u'ほ', u'ま',
        u'み', u'む', u'め', u'も', u'や', u'ゆ', u'よ', u'ら', u'り', u'る', u'れ', u'ろ', u'わ', u'ん', u'゛', u'゜',
    };

    const std::vector<uint32_t> kata1 = {
        u'ヲ', u'ァ', u'ィ', u'ゥ', u'ェ', u'ォ', u'ャ', u'ュ', u'ョ', u'ッ', u'ー',
    };

    const std::vector<uint32_t> kata2 = {
        u'ア', u'イ', u'ウ', u'エ', u'オ', u'カ', u'キ', u'ク', u'ケ', u'コ', u'サ', u'シ', u'ス', u'セ', u'ソ',
        u'タ', u'チ', u'ツ', u'テ', u'ト', u'ナ', u'ニ', u'ヌ', u'ネ', u'ノ', u'ハ', u'ヒ', u'フ', u'ヘ', u'ホ',
        u'マ', u'ミ', u'ム', u'メ', u'モ', u'ヤ', u'ユ', u'ヨ', u'ラ', u'リ', u'ル', u'レ', u'ロ', u'ワ', u'ン',
    };

    std::wstring wstr = StringToU16(str);
    bool hiraganaMode = false;

    for (const auto& c : wstr) {
        if (c < 0x80) {
            printImpl(printer, static_cast<char>(c));
        } else if (c == GFXP_HIRAGANA_CHAR) {
            hiraganaMode = true;
        } else if (c == GFXP_KATAKANA_CHAR) {
            hiraganaMode = false;
        } else if (c >= u'｡' && c <= u'ﾟ') { // katakana (hankaku)
            if (hiraganaMode && c >= u'ｦ' && c <= u'ｿ') {
                printImpl(printer, c - 0xFEC0 - 0x20); // Hiragana Mode, Block 1
            } else if (hiraganaMode && c >= u'ﾀ' && c <= u'ﾝ') {
                printImpl(printer, c - 0xFEC0 + 0x20); // Hiragana Mode, Block 2
            } else {
                printImpl(printer, c - 0xFEC0);
            }
        } else if (c == u'　') { // zenkaku space
            printImpl(printer, u' ');
        } else {
            auto it = std::find(hira1.begin(), hira1.end(), c);
            if (it != hira1.end()) { // hiragana block 1
                printImpl(printer, static_cast<char>(0x86 + std::distance(hira1.begin(), it)));
            }

            auto it2 = std::find(hira2.begin(), hira2.end(), c);
            if (it2 != hira2.end()) { // hiragana block 2
                printImpl(printer, static_cast<char>(0xe0 + std::distance(hira2.begin(), it2)));
            }

            auto it3 = std::find(kata1.begin(), kata1.end(), c);
            if (it3 != kata1.end()) { // katakana zenkaku block 1
                printImpl(printer, static_cast<char>(0xa6 + std::distance(kata1.begin(), it3)));
            }

            auto it4 = std::find(kata2.begin(), kata2.end(), c);
            if (it4 != kata2.end()) { // katakana zenkaku block 2
                printImpl(printer, static_cast<char>(0xb1 + std::distance(kata2.begin(), it4)));
            }
        }
    }
}

Color_RGB8 GetColorForControllerLED() {
    auto brightness = CVarGetFloat(CVAR_SETTING("LEDBrightness"), 1.0f) / 1.0f;
    Color_RGB8 color = { 0, 0, 0 };
    if (brightness > 0.0f) {
        LEDColorSource source =
            static_cast<LEDColorSource>(CVarGetInteger(CVAR_SETTING("LEDColorSource"), LED_SOURCE_TUNIC_ORIGINAL));
        bool criticalOverride = CVarGetInteger(CVAR_SETTING("LEDCriticalOverride"), 1);
        if (gPlayState && (source == LED_SOURCE_TUNIC_ORIGINAL || source == LED_SOURCE_TUNIC_COSMETICS)) {
            switch (CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC)) {
                case EQUIP_VALUE_TUNIC_KOKIRI:
                    color = source == LED_SOURCE_TUNIC_COSMETICS
                                ? CVarGetColor24(CVAR_COSMETIC("Link.KokiriTunic.Value"), kokiriColor)
                                : kokiriColor;
                    break;
                case EQUIP_VALUE_TUNIC_GORON:
                    color = source == LED_SOURCE_TUNIC_COSMETICS
                                ? CVarGetColor24(CVAR_COSMETIC("Link.GoronTunic.Value"), goronColor)
                                : goronColor;
                    break;
                case EQUIP_VALUE_TUNIC_ZORA:
                    color = source == LED_SOURCE_TUNIC_COSMETICS
                                ? CVarGetColor24(CVAR_COSMETIC("Link.ZoraTunic.Value"), zoraColor)
                                : zoraColor;
                    break;
            }
        }
        if (gPlayState && (source == LED_SOURCE_NAVI_ORIGINAL || source == LED_SOURCE_NAVI_COSMETICS)) {
            Actor* arrowPointedActor = gPlayState->actorCtx.targetCtx.arrowPointedActor;
            if (arrowPointedActor) {
                ActorCategory category = (ActorCategory)arrowPointedActor->category;
                switch (category) {
                    case ACTORCAT_PLAYER:
                        if (source == LED_SOURCE_NAVI_COSMETICS &&
                            CVarGetInteger(CVAR_COSMETIC("Navi.IdlePrimary.Changed"), 0)) {
                            color = CVarGetColor24(CVAR_COSMETIC("Navi.IdlePrimary.Value"), defaultIdleColor.inner);
                            break;
                        }
                        color = LEDColorDefaultNaviColorList[category].inner;
                        break;
                    case ACTORCAT_NPC:
                        if (source == LED_SOURCE_NAVI_COSMETICS &&
                            CVarGetInteger(CVAR_COSMETIC("Navi.NPCPrimary.Changed"), 0)) {
                            color = CVarGetColor24(CVAR_COSMETIC("Navi.NPCPrimary.Value"), defaultNPCColor.inner);
                            break;
                        }
                        color = LEDColorDefaultNaviColorList[category].inner;
                        break;
                    case ACTORCAT_ENEMY:
                    case ACTORCAT_BOSS:
                        if (source == LED_SOURCE_NAVI_COSMETICS &&
                            CVarGetInteger(CVAR_COSMETIC("Navi.EnemyPrimary.Changed"), 0)) {
                            color = CVarGetColor24(CVAR_COSMETIC("Navi.EnemyPrimary.Value"), defaultEnemyColor.inner);
                            break;
                        }
                        color = LEDColorDefaultNaviColorList[category].inner;
                        break;
                    default:
                        if (source == LED_SOURCE_NAVI_COSMETICS &&
                            CVarGetInteger(CVAR_COSMETIC("Navi.PropsPrimary.Changed"), 0)) {
                            color = CVarGetColor24(CVAR_COSMETIC("Navi.PropsPrimary.Value"), defaultPropsColor.inner);
                            break;
                        }
                        color = LEDColorDefaultNaviColorList[category].inner;
                }
            } else { // No target actor.
                if (source == LED_SOURCE_NAVI_COSMETICS &&
                    CVarGetInteger(CVAR_COSMETIC("Navi.IdlePrimary.Changed"), 0)) {
                    color = CVarGetColor24(CVAR_COSMETIC("Navi.IdlePrimary.Value"), defaultIdleColor.inner);
                } else {
                    color = LEDColorDefaultNaviColorList[ACTORCAT_PLAYER].inner;
                }
            }
        }
        if (source == LED_SOURCE_CUSTOM) {
            color = CVarGetColor24(CVAR_SETTING("LEDPort1Color"), { 255, 255, 255 });
        }
        if (gPlayState && (criticalOverride || source == LED_SOURCE_HEALTH)) {
            if (HealthMeter_IsCritical()) {
                color = { 0xFF, 0, 0 };
            } else if (gSaveContext.healthCapacity != 0 && source == LED_SOURCE_HEALTH) {
                if (gSaveContext.health / (float)gSaveContext.healthCapacity <= 0.4f) {
                    color = { 0xFF, 0xFF, 0 };
                } else {
                    color = { 0, 0xFF, 0 };
                }
            }
        }
        color.r = static_cast<u8>(color.r * brightness);
        color.g = static_cast<u8>(color.g * brightness);
        color.b = static_cast<u8>(color.b * brightness);
    }

    return color;
}

extern "C" void OTRControllerCallback(uint8_t rumble) {
    // We call this every tick, SDL accounts for this use and prevents driver spam
    // https://github.com/libsdl-org/SDL/blob/f17058b562c8a1090c0c996b42982721ace90903/src/joystick/SDL_joystick.c#L1114-L1144
    Ship::Context::GetRawInstance()->GetControlDeck()->GetControllerByPort(0)->GetLED()->SetLEDColor(
        GetColorForControllerLED());

    static std::shared_ptr<SohInputEditorWindow> controllerConfigWindow = nullptr;
    if (controllerConfigWindow == nullptr) {
        controllerConfigWindow = std::dynamic_pointer_cast<SohInputEditorWindow>(
            std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                ->GetGuiWindow("Controller Configuration"));
    } else if (controllerConfigWindow->TestingRumble()) {
        return;
    }

    if (rumble) {
        Ship::Context::GetRawInstance()->GetControlDeck()->GetControllerByPort(0)->GetRumble()->StartRumble();
    } else {
        Ship::Context::GetRawInstance()->GetControlDeck()->GetControllerByPort(0)->GetRumble()->StopRumble();
    }
}

extern "C" float OTRGetAspectRatio() {
    return Ship::Context::GetRawInstance()->GetWindow()->GetAspectRatio();
}

extern "C" float OTRGetDimensionFromLeftEdge(float v) {
    return (SCREEN_WIDTH / 2 - SCREEN_HEIGHT / 2 * OTRGetAspectRatio() + (v));
}

extern "C" float OTRGetDimensionFromRightEdge(float v) {
    return (SCREEN_WIDTH / 2 + SCREEN_HEIGHT / 2 * OTRGetAspectRatio() - (SCREEN_WIDTH - v));
}

// Gets the width of the current render target area
extern "C" uint32_t OTRGetGameRenderWidth() {
    auto fastWnd = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
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
    auto fastWnd = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    auto intP = fastWnd->GetInterpreterWeak().lock();

    if (!intP) {
        assert(false && "Lost reference to Fast::Interpreter");
        return 240;
    }

    uint32_t height, width;
    intP->GetCurDimensions(&width, &height);

    return height;
}

f32 floorf(f32 x); // RANDOTODO False positive error "allowing all exceptions is incompatible with previous function"
f32 ceilf(f32 x);  // This gets annoying

extern "C" int16_t OTRGetRectDimensionFromLeftEdge(float v) {
    return ((int)floorf(OTRGetDimensionFromLeftEdge(v)));
}

extern "C" int16_t OTRGetRectDimensionFromRightEdge(float v) {
    return ((int)ceilf(OTRGetDimensionFromRightEdge(v)));
}

int AudioPlayer_Buffered(void) {
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
    if (Ship::Context::GetRawInstance()
            ->GetControlDeck()
            ->GetControllerByPort(static_cast<uint8_t>(slot))
            ->GetRumble()
            ->GetAllRumbleMappings()
            .empty()) {
        return 0;
    }

    // don't rumble if we don't have connected gamepads
    if (Ship::Context::GetRawInstance()
            ->GetControlDeck()
            ->GetConnectedPhysicalDeviceManager()
            ->GetConnectedSDLGamepadsForPort(static_cast<s32>(slot))
            .empty()) {
        return 0;
    }

    // rumble
    return 1;
}

extern "C" size_t GetEquipNowMessage(char* buffer, char* src, const size_t maxBufferSize) {
    CustomMessage customMessage("\x04\x1A\x08"
                                "Would you like to equip it now?"
                                "\x09&&"
                                "\x1B%g"
                                "Yes"
                                "&"
                                "No"
                                "%w\x02",
                                "\x04\x1A\x08"
                                "M"
                                "\x9A"
                                "chtest Du es jetzt ausr\x9Esten?"
                                "\x09&&"
                                "\x1B%g"
                                "Ja!"
                                "&"
                                "Nein!"
                                "%w\x02",
                                "\x04\x1A\x08"
                                "D\x96sirez-vous l'\x96quiper maintenant?"
                                "\x09&&"
                                "\x1B%g"
                                "Oui"
                                "&"
                                "Non"
                                "%w\x02");
    customMessage.Format();

    std::string postfix = customMessage.GetForCurrentLanguage();
    std::string str;
    std::string FixedBaseStr(src);
    size_t RemoveControlChar = FixedBaseStr.find_first_of("\x02");

    if (RemoveControlChar != std::string::npos) {
        FixedBaseStr = FixedBaseStr.substr(0, RemoveControlChar);
    }
    str = FixedBaseStr + postfix;

    if (!str.empty()) {
        memset(buffer, 0, maxBufferSize);
        const size_t copiedCharLen = std::min<size_t>(maxBufferSize - 1, str.length());
        memcpy(buffer, str.c_str(), copiedCharLen);
        return copiedCharLen;
    }
    return 0;
}

extern "C" void Randomizer_ParseSpoiler(const char* fileLoc) {
    OTRGlobals::Instance->gRandoContext->ParseSpoiler(fileLoc);
}

extern "C" u32 SpoilerFileExists(const char* spoilerFileName) {
    return OTRGlobals::Instance->gRandomizer->SpoilerFileExists(spoilerFileName);
}

extern "C" u8 Randomizer_GetSettingValue(RandomizerSettingKey randoSettingKey) {
    return OTRGlobals::Instance->gRandoContext->GetOption(randoSettingKey).Get();
}

extern "C" RandomizerCheck Randomizer_GetCheckFromActor(s16 actorId, s16 sceneNum, s16 actorParams) {
    return OTRGlobals::Instance->gRandomizer->GetCheckFromActor(actorId, sceneNum, actorParams);
}

extern "C" ShopItemIdentity Randomizer_IdentifyShopItem(s32 sceneNum, u8 slotIndex) {
    return OTRGlobals::Instance->gRandomizer->IdentifyShopItem(sceneNum, slotIndex);
}

extern "C" GetItemEntry ItemTable_Retrieve(int16_t getItemID) {
    GetItemEntry giEntry = ItemTableManager::Instance->RetrieveItemEntry(MOD_NONE, getItemID);
    return giEntry;
}

extern "C" GetItemEntry ItemTable_RetrieveEntry(s16 tableID, s16 getItemID) {
    if (tableID == MOD_RANDOMIZER) {
        return Rando::StaticData::RetrieveItem(static_cast<RandomizerGet>(getItemID)).GetGIEntry_Copy();
    }
    return ItemTableManager::Instance->RetrieveItemEntry(tableID, getItemID);
}

extern "C" GetItemEntry Randomizer_GetItemFromKnownCheck(RandomizerCheck randomizerCheck, GetItemID ogId) {
    return OTRGlobals::Instance->gRandomizer->GetItemFromKnownCheck(randomizerCheck, ogId);
}

extern "C" GetItemEntry Randomizer_GetItemFromKnownCheckWithoutObtainabilityCheck(RandomizerCheck randomizerCheck,
                                                                                  GetItemID ogId) {
    return OTRGlobals::Instance->gRandomizer->GetItemFromKnownCheck(randomizerCheck, ogId, false);
}

extern "C" ItemObtainability Randomizer_GetItemObtainabilityFromRandomizerCheck(RandomizerCheck randomizerCheck) {
    return OTRGlobals::Instance->gRandomizer->GetItemObtainabilityFromRandomizerCheck(randomizerCheck);
}

extern "C" bool Randomizer_IsCheckShuffled(RandomizerCheck rc) {
    return CheckTracker::IsCheckShuffled(rc);
}

extern "C" GetItemEntry GetItemMystery() {
    return GET_ITEM_MYSTERY;
}

extern "C" uint8_t Randomizer_IsSeedGenerated() {
    return OTRGlobals::Instance->gRandoContext->IsSeedGenerated() ? 1 : 0;
}

extern "C" uint8_t Randomizer_IsSpoilerLoaded() {
    return OTRGlobals::Instance->gRandoContext->IsSpoilerLoaded() ? 1 : 0;
}

extern "C" void Randomizer_SetSpoilerLoaded(bool spoilerLoaded) {
    OTRGlobals::Instance->gRandoContext->SetSpoilerLoaded(spoilerLoaded);
}

extern "C" uint8_t Randomizer_GenerateRandomizer() {
    return GenerateRandomizer() ? 1 : 0;
}

extern "C" void Randomizer_ShowRandomizerMenu() {
    SohGui::ShowRandomizerSettingsMenu();
}

extern "C" void EntranceTracker_SetCurrentGrottoID(s16 entranceIndex) {
    EntranceTracker::SetCurrentGrottoIDForTracker(entranceIndex);
}

extern "C" void EntranceTracker_SetLastEntranceOverride(s16 entranceIndex) {
    EntranceTracker::SetLastEntranceOverrideForTracker(entranceIndex);
}

extern "C" void Gfx_RegisterBlendedTexture(const char* name, u8* mask, u8* replacement) {
    if (auto intP = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow())
                        ->GetInterpreterWeak()
                        .lock()) {
        intP->RegisterBlendedTexture(name, mask, replacement);
    } else {
        assert(false && "Lost reference to Fast::Interpreter");
    }
}

extern "C" void Gfx_UnregisterBlendedTexture(const char* name) {
    if (auto intP = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow())
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
        texAddr = (const uint8_t*)ResourceMgr_GetResourceDataByNameHandlingMQ(imgName);
    }

    if (auto intP = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow())
                        ->GetInterpreterWeak()
                        .lock()) {
        intP->TextureCacheDelete(texAddr);
    } else {
        assert(false && "Lost reference to Fast::Interpreter");
    }
}

// ============================================================
// ComboShip exports — soh.dll side
// ============================================================

extern "C" __declspec(dllexport) void SOH_Init() {
    // ComboShip: InitOTR takes (argc, argv) for CLI-driven extraction, but we drive extraction
    // separately (SOH_Extract) and have no CLI args here, so pass none.
    InitOTR(0, nullptr);
}

extern "C" void (*gComboSaveInitCallback)(int fileNum) = nullptr;

extern "C" __declspec(dllexport) void SOH_SetOnNewSaveCallback(void (*cb)(int fileNum)) {
    gComboSaveInitCallback = cb;
}

extern "C" void (*gComboSceneSwitchCallback)(int fileNum) = nullptr;

extern "C" __declspec(dllexport) void SOH_SetOnSceneSwitchCallback(void (*cb)(int fileNum)) {
    gComboSceneSwitchCallback = cb;
}

#ifdef COMBO_BUILD
// ComboShip: Anchor transport seam. The persistent socket lives in ComboShip.exe; these exports
// wire the launcher's connection to soh's in-place Anchor (declspec must follow extern "C" or the
// symbol isn't exported). See docs/UPSTREAM_MERGES.md.
extern "C" __declspec(dllexport) void SOH_SetAnchorSend(void (*cb)(const char*)) {
    gComboAnchorSend = cb;
}
extern "C" __declspec(dllexport) void SOH_SetAnchorConnect(void (*cb)(const char*, uint16_t)) {
    gComboAnchorConnect = cb;
}
extern "C" __declspec(dllexport) void SOH_SetAnchorDisconnect(void (*cb)(void)) {
    gComboAnchorDisconnect = cb;
}
extern "C" __declspec(dllexport) void SOH_Anchor_RecvJson(const char* json) {
    if (Anchor::Instance && json) {
        Anchor::Instance->InjectIncomingJson(json);
    }
}
extern "C" __declspec(dllexport) void SOH_Anchor_OnConnected(void) {
    if (Anchor::Instance) {
        Anchor::Instance->SetConnectedFromCombo(true);
    }
}
extern "C" __declspec(dllexport) void SOH_Anchor_OnDisconnected(void) {
    if (Anchor::Instance) {
        Anchor::Instance->SetConnectedFromCombo(false);
    }
}

// ComboShip: cross-game item delivery seam (issue #3). When the other game collects a check whose
// item belongs to OOT, the launcher calls SOH_GrantCrossItem to grant it straight into OOT's
// resident save — even when OOT is the dormant (frozen) game. We use Randomizer_Item_Give, which
// writes gSaveContext directly and is play-state-independent (Magic_Fill ignores `play`,
// Rupees_ChangeBy null-guards gPlayState), so it is safe against a frozen gPlayState. The save is
// persisted immediately so the item survives quitting before ever switching into OOT. See
// docs/UPSTREAM_MERGES.md.
extern "C" __declspec(dllexport) void SOH_GrantCrossItem(const char* itemName) {
    if (!itemName)
        return;
    auto it = Rando::StaticData::itemNameToEnum.find(itemName);
    if (it == Rando::StaticData::itemNameToEnum.end()) {
        SPDLOG_WARN("[ComboShip] SOH_GrantCrossItem: unknown OOT item '{}'", itemName);
        return;
    }
    GetItemEntry gie = Rando::StaticData::RetrieveItem(it->second).GetGIEntry_Copy();
    // ComboShip: a resolved OOT item can be a vanilla (MOD_NONE) entry, which Randomizer_Item_Give
    // asserts against. Dispatch by mod index exactly like Anchor's HandlePacket_GiveItem.
    if (gie.modIndex == MOD_NONE) {
        if (gie.getItemId == GI_SWORD_BGS) {
            gSaveContext.bgsFlag = true;
        }
        Item_Give(gPlayState, static_cast<u8>(gie.itemId));
    } else if (gie.modIndex == MOD_RANDOMIZER) {
        if (gie.getItemId == RG_ICE_TRAP) {
            gSaveContext.ship.pendingIceTrapCount++; // defer; don't spring on a dormant/foreign grant
        } else {
            Randomizer_Item_Give(gPlayState, gie); // save-direct
        }
    }
    // Full heal on heart container/piece, and roll over a 4th heart piece (mirrors Anchor handler).
    if (gie.gid == GID_HEART_CONTAINER || gie.gid == GID_HEART_PIECE) {
        gSaveContext.healthAccumulator = 0x140;
    }
    s32 heartPieces = (s32)(gSaveContext.inventory.questItems & 0xF0000000) >> (QUEST_HEART_PIECE + 4);
    if (heartPieces >= 4) {
        gSaveContext.inventory.questItems &= ~0xF0000000;
        gSaveContext.inventory.questItems += (heartPieces % 4) << (QUEST_HEART_PIECE + 4);
        gSaveContext.healthCapacity += 0x10 * (heartPieces / 4);
        gSaveContext.health += 0x10 * (heartPieces / 4);
    }
    if (SaveManager::Instance && gSaveContext.fileNum != 0xFF) {
        SaveManager::Instance->SaveFile(gSaveContext.fileNum); // persist NOW
    }
    SPDLOG_INFO("[ComboShip] SOH_GrantCrossItem: granted '{}' into OOT save", itemName);
}

// ComboShip: mark a foreign OOT check obtained without re-delivering — used on the NETWORK receive
// path so a client that gets a teammate's broadcast won't later physically collect the same check
// and double-deliver. Save-only (no grant), persisted immediately.
extern "C" __declspec(dllexport) void SOH_MarkForeignObtained(const char* checkName) {
    if (!checkName)
        return;
    auto it = Rando::StaticData::locationNameToEnum.find(checkName);
    if (it == Rando::StaticData::locationNameToEnum.end()) {
        SPDLOG_WARN("[ComboShip] SOH_MarkForeignObtained: unknown OOT check '{}'", checkName);
        return;
    }
    auto loc = OTRGlobals::Instance->gRandoContext->GetItemLocation(it->second);
    loc->SetCheckStatus(RCSHOW_COLLECTED);
    CheckTracker::SpoilAreaFromCheck(it->second);
    CheckTracker::RecalculateAllAreaTotals();
    CheckTracker::RecalculateAvailableChecks();
    if (SaveManager::Instance && gSaveContext.fileNum != 0xFF) {
        SaveManager::Instance->SaveSection(gSaveContext.fileNum, SECTION_ID_TRACKER_DATA, true);
    }
    SPDLOG_INFO("[ComboShip] SOH_MarkForeignObtained: marked OOT check '{}' collected", checkName);
}

// ComboShip: routing seam — the launcher registers DeliverCrossItem here so OOT's foreign-check
// detection can hand an item to the OTHER game immediately (mirrors SOH_SetAnchorSend).
extern "C" void (*gComboCrossDeliver)(int targetGame, const char* itemName) = nullptr;
extern "C" __declspec(dllexport) void SOH_SetCrossDeliver(void (*cb)(int, const char*)) {
    gComboCrossDeliver = cb;
}
// ComboShip: routing seam for the network-receive idempotency mark (see SOH_MarkForeignObtained).
extern "C" void (*gComboMarkForeignObtained)(int srcGame, const char* checkName) = nullptr;
extern "C" __declspec(dllexport) void SOH_SetMarkForeignObtained(void (*cb)(int, const char*)) {
    gComboMarkForeignObtained = cb;
}
#endif

#ifdef COMBO_BUILD
// Defined in soh/src/code/main.c: re-enters ONLY OOT's game loop (no heap/thread re-init).
extern "C" void SOH_RunGameLoop(void);
// Defined in soh/src/code/graph.c: resets the frame state machine so SOH_RunGameLoop restarts the
// gamestate sequence from TitleSetup instead of resuming into the destroyed post-handoff gamestate.
extern "C" void SOH_ResetFrameLoopForResume(void);
// Defined in soh/soh/z_message_OTR.cpp: rebuilds the message-data tables whose cached pointers were
// left dangling by the forward transition's UnloadResources.
extern "C" void OTRMessage_ResetForResume(void);
// Consumed by TitleSetup_InitImpl (soh/src/code/title_setup.c): >= 0 => skip title/file-select, load
// this slot, and jump straight to Play at the Mido's-House door.
extern "C" s32 gComboReturnFileNum = -1;

// Re-activate OOT's resources and re-init the pieces SOH_PrepareForTransition tore down.
static void SOH_ReinitForResume() {
    auto ctx = Ship::Context::GetInstance();

    // Re-activate OOT's own ResourceManager. Its archives, factories, and resource cache stayed
    // resident the entire time MM was running (MM used its own RM), so nothing was unloaded and no
    // cached resource pointer dangles -> no archive swap, no UnloadResources, no factory re-register,
    // no message-table/audio-heap reset needed. This is what eliminates the whole dangling class.
    ctx->SetResourceManager(sOOTResourceManager);

    // Restart OOT's audio thread (SOH_PrepareForTransition stopped it). Soundfonts/samples are still
    // resident in OOT's RM, so the thread resumes against valid data with no reload/heap reset.
    OTRAudio_Init(); // counterpart to OTRAudio_Exit() in SOH_PrepareForTransition
    // ComboShip: do NOT SohGui::SetupGuiElements() here. OOT's windows persist across the transition,
    // so re-creating them hits AddGuiWindow's duplicate-name rejection -> the new windows never get
    // InitElement'd -> freeing their uninitialized buffers later crashes. The resident windows are
    // still fully initialized; we only need to swap the active RM/audio (above) and restore the menu.
    // Restore OOT's menu into the shared Gui's single menu slot (MM set it to its BenMenu while it was
    // the active game). mSohMenu persists.
#ifndef COMBO_BUILD
    ctx->GetWindow()->GetGui()->SetMenu(SohGui::GetSohMenu());
#endif
    // ComboShip: comboui's menu stays installed across transitions; do not restore SohMenu.
}

// ComboShip: symmetric marker mirroring MM_NotifyComboTransition; called before SOH_ResumeGame.
extern "C" __declspec(dllexport) void SOH_NotifyComboReturn(void) {
    // Currently a no-op; kept for symmetry with the forward transition's notify call.
}

// ComboShip: draw OOT's menu content (content-only, themed) into the current ImGui window.
// onlyCsv: if non-empty, comma-separated allow-list of "Header" or "Header/Sidebar" paths to show.
// skipCsv: if onlyCsv is empty, comma-separated block-list of paths to hide.
extern "C" __declspec(dllexport) void SOH_DrawSettings(const char* onlyCsv, const char* skipCsv) {
    // ComboShip: soh.dll's per-module ImGui GImGui isn't current when OOT is backgrounded (MM is
    // foreground and the player opens the Shared/OOT tab). Point it at the shared context before any
    // ImGui call, else ImGui::GetCurrentWindow() is null and we crash.
    ComboMenuContext::UseSharedImGuiContext();
    auto menu = SohGui::GetSohMenu();
    if (!menu)
        return;
    // ComboShip: in combo this menu is never installed as the active Gui menu, so libultraship never
    // calls Init()/InitElement() (which set up disabledMap, window backends, and the theme). Init() is
    // idempotent; call it once before drawing, else widget PreFuncs doing disabledMap.at(...) throw.
    menu->Init();
    std::set<std::string> only, skip;
    auto parseCsv = [](const char* csv, std::set<std::string>& out) {
        if (!csv || !csv[0])
            return;
        std::stringstream ss(csv);
        std::string item;
        while (std::getline(ss, item, ','))
            if (!item.empty())
                out.insert(item);
    };
    parseCsv(onlyCsv, only);
    parseCsv(skipCsv, skip);
    menu->DrawContent(only, skip);
}

// ComboShip: expose OOT's SohMenu C-ABI emitter + invoke-by-index helpers so comboui can ingest the
// flat CwMenu (combo/menu/ComboMenuABI.h) via GetProcAddress and drive widgets back by index. The
// SohMenu instance is built at menu setup and kept for process life; comboui owns the menu slot, so
// libultraship's Gui loop never drives this menu — see SOH_MenuDrawCustom for why Init()/Update()
// must run before invoking a custom widget. soh.dll has its own per-module ImGui GImGui — see
// combo/menu/ComboMenuSharedContext.h.

extern "C" __declspec(dllexport) const CwMenu* SOH_ExportMenu(void) {
    ComboMenuContext::UseSharedImGuiContext();
    auto menu = SohGui::GetSohMenu();
    return menu ? menu->ExportComboMenu() : nullptr;
}

extern "C" __declspec(dllexport) void SOH_MenuInvokeCallback(int32_t i) {
    ComboMenuContext::UseSharedImGuiContext();
    if (auto menu = SohGui::GetSohMenu())
        menu->InvokeCallbackByIndex(i);
}

extern "C" __declspec(dllexport) int32_t SOH_MenuEvalDisabled(int32_t i, const char** outReason) {
    ComboMenuContext::UseSharedImGuiContext();
    auto menu = SohGui::GetSohMenu();
    return menu ? menu->EvalDisabledByIndex(i, outReason) : 0;
}

extern "C" __declspec(dllexport) void SOH_MenuDrawCustom(int32_t i) {
    // Like SOH_DrawSettings: soh.dll's per-module ImGui GImGui isn't current when OOT is backgrounded,
    // so point it at the shared context before any ImGui call.
    ComboMenuContext::UseSharedImGuiContext();
    // Phase 0 spike contract: comboui owns the menu slot so the Gui loop never drives this menu's
    // lifecycle. A custom widget reads THEME_COLOR (menuThemeIndex), set in UpdateElement(), so
    // Init()+Update() must run BEFORE invoking, else ColorValues.at() throws. Init() is idempotent.
    if (auto menu = SohGui::GetSohMenu()) {
        menu->Init();
        menu->Update();
        menu->DrawCustomByIndex(i);
    }
}

// ComboShip: MM->OOT return — re-enter OOT's game loop on the SAME shared context/window, swap
// archives back to OOT, reload the OOT save, and spawn Link at the Mido's-House door in Kokiri
// Forest. Counterpart to MM's reuse path in BenPort.cpp.
extern "C" bool WindowIsRunning(void);

extern "C" __declspec(dllexport) void SOH_ResumeGame(void) {
    auto ctx = Ship::Context::GetInstance();
    // Flush every log line immediately so the resume diagnostics survive a hard crash (the console
    // window closes on crash; the log file is what we read afterward).
    ctx->GetLogger()->flush_on(spdlog::level::trace);
    SPDLOG_INFO("[ComboShip] SOH_ResumeGame: begin (gSaveContext.fileNum={})", (int)gSaveContext.fileNum);

    // 1. Re-activate OOT's ResourceManager + restart audio/gui (resources stayed resident, so no
    //    message-table/audio reset is needed — that whole dangling class is gone with per-game RMs).
    SOH_ReinitForResume();
    SPDLOG_INFO("[ComboShip] SOH_ResumeGame: SOH_ReinitForResume done");

    // 2. Re-arm the shared window so OOT's `while (WindowIsRunning())` loop runs instead of
    //    returning immediately (MM cleared mIsRunning when its loop exited).
    if (auto fast3d = std::dynamic_pointer_cast<Fast::Fast3dWindow>(ctx->GetWindow())) {
        fast3d->SetIsRunning(true);
    }

    // 3. Re-sync this DLL's ImGui current-context (GImGui is per-module).
    ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());

    // 4. Hand off to OOT's boot path: capture the save slot, reset the frame state machine, and let
    //    TitleSetup jump straight to Play at the Mido's-House door (mirrors FileChoose + MM title_setup).
    //    The save itself is loaded by TitleSetup via Sram_OpenSave, exactly like normal file select.
    gComboReturnFileNum = (s32)gSaveContext.fileNum;
    SOH_ResetFrameLoopForResume();
    SPDLOG_INFO("[ComboShip] SOH_ResumeGame: entering OOT loop (gComboReturnFileNum={}, WindowIsRunning={})",
                gComboReturnFileNum, WindowIsRunning());

    // 5. Re-run OOT's game loop (returns when the shared window's running flag is cleared again).
    SOH_RunGameLoop();
    SPDLOG_INFO("[ComboShip] SOH_ResumeGame: OOT loop RETURNED (WindowIsRunning={})", WindowIsRunning());
}

// ComboShip: re-activate OOT as the foreground game WITHOUT entering its game loop. Used once at
// startup right after MM is eagerly booted, which left MM's RM active and tore down OOT's audio/GUI.
// Restores OOT's RM/audio/GUI/menu so OOT's first real boot (SOH_RunMain) renders correctly. Like
// SOH_ResumeGame minus the frame-loop reset and game loop — SOH_RunMain runs the loop.
extern "C" __declspec(dllexport) void SOH_ResumeForeground(void) {
    auto ctx = Ship::Context::GetInstance();
    SOH_ReinitForResume(); // OOT RM active, OOT audio, OOT GUI + menu
    // Re-sync this DLL's ImGui current-context (GImGui is per-module).
    ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());
}
#endif

#if not defined(__SWITCH__) && not defined(__WIIU__)
extern "C" __declspec(dllexport) bool SOH_Extract(const char* searchPath) {
    std::string path = searchPath ? searchPath : std::filesystem::current_path().string();
    std::string installPath = Ship::Context::GetAppBundlePath();
    Extractor extract;
    if (!extract.Run(path)) {
        return false;
    }
    // Upstream merge: CallZapd gained two atomic progress counters (extracted / total).
    std::atomic<size_t> extractCount = 0, totalExtract = 0;
    extract.CallZapd(installPath, path, &extractCount, &totalExtract);
    return true;
}

// ComboShip: UI-less extraction primitives. The launcher's combo-owned extraction screen (comboui)
// owns the ROM picker + progress bar; these do the work and expose progress for it to poll. The ROM
// path is supplied explicitly (no native dialog here). See combo/ComboExtract.h + docs/UPSTREAM_MERGES.md.
static std::atomic<size_t> gComboExtractCount{ 0 };
static std::atomic<size_t> gComboExtractTotal{ 0 };
static std::atomic<bool> gComboExtractDone{ false };
static std::atomic<bool> gComboExtractSuccess{ false };
static std::future<void> gComboExtractFuture;
static std::string gComboExtractRomPath;

// Returns nonzero if romPath is a recognized OoT ROM (validation only, no dialog, no extraction).
extern "C" __declspec(dllexport) int SOH_ValidateRom(const char* romPath) {
    if (!romPath) {
        return 0;
    }
    Extractor extract;
    return extract.RunFileStandalone(romPath) ? 1 : 0;
}

// Kicks ZAPD extraction of romPath on a background task. Non-blocking; returns 0 if a job is already
// running or the arg is null. Poll SOH_GetExtractionProgress for completion.
extern "C" __declspec(dllexport) int SOH_StartExtraction(const char* romPath) {
    if (!romPath) {
        return 0;
    }
    if (gComboExtractFuture.valid() &&
        gComboExtractFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return 0; // a job is still running
    }
    gComboExtractRomPath = romPath;
    gComboExtractCount = 0;
    gComboExtractTotal = 0;
    gComboExtractDone = false;
    gComboExtractSuccess = false;
    gComboExtractFuture = std::async(std::launch::async, []() {
        bool ok = false;
        try {
            Extractor extract;
            if (extract.RunFileStandalone(gComboExtractRomPath)) {
                std::string installPath = Ship::Context::GetAppBundlePath();
                std::string exportPath = Ship::Context::GetAppDirectoryPath(appShortName);
                ok = extract.CallZapd(installPath, exportPath, &gComboExtractCount, &gComboExtractTotal);
            }
        } catch (...) {
            ok = false; // a ZAPD failure surfaces as done && !success, never crashes the launcher
        }
        gComboExtractSuccess = ok;
        gComboExtractDone = true;
    });
    return 1;
}

extern "C" __declspec(dllexport) void SOH_GetExtractionProgress(unsigned long long* count, unsigned long long* total,
                                                                int* done, int* success) {
    if (count) {
        *count = (unsigned long long)gComboExtractCount.load();
    }
    if (total) {
        *total = (unsigned long long)gComboExtractTotal.load();
    }
    if (done) {
        *done = gComboExtractDone.load() ? 1 : 0;
    }
    if (success) {
        *success = gComboExtractSuccess.load() ? 1 : 0;
    }
}
#endif

#ifdef COMBO_BUILD
// ComboShip: SoH does shop/scrub/merchant setup inline inside Fill() — choosing which shopsanity slots
// are shuffled (custom price, filled by the main fill) vs left vanilla (a real RG_BUY_* item placed
// directly), and pricing scrubs and merchants. The combo cross-world generator REPLACES Fill() and
// skips all of this; without it the combined fill drops arbitrary OOT/MM items into shop slots with no
// shop setup, producing blank slots that crash on purchase (GetItemName(ITEM_NONE)). The orchestration
// below reuses SoH's own primitives (shops.hpp), so fill.cpp stays byte-intact.
//
// Determinism: the dump (which decides which shop slots the cross-world fill may use) and the apply
// (which sets the actual prices + vanilla items) must make IDENTICAL random choices, and re-running the
// same combo seed must reproduce them exactly. So both seed SoH's rando RNG (Random_Init) from the
// combo master seed via Combo_SeedShopRng() immediately before computing the shuffled-slot set. With
// the same seed + same RNG consumption order, RO_SHOPSANITY_RANDOM counts and all prices match between
// dump and apply and are fully reproducible. The combo seed is supplied by SOH_SetComboRandoSeed.
static uint64_t sComboRandoSeed = 0;
static bool sComboRandoSeedSet = false;

// Reseed SoH's rando RNG to the combo master seed. Call right before any shop-setup RNG use so the dump
// and apply consume an identical random sequence (→ identical shuffled set + prices, reproducible).
static void Combo_SeedShopRng() {
    if (sComboRandoSeedSet) {
        Random_Init(sComboRandoSeed);
    }
}
static const PriceSettingsStruct kComboShopPrices = {
    RSK_SHOPSANITY_PRICES,
    RSK_SHOPSANITY_PRICES_FIXED_PRICE,
    RSK_SHOPSANITY_PRICES_RANGE_1,
    RSK_SHOPSANITY_PRICES_RANGE_2,
    RSK_SHOPSANITY_PRICES_NO_WALLET_WEIGHT,
    RSK_SHOPSANITY_PRICES_CHILD_WALLET_WEIGHT,
    RSK_SHOPSANITY_PRICES_ADULT_WALLET_WEIGHT,
    RSK_SHOPSANITY_PRICES_GIANT_WALLET_WEIGHT,
    RSK_SHOPSANITY_PRICES_TYCOON_WALLET_WEIGHT,
    RSK_SHOPSANITY_PRICES_AFFORDABLE,
};
static const PriceSettingsStruct kComboScrubPrices = {
    RSK_SCRUBS_PRICES,
    RSK_SCRUBS_PRICES_FIXED_PRICE,
    RSK_SCRUBS_PRICES_RANGE_1,
    RSK_SCRUBS_PRICES_RANGE_2,
    RSK_SCRUBS_PRICES_NO_WALLET_WEIGHT,
    RSK_SCRUBS_PRICES_CHILD_WALLET_WEIGHT,
    RSK_SCRUBS_PRICES_ADULT_WALLET_WEIGHT,
    RSK_SCRUBS_PRICES_GIANT_WALLET_WEIGHT,
    RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT,
    RSK_SCRUBS_PRICES_AFFORDABLE,
};
static const PriceSettingsStruct kComboMerchantPrices = {
    RSK_MERCHANT_PRICES,
    RSK_MERCHANT_PRICES_FIXED_PRICE,
    RSK_MERCHANT_PRICES_RANGE_1,
    RSK_MERCHANT_PRICES_RANGE_2,
    RSK_MERCHANT_PRICES_NO_WALLET_WEIGHT,
    RSK_MERCHANT_PRICES_CHILD_WALLET_WEIGHT,
    RSK_MERCHANT_PRICES_ADULT_WALLET_WEIGHT,
    RSK_MERCHANT_PRICES_GIANT_WALLET_WEIGHT,
    RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT,
    RSK_MERCHANT_PRICES_AFFORDABLE,
};

// The shop slots shopsanity shuffles, using SoH's exact per-shop index pattern. Deterministic for a
// specific shopsanity count (GetShopsanityReplaceAmount returns a fixed value, no RNG), so the dump and
// the apply compute an identical set.
static std::unordered_set<RandomizerCheck> Combo_ShuffledShopSlots() {
    std::unordered_set<RandomizerCheck> shuffled;
    auto ctx = OTRGlobals::Instance->gRandoContext;
    if (ctx->GetOption(RSK_SHOPSANITY).Is(RO_SHOPSANITY_OFF)) {
        return shuffled;
    }
    static const std::array<int, 8> indices = { 7, 5, 8, 6, 3, 1, 4, 2 }; // matches fill.cpp
    const auto& shopLocs = Rando::StaticData::GetShopLocations();
    constexpr int kLocationsPerShop = 8;
    for (size_t shop = 0; shop < shopLocs.size() / kLocationsPerShop; ++shop) {
        int num = GetShopsanityReplaceAmount();
        for (int j = 0; j < num && j < (int)indices.size(); ++j) {
            shuffled.insert(shopLocs[shop * kLocationsPerShop + indices[j] - 1]);
        }
    }
    return shuffled;
}

static bool Combo_IsShopSlot(RandomizerCheck rc, const std::unordered_set<RandomizerCheck>& shopSlots) {
    return shopSlots.count(rc) > 0;
}

// Re-establish shop/scrub/merchant setup on the live rando context. Called from SOH_ApplyRandoPlacements
// AFTER ItemReset() (which clears it) and BEFORE the combo placements are applied: shuffled shop slots
// get a custom price (the combo fill's item is placed into them next), every other shop slot gets its
// vanilla RG_BUY_* item, and scrubs/merchants get prices — exactly as SoH's Fill() would.
static void Combo_SetupOOTShops() {
    auto ctx = OTRGlobals::Instance->gRandoContext;

    // Seed identically to the dump so the shuffled-slot set (and all prices below) match it exactly.
    Combo_SeedShopRng();

    if (ctx->GetOption(RSK_SHOPSANITY).Is(RO_SHOPSANITY_OFF)) {
        PlaceVanillaShopItems();
    } else {
        auto shuffled = Combo_ShuffledShopSlots();
        for (RandomizerCheck rc : Rando::StaticData::GetShopLocations()) {
            Rando::ItemLocation* loc = ctx->GetItemLocation(rc);
            if (shuffled.count(rc)) {
                loc->SetCustomPrice(GetRandomPrice(Rando::StaticData::GetLocation(rc), kComboShopPrices));
            } else {
                loc->PlaceVanillaItem(); // vanilla RG_BUY_* — a valid, sellable shop item
            }
        }
    }

    // Scrub prices (only meaningful when scrubs are shuffled; harmless otherwise).
    for (RandomizerCheck rc : Rando::StaticData::GetScrubLocations()) {
        uint16_t price = ctx->GetOption(RSK_SHUFFLE_SCRUBS).Is(RO_SCRUBS_ALL)
                             ? GetRandomPrice(Rando::StaticData::GetLocation(rc), kComboScrubPrices)
                             : Rando::StaticData::GetLocation(rc)->GetVanillaPrice();
        ctx->GetItemLocation(rc)->SetCustomPrice(price);
    }

    // Merchant + magic-bean prices.
    const bool merchAll = ctx->GetOption(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_ALL);
    const bool merchAllButBeans = ctx->GetOption(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_ALL_BUT_BEANS);
    const bool beans = ctx->GetOption(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_BEANS_ONLY) || merchAll;
    ctx->GetItemLocation(RC_ZR_MAGIC_BEAN_SALESMAN)
        ->SetCustomPrice(
            beans ? GetRandomPrice(Rando::StaticData::GetLocation(RC_ZR_MAGIC_BEAN_SALESMAN), kComboMerchantPrices)
                  : Rando::StaticData::GetLocation(RC_ZR_MAGIC_BEAN_SALESMAN)->GetVanillaPrice());
    for (RandomizerCheck rc : Rando::StaticData::GetMerchantLocations()) {
        uint16_t price = (merchAll || merchAllButBeans)
                             ? GetRandomPrice(Rando::StaticData::GetLocation(rc), kComboMerchantPrices)
                             : Rando::StaticData::GetLocation(rc)->GetVanillaPrice();
        ctx->GetItemLocation(rc)->SetCustomPrice(price);
    }
}

// Combo master seed for OOT-side reproducible generation (shop/scrub/merchant RNG). Set by the combo
// launcher before SOH_DumpRandoStaticData and reused at SOH_ApplyRandoPlacements so both agree.
extern "C" __declspec(dllexport) void SOH_SetComboRandoSeed(uint64_t seed) {
    sComboRandoSeed = seed;
    sComboRandoSeedSet = true;
}
#endif

// ComboShip: snapshot every OOT rando option as {cvarName: value}. The combo orchestrator stores
// this in the consolidated spoiler so a dropped/reloaded seed reproduces the exact settings on any
// machine (OOT options are CVar-backed; SOH_RestoreRandoSettings writes them back).
extern "C" __declspec(dllexport) const char* SOH_DumpRandoSettings(void) {
    static std::string cached;
    nlohmann::json j = nlohmann::json::object();
    for (const auto& opt : Rando::Settings::GetInstance()->GetAllOptions()) {
        const std::string& cv = opt.GetCVarName();
        if (!cv.empty())
            j[cv] = static_cast<int>(opt.GetOptionIndex());
    }
    cached = j.dump();
    return cached.c_str();
}

// ComboShip: restore OOT rando settings from a {cvarName:value} snapshot (written by
// SOH_DumpRandoSettings into the consolidated spoiler). Used by the reload/drop path so a seed plays
// with its own settings; SOH_PrepRandoContext then pushes them into the Context via SetAllToContext.
extern "C" __declspec(dllexport) void SOH_RestoreRandoSettings(const char* json) {
    if (!json)
        return;
    try {
        auto j = nlohmann::json::parse(json);
        for (auto it = j.begin(); it != j.end(); ++it)
            CVarSetInteger(it.key().c_str(), it.value().get<int>());
    } catch (...) {}
}

// ComboShip: the settings-scoped pool prep that SOH_ApplyRandoPlacements depends on (ItemReset
// iterates allLocations). On generation this runs inside SOH_DumpRandoStaticData; the reload/drop
// path (which skips the fill) must run it explicitly before applying saved placements.
extern "C" __declspec(dllexport) void SOH_PrepRandoContext(void) {
    try {
        auto ctx = OTRGlobals::Instance->gRandoContext;
        Rando::Settings::GetInstance()->SetAllToContext();
        ctx->GetLogic()->Reset();
        ctx->FinalizeSettings({}, {});
        RegionTable_Init();
        ctx->GenerateLocationPool();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[ComboShip] SOH_PrepRandoContext: {}", e.what());
    } catch (...) {}
}

// ComboShip: coherent OOT rando dump for the combo generator. Runs the headless prep sequence
// (GetLogic()->Reset, FinalizeSettings, RegionTable_Init, GenerateLocationPool) so ctx->allLocations
// holds the real shuffled-check set for the current settings, then dumps those checks + their vanilla
// items — keeping the generator's permutation coherent with Randomizer_InitSaveFile. Recomputes every
// call (result depends on live CVar/settings). If the prep throws, falls back to iterating all RC_MAX
// so the dump always succeeds. Caller MUST invoke this AFTER SOH_Init() returns.
extern "C" __declspec(dllexport) const char* SOH_DumpRandoStaticData(void) {
    static std::string cached;

    nlohmann::json checks = nlohmann::json::array();
    nlohmann::json items = nlohmann::json::array();

    bool usedPool = false;
    try {
        auto ctx = OTRGlobals::Instance->gRandoContext;

        // Headless prep (settings -> context, reset, region tables, settings-scoped pool). Shared
        // with the reload/drop path via SOH_PrepRandoContext so allLocations holds only the checks
        // the current settings shuffle, honoring the menu choices.
        SOH_PrepRandoContext();

#ifdef COMBO_BUILD
        // ComboShip: a non-shuffled shop slot holds a vanilla RG_BUY_* item (placed by
        // Combo_SetupOOTShops at apply time). It must NOT enter the cross-world pool, or the combined
        // fill would drop an arbitrary OOT/MM item there with no shop setup -> crash on purchase. Emit
        // ONLY the shuffled shop slots; leave the rest to SoH's vanilla shop setup. The shuffled set is
        // deterministic for a given shopsanity count, so it matches what the apply does.
        Combo_SeedShopRng(); // seed identically to the apply so the shuffled set matches exactly
        const auto comboShuffledShops = Combo_ShuffledShopSlots();
        const auto& comboShopLocsVec = Rando::StaticData::GetShopLocations();
        const std::unordered_set<RandomizerCheck> comboShopSlots(comboShopLocsVec.begin(), comboShopLocsVec.end());
#endif

        // Iterate allLocations (the settings-scoped check set) instead of all RC_MAX.
        for (RandomizerCheck rc : ctx->allLocations) {
            Rando::Location* loc = Rando::StaticData::GetLocation(rc);
            if (!loc)
                continue;
            const std::string& name = loc->GetName();
            if (name.empty())
                continue;

#ifdef COMBO_BUILD
            if (Combo_IsShopSlot(rc, comboShopSlots) && !comboShuffledShops.count(rc)) {
                continue; // vanilla shop slot — owned by SoH's shop setup, not the cross-world fill
            }
#endif

            RandomizerGet vanillaRG = loc->GetVanillaItem();
            if (vanillaRG == RG_NONE)
                continue;

            const std::string& vigName = Rando::StaticData::RetrieveItem(vanillaRG).GetName().GetEnglish();
            if (vigName.empty())
                continue;

            // ComboShip: advancement flag — the combined fill logic-places only progression items.
            checks.push_back({ { "name", name },
                               { "vanillaItem", vigName },
                               { "advancement", Rando::StaticData::RetrieveItem(vanillaRG).IsAdvancement() } });
        }

#ifdef COMBO_BUILD
        // ComboShip: the loop above emits each check's VANILLA item, which MISSES the items the SETTINGS
        // ADD to the pool — the shuffled "skill" items (Open Chest, Speak *, Climb, Crawl). They are not
        // the vanilla item of any check, so without them the cross-world fill can never grant
        // CAN_OPEN_CHEST / CAN_SPEAK_* etc., and EVERY chest/scrub/shop is logically unreachable ->
        // generation dead-ends. Inject the enabled skill items into the pool, overwriting an equal
        // number of junk checks so items stay 1:1 with checks. (Swim/Grab map to Progressive
        // Scale/Strength, which the vanilla pool already carries, so they need no injection.)
        {
            std::vector<RandomizerGet> skills;
            if (ctx->GetOption(RSK_SHUFFLE_OPEN_CHEST))
                skills.push_back(RG_OPEN_CHEST);
            if (ctx->GetOption(RSK_SHUFFLE_CLIMB))
                skills.push_back(RG_CLIMB);
            if (ctx->GetOption(RSK_SHUFFLE_CRAWL))
                skills.push_back(RG_CRAWL);
            if (ctx->GetOption(RSK_SHUFFLE_SPEAK)) {
                skills.push_back(RG_SPEAK_DEKU);
                skills.push_back(RG_SPEAK_GERUDO);
                skills.push_back(RG_SPEAK_GORON);
                skills.push_back(RG_SPEAK_HYLIAN);
                skills.push_back(RG_SPEAK_KOKIRI);
                skills.push_back(RG_SPEAK_ZORA);
            }
            size_t si = 0;
            for (auto& c : checks) {
                if (si >= skills.size())
                    break;
                if (c.value("advancement", true))
                    continue; // only overwrite a junk slot
                const std::string& sn = Rando::StaticData::RetrieveItem(skills[si]).GetName().GetEnglish();
                if (sn.empty()) {
                    ++si;
                    continue;
                }
                c["vanillaItem"] = sn;
                c["advancement"] = true;
                ++si;
            }
            if (si < skills.size()) {
                SPDLOG_WARN("[ComboShip] SOH_DumpRandoStaticData: injected only {}/{} skill items (too few junk slots)",
                            si, skills.size());
            }
        }
#endif
        usedPool = true;
    } catch (const std::exception& e) {
        SPDLOG_WARN("[ComboShip] SOH_DumpRandoStaticData: RegionTable_Init/GenerateLocationPool threw ({}); "
                    "falling back to full RC_MAX dump",
                    e.what());
    } catch (...) {
        SPDLOG_WARN("[ComboShip] SOH_DumpRandoStaticData: RegionTable_Init/GenerateLocationPool threw unknown "
                    "exception; falling back to full RC_MAX dump");
    }

    if (!usedPool) {
        // Fallback: dump every check that has a vanilla item (old Inc2 behaviour).
        for (int i = 0; i < RC_MAX; ++i) {
            Rando::Location* loc = Rando::StaticData::GetLocation(static_cast<RandomizerCheck>(i));
            if (!loc)
                continue;
            const std::string& name = loc->GetName();
            if (name.empty())
                continue;

            RandomizerGet vanillaRG = loc->GetVanillaItem();
            if (vanillaRG == RG_NONE)
                continue;

            const std::string& vigName = Rando::StaticData::RetrieveItem(vanillaRG).GetName().GetEnglish();
            if (vigName.empty())
                continue;

            // ComboShip: advancement flag — same as the pool path above.
            checks.push_back({ { "name", name },
                               { "vanillaItem", vigName },
                               { "advancement", Rando::StaticData::RetrieveItem(vanillaRG).IsAdvancement() } });
        }
    }

    // Items list: full item table (for the generator's reference; vanillaItems come from checks above).
    for (int rg = 0; rg < RG_MAX; ++rg) {
        Rando::Item& item = Rando::StaticData::RetrieveItem(static_cast<RandomizerGet>(rg));
        const std::string& name = item.GetName().GetEnglish();
        if (name.empty())
            continue;
        // ComboShip: OOT item names are already human English; displayName == name keeps the
        // dump schema symmetric with MM's (which needs the distinction: RI_* vs human).
        items.push_back({ { "name", name }, { "displayName", name } });
    }

    cached = nlohmann::json{ { "checks", std::move(checks) }, { "items", std::move(items) } }.dump();
    return cached.c_str();
}

// ComboShip: apply a placement mapping produced by the combo generator.
// Input JSON: {"<checkName>":"<itemName>", ...} (the "oot" object from the combined spoiler).
// For each entry, look up the check and item enums and place it. Then SetSeedGenerated(true) so
// Sram_InitSave proceeds into Randomizer_InitSaveFile(). Does NOT call OOT's own
// Fill()/GenerateItemPool() — the combo generator owns the placement.
extern "C" __declspec(dllexport) void SOH_ApplyRandoPlacements(const char* json) {
    if (!json) {
        SPDLOG_ERROR("[ComboShip] SOH_ApplyRandoPlacements: null JSON");
        return;
    }
    try {
        auto ctx = OTRGlobals::Instance->gRandoContext;

        // ItemReset so all locations start with RG_NONE before we apply our placement.
        ctx->ItemReset();

        // ComboShip: ItemReset wipes shop prices + placements, so re-run SoH's shop/scrub/merchant
        // setup here (vanilla slots get their RG_BUY_* item + prices; shuffled slots get a custom
        // price). The combo placements below only cover the shuffled slots (the dump excluded the
        // vanilla ones), so they land on validly-priced slots and don't clobber the vanilla items.
#ifdef COMBO_BUILD
        Combo_SetupOOTShops();
#endif

        nlohmann::json placements = nlohmann::json::parse(json);
        int placed = 0, skipped = 0;
        for (auto& [name, itemVal] : placements.items()) {
            if (!itemVal.is_string()) {
                ++skipped;
                continue;
            }
            const std::string itemName = itemVal.get<std::string>();

            auto rcIt = Rando::StaticData::locationNameToEnum.find(name);
            if (rcIt == Rando::StaticData::locationNameToEnum.end()) {
                SPDLOG_WARN("[ComboShip] SOH_ApplyRandoPlacements: unknown location '{}'", name);
                ++skipped;
                continue;
            }
            auto rgIt = Rando::StaticData::itemNameToEnum.find(itemName);
            if (rgIt == Rando::StaticData::itemNameToEnum.end()) {
                SPDLOG_WARN("[ComboShip] SOH_ApplyRandoPlacements: unknown item '{}' at '{}'", itemName, name);
                ++skipped;
                continue;
            }

            RandomizerCheck rc = rcIt->second;
            RandomizerGet rg = rgIt->second;
            ctx->PlaceItemInLocation(rc, rg, false, false);
            ++placed;
        }

        ctx->SetSeedGenerated(true);
        SPDLOG_INFO("[ComboShip] SOH_ApplyRandoPlacements: placed={} skipped={} seedGenerated=true", placed, skipped);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[ComboShip] SOH_ApplyRandoPlacements: exception: {}", e.what());
    } catch (...) { SPDLOG_ERROR("[ComboShip] SOH_ApplyRandoPlacements: unknown exception"); }
}

// ComboShip: set the file-select seed-hash icons. The combo generator owns generation and never
// runs OOT's Playthrough_Init (which normally calls GenerateHash), so hashIconIndexes would stay
// all-zero -> five Deku Nuts. The combo orchestrator passes a settings-aware hash value here (after
// SOH_ApplyRandoPlacements, which ItemResets). GenerateHash() fills hashIconIndexes from the string,
// then SaveManager persists it into the save's meta on creation, exactly as stock SoH.
extern "C" __declspec(dllexport) void SOH_SetComboSeedHash(uint32_t hashValue) {
    auto ctx = OTRGlobals::Instance->gRandoContext;
    if (!ctx)
        return;
    ctx->SetHash(std::to_string(hashValue));
    GenerateHash();
}

// ComboShip: generate callback — fired by Sram_InitSave before save creation, giving the combo
// orchestrator a chance to run the generator and apply placements before
// Randomizer_InitSaveFile() consumes them.
extern "C" void (*gComboGenerateCallback)(int fileNum) = nullptr;

extern "C" __declspec(dllexport) void SOH_SetOnComboGenerateCallback(void (*cb)(int fileNum)) {
    gComboGenerateCallback = cb;
}

// ComboShip: window-driven generate request — the UI (comboui Generate button or the native
// file-select "Generate a new seed" option) calls SOH_TriggerComboGenerate; soh.dll forwards to the
// launcher handler, which runs the fill on a WORKER THREAD so the main loop keeps rendering + playing
// music + showing progress. The launcher owns the single ComboGenProgress and shares a read-only
// pointer here via SOH_SetComboProgressPtr; the main-thread apply is driven via SOH_PollComboFinalize.
#include "gui/ComboGenProgress.h"
extern "C" void (*gComboGenerateRequestCallback)(const char*) = nullptr;
static const ComboRando::ComboGenProgress* gComboProgressPtr = nullptr;
static int (*gComboFinalizeCallback)() = nullptr;

extern "C" __declspec(dllexport) void SOH_SetOnComboGenerateRequestCallback(void (*cb)(const char*)) {
    gComboGenerateRequestCallback = cb;
}

extern "C" __declspec(dllexport) void SOH_SetComboProgressPtr(const ComboRando::ComboGenProgress* p) {
    gComboProgressPtr = p;
}

extern "C" __declspec(dllexport) const ComboRando::ComboGenProgress* SOH_GetComboGenProgress(void) {
    return gComboProgressPtr;
}

// C-friendly progress percent (0-100) for the native file-select screen (which is C and can't read
// the C++ atomics directly).
extern "C" __declspec(dllexport) int SOH_GetComboGenPercent(void) {
    if (!gComboProgressPtr)
        return 0;
    int total = gComboProgressPtr->total.load();
    int placed = gComboProgressPtr->placed.load();
    return total > 0 ? static_cast<int>((100LL * placed) / total) : 0;
}

extern "C" __declspec(dllexport) void SOH_SetOnComboFinalizeCallback(int (*cb)()) {
    gComboFinalizeCallback = cb;
}

// Called every frame on the main thread from the file-select loop. Runs the launcher's pending
// main-thread apply; returns nonzero once generation is fully resolved (finalized or failed).
extern "C" __declspec(dllexport) int SOH_PollComboFinalize(void) {
    return gComboFinalizeCallback ? gComboFinalizeCallback() : 1;
}

// ComboShip: reload a consolidated seed file so it's playable without regenerating (remember-seed +
// drag-drop). The launcher does the work on the calling (main) thread; path null/empty = the
// remembered pending file. Returns 1 if a seed was loaded.
static int (*gComboReloadCallback)(const char*) = nullptr;
extern "C" __declspec(dllexport) void SOH_SetOnComboReloadCallback(int (*cb)(const char*)) {
    gComboReloadCallback = cb;
}
extern "C" __declspec(dllexport) int SOH_RequestComboReload(const char* path) {
    return gComboReloadCallback ? gComboReloadCallback(path) : 0;
}

// ComboShip: the active save slot (combo seed key), or -1 if none. Used by the comboui hint system to
// find the loaded seed's per-slot consolidated file.
extern "C" __declspec(dllexport) int SOH_GetActiveFileNum(void) {
    return (gSaveContext.fileNum == 0xFF) ? -1 : (int)gSaveContext.fileNum;
}

// ComboShip: JSON array of OOT rando checks the player has obtained, for the sphere-hint system
// (which step is "done"). Reads the live rando Context; safe to call while OOT is dormant.
extern "C" __declspec(dllexport) const char* Combo_SOH_GetObtainedChecks(void) {
    static std::string cached;
    nlohmann::json out = nlohmann::json::array();
    auto ctx = OTRGlobals::Instance->gRandoContext;
    if (ctx) {
        for (int i = 0; i < RC_MAX; ++i) {
            RandomizerCheck rc = static_cast<RandomizerCheck>(i);
            Rando::ItemLocation* loc = ctx->GetItemLocation(rc);
            if (loc && loc->HasObtained()) {
                Rando::Location* sl = Rando::StaticData::GetLocation(rc);
                if (sl && !sl->GetName().empty())
                    out.push_back(sl->GetName());
            }
        }
    }
    cached = out.dump();
    return cached.c_str();
}

// Trigger combo generation. Gated on RandoGenerating so a second press during generation is a no-op;
// reads the seed from the shared CVar (written by the comboui seed field). Sets RandoGenerating=1 so
// the file-select loop swaps to gallop music + shows progress; the finalize poll clears it.
extern "C" __declspec(dllexport) void SOH_TriggerComboGenerate(void) {
    if (CVarGetInteger(CVAR_GENERAL("RandoGenerating"), 0) != 0)
        return; // already generating
    if (!gComboGenerateRequestCallback)
        return;
    const char* seed = CVarGetString(CVAR_GENERAL("ComboSeed"), "");
    CVarSetInteger(CVAR_GENERAL("RandoGenerating"), 1);
    gComboGenerateRequestCallback(seed);
}

// ComboShip: true when the active OOT gamestate is the file-select screen. The combo Generate action
// is gated to this so the worker can't race a live game tick (the prior off-thread crash class).
// GameState::init is cleared after init runs, so match on ::main (set to FileChoose_Main for the
// state's lifetime by FileChoose_Init).
extern "C" void FileChoose_Main(GameState* thisx);
extern "C" __declspec(dllexport) uint8_t SOH_IsOnFileSelect(void) {
    return (gPlayState == NULL && gGameState != NULL && gGameState->main == (GameStateFunc)FileChoose_Main) ? 1 : 0;
}

extern "C" __declspec(dllexport) void SOH_SetSeedGenerated(uint8_t g) {
    if (OTRGlobals::Instance && OTRGlobals::Instance->gRandoContext)
        OTRGlobals::Instance->gRandoContext->SetSeedGenerated(g != 0);
}

// ComboShip: OOT combo-logic exports — thin wrappers around the existing logic engine. The
// combined fill drives these to query "given owned items, which checks are reachable?"

static bool sOracleInitialized = false;

static void EnsureOracleInit() {
    if (sOracleInitialized)
        return;
    auto ctx = OTRGlobals::Instance->gRandoContext;
    Rando::Settings::GetInstance()->SetAllToContext(); // ComboShip: apply chosen CVar settings before finalizing
    ctx->GetLogic()->Reset();
    ctx->FinalizeSettings({}, {});
    RegionTable_Init();
    ctx->GenerateLocationPool();
    // ComboShip: deliberately do NOT call GenerateItemPool() here. It builds OOT's item pool for OOT's
    // OWN fill (which the combo layer never runs), and asserts `itemPool.size() <= locCount`; under the
    // headless default settings the pool isn't balanced, so that assert aborts. The oracle only needs
    // reachability, which reads the logic/region state + allLocations — neither needs the item pool.
    GenerateStartingInventory();
    sOracleInitialized = true;
}

extern "C" __declspec(dllexport) void Combo_SOH_Rando_Reset(void) {
    auto ctx = OTRGlobals::Instance->gRandoContext;
    EnsureOracleInit();
    ctx->GetLogic()->Reset();
    Regions::AccessReset();
    ctx->LocationReset();
    ctx->ItemReset();
    ApplyStartingInventory();
}

extern "C" __declspec(dllexport) void Combo_SOH_Rando_SetOwnedItems(const char* itemNamesJson) {
    if (!itemNamesJson)
        return;
    auto ctx = OTRGlobals::Instance->gRandoContext;
    try {
        auto items = nlohmann::json::parse(itemNamesJson);
        for (const auto& name : items) {
            auto it = Rando::StaticData::itemNameToEnum.find(name.get<std::string>());
            if (it == Rando::StaticData::itemNameToEnum.end())
                continue;
            Rando::Item& item = Rando::StaticData::RetrieveItem(it->second);
            item.ApplyEffect();
        }
    } catch (...) {}
}

extern "C" __declspec(dllexport) const char* Combo_SOH_Rando_GetReachableChecks(void) {
    static std::string buf;
    auto ctx = OTRGlobals::Instance->gRandoContext;
    auto reachable = ReachabilitySearch(ctx->allLocations);
    nlohmann::json out = nlohmann::json::array();
    for (RandomizerCheck rc : reachable) {
        const std::string& name = Rando::StaticData::GetLocation(rc)->GetName();
        if (!name.empty())
            out.push_back(name);
    }
    buf = out.dump();
    return buf.c_str();
}

extern "C" __declspec(dllexport) void Combo_SOH_Rando_PlaceItem(const char* checkName, const char* itemName) {
    if (!checkName || !itemName)
        return;
    auto ctx = OTRGlobals::Instance->gRandoContext;
    auto rcIt = Rando::StaticData::locationNameToEnum.find(checkName);
    if (rcIt == Rando::StaticData::locationNameToEnum.end())
        return;
    auto rgIt = Rando::StaticData::itemNameToEnum.find(itemName);
    if (rgIt == Rando::StaticData::itemNameToEnum.end())
        return;
    ctx->PlaceItemInLocation(rcIt->second, rgIt->second, false, false);
}

// ComboShip: Link's Pocket is a rando-only check absent from the cross-world dump, so the combined
// fill never assigns it. Decide its item per RSK_LINKS_POCKET here so the launcher can reserve it
// from the cross pool. Returns { "Link's Pocket": {"item":"<name>"} } (dungeon-reward) or
// {"category":"advancement"|"any"} (combo picks); {} for NOTHING. See docs/UPSTREAM_MERGES.md.
extern "C" __declspec(dllexport) const char* SOH_GetForcedPlacements(uint32_t seed) {
    static std::string buf;
    nlohmann::json out = nlohmann::json::object();
    try {
        auto ctx = OTRGlobals::Instance->gRandoContext;
        Rando::Settings::GetInstance()->SetAllToContext(); // ensure chosen CVar settings are live
        auto lp = ctx->GetOption(RSK_LINKS_POCKET);
        if (lp.Is(RO_LINKS_POCKET_DUNGEON_REWARD)) {
            std::vector<RandomizerGet> rewards;
            auto addRange = [&](int lo, int hi) {
                for (int r = lo; r <= hi; ++r)
                    rewards.push_back(static_cast<RandomizerGet>(r));
            };
            auto sub = ctx->GetOption(RSK_LINKS_POCKET_REWARD);
            if (sub.Is(RO_LINKS_POCKET_ANY_STONE)) {
                addRange(RG_KOKIRI_EMERALD, RG_ZORA_SAPPHIRE);
            } else if (sub.Is(RO_LINKS_POCKET_LIGHT_MEDALLION)) {
                rewards.push_back(RG_LIGHT_MEDALLION);
            } else if (sub.Is(RO_LINKS_POCKET_ANY_MEDALLION)) {
                addRange(RG_FOREST_MEDALLION, RG_LIGHT_MEDALLION);
            } else { // RO_LINKS_POCKET_ANY_REWARD (default)
                addRange(RG_KOKIRI_EMERALD, RG_ZORA_SAPPHIRE);
                addRange(RG_FOREST_MEDALLION, RG_LIGHT_MEDALLION);
            }
            if (!rewards.empty()) {
                uint64_t s = (seed ? seed : 0x9E3779B97F4A7C15ULL) * 6364136223846793005ULL + 1442695040888963407ULL;
                RandomizerGet pick = rewards[static_cast<size_t>(s >> 33) % rewards.size()];
                out["Link's Pocket"]["item"] = Rando::StaticData::RetrieveItem(pick).GetName().GetEnglish();
            }
        } else if (lp.Is(RO_LINKS_POCKET_ADVANCEMENT)) {
            out["Link's Pocket"]["category"] = "advancement";
        } else if (lp.Is(RO_LINKS_POCKET_ANYTHING)) {
            out["Link's Pocket"]["category"] = "any";
        }
        // RO_LINKS_POCKET_NOTHING: nothing to force.
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[ComboShip] SOH_GetForcedPlacements: {}", e.what());
    } catch (...) {}
    buf = out.dump();
    return buf.c_str();
}

bool SoH_HandleConfigDrop(char* filePath) {
    if (SohUtils::IsStringEmpty(filePath)) {
        return false;
    }
    try {
        std::ifstream configStream(filePath);
        if (!configStream) {
            return false;
        }

        nlohmann::json configJson;
        configStream >> configJson;

        if (!configJson.contains("CVars")) {
            return false;
        }

        CVarClearBlock(CVAR_PREFIX_ENHANCEMENT);
        CVarClearBlock(CVAR_PREFIX_CHEAT);
        CVarClearBlock(CVAR_PREFIX_RANDOMIZER_SETTING);
        CVarClearBlock(CVAR_PREFIX_RANDOMIZER_ENHANCEMENT);
        CVarClearBlock(CVAR_PREFIX_DEVELOPER_TOOLS);

        // Flatten everything under CVars into a single array
        auto cvars = configJson["CVars"].flatten();

        for (auto& [key, value] : cvars.items()) {
            // Replace slashes with dots in key, and remove leading dot
            std::string path = key;
            std::replace(path.begin(), path.end(), '/', '.');
            if (path[0] == '.') {
                path.erase(0, 1);
            }
            if (value.is_string()) {
                CVarSetString(path.c_str(), value.get<std::string>().c_str());
            } else if (value.is_number_integer()) {
                CVarSetInteger(path.c_str(), value.get<int>());
            } else if (value.is_number_float()) {
                CVarSetFloat(path.c_str(), value.get<float>());
            }
        }

        auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
        gui->GetGuiWindow("Console")->Hide();
        gui->GetGuiWindow("Actor Viewer")->Hide();
        gui->GetGuiWindow("Collision Viewer")->Hide();
        gui->GetGuiWindow("Save Editor")->Hide();
        gui->GetGuiWindow("Display List Viewer")->Hide();
        gui->GetGuiWindow("Stats")->Hide();
        std::dynamic_pointer_cast<Ship::ConsoleWindow>(
            std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                ->GetGuiWindow("Console"))
            ->ClearBindings();

        Rando::Settings::GetInstance()->UpdateAllOptions();
        SohGui::MarkRandomizerMenusDirty();
        gui->SaveConsoleVariablesNextFrame();
        ShipInit::Init("*");

        uint32_t finalHash = SohUtils::Hash(configJson.dump());
        gui->GetGameOverlay()->TextDrawNotification(30.0f, true, "Configuration Loaded. Hash: %d", finalHash);
        return true;
    } catch (std::exception& e) {
        SPDLOG_ERROR("Failed to load config file: {}", e.what());
        auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
        gui->GetGameOverlay()->TextDrawNotification(30.0f, true, "Failed to load config file");
        return false;
    } catch (...) {
        SPDLOG_ERROR("Failed to load config file");
        auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
        gui->GetGameOverlay()->TextDrawNotification(30.0f, true, "Failed to load config file");
        return false;
    }
    return false;
}

// Number of interpolated frames
extern "C" uint32_t Ship_GetInterpolationFrameCount() {
    return static_cast<uint32_t>(ceil((float)OTRGlobals::Instance->GetInterpolationFPS() / 20.0f));
}
