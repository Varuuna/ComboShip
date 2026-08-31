#include "OTRGlobals.h"
#include "OTRAudio.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_set>
#include <vector>
#include <chrono>
#include <optional>
#include <imgui.h>

#include "ResourceManagerHelpers.h"
#include <fast/Fast3dWindow.h>
#include <libultraship/bridge/audiobridge.h>
#include <libultraship/bridge/gfxdebuggerbridge.h>
#include <libultraship/bridge/windowbridge.h>
#include <ship/Context.h>
#include <ship/resource/CrossRMRegistry.h>
#include <ship/resource/ResourceManagerScope.h>
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
#include <ship/resource/archive/O2rArchive.h>
#include <ship/utils/binarytools/MemoryStream.h>
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
#include "soh/Enhancements/randomizer/Traps.h" // ComboShip: Rando::Traps::CanBeTrapModel for disguise curation
#include "soh/Enhancements/randomizer/3drando/fill.hpp"
#include "soh/Enhancements/randomizer/3drando/shops.hpp"
#include "soh/Enhancements/randomizer/rng.h"
#include "soh/Enhancements/randomizer/location_access.h"
#include "soh/Enhancements/randomizer/3drando/item_pool.hpp"
#include "soh/Enhancements/randomizer/3drando/starting_inventory.hpp"
#include "soh/Enhancements/randomizer/entrance.h"        // ComboShip: ENTRANCE_SHUFFLE_FAILURE
#include "soh/Enhancements/randomizer/3drando/hints.hpp" // ComboShip: CreateChildAltarHint/CreateAdultAltarHint
#include "soh/Enhancements/randomizer/randomizer_check_objects.h" // ComboShip: GetRCAreaName/AreaIsDungeon for hint dump
#include "soh/Enhancements/randomizer/trial.h"                    // ComboShip: GetTrials() for resolved trial dump
#include "soh/Enhancements/randomizer/randomizerEnumStrings.h"    // ComboShip: EnumToString<RandomizerHintTextKey>()
#include "soh/Enhancements/randomizer/hint.h" // ComboShip: Rando::Hint/AddHint for SOH_ApplyComboHints
#include "Enhancements/gameplaystats.h"
#ifdef COMBO_BUILD
#include "soh/Enhancements/TimeDisplay/TimeDisplay.h" // ComboShip: NAVI_* phase bounds for SOH_GetOverlayTimers
#endif
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
#include "Enhancements/Restorations/GetItemManipulation.h"
#include "Enhancements/Lang/Lang.h"
#include "soh/SohGui/ImGuiUtils.h"
#include "ActorDB.h"
#include "SaveManager.h"
#include "soh/Network/CrowdControl/CrowdControl.h"
#include "soh/Network/Sail/Sail.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/util.h" // ComboShip: SohUtils::GetSceneName (Anchor roster), AppendVector (entrance-shuffle pool)
#include "soh/Enhancements/randomizer/SeedContext.h" // ComboShip: Rando::Context::GetSeed for roster seed-mismatch
#include "Enhancements/game-interactor/GameInteractor.h"
#include "Enhancements/randomizer/draw.h"
#include <libultraship/controller/controldeck/ControlDeck.h>
#include <fast/resource/ResourceType.h>

// Resource Types/Factories
#include <fast/resource/type/Matrix.h>
#include "soh/resource/type/SohResourceType.h"
#include "soh/resource/type/Animation.h"
#include "soh/resource/type/Skeleton.h"
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
#include "rando/CrossForeign.h"     // ComboShip (#164): g_comboForeignJson for the hint-key map replay
#include "soh/Enhancements/randomizer/hook_handlers.h" // ComboShip (#164): OOT_ForeignMapGen
#include <functional>                                  // ComboShip (#164): shared hint-resolution callbacks
#endif

#ifdef _MSC_VER
#define strdup _strdup
#endif

#ifdef _MSC_VER
#define strdup _strdup
#endif

#ifdef _MSC_VER
#define strdup _strdup
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
#ifdef COMBO_BUILD
    // ComboShip (issue 24): OOT + MM share this one Context, so this is the single combined config.
    // Named comboship.json to make that explicit and to gate the first-launch settings import. See
    // docs/UPSTREAM_MERGES.md.
    context = Ship::Context::CreateUninitializedInstance("Ship of Harkinian", appShortName, "comboship.json");
#else
    context = Ship::Context::CreateUninitializedInstance("Ship of Harkinian", appShortName, "shipofharkinian.json");
#endif

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

#ifdef COMBO_BUILD
    // ComboShip: the ROM extraction screen runs between this ctor (SOH_InitWindowOnly) and
    // Initialize() — without this, a file dropped there reaches the gfx backend with a null
    // FileDropMgr, and the screen couldn't accept dropped ROMs. Initialize()'s own
    // InitFileDropMgr call is idempotent, so the full-boot path is unchanged.
    context->InitFileDropMgr();
#endif

    auto sohInputEditorWindow =
        std::make_shared<SohInputEditorWindow>(CVAR_WINDOW("ControllerConfiguration"), "Configure Controller");
    sohFast3dWindow =
        std::make_shared<Fast::Fast3dWindow>(std::vector<std::shared_ptr<Ship::GuiWindow>>({ sohInputEditorWindow }));
    context->InitWindow(sohFast3dWindow);
#ifdef COMBO_BUILD
    // ImGui's current-context global (GImGui) is a per-module static. libultraship.dll created
    // the context inside InitWindow; point this DLL's GImGui at it before any ImGui use here
    // (e.g. CreateFontWithSize below), or ImGui::GetIO() asserts on a null context.
    ImGui::SetCurrentContext(context->GetWindow()->GetGui()->GetImGuiContext());
#endif

    SohGui::SetupMenu();

    if (sohArchiveVersionMatch) {

        auto overlay = context->GetWindow()->GetGui()->GetGameOverlay();
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

#ifdef COMBO_BUILD
// ComboShip: rando-only headless ctor — Context + config + CVars, no ControlDeck/RM/Console/Window/GUI.
OTRGlobals::OTRGlobals(HeadlessRandoTag) {
    context = Ship::Context::CreateUninitializedInstance("Ship of Harkinian", appShortName, "comboship.json");
    context->InitConfiguration();
    context->InitConsoleVariables();
    // Detect quest availability from the o2r files without loading archives (mirrors Initialize's hash
    // check) so IsQuestOfLocationActive keeps the right dungeon locations in the pool.
    hasOriginal = std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs("oot.o2r", appShortName));
    hasMasterQuest = std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs("oot-mq.o2r", appShortName));
    if (!hasOriginal && !hasMasterQuest)
        hasOriginal = true; // fallback: assume vanilla so the location pool isn't empty
}
#endif

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
        std::string modsPath = Ship::Context::LocateFileAcrossAppDirs("mods/" + appShortName, appShortName);
        if (!std::filesystem::exists(modsPath)) {
            // Create mods folder relative to app dir
            modsPath = Ship::Context::GetPathRelativeToAppDirectory("mods/" + appShortName, appShortName);
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
    Ship::Context::GetRawInstance()->GetLogger()->set_pattern("[%H:%M:%S.%e] [%s:%#] [%^%l%$] %v");

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
#define SAMPLES_MID 544
#define SAMPLES_LOW 528
#define AUDIO_FRAMES_PER_UPDATE (R_UPDATE_RATE > 0 ? R_UPDATE_RATE : 1)
#define NUM_AUDIO_CHANNELS 2

    // The sequencer advances a fixed slice of musical time per engine update
    // (tempoInternalToExternal in audio_heap.c assumes 60 updates/sec), so with
    // production paced by backend buffer fill the sample count must average
    // exactly 32000/60 = 533.33 per update or tempo drifts.
    // Two thirds 528 one third 544 gives 533.33.
    int32_t sample_debt_thirds = 0;

    // Single producer routine used by both wake-driven and pre-buffer loops.
    // Picks per-iteration sample count itself, then produces and plays it.
    auto produce_next_batch = [&]() {
        u32 num_audio_samples = sample_debt_thirds > 0 ? SAMPLES_MID : SAMPLES_LOW;
        sample_debt_thirds += (1600 - 3 * (int32_t)num_audio_samples) * AUDIO_FRAMES_PER_UPDATE;

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

            // Producer guard (banteg/Shipwright#6594): skip advancing the audio
            // engine if the backend ring cannot accept the largest next burst.
            // Generating PCM that DoPlay() would refuse creates a discontinuity
            // audible as a click. The pre-buffer loop below will catch up once
            // the backend drains enough.
            if (AudioPlayer_Buffered() + SAMPLES_MID * AUDIO_FRAMES_PER_UPDATE > AudioPlayer_GetDesiredBuffered()) {
                audio.processing = false;
            } else {
                produce_next_batch();
                audio.processing = false;
            }
        }

        // Pre-buffer: fill the reservoir while the backend can accept more,
        // without waiting for the next frame signal. This absorbs load spikes.
        // Safe for BGM — the N64 sequencer advances independently of gameplay.
        // The producer guard (same as above) prevents advancing the audio engine
        // when the backend ring is already at capacity.
        while (audio.running && AudioPlayer_Buffered() < AudioPlayer_GetDesiredBuffered()) {
            if (AudioPlayer_Buffered() + SAMPLES_MID * AUDIO_FRAMES_PER_UPDATE > AudioPlayer_GetDesiredBuffered()) {
                break;
            }
            produce_next_batch();
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

// ComboShip: forward declarations — defined further down with the combo exports.
extern "C" void (*gComboSceneSwitchCallback)(int fileNum);
// Launcher poll: returns the next save slot backed up for a release mismatch, or -1 if none.
extern "C" int (*gComboOutdatedSaveNotice)();

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

// ComboShip: true ONLY during a headless rando run (set by SOH_InitRandoHeadless, never by the game).
// Lets Lang::Translate return the raw key instead of asserting when language data isn't loaded (headless
// has no ResourceManager). The game leaves this false, so its assert stays live. See docs/UPSTREAM_MERGES.md.
bool gComboHeadlessRando = false;

// Rando-only headless init: Context config/CVars + rando static data — NO window, RM, audio, or GUI.
// Enough for the reachability oracles so a headless tool can generate + validate cross-world seeds
// without opening the game. See docs/UPSTREAM_MERGES.md.
extern "C" __declspec(dllexport) void SOH_InitRandoHeadless() {
    if (OTRGlobals::Instance)
        return; // already initialized (full boot or a prior headless call)
    gComboHeadlessRando = true;
    OTRGlobals::Instance = new OTRGlobals(OTRGlobals::HeadlessRandoTag{});
    CVarLoad(); // populate CVars from comboship.json so the user's rando settings are live
    // Rando bootstrap (subset of OTRGlobals::Initialize). CreateInstance first, unlike the full path,
    // to avoid dereferencing the still-null gRandoContext member.
    OTRGlobals::Instance->gRandoContext = Rando::Context::CreateInstance();
    OTRGlobals::Instance->gRandoContext->InitStaticData();
    Rando::Settings::GetInstance()->AssignContext(OTRGlobals::Instance->gRandoContext);
    Rando::StaticData::InitItemTable();
    // These location-table registrations normally run as RegisterShipInitFunc callbacks during full boot
    // (ShipInit::InitAll), which headless skips — so without them pots/grass/crates/freestanding/etc. checks
    // don't exist and any seed shuffling them is massively under-modeled (the validator would call it
    // unbeatable). They're pure data (no assets/RM), so call them directly here.
    Rando::StaticData::RegisterSongLocations();
    Rando::StaticData::RegisterBeehiveLocations();
    Rando::StaticData::RegisterCowLocations();
    Rando::StaticData::RegisterFishLocations();
    Rando::StaticData::RegisterFairyLocations();
    Rando::StaticData::RegisterPotLocations();
    Rando::StaticData::RegisterFreestandingLocations();
    Rando::StaticData::RegisterGrassLocations();
    Rando::StaticData::RegisterCrateLocations();
    Rando::StaticData::RegisterRockLocations();
    Rando::StaticData::RegisterTreeLocations();
    Rando::StaticData::RegisterSignLocations();
    Rando::StaticData::RegisterWonderItemLocations();
    Rando::StaticData::RegisterBeggarLocations();
    Rando::StaticData::RegisterIcicleLocations();
    Rando::StaticData::RegisterRedIceLocations();
    // Build the option/trick tables (normally the rando menu's job, which headless lacks) so a spoiler's
    // settings can reach the Context. Option/trick display names route through Lang::Translate, which
    // returns the raw key headless (via gComboHeadlessRando) — no assets needed.
    Rando::Settings::GetInstance()->CreateOptions();
}

// ComboShip (issue 24): apply a launcher-merged config (JSON object) to the live Config and reload the
// dependent subsystems. The launcher does the per-leaf merge (SoH wins) and excludes the Window block;
// here we install each block, persist, and reload CVars + controller mappings. Runs before
// Combo_FinishInit so RunVersionUpdates() then sees the imported state.
extern "C" __declspec(dllexport) int SOH_ApplyImportedConfig(const char* mergedJsonUtf8) {
    if (!mergedJsonUtf8 || !OTRGlobals::Instance) {
        return 0;
    }
    try {
        nlohmann::json merged = nlohmann::json::parse(mergedJsonUtf8);
        auto conf = OTRGlobals::Instance->context->GetConfig();
        for (auto& [key, value] : merged.items()) {
            if (key == "Window") {
                continue; // machine-specific; excluded per design
            }
            if (key == "ConfigVersion") {
                if (value.is_number_unsigned()) {
                    conf->SetUInt("ConfigVersion", value.get<uint32_t>());
                }
                continue;
            }
            if (value.is_object()) {
                conf->SetBlock(key, value);
            }
        }
        conf->Save(); // persist first: CVarLoad() reloads CVars from disk
        CVarLoad();
        if (auto deck = OTRGlobals::Instance->context->GetControlDeck()) {
            for (uint8_t p = 0; p < 4; p++) {
                if (auto c = deck->GetControllerByPort(p); c && c->HasConfig()) {
                    c->ReloadAllMappingsFromConfig();
                }
            }
        }
        return 1;
    } catch (...) { return 0; }
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
    conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion7Updater>());
    conf->RunVersionUpdates();

#ifdef COMBO_BUILD
    // ComboShip: SoH's old float leaf clashes with 2Ship's LinkVoiceFreqMultiplier.{Enable,Scale}
    // children (Config::Save unflatten throws); migrate it to the .Scale child once and persist
    // immediately (a mid-session ConsoleVariable::Load would otherwise resurrect it from disk).
    static const char* kOldVoicePitchCVar = "gAudioEditor.LinkVoiceFreqMultiplier";
    if (OTRGlobals::Instance->context->GetConsoleVariables()->Get(kOldVoicePitchCVar) != nullptr) {
        float oldPitch = CVarGetFloat(kOldVoicePitchCVar, (float)CVarGetInteger(kOldVoicePitchCVar, 0));
        CVarClear(kOldVoicePitchCVar);
        if (oldPitch > 0.0f) {
            CVarSetFloat(CVAR_LINK_VOICE_FREQ_MULTIPLIER, oldPitch);
        }
        CVarSave();
    }
#endif

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

    // ComboShip release gate: drain slots the launcher backed up for a version mismatch and show a
    // popup on this (main) thread — the launcher only records slots, never touches ImGui.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if (!gComboOutdatedSaveNotice)
            return;
        for (int slot; (slot = gComboOutdatedSaveNotice()) >= 0;) {
            SohGui::RegisterPopup("Outdated ComboShip Save",
                                  "The save in slot " + std::to_string(slot + 1) +
                                      " was made by a different ComboShip version and has been backed up.");
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
    // Auto-reconnect Anchor from the persisted enable flag on boot (combo: the launcher wires the
    // connect transport before SOH_Init, so Enable() opens a real socket rather than wedging).
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
    // Destroys the Context (libultraship owns it since #1103). Was previously implicit: soh dropped
    // the last shared_ptr. Must stay here — MM_Deinit runs first and only clears its own pointer.
    Ship::Context::DestroyInstance();
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

// Interpolated frames of a tick are evenly spaced numerators time+step, time+2*step, ... over denom.
void RunCommands(Gfx* Commands, int time, int step, int denom, int count) {
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
    for (int i = 0; i < count; i++) {
        time += step;
        std::unordered_map<Mtx*, MtxF> mtx_replacements =
            (time == denom) ? std::unordered_map<Mtx*, MtxF>() : FrameInterpolation_Interpolate((float)time / denom);
        intp->mInterpolationT = (float)time / denom;
        wnd->DrawAndRunGraphicsCommands(Commands, mtx_replacements);
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

    int start_time = time;
    int count = 0;
    while (time + original_fps <= next_original_frame) {
        time += original_fps;
        count++;
    }

    time -= fps;

    if (wnd != nullptr) {
        wnd->SetTargetFps(fps);
    }

    int step = original_fps;
    // When the gfx debugger is active, only run with the final mtx
    if (GfxDebuggerIsDebugging()) {
        start_time = next_original_frame;
        step = 0;
        count = 1;
    }

    RunCommands(commands, start_time, step, next_original_frame, count);

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
    // A negative getItemId makes the vanilla lookup `sGetItemTable[getItemId - 1]` read out of
    // bounds below the table (Get Item Manipulation); reproduce the console result of that read.
    if (getItemID < 0) {
        return Gim_RetrieveOobGetItemEntry(getItemID);
    }
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

// ComboShip: the current save's file name (8 font-code bytes). Valid inside the new-save callback,
// where the launcher copies it into the matching MM save.
extern "C" __declspec(dllexport) void SOH_GetCurrentPlayerName(unsigned char out8[8]) {
    for (int i = 0; i < 8; i++) {
        out8[i] = gSaveContext.playerName[i];
    }
}

// ComboShip (#83): OOT persists these per-save; MM keeps its equivalents in global.json, which combo
// never writes (MM's file select is never reached), so MM falls back to SaveContext_Init's hardcoded
// defaults — notably Switch targeting. MM adopts OOT's values on entry instead. Language is
// deliberately excluded: the two games' enums disagree (OOT ENG=0, MM JPN=0).
extern "C" __declspec(dllexport) void SOH_GetGlobalOptions(int* zTarget, int* audio) {
    if (zTarget)
        *zTarget = gSaveContext.zTargetSetting;
    if (audio)
        *audio = gSaveContext.audioSetting;
}

extern "C" void (*gComboSceneSwitchCallback)(int fileNum) = nullptr;

extern "C" __declspec(dllexport) void SOH_SetOnSceneSwitchCallback(void (*cb)(int fileNum)) {
    gComboSceneSwitchCallback = cb;
}

extern "C" void (*gComboSaveLoadCallback)(int fileNum) = nullptr;

// ComboShip: fires when OOT loads a save into gameplay (file select / debug select / warp). The
// launcher uses it to pull the matching MM save into dormant MM memory (tracker peek). Call after
// SOH_Init (needs GameInteractor).
extern "C" __declspec(dllexport) void SOH_SetOnLoadSaveCallback(void (*cb)(int fileNum)) {
    gComboSaveLoadCallback = cb;
    static bool sHooked = false;
    if (!sHooked && GameInteractor::Instance) {
        sHooked = true;
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>([](int32_t fileNum) {
            if (gComboSaveLoadCallback) {
                gComboSaveLoadCallback((int)fileNum);
            }
        });
    }
}

#ifdef COMBO_BUILD
// ComboShip: launcher registers its release-eviction poll; OOT drains it each frame (main thread).
extern "C" int (*gComboOutdatedSaveNotice)() = nullptr;
extern "C" __declspec(dllexport) void SOH_SetOutdatedSaveNotice(int (*fn)()) {
    gComboOutdatedSaveNotice = fn;
}

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
// A6: launcher registers its per-frame dormant-pump fn; the active game calls it each frame (see the
// OnGameFrameUpdate hook) so the launcher can drive the dormant sibling's apply on the game thread.
extern "C" void (*gComboPumpDormant)() = nullptr;
extern "C" __declspec(dllexport) void SOH_SetPumpDormant(void (*cb)()) {
    gComboPumpDormant = cb;
}
extern "C" __declspec(dllexport) void SOH_Anchor_PumpDormant(void) {
    if (Anchor::Instance) {
        Anchor::Instance->PumpDormant();
    }
}
// Bug 2: launcher-orchestrated resync (auto on connect + combo menu button), dormant-safe.
// Finding 3: never let an exception unwind across this extern "C" boundary.
extern "C" __declspec(dllexport) void SOH_Anchor_RequestResync(void) {
    try {
        if (Anchor::Instance) {
            Anchor::Instance->RequestResyncDormantSafe();
        }
    } catch (const std::exception& e) { SPDLOG_ERROR("[SOH_Anchor_RequestResync] {}", e.what()); } catch (...) {
        SPDLOG_ERROR("[SOH_Anchor_RequestResync] unknown exception");
    }
}

// ComboShip: combo-native Anchor connection panel drives Enable/Disable (soh's own menu is hidden in
// combo). Mirrors Menu.cpp's Enable/Disable path incl. the Enabled CVar write.
extern "C" __declspec(dllexport) void SOH_Anchor_SetEnabled(int enabled) {
    try {
        if (!Anchor::Instance) {
            return;
        }
        if (enabled) {
            CVarSetInteger(CVAR_REMOTE_ANCHOR("Enabled"), 1);
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            Anchor::Instance->Enable();
        } else {
            CVarClear(CVAR_REMOTE_ANCHOR("Enabled"));
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            Anchor::Instance->Disable();
        }
    } catch (const std::exception& e) { SPDLOG_ERROR("[SOH_Anchor_SetEnabled] {}", e.what()); } catch (...) {
        SPDLOG_ERROR("[SOH_Anchor_SetEnabled] unknown exception");
    }
}

// ComboShip: connection state for the combo panel status line/gating. bit0=isEnabled, bit1=isConnected.
extern "C" __declspec(dllexport) int SOH_Anchor_GetConnectionState(void) {
    if (!Anchor::Instance) {
        return 0;
    }
    return (Anchor::Instance->isEnabled ? 1 : 0) | (Anchor::Instance->isConnected ? 2 : 0);
}

// ComboShip: owner-gating for the combo panel's room-admin section. bit0=isOwner,
// bit1=isGlobalRoom. 0 if Anchor not connected. Mirrors AnchorAdminMenu's gate.
extern "C" __declspec(dllexport) int SOH_Anchor_GetOwnerInfo(void) {
    try {
        auto anchor = Anchor::Instance;
        if (!anchor || !anchor->isEnabled || !anchor->isConnected) {
            return 0;
        }
        bool isOwner = anchor->roomState.ownerClientId == anchor->ownClientId;
        bool isGlobalRoom = std::string("soh-global") == CVarGetString(CVAR_REMOTE_ANCHOR("RoomId"), "");
        return (isOwner ? 1 : 0) | (isGlobalRoom ? 2 : 0);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[SOH_Anchor_GetOwnerInfo] {}", e.what());
        return 0;
    } catch (...) {
        SPDLOG_ERROR("[SOH_Anchor_GetOwnerInfo] unknown exception");
        return 0;
    }
}

// ComboShip: broadcast the RoomSettings.* CVar changes made in the combo admin panel to the room.
extern "C" __declspec(dllexport) void SOH_Anchor_SendRoomState(void) {
    try {
        if (Anchor::Instance) {
            Anchor::Instance->SendPacket_UpdateRoomState();
        }
    } catch (const std::exception& e) { SPDLOG_ERROR("[SOH_Anchor_SendRoomState] {}", e.what()); } catch (...) {
        SPDLOG_ERROR("[SOH_Anchor_SendRoomState] unknown exception");
    }
}

// ComboShip: clear team state for every team present in the room (mirrors AnchorAdminMenu's button).
extern "C" __declspec(dllexport) void SOH_Anchor_ClearTeamState(void) {
    try {
        if (!Anchor::Instance) {
            return;
        }
        std::set<std::string> teams;
        for (auto& [clientId, client] : Anchor::Instance->clients) {
            teams.insert(client.teamId);
        }
        for (auto& team : teams) {
            Anchor::Instance->SendPacket_ClearTeamState(team);
        }
    } catch (const std::exception& e) { SPDLOG_ERROR("[SOH_Anchor_ClearTeamState] {}", e.what()); } catch (...) {
        SPDLOG_ERROR("[SOH_Anchor_ClearTeamState] unknown exception");
    }
}

// ComboShip: stateless OOT scene-name lookup for the combo room window. The launcher owns the roster
// now; comboui resolves each OOT peer's area name from its raw scene id via this (works while dormant).
extern "C" __declspec(dllexport) const char* SOH_Anchor_ResolveScene(int sceneId) {
    static std::string cached;
    if (sceneId >= 0 && sceneId < 1000) {
        cached = SohUtils::GetSceneName(sceneId);
    } else {
        cached = "";
    }
    return cached.c_str();
}

// ComboShip: same-game teleport trigger for the combo room window (OOT active + OOT peer).
// Wraps SendPacket_RequestTeleport, which re-validates via CanTeleportTo and no-ops if disallowed.
extern "C" __declspec(dllexport) void SOH_Anchor_RequestTeleport(uint32_t clientId) {
    try {
        if (Anchor::Instance) {
            Anchor::Instance->SendPacket_RequestTeleport(clientId);
        }
    } catch (const std::exception& e) { SPDLOG_ERROR("[SOH_Anchor_RequestTeleport] {}", e.what()); } catch (...) {
        SPDLOG_ERROR("[SOH_Anchor_RequestTeleport] unknown exception");
    }
}

// ComboShip: cross-game item delivery seam (issue #3). When the other game collects a check whose
// item belongs to OOT, the launcher calls SOH_GrantCrossItem to grant it straight into OOT's
// resident save — even when OOT is the dormant (frozen) game. We use Randomizer_Item_Give, which
// writes gSaveContext directly and is play-state-independent (Magic_Fill ignores `play`,
// Rupees_ChangeBy null-guards gPlayState), so it is safe against a frozen gPlayState. The save is
// persisted immediately so the item survives quitting before ever switching into OOT. See
// docs/UPSTREAM_MERGES.md.
// ComboShip: save-only side effects RandomizerOnItemReceiveHandler applies on a normal pickup
// (hook_handlers.cpp); grants that bypass the receive hook must mirror them or they're lost.
void Combo_ApplyItemReceiveSideEffects(const GetItemEntry& gie) {
    if (gie.modIndex == MOD_NONE) {
        switch (gie.itemId) {
            case ITEM_SHIELD_DEKU:
                Flags_SetRandomizerInf(RAND_INF_HAS_FOUND_DEKU_SHIELD);
                break;
            case ITEM_SHIELD_HYLIAN:
                Flags_SetRandomizerInf(RAND_INF_HAS_FOUND_HYLIAN_SHIELD);
                break;
            case ITEM_TUNIC_GORON:
                Flags_SetRandomizerInf(RAND_INF_HAS_FOUND_GORON_TUNIC);
                break;
            case ITEM_TUNIC_ZORA:
                Flags_SetRandomizerInf(RAND_INF_HAS_FOUND_ZORA_TUNIC);
                break;
            case ITEM_SONG_EPONA:
                Flags_SetEventChkInf(EVENTCHKINF_EPONA_OBTAINED);
                break;
        }
    }
    // Skip Planting Beans pre-plants on Bean Pack receipt; the live Flags_SetSwitch half of the hook
    // is deliberately skipped (dormant/foreign scene) — flags apply on next scene load.
    if (gie.modIndex == MOD_RANDOMIZER && gie.getItemId == RG_MAGIC_BEAN_PACK &&
        OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_SKIP_PLANTING_BEANS)) {
        gSaveContext.sceneFlags[SCENE_DEATH_MOUNTAIN_CRATER].swch |= (1 << 3);
        gSaveContext.sceneFlags[SCENE_DEATH_MOUNTAIN_TRAIL].swch |= (1 << 6);
        gSaveContext.sceneFlags[SCENE_DESERT_COLOSSUS].swch |= (1 << 24);
        gSaveContext.sceneFlags[SCENE_GERUDO_VALLEY].swch |= (1 << 3);
        gSaveContext.sceneFlags[SCENE_GRAVEYARD].swch |= (1 << 3);
        gSaveContext.sceneFlags[SCENE_KOKIRI_FOREST].swch |= (1 << 9);
        gSaveContext.sceneFlags[SCENE_LAKE_HYLIA].swch |= (1 << 1);
        gSaveContext.sceneFlags[SCENE_LOST_WOODS].swch |= (1 << 4) | (1 << 18);
        gSaveContext.sceneFlags[SCENE_ZORAS_RIVER].swch |= (1 << 3);
        AMMO(ITEM_BEAN) = 0;
    }
}

// ComboShip: save-direct grant of a resolved OOT item. Shared by SOH_GrantCrossItem and Anchor's
// team-state backfill so both apply identical dispatch + side effects + persist.
void Combo_GrantResolvedOOT(const GetItemEntry& gie) {
    // ComboShip (#84): drop bottle CONTENTS when no bottle is free. Milk Bottle and Ruto's Letter are
    // excluded exactly as Item_Give excludes them — they create a new bottle, so gating them here
    // would permanently lose Ruto's Letter and softlock the seed.
    if (gie.modIndex == MOD_NONE &&
        (((gie.itemId >= ITEM_POTION_RED) && (gie.itemId <= ITEM_POE)) || (gie.itemId == ITEM_MILK)) &&
        gie.itemId != ITEM_MILK_BOTTLE && gie.itemId != ITEM_LETTER_RUTO && !Inventory_HasEmptyBottle()) {
        SPDLOG_INFO("[ComboShip] OOT cross-grant: no empty bottle, dropping bottle contents");
        return;
    }
    // A resolved OOT item can be a vanilla (MOD_NONE) entry, which Randomizer_Item_Give asserts
    // against. Dispatch by mod index exactly like Anchor's HandlePacket_GiveItem.
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
    Combo_ApplyItemReceiveSideEffects(gie); // receive-hook effects the save-direct grant bypasses
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
}

extern "C" __declspec(dllexport) void SOH_GrantCrossItem(const char* itemName) {
    if (!itemName)
        return;
    auto it = Rando::StaticData::itemNameToEnum.find(itemName);
    if (it == Rando::StaticData::itemNameToEnum.end()) {
        SPDLOG_WARN("[ComboShip] SOH_GrantCrossItem: unknown OOT item '{}'", itemName);
        return;
    }
    GetItemEntry gie = Rando::StaticData::RetrieveItem(it->second).GetGIEntry_Copy();
    Combo_GrantResolvedOOT(gie);
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
        // Full save (not SaveSection): incremental tracker saves demote COLLECTED->SCUMMED on
        // disk, and no OOT flag exists for a foreign-collected check to promote it back from.
        SaveManager::Instance->SaveFile(gSaveContext.fileNum);
    }
    SPDLOG_INFO("[ComboShip] SOH_MarkForeignObtained: marked OOT check '{}' collected", checkName);
}

// ComboShip: routing seam — the launcher registers DeliverCrossItem here so OOT's foreign-check
// detection can hand an item to the OTHER game immediately (mirrors SOH_SetAnchorSend).
extern "C" void (*gComboCrossDeliver)(int targetGame, const char* itemName, const char* srcCheckName) = nullptr;
extern "C" __declspec(dllexport) void SOH_SetCrossDeliver(void (*cb)(int, const char*, const char*)) {
    gComboCrossDeliver = cb;
}
// ComboShip: routing seam for the network-receive idempotency mark (see SOH_MarkForeignObtained).
extern "C" void (*gComboMarkForeignObtained)(int srcGame, const char* checkName) = nullptr;
extern "C" __declspec(dllexport) void SOH_SetMarkForeignObtained(void (*cb)(int, const char*)) {
    gComboMarkForeignObtained = cb;
}
// ComboShip: end-gating seam. z_boss_ganon2.c calls gComboFinalBossDefeated when Ganon dies to learn
// whether MM's Majora is also dead (=> play OOT's ending) or not (=> warp to the portal to finish MM).
extern "C" int (*gComboFinalBossDefeated)(int game, int fileNum) = nullptr;
extern "C" __declspec(dllexport) void SOH_SetFinalBossDefeatedCb(int (*cb)(int, int)) {
    gComboFinalBossDefeated = cb;
}

// ComboShip (#136): Triforce Hunt is ONE combined goal across both games, so the launcher owns it and
// pushes it here. hunt=0 means the normal both-bosses goal; required is the combined piece count.
extern "C" int gComboGoalHunt = 0;
extern "C" int gComboGoalRequired = 0;
// This game's share of the combined piece total, forced in FinalizeSettings. -1 = unset (old seed),
// so OOT's own slider decides. Clamped to the option's 0..100 range.
extern "C" int gComboGoalPieces = -1;
extern "C" __declspec(dllexport) void SOH_SetComboGoal(int hunt, int required, int pieces) {
    gComboGoalHunt = hunt ? 1 : 0;
    gComboGoalRequired = gComboGoalHunt ? required : 0;
    gComboGoalPieces = pieces < 0 ? -1 : (pieces > 100 ? 100 : pieces);
}
// The goal currently in force (comboui reads it for the combined-progress readout). Returns hunt on/off.
extern "C" __declspec(dllexport) int SOH_GetComboGoal(int* required) {
    if (required != NULL) {
        *required = gComboGoalRequired;
    }
    return gComboGoalHunt;
}
// Menu-authored goal CVars, read here because the launcher has no CVar access. Returns hunt on/off.
extern "C" __declspec(dllexport) int SOH_ReadComboGoalCVars(int* required, int* total) {
    const int hunt = CVarGetInteger("gCombo.Rando.TriforceHunt", 0) != 0 ? 1 : 0;
    int req = CVarGetInteger("gCombo.Rando.TriforceRequired", 15);
    int tot = CVarGetInteger("gCombo.Rando.TriforceTotal", 15);
    // 100 = CrossWorldRando.h kMaxComboTriforcePieces (past that OOT's half overflows its item pool).
    req = std::clamp(req, 1, 100);
    tot = std::clamp(tot, req, 100);
    if (required != NULL) {
        *required = hunt ? req : 0;
    }
    if (total != NULL) {
        *total = tot; // the hunt-off force to 0 pieces happens game-side, so report the raw total
    }
    return hunt;
}
// ComboShip (#135): which game a new file starts in. The launcher resolves OOT/MM/Random per seed and
// pushes the concrete value here; FinalizeSettings forces the settings an MM start needs.
extern "C" int gComboStartingGameMM = 0;
extern "C" __declspec(dllexport) void SOH_SetComboStartingGame(int mmStart) {
    gComboStartingGameMM = mmStart ? 1 : 0;
}
// Menu-authored CVar (0 = OOT, 1 = MM, 2 = Random), read here because the launcher has no CVar access.
extern "C" __declspec(dllexport) int SOH_ReadComboStartingGameCVar(void) {
    return CVarGetInteger("gCombo.Rando.StartingGame", 0);
}
// An explicit MM start forces these three, so grey them out. Under Random they stay editable — the
// force is silent when MM rolls. HandleStartingAgeUI owns RSK_STARTING_AGE in both directions.
extern "C" __declspec(dllexport) void SOH_RefreshComboStartingGameUI(void) {
    auto settings = Rando::Settings::GetInstance();
    if (settings == nullptr) {
        return;
    }
    settings->HandleStartingAgeUI();
    const bool mmStart = CVarGetInteger("gCombo.Rando.StartingGame", 0) == 1;
    for (const RandomizerSettingKey key : { RSK_EXCLUDE_MASK_SHOP_KEY, RSK_EXCLUDE_MASK_SHOP_ENTRANCE }) {
        if (mmStart) {
            settings->GetOption(key).Disable("Starting Game is Majora's Mask");
        } else {
            settings->GetOption(key).Enable();
        }
    }
}

extern "C" __declspec(dllexport) int SOH_GetTriforcePieceCount(void) {
    return gSaveContext.ship.quest.data.randomizer.triforcePiecesCollected;
}
// The OTHER game's piece count, so pickup messages/hints can show the combined progress.
extern "C" int (*gComboOtherTriforceCount)(void) = nullptr;
extern "C" __declspec(dllexport) void SOH_SetOtherTriforceCountCb(int (*cb)(void)) {
    gComboOtherTriforceCount = cb;
}
// Poked after every piece grant (active or dormant); the launcher evaluates the combined total.
extern "C" void (*gComboTriforceProgress)(int game, int fileNum) = nullptr;
extern "C" __declspec(dllexport) void SOH_SetTriforceProgressCb(void (*cb)(int, int)) {
    gComboTriforceProgress = cb;
}
// Goal reached: active = the native credits-warp flag; dormant = mark the file complete and persist.
// The dormant save can throw, and the launcher calls this — no exception may cross the C-ABI boundary.
extern "C" __declspec(dllexport) void SOH_TriggerTriforceCredits(int dormant) try {
    if (dormant) {
        gSaveContext.ship.stats.gameComplete = 1;
        if (SaveManager::Instance && gSaveContext.fileNum >= 0 && gSaveContext.fileNum <= 2) {
            SaveManager::Instance->SaveFile(gSaveContext.fileNum);
        }
        return;
    }
    GameInteractor_SetTriforceHuntCreditsWarpActive(true);
} catch (const std::exception& e) {
    SPDLOG_ERROR("[ComboShip] SOH_TriggerTriforceCredits threw: {}", e.what());
} catch (...) { SPDLOG_ERROR("[ComboShip] SOH_TriggerTriforceCredits threw a non-std exception"); }
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
    auto ctx = Ship::Context::GetRawInstance();

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
    // Menu code can load OOT resources — scope OOT's own RM, not the foreground game's (also in the
    // eval/draw/apply exports below; see combo/gui/ComboWidgetRender.h).
    Ship::ResourceManagerScope rmScope(Ship::CrossRMRegistry::Get("oot"));
    if (auto menu = SohGui::GetSohMenu())
        menu->InvokeCallbackByIndex(i);
}

// ComboShip: re-run the ShipInit func(s) registered for this CVar, mirroring what soh's native
// UIWidgets does after a widget change. Without this, settings changed via the combo menu only take
// effect on the next ShipInit::InitAll (game boot / new save) — enhancements wouldn't apply live.
extern "C" __declspec(dllexport) void SOH_MenuApplyCVarChange(const char* cvar) {
    Ship::ResourceManagerScope rmScope(Ship::CrossRMRegistry::Get("oot")); // ShipInit funcs load OOT resources
    if (cvar && cvar[0])
        ShipInit::Init(cvar);
}

// ComboShip: combo owns generation and never reaches GenerateRandomizerImgui, the only vanilla fire site
// of this hook — so cosmetics/audio "randomize on rando gen" never ran. The launcher fires it instead.
extern "C" __declspec(dllexport) void SOH_FireGenerationCompleteHooks(void) {
    Ship::ResourceManagerScope rmScope(Ship::CrossRMRegistry::Get("oot")); // gfx patches load OOT resources
    // Subscribers hit std::map::at and allocate; a throw must not unwind across the C ABI into the exe.
    try {
        GameInteractor::Instance->ExecuteHooks<GameInteractor::OnGenerationCompletion>();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[ComboShip] SOH_FireGenerationCompleteHooks: gen hook threw: {}", e.what());
    } catch (...) { SPDLOG_ERROR("[ComboShip] SOH_FireGenerationCompleteHooks: gen hook threw a non-std exception"); }
}

#ifdef COMBO_BUILD
// ComboShip: true when OOT is the foreground game (queries comboui's ComboUI_GetForegroundGame, resolved
// once). SohMenuDevTools uses it to gate OOT's live-world dev viewers — opening OOT's tab while MM is
// foreground must not draw against OOT's dormant/swapped play state. comboui is always loaded under
// ComboShip; if it somehow isn't, default to true (draw) rather than hiding the tools.
bool Combo_OotIsForeground(void) {
    static int (*sFn)(void) = nullptr;
    static bool sTried = false;
    if (!sTried) {
        sTried = true;
        if (HMODULE h = GetModuleHandleA("comboui.dll"))
            sFn = (int (*)(void))GetProcAddress(h, "ComboUI_GetForegroundGame");
    }
    return sFn ? (sFn() == 0) : true;
}

// ComboShip (#127): OOT's pause state, read by comboui's dormant-tracker gate (a dormant game's own
// pause state is stale, so the foreground game's is queried across the DLL boundary).
extern "C" __declspec(dllexport) int SOH_IsPausedForCombo(void) {
    return gPlayState != nullptr && gPlayState->pauseCtx.state > 0;
}

// ComboShip: Ctrl+R reset. Set when a reset should return the whole session to first-boot; read by
// SOH_ResumeGame so OOT boots to the title sequence instead of resuming the dormant save.
static bool sComboResetPending = false;

// ComboShip (#89): MM-initiated equivalent of the reset flag — an owl save quits to OOT's title
// rather than MM's own file select, which combo has no path to.
extern "C" __declspec(dllexport) void SOH_SetComboBootToTitle(void) {
    sComboResetPending = true;
}

// Handle a reset request. If MM is foreground, bounce back to OOT (MM saves if autosave is on, then
// goes dormant) and flag OOT to boot to the title sequence on resume. Returns true if it took over the
// reset; false lets the caller run OOT's normal reset (which already lands on the boot sequence).
bool Combo_HandleReset(void) {
    if (Combo_OotIsForeground())
        return false;
    sComboResetPending = true;
    static void (*sFn)(void) = nullptr;
    static bool sTried = false;
    if (!sTried) {
        sTried = true;
        if (HMODULE h = GetModuleHandleA("2ship.dll"))
            sFn = (void (*)(void))GetProcAddress(h, "MM_RequestComboReturn");
    }
    if (sFn)
        sFn();
    return true;
}
#endif

// ComboShip: open the combo menu on the Randomizer tab (file-select "Open Randomizer Settings").
// The menu lives in comboui.dll; its visibility is object-state, not the CVar, so route through
// the export rather than setting gOpenWindows.Menu.
extern "C" void SOH_OpenComboRandoSettings(void) {
#ifdef COMBO_BUILD
    if (HMODULE h = GetModuleHandleA("comboui.dll")) {
        if (auto fn = (void (*)(void))GetProcAddress(h, "ComboUI_OpenRandomizerSettings"))
            fn();
    }
#endif
}

extern "C" __declspec(dllexport) int32_t SOH_MenuEvalDisabled(int32_t i, const char** outReason) {
    ComboMenuContext::UseSharedImGuiContext();
    Ship::ResourceManagerScope rmScope(Ship::CrossRMRegistry::Get("oot"));
    auto menu = SohGui::GetSohMenu();
    return menu ? menu->EvalDisabledByIndex(i, outReason) : 0;
}

extern "C" __declspec(dllexport) void SOH_MenuDrawCustom(int32_t i) {
    // Like SOH_DrawSettings: soh.dll's per-module ImGui GImGui isn't current when OOT is backgrounded,
    // so point it at the shared context before any ImGui call.
    ComboMenuContext::UseSharedImGuiContext();
    Ship::ResourceManagerScope rmScope(Ship::CrossRMRegistry::Get("oot"));
    // Phase 0 spike contract: comboui owns the menu slot so the Gui loop never drives this menu's
    // lifecycle. A custom widget reads THEME_COLOR (menuThemeIndex), set in UpdateElement(), so
    // Init()+Update() must run BEFORE invoking, else ColorValues.at() throws. Init() is idempotent.
    if (auto menu = SohGui::GetSohMenu()) {
        menu->Init();
        menu->Update();
        menu->DrawCustomByIndex(i);
    }
}

// Draws widget i via OOT's real MenuDrawItem (UIWidgets) into comboui's current window/cell. Same
// context/RM/Init+Update contract as SOH_MenuDrawCustom. Returns 1 if the CVar changed this frame.
extern "C" __declspec(dllexport) int32_t SOH_MenuDrawWidget(int32_t i, int32_t width) {
    ComboMenuContext::UseSharedImGuiContext();
    Ship::ResourceManagerScope rmScope(Ship::CrossRMRegistry::Get("oot"));
    if (auto menu = SohGui::GetSohMenu()) {
        menu->Init();
        menu->Update();
        return menu->DrawWidgetByIndex(i, width);
    }
    return 0;
}

// ComboShip: MM->OOT return — re-enter OOT's game loop on the SAME shared context/window, swap
// archives back to OOT, reload the OOT save, and spawn Link at the Mido's-House door in Kokiri
// Forest. Counterpart to MM's reuse path in BenPort.cpp.
extern "C" bool WindowIsRunning(void);

extern "C" __declspec(dllexport) void SOH_ResumeGame(void) {
    auto ctx = Ship::Context::GetRawInstance();
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
    // ComboShip: on a reset return, leave gComboReturnFileNum < 0 so TitleSetup boots to the title
    // sequence (first-boot) instead of jumping straight back into Play on the saved slot.
    gComboReturnFileNum = sComboResetPending ? -1 : (s32)gSaveContext.fileNum;
    sComboResetPending = false;
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
    auto ctx = Ship::Context::GetRawInstance();
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

// ComboShip: header-only version check for the folder auto-scan (no full-ROM read/CRC).
extern "C" __declspec(dllexport) int SOH_ClassifyRom(const char* romPath) {
    if (!romPath) {
        return 0;
    }
    Extractor extract;
    return extract.ClassifyRom(romPath) ? 1 : 0;
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

// Dump-time snapshot of the shuffled-slot set (GAP-8): the validator forces glitchless logic after
// the dump, which would shrink a No-Logic seed's 8-slot shops to 7 on every recompute. The
// computation still runs each call so the RNG stream stays identical to a cache miss.
static std::unordered_set<RandomizerCheck> sComboShuffledSlotsCache;
static bool sComboShuffledSlotsCacheValid = false;

static const std::unordered_set<RandomizerCheck>& Combo_GetShuffledShopSlots() {
    auto computed = Combo_ShuffledShopSlots();
    if (!sComboShuffledSlotsCacheValid) {
        sComboShuffledSlotsCache = std::move(computed);
        sComboShuffledSlotsCacheValid = true;
    }
    return sComboShuffledSlotsCache;
}

// Min-set placements (ComboFillConfined's native shopsanity AssumedFill), snapshotted so oracle
// resets replay them exactly; the fill result also flows into the spoiler as fixed[], so the apply
// path re-places them from the placement map instead of this cache.
static std::vector<std::pair<RandomizerCheck, RandomizerGet>> sComboMinShopCache;
static bool sComboMinShopCacheValid = false;

void Combo_SnapshotMinShopItems() {
    auto ctx = OTRGlobals::Instance->gRandoContext;
    sComboMinShopCache.clear();
    for (RandomizerCheck rc : Rando::StaticData::GetShopLocations()) {
        RandomizerGet rg = ctx->GetItemLocation(rc)->GetPlacedRandomizerGet();
        if (!ctx->GetItemLocation(rc)->HasCustomPrice() && rg != RG_NONE)
            sComboMinShopCache.push_back({ rc, rg });
    }
    sComboMinShopCacheValid = true;
}

// Shop items + shop/scrub/merchant prices, exactly as Fill() sets them; called wherever ItemReset()
// cleared them (ComboFillConfined, oracle reset, apply). Idempotent: reseeded per call.
void Combo_SetupOOTShops() {
    auto ctx = OTRGlobals::Instance->gRandoContext;

    // Seed identically to the dump so the shuffled-slot set (and all prices below) match it exactly.
    Combo_SeedShopRng();

    if (ctx->GetOption(RSK_SHOPSANITY).Is(RO_SHOPSANITY_OFF)) {
        PlaceVanillaShopItems();
    } else {
        const auto& shuffled = Combo_GetShuffledShopSlots();
        for (RandomizerCheck rc : Rando::StaticData::GetShopLocations()) {
            Rando::ItemLocation* loc = ctx->GetItemLocation(rc);
            if (shuffled.count(rc)) {
                loc->SetCustomPrice(GetRandomPrice(Rando::StaticData::GetLocation(rc), kComboShopPrices));
            }
            // Non-shuffled slots: min-set items, replayed from the cache below (generation leaves them
            // empty here — ComboFillConfined's AssumedFill places them right after; apply uses the map).
        }
        if (sComboMinShopCacheValid) {
            for (const auto& [rc, rg] : sComboMinShopCache)
                ctx->PlaceItemInLocation(rc, rg, false, false);
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

// Spoiler prices (name -> rupees); reload/validator set them to override the seeded rolls after
// every price-establishing step. Empty on the generation path (rolls stand).
static std::unordered_map<std::string, uint16_t> sComboCheckPriceOverrides;

extern "C" __declspec(dllexport) void SOH_SetCheckPrices(const char* json) {
    sComboCheckPriceOverrides.clear();
    if (!json)
        return;
    try {
        auto j = nlohmann::json::parse(json);
        for (auto it = j.begin(); it != j.end(); ++it)
            sComboCheckPriceOverrides[it.key()] = static_cast<uint16_t>(it.value().get<int>());
    } catch (const std::exception& e) { SPDLOG_WARN("[ComboShip] SOH_SetCheckPrices: bad JSON: {}", e.what()); }
}

static void Combo_ApplyPriceOverrides() {
    if (sComboCheckPriceOverrides.empty())
        return;
    auto ctx = OTRGlobals::Instance->gRandoContext;
    for (const auto& [name, price] : sComboCheckPriceOverrides) {
        auto it = Rando::StaticData::locationNameToEnum.find(name);
        if (it != Rando::StaticData::locationNameToEnum.end())
            ctx->GetItemLocation(it->second)->SetCustomPrice(price);
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
    // String CVar outside GetAllOptions(); without it a replayed spoiler inherits the local machine's
    // exclusions (GAP-7). Restore below is type-aware.
    j[CVAR_RANDOMIZER_SETTING("ExcludedLocations")] = CVarGetString(CVAR_RANDOMIZER_SETTING("ExcludedLocations"), "");
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
        // Snapshot is authoritative: pre-clear so a spoiler without the key (pre-GAP-7, generated
        // with no exclusions applied) doesn't inherit this machine's local exclusions.
        CVarSetString(CVAR_RANDOMIZER_SETTING("ExcludedLocations"), "");
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.value().is_string())
                CVarSetString(it.key().c_str(), it.value().get<std::string>().c_str());
            else
                CVarSetInteger(it.key().c_str(), it.value().get<int>());
        }
    } catch (...) {}
}

// ComboShip: the settings-scoped pool prep that SOH_ApplyRandoPlacements depends on (ItemReset
// iterates allLocations). On generation this runs inside SOH_DumpRandoStaticData; the reload/drop
// path (which skips the fill) must run it explicitly before applying saved placements.
// ComboShip: the player's enabled tricks live in the EnabledTricks CVar (CSV of stable trick NameTags,
// written by the rando menu). Nothing else pushes them into the Context — SetAllToContext reads the
// per-trick Options, whose cvar names are empty, so it leaves every trick off. Apply the CVar to
// ctx->GetTrickOption AFTER each SetAllToContext so the cross-world fill AND the reachability oracle honor
// the player's tricks (identically). Clears all first so the list is authoritative. See UPSTREAM_MERGES.md.
static void Combo_ApplyEnabledTricks() {
    auto ctx = OTRGlobals::Instance->gRandoContext;
    for (int i = 0; i < RT_MAX; i++)
        ctx->GetTrickOption(static_cast<RandomizerTrick>(i)).Set(RO_GENERIC_OFF);
    std::string csv = CVarGetString(CVAR_RANDOMIZER_SETTING("EnabledTricks"), "");
    for (size_t start = 0; start < csv.size();) {
        size_t comma = csv.find(',', start);
        std::string tag = csv.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        start = (comma == std::string::npos) ? csv.size() : comma + 1;
        if (tag.empty())
            continue;
        auto it = Rando::StaticData::trickToEnum.find(tag);
        if (it != Rando::StaticData::trickToEnum.end())
            ctx->GetTrickOption(it->second).Set(RO_GENERIC_ON);
    }
}

// The ExcludedLocations CSV (check IDs) is only parsed on SoH's GUI generate path
// (randomizer.cpp:930); the combo prep paths must parse it too or exclusions never apply headless.
static std::set<RandomizerCheck> Combo_ParseExcludedLocations() {
    std::set<RandomizerCheck> excluded;
    std::stringstream ss(CVarGetString(CVAR_RANDOMIZER_SETTING("ExcludedLocations"), ""));
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        try {
            excluded.insert(static_cast<RandomizerCheck>(std::stoi(tok)));
        } catch (...) {}
    }
    return excluded;
}

extern "C" __declspec(dllexport) void SOH_PrepRandoContext(void) {
    try {
        auto ctx = OTRGlobals::Instance->gRandoContext;
        Rando::Settings::GetInstance()->SetAllToContext();
        Combo_ApplyEnabledTricks();
        ctx->GetLogic()->Reset();
        ctx->FinalizeSettings(Combo_ParseExcludedLocations(), {});
        RegionTable_Init();
        ctx->GenerateLocationPool();
    } catch (const std::exception& e) { SPDLOG_ERROR("[ComboShip] SOH_PrepRandoContext: {}", e.what()); } catch (...) {
    }
}

// ComboShip: forward decl (defined with the oracle exports below) — see SOH_ShuffleEntrancesForCombo.
static void EnsureOracleInit();

// ComboShip: native Fill()'s entrance-shuffle prologue (3drando/fill.cpp), run headlessly — the combo
// generator never runs Fill(), so without this the OOT entrance options do nothing in combo seeds.
// Deterministic per seed, so generation/reload/gentest re-derive the same layout. Call after the
// dump/prep (settings finalized). Returns 1 on success or shuffle-off, 0 when every retry failed.
extern "C" __declspec(dllexport) int SOH_ShuffleEntrancesForCombo(uint64_t seed) {
    try {
        auto ctx = OTRGlobals::Instance->gRandoContext;
        // The CVar-less master toggle, derived from the individual options by FinalizeSettings.
        if (!ctx->GetOption(RSK_SHUFFLE_ENTRANCES)) {
            // Clear a previous seed's layout so it can't leak into this save via SaveManager.
            ctx->GetEntranceShuffler()->UnshuffleAllEntrances();
            CreateWarpSongTexts(); // vanilla destinations; native VanillaFill() does this too
            return 1;
        }
        // Burn the oracle's lazy init NOW — its first Reset would RegionTable_Init the graph back to
        // vanilla mid-fill, making logic validate a world the save doesn't play.
        EnsureOracleInit();
        // Native Fill() retries the whole prologue up to 5x on shuffle failure.
        for (int retry = 0; retry < 5; ++retry) {
            ctx->GetEntranceShuffler()->playthroughEntrances.clear();
            RegionTable_Init(); // vanilla graph baseline (needed on retries/regeneration)
            // GenerateItemPool asserts pool <= EMPTY locations, so drop a prior seed's placements first
            // (native Fill() ItemResets here too). Callers must read forced placements before this.
            ctx->ItemReset();
            GenerateItemPool(); // ValidateEntrances' all-items pass reads itemPool; self-clears
            GenerateStartingInventory();
            // Temp shop items (worst-case shopsanity) for world validation, as Fill() does.
            SohUtils::AppendVector(itemPool, GetMinVanillaShopItems(8));
            Random_Init(seed + retry);
            int ret = ctx->GetEntranceShuffler()->ShuffleAllEntrances();
            std::erase_if(itemPool, [](const auto item) {
                return Rando::StaticData::RetrieveItem(item).GetItemType() == ITEMTYPE_SHOP;
            });
            if (ret == ENTRANCE_SHUFFLE_FAILURE) {
                SPDLOG_WARN("[ComboShip] SOH_ShuffleEntrancesForCombo: shuffle failed (retry {})", retry);
                continue;
            }
            SetAreas();
            ctx->GetEntranceShuffler()->CreateEntranceOverrides();
            // Warp song destination texts, as native Fill() does. SOH_ApplyComboHints also calls this,
            // but only when hints are enabled — without this a hints-off seed shows "No Hint".
            CreateWarpSongTexts();
            return 1;
        }
        SPDLOG_ERROR("[ComboShip] SOH_ShuffleEntrancesForCombo: no valid layout after 5 retries");
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[ComboShip] SOH_ShuffleEntrancesForCombo: {}", e.what());
    } catch (...) { SPDLOG_ERROR("[ComboShip] SOH_ShuffleEntrancesForCombo: unknown exception"); }
    return 0;
}

// ComboShip: resolved entrance overrides as JSON for the consolidated spoiler's "entrances.oot".
// Informational — reload re-derives via SOH_ShuffleEntrancesForCombo.
extern "C" __declspec(dllexport) const char* SOH_DumpEntranceOverrides(void) {
    static std::string buf;
    nlohmann::json out = nlohmann::json::array();
    auto& overrides = OTRGlobals::Instance->gRandoContext->GetEntranceShuffler()->entranceOverrides;
    for (const EntranceOverride& o : overrides) {
        if (o.type == 0 && o.index == 0 && o.override == 0)
            break; // zero terminator (table is zero-filled past the last real override)
        out.push_back({ { "type", o.type },
                        { "index", o.index },
                        { "destination", o.destination },
                        { "override", o.override },
                        { "overrideDestination", o.overrideDestination } });
    }
    buf = out.dump();
    return buf.c_str();
}

// ComboShip: install a recorded entrance layout (the spoiler's "entrances.oot" array) into the live
// region graph. The validator can't re-derive it: ShuffleAllEntrances validates with logic, so a
// different trick set (all-tricks pass) can accept a different layout than generation did.
extern "C" __declspec(dllexport) int SOH_ApplyEntranceOverridesForCombo(const char* json) {
    try {
        auto ctx = OTRGlobals::Instance->gRandoContext;
        EnsureOracleInit(); // same rationale as SOH_ShuffleEntrancesForCombo: a later lazy init would
                            // RegionTable_Init the graph back to vanilla
        nlohmann::json spoiler;
        spoiler["entrances"] = nlohmann::json::parse(json ? json : "[]");
        // ParseJson = UnshuffleAllEntrances + RegionTable_Init + ApplyEntranceOverrides + SetAreas,
        // so an empty array correctly resets to the vanilla graph.
        ctx->GetEntranceShuffler()->ParseJson(spoiler);
        CreateWarpSongTexts();
        return 1;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[ComboShip] SOH_ApplyEntranceOverridesForCombo: {}", e.what());
    } catch (...) { SPDLOG_ERROR("[ComboShip] SOH_ApplyEntranceOverridesForCombo: unknown exception"); }
    return 0;
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
    nlohmann::json pool = nlohmann::json::array();
    nlohmann::json fixed = nlohmann::json::array();
    nlohmann::json items = nlohmann::json::array();
    nlohmann::json prices = nlohmann::json::object();
    // ComboShip: SoH's curated ice-trap disguise set (native possibleIceTrapModels) — combo skips
    // GenerateItemPool's fill, so the apply restores this instead of re-deriving from placed items.
    nlohmann::json iceTrapModels = nlohmann::json::array();
    // ComboShip: OOT accessibility settings the combo fill honors per-game. Defaults = ALL_REACHABLE
    // (safe: unchanged fill behavior) if the prep throws before these are read.
    nlohmann::json accessibility = { { "noLogic", false },
                                     { "allLocationsReachable", true },
                                     { "lockOverworldDoors", false } };

    // ComboShip: mirror MM (BenPort isAdvancement) — hearts are never logic-required under glitchless,
    // so class PoH/HC/treasure-game heart as junk. Shrinks the OOT advancement pool (fewer dead-ends).
    auto comboIsAdv = [](RandomizerGet rg) {
        if (rg == RG_PIECE_OF_HEART || rg == RG_HEART_CONTAINER || rg == RG_TREASURE_GAME_HEART)
            return false;
        return Rando::StaticData::RetrieveItem(rg).IsAdvancement();
    };

    // ComboShip: native category, so the cross fill can trim ONLY junk — `advancement` alone can't say
    // that (it lumps junk with hearts/traps). Unknown maps to "major" so it is never trimmable.
    auto comboCategory = [](RandomizerGet rg) -> const char* {
        switch (Rando::StaticData::RetrieveItem(rg).GetCategory()) {
            case ITEM_CATEGORY_JUNK:
                return "junk";
            case ITEM_CATEGORY_LESSER:
                return "lesser";
            case ITEM_CATEGORY_HEALTH:
                return "health";
            case ITEM_CATEGORY_BOSS_KEY:
                return "bossKey";
            case ITEM_CATEGORY_SMALL_KEY:
                return "smallKey";
            case ITEM_CATEGORY_SKULLTULA_TOKEN:
                return "token";
            case ITEM_CATEGORY_MAJOR:
                return "major";
        }
        return "major";
    };

    bool usedPool = false;
    try {
        auto ctx = OTRGlobals::Instance->gRandoContext;

        // Headless prep (settings -> context, reset, region tables, settings-scoped pool). Shared
        // with the reload/drop path via SOH_PrepRandoContext so allLocations holds only the checks
        // the current settings shuffle, honoring the menu choices.
        SOH_PrepRandoContext();

#ifdef COMBO_BUILD
        // Seed the rando RNG from the combo seed BEFORE confined placement so dungeon rewards/own-dungeon
        // items/songs are reproducible per seed (mirrors the MM side); Combo_SeedShopRng() below re-seeds
        // so the shop-slot set stays dump/apply-consistent. Confine own-dungeon/reward/song items via
        // OOT's own logic; the residual itemPool is the free cross-world pool.
        // Generation entry: drop any reload-set price overrides or a prior seed's prices would
        // silently win over this seed's rolls in every oracle reset and in the final apply.
        sComboCheckPriceOverrides.clear();
        sComboShuffledSlotsCacheValid = false; // new seed/settings -> fresh slot-set snapshot
        sComboMinShopCacheValid = false;       // fresh min-set placements (re-filled inside ComboFillConfined)
        Combo_SeedShopRng();
        ComboFillConfined();

        // Barren parity: native CalculateBarren counts a region non-barren if it holds a major item OR
        // (under the shop shield/tunic gate) a shield/tunic. Fold the gate into the exported `major`.
        const bool shieldTunicGate = ctx->GetOption(RSK_SHOP_SHIELDS_AND_TUNICS_ONLY_REFILL).Is(RO_GENERIC_ON);
        auto isMajor = [&](RandomizerGet rg) {
            const auto& it = Rando::StaticData::RetrieveItem(rg);
            return it.IsMajorItem() || (shieldTunicGate && it.IsShieldOrTunic());
        };

        // Partition allLocations: pre-placed (confined) -> fixed, empty -> fillable check.
        for (RandomizerCheck rc : ctx->allLocations) {
            Rando::Location* loc = Rando::StaticData::GetLocation(rc);
            if (!loc)
                continue;
            const std::string& name = loc->GetName();
            if (name.empty())
                continue;
            // Link's Pocket is owned by the forced-placement mechanism (SOH_GetForcedPlacements); the
            // dump must not emit it as a check or a fixed placement or it gets placed twice. RC_WINCON is
            // a logic-only win-condition marker (no item), not a real check.
            if (rc == RC_LINKS_POCKET || rc == RC_WINCON)
                continue;

            // Shop min-set Buy items are placed by ComboFillConfined, so they flow through the generic
            // pre-placed branch below: fixed[] for the oracle AND spoiler placements for the apply.
            RandomizerGet placed = ctx->GetItemLocation(rc)->GetPlacedRandomizerGet();
            if (placed != RG_NONE) {
                const std::string& in = Rando::StaticData::RetrieveItem(placed).GetName().GetEnglish();
                // `hintable` = native SetAsHintable state, captured here because oracle ItemResets wipe it.
                // Confined fills (own-dungeon, rewards, min-set shops, excluded junk) leave it false.
                if (!in.empty())
                    fixed.push_back({ { "check", name },
                                      { "item", in },
                                      { "advancement", comboIsAdv(placed) },
                                      { "major", isMajor(placed) },
                                      { "hintable", ctx->GetItemLocation(rc)->IsHintable() } });
            } else {
                // A real fillable check. GenerateLocationPool already decided what's shuffled and the
                // meta markers (Link's Pocket, wincon) are skipped above, so emit regardless of vanilla
                // item — red ice / icicles / fountain fairies have NO vanilla item (the item exists only
                // when shuffled) and were being wrongly dropped, leaving them unfilled ("No Item").
                checks.push_back({ { "name", name } });
            }
        }

        // Pool = the real free item pool (every settings-added item, confined items already removed).
        // ComboShip: `major` (IsMajorItem) feeds the native barren predicate (barren = no WotH + no major).
        for (RandomizerGet rg : itemPool) {
            const std::string& in = Rando::StaticData::RetrieveItem(rg).GetName().GetEnglish();
            if (in.empty())
                continue;
            pool.push_back({ { "name", in },
                             { "advancement", comboIsAdv(rg) },
                             { "major", isMajor(rg) },
                             { "category", comboCategory(rg) } });
        }
        // itemPool excludes shop slots (CountEmptyLocations(false)); shuffled shop checks are covered
        // by junk, exactly like native FastFill's GetJunkItem() padding — Buy items stay shop-only.
        while (pool.size() < checks.size()) {
            RandomizerGet jg = GetJunkItem();
            pool.push_back({ { "name", Rando::StaticData::RetrieveItem(jg).GetName().GetEnglish() },
                             { "advancement", comboIsAdv(jg) },
                             { "major", isMajor(jg) },
                             { "category", comboCategory(jg) } });
        }
        // Rolled prices (set by ComboFillConfined at Fill()'s native position) for every priced
        // check type — the consolidated spoiler carries these so the validator/reload never guess.
        for (RandomizerCheck rc : ctx->allLocations) {
            Rando::Location* loc = Rando::StaticData::GetLocation(rc);
            if (!loc || loc->GetName().empty())
                continue;
            auto t = loc->GetRCType();
            if (t == RCTYPE_SHOP || t == RCTYPE_SCRUB || t == RCTYPE_MERCHANT)
                prices[loc->GetName()] = ctx->GetItemLocation(rc)->GetPrice();
        }

        // ComboShip: ComboFillConfined ran GenerateItemPool, so possibleIceTrapModels now holds the
        // native curated disguise set — export it (minus any entries GetTrapName can't name, which
        // would assert) so the apply gets native parity.
        for (RandomizerGet rg : ctx->possibleIceTrapModels) {
            const std::string& mn = Rando::StaticData::RetrieveItem(rg).GetName().GetEnglish();
            if (!mn.empty() && Rando::Traps::CanBeTrapModel(rg))
                iceTrapModels.push_back(mn);
        }

        // ComboShip: accessibility settings the combo fill maps to an OotAccess mode (per-game relax).
        accessibility["noLogic"] = ctx->GetOption(RSK_LOGIC_RULES).Is(RO_LOGIC_NO_LOGIC);
        accessibility["allLocationsReachable"] = static_cast<bool>(ctx->GetOption(RSK_ALL_LOCATIONS_REACHABLE));
        accessibility["lockOverworldDoors"] = static_cast<bool>(ctx->GetOption(RSK_LOCK_OVERWORLD_DOORS));

        usedPool = true;
#else
        // Non-combo (unused outside ComboShip): old vanilla-per-check emission.
        for (RandomizerCheck rc : ctx->allLocations) {
            Rando::Location* loc = Rando::StaticData::GetLocation(rc);
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
            checks.push_back(
                { { "name", name }, { "vanillaItem", vigName }, { "advancement", comboIsAdv(vanillaRG) } });
        }
        usedPool = true;
#endif
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
            checks.push_back(
                { { "name", name }, { "vanillaItem", vigName }, { "advancement", comboIsAdv(vanillaRG) } });
        }
    }

    // Items list: full item table metadata (display name + advancement for foreign items).
    for (int rg = 0; rg < RG_MAX; ++rg) {
        Rando::Item& item = Rando::StaticData::RetrieveItem(static_cast<RandomizerGet>(rg));
        const std::string& name = item.GetName().GetEnglish();
        if (name.empty())
            continue;
        // ComboShip: OOT item names are already human English; displayName == name keeps the
        // dump schema symmetric with MM's (which needs the distinction: RI_* vs human).
        // advancement drives whether a foreign item plays the held-up pickup animation.
        // ComboShip: "trap" lets the cross-world layer disguise a foreign trap in the other game.
        // ComboShip: "trickNames" are OOT's curated fake names, so a foreign trap disguised as this
        // item can lie with a real near-miss name instead of a letter-doubled one.
        items.push_back({ { "name", name },
                          { "displayName", name },
                          { "advancement", comboIsAdv(static_cast<RandomizerGet>(rg)) },
                          { "trap", rg == RG_ICE_TRAP },
                          { "trickNames", Rando::Traps::GetTrickNamesEnglish(static_cast<uint16_t>(rg)) } });
    }

    cached = nlohmann::json{
        { "checks", std::move(checks) },
        { "pool", std::move(pool) },
        { "fixed", std::move(fixed) },
        { "items", std::move(items) },
        { "prices", std::move(prices) },
        { "iceTrapModels", std::move(iceTrapModels) },
        { "accessibility", std::move(accessibility) }
    }.dump();
    return cached.c_str();
}

// ComboShip: serialize a HintText's clear/ambiguous/obscure CustomMessage variants to JSON, with
// "[[N]]" markers intact so the combo hint composer can splice its own text in. MF_ENCODE (not
// MF_RAW) so the native colors vector gets baked into %g/%w escapes before it's lost to JSON export;
// otherwise the reconstructed CustomMessage on the combo side has no colors and displays as plain text.
static nlohmann::json Combo_CustomMessageToJson(const CustomMessage& msg) {
    return { { "en", msg.GetEnglish(MF_ENCODE) },
             { "de", msg.GetGerman(MF_ENCODE) },
             { "fr", msg.GetFrench(MF_ENCODE) } };
}
static nlohmann::json Combo_HintTextToJson(const HintText& ht) {
    nlohmann::json ambiguous = nlohmann::json::array();
    for (size_t i = 0; i < ht.GetAmbiguousSize(); ++i)
        ambiguous.push_back(Combo_CustomMessageToJson(ht.GetAmbiguous(i)));
    nlohmann::json obscure = nlohmann::json::array();
    for (size_t i = 0; i < ht.GetObscureSize(); ++i)
        obscure.push_back(Combo_CustomMessageToJson(ht.GetObscure(i)));
    return { { "clear", Combo_CustomMessageToJson(ht.GetClear()) },
             { "ambiguous", std::move(ambiguous) },
             { "obscure", std::move(obscure) } };
}

// ComboShip: hintTextTable keys the combo hint composer (CrossHints.h) can actually emit — every
// other RHT_* key is a native-only template (native reads its own in-process table directly, so
// trimming this JSON export never affects native hint behavior). Keeping the dump to this allowlist
// (+ RHT_JUNK* below) is most of the Debug dump-size win: the full table is ~1646 entries.
static bool Combo_IsUsedHintTemplate(const std::string& name) {
    static const std::unordered_set<std::string> kAllow = {
        "RHT_WAY_OF_THE_HERO",
        "RHT_FOOLISH",
        "RHT_CAN_BE_FOUND_AT",
        "RHT_HOARDS",
        "RHT_GANONDORF_HINT_LA_ONLY",
        "RHT_GANONDORF_HINT_MS_ONLY",
        "RHT_GANONDORF_HINT_LA_AND_MS",
        "RHT_YOUR_POCKET",
        // Altar templates + option-driven end clauses (Fix 3: combo composes altar hints itself).
        "RHT_CHILD_ALTAR_STONES",
        "RHT_CHILD_ALTAR_TEXT_END_DOTOPEN",
        "RHT_CHILD_ALTAR_TEXT_END_DOTSONGONLY",
        "RHT_CHILD_ALTAR_TEXT_END_DOTCLOSED",
        "RHT_ADULT_ALTAR_MEDALLIONS",
        "RHT_ADULT_ALTAR_TEXT_END",
        "RHT_BRIDGE_OPEN_HINT",
        "RHT_BRIDGE_VANILLA_HINT",
        "RHT_BRIDGE_STONES_HINT",
        "RHT_BRIDGE_MEDALLIONS_HINT",
        "RHT_BRIDGE_REWARDS_HINT",
        "RHT_BRIDGE_DUNGEONS_HINT",
        "RHT_BRIDGE_TOKENS_HINT",
        "RHT_BRIDGE_TRIFORCE_PIECES_HINT",
        "RHT_BRIDGE_GREG_HINT",
        "RHT_GANON_BK_START_WITH_HINT",
        "RHT_GANON_BK_VANILLA_HINT",
        "RHT_GANON_BK_OWN_DUNGEON_HINT",
        "RHT_GANON_BK_ANY_DUNGEON_HINT",
        "RHT_GANON_BK_OVERWORLD_HINT",
        "RHT_GANON_BK_ANYWHERE_HINT",
        "RHT_GBK_STONES_HINT",
        "RHT_GBK_MEDALLIONS_HINT",
        "RHT_GBK_REWARDS_HINT",
        "RHT_GBK_DUNGEONS_HINT",
        "RHT_GBK_TOKENS_HINT",
        "RHT_GBK_TRIFORCE_PIECES_HINT",
        "RHT_GANONS_SOUL_STONES_HINT",
        "RHT_GANONS_SOUL_MEDALLIONS_HINT",
        "RHT_GANONS_SOUL_REWARDS_HINT",
        "RHT_GANONS_SOUL_DUNGEONS_HINT",
        "RHT_GANONS_SOUL_TOKENS_HINT",
        "RHT_GANONS_SOUL_TRIFORCE_PIECES_HINT",
        "RHT_WINCON_ANYWHERE_HINT",
        "RHT_WINCON_STONES_HINT",
        "RHT_WINCON_MEDALLIONS_HINT",
        "RHT_WINCON_REWARDS_HINT",
        "RHT_WINCON_DUNGEONS_HINT",
        "RHT_WINCON_TOKENS_HINT",
        "RHT_WINCON_TRIFORCE_PIECES_HINT",
    };
    return kAllow.count(name) != 0 || name.rfind("RHT_JUNK", 0) == 0;
}

// ComboShip: hint schema/data export for the combo hint layer (cross-game hint system, Phase 2).
// Pure reader of already-resolved Context state (options, StaticData tables, resolved trials) — does
// NOT call SOH_PrepRandoContext (that re-rolls RNG-derived state like trial selection). Must be
// called once per successful fill, immediately after that attempt's SOH_DumpRandoStaticData, so the
// Context is still the winning attempt's state. Returns a JSON object; never throws across the ABI.
// Checks[]/items[] dump every static check/item regardless of this seed's placements (the combo
// distributor decides which are hintable for its combined world) — trimming that further to a
// placed-set filter hit a reproducible crash during headless verification and was backed out; only
// hintTextTable (below) is trimmed for now. See docs/UPSTREAM_MERGES.md cross-hints entry.
extern "C" __declspec(dllexport) const char* SOH_DumpRandoHintData(void) {
    static std::string cached;
    nlohmann::json out = nlohmann::json::object();
    try {
        auto ctx = OTRGlobals::Instance->gRandoContext;

        // Resolved hint-affecting options (honest player settings; the reload path's force-off
        // happens later, in SOH_ApplyRandoPlacements, so this read is unaffected by it).
        // Altar end-clause option-composition (Fix 3): resolve the exact RHT_* template key + count
        // combo should splice in, mirroring hint.cpp's GetBridgeReqsText/GetGanonBossKeyText/
        // GetGanonsSoulText/GetWinconText option->template selection exactly (same Is() checks) —
        // avoids the combo side having to guess RSK_* enum ordinals.
        auto bridge = [&]() -> std::pair<const char*, int> {
            auto& o = ctx->GetOption(RSK_RAINBOW_BRIDGE);
            if (o.Is(RO_BRIDGE_ALWAYS_OPEN))
                return { "RHT_BRIDGE_OPEN_HINT", 0 };
            if (o.Is(RO_BRIDGE_VANILLA))
                return { "RHT_BRIDGE_VANILLA_HINT", 0 };
            if (o.Is(RO_BRIDGE_STONES))
                return { "RHT_BRIDGE_STONES_HINT", ctx->GetOption(RSK_RAINBOW_BRIDGE_STONE_COUNT).Get() };
            if (o.Is(RO_BRIDGE_MEDALLIONS))
                return { "RHT_BRIDGE_MEDALLIONS_HINT", ctx->GetOption(RSK_RAINBOW_BRIDGE_MEDALLION_COUNT).Get() };
            if (o.Is(RO_BRIDGE_DUNGEON_REWARDS))
                return { "RHT_BRIDGE_REWARDS_HINT", ctx->GetOption(RSK_RAINBOW_BRIDGE_REWARD_COUNT).Get() };
            if (o.Is(RO_BRIDGE_DUNGEONS))
                return { "RHT_BRIDGE_DUNGEONS_HINT", ctx->GetOption(RSK_RAINBOW_BRIDGE_DUNGEON_COUNT).Get() };
            if (o.Is(RO_BRIDGE_TOKENS))
                return { "RHT_BRIDGE_TOKENS_HINT", ctx->GetOption(RSK_RAINBOW_BRIDGE_TOKEN_COUNT).Get() };
            if (o.Is(RO_BRIDGE_TRIFORCE_PIECES))
                return { "RHT_BRIDGE_TRIFORCE_PIECES_HINT", ctx->GetOption(RSK_RAINBOW_BRIDGE_TRIFORCE_COUNT).Get() };
            if (o.Is(RO_BRIDGE_GREG))
                return { "RHT_BRIDGE_GREG_HINT", 0 };
            return { "", 0 };
        }();
        auto gbk = [&]() -> std::pair<const char*, int> {
            auto& o = ctx->GetOption(RSK_GANONS_BOSS_KEY);
            if (o.Is(RO_GANON_BOSS_KEY_STARTWITH))
                return { "RHT_GANON_BK_START_WITH_HINT", 0 };
            if (o.Is(RO_GANON_BOSS_KEY_VANILLA))
                return { "RHT_GANON_BK_VANILLA_HINT", 0 };
            if (o.Is(RO_GANON_BOSS_KEY_OWN_DUNGEON))
                return { "RHT_GANON_BK_OWN_DUNGEON_HINT", 0 };
            if (o.Is(RO_GANON_BOSS_KEY_ANY_DUNGEON))
                return { "RHT_GANON_BK_ANY_DUNGEON_HINT", 0 };
            if (o.Is(RO_GANON_BOSS_KEY_OVERWORLD))
                return { "RHT_GANON_BK_OVERWORLD_HINT", 0 };
            if (o.Is(RO_GANON_BOSS_KEY_ANYWHERE))
                return { "RHT_GANON_BK_ANYWHERE_HINT", 0 };
            if (o.Is(RO_GANON_BOSS_KEY_STONES))
                return { "RHT_GBK_STONES_HINT", ctx->GetOption(RSK_GBK_STONE_COUNT).Get() };
            if (o.Is(RO_GANON_BOSS_KEY_MEDALLIONS))
                return { "RHT_GBK_MEDALLIONS_HINT", ctx->GetOption(RSK_GBK_MEDALLION_COUNT).Get() };
            if (o.Is(RO_GANON_BOSS_KEY_REWARDS))
                return { "RHT_GBK_REWARDS_HINT", ctx->GetOption(RSK_GBK_REWARD_COUNT).Get() };
            if (o.Is(RO_GANON_BOSS_KEY_DUNGEONS))
                return { "RHT_GBK_DUNGEONS_HINT", ctx->GetOption(RSK_GBK_DUNGEON_COUNT).Get() };
            if (o.Is(RO_GANON_BOSS_KEY_TOKENS))
                return { "RHT_GBK_TOKENS_HINT", ctx->GetOption(RSK_GBK_TOKEN_COUNT).Get() };
            if (o.Is(RO_GANON_BOSS_KEY_TRIFORCE_PIECES))
                return { "RHT_GBK_TRIFORCE_PIECES_HINT", ctx->GetOption(RSK_GBK_TRIFORCE_COUNT).Get() };
            return { "", 0 };
        }();
        auto soul = [&]() -> std::pair<const char*, int> {
            auto& o = ctx->GetOption(RSK_GANONS_SOUL);
            if (o.Is(RO_GANONS_SOUL_STONES))
                return { "RHT_GANONS_SOUL_STONES_HINT", ctx->GetOption(RSK_GANONS_SOUL_STONE_COUNT).Get() };
            if (o.Is(RO_GANONS_SOUL_MEDALLIONS))
                return { "RHT_GANONS_SOUL_MEDALLIONS_HINT", ctx->GetOption(RSK_GANONS_SOUL_MEDALLION_COUNT).Get() };
            if (o.Is(RO_GANONS_SOUL_REWARDS))
                return { "RHT_GANONS_SOUL_REWARDS_HINT", ctx->GetOption(RSK_GANONS_SOUL_REWARD_COUNT).Get() };
            if (o.Is(RO_GANONS_SOUL_DUNGEONS))
                return { "RHT_GANONS_SOUL_DUNGEONS_HINT", ctx->GetOption(RSK_GANONS_SOUL_DUNGEON_COUNT).Get() };
            if (o.Is(RO_GANONS_SOUL_TOKENS))
                return { "RHT_GANONS_SOUL_TOKENS_HINT", ctx->GetOption(RSK_GANONS_SOUL_TOKEN_COUNT).Get() };
            if (o.Is(RO_GANONS_SOUL_TRIFORCE_PIECES))
                return { "RHT_GANONS_SOUL_TRIFORCE_PIECES_HINT", ctx->GetOption(RSK_GANONS_SOUL_TRIFORCE_COUNT).Get() };
            return { "", 0 }; // RO_GANONS_SOUL_NONE: native emits nothing for this clause either
        }();
        auto wincon = [&]() -> std::pair<const char*, int> {
            auto& o = ctx->GetOption(RSK_WINCON);
            if (o.Is(RO_WINCON_ANYWHERE))
                return { "RHT_WINCON_ANYWHERE_HINT", 0 };
            if (o.Is(RO_WINCON_STONES))
                return { "RHT_WINCON_STONES_HINT", ctx->GetOption(RSK_WINCON_STONE_COUNT).Get() };
            if (o.Is(RO_WINCON_MEDALLIONS))
                return { "RHT_WINCON_MEDALLIONS_HINT", ctx->GetOption(RSK_WINCON_MEDALLION_COUNT).Get() };
            if (o.Is(RO_WINCON_REWARDS))
                return { "RHT_WINCON_REWARDS_HINT", ctx->GetOption(RSK_WINCON_REWARD_COUNT).Get() };
            if (o.Is(RO_WINCON_DUNGEONS))
                return { "RHT_WINCON_DUNGEONS_HINT", ctx->GetOption(RSK_WINCON_DUNGEON_COUNT).Get() };
            if (o.Is(RO_WINCON_TOKENS))
                return { "RHT_WINCON_TOKENS_HINT", ctx->GetOption(RSK_WINCON_TOKEN_COUNT).Get() };
            if (o.Is(RO_WINCON_TRIFORCE_PIECES))
                return { "RHT_WINCON_TRIFORCE_PIECES_HINT", ctx->GetOption(RSK_WINCON_TRIFORCE_COUNT).Get() };
            return { "", 0 };
        }();
        const char* doorOfTimeKey =
            ctx->GetOption(RSK_DOOR_OF_TIME).Is(RO_DOOROFTIME_OPEN)       ? "RHT_CHILD_ALTAR_TEXT_END_DOTOPEN"
            : ctx->GetOption(RSK_DOOR_OF_TIME).Is(RO_DOOROFTIME_SONGONLY) ? "RHT_CHILD_ALTAR_TEXT_END_DOTSONGONLY"
                                                                          : "RHT_CHILD_ALTAR_TEXT_END_DOTCLOSED";

        out["options"] = {
            { "gossipStoneHints", static_cast<int>(ctx->GetOption(RSK_GOSSIP_STONE_HINTS).Get()) },
            { "hintClarity", static_cast<int>(ctx->GetOption(RSK_HINT_CLARITY).Get()) },
            { "hintDistribution", static_cast<int>(ctx->GetOption(RSK_HINT_DISTRIBUTION).Get()) },
            { "ganondorfHint", static_cast<int>(ctx->GetOption(RSK_GANONDORF_HINT).Get()) },
            // Ganondorf's hint text has three variants, chosen by these two (see CreateGanondorfHint).
            { "shuffleMasterSword", static_cast<int>(ctx->GetOption(RSK_SHUFFLE_MASTER_SWORD).Get()) },
            { "startingMasterSword", static_cast<int>(ctx->GetOption(RSK_STARTING_MASTER_SWORD).Get()) },
            { "warpSongHints", static_cast<int>(ctx->GetOption(RSK_WARP_SONG_HINTS).Get()) },
            { "totAltarHint", static_cast<int>(ctx->GetOption(RSK_TOT_ALTAR_HINT).Get()) },
            { "doorOfTimeTemplate", doorOfTimeKey },
            { "bridgeTemplate", bridge.first },
            { "bridgeCount", bridge.second },
            { "gbkTemplate", gbk.first },
            { "gbkCount", gbk.second },
            { "soulTemplate", soul.first },
            { "soulCount", soul.second },
            { "winconTemplate", wincon.first },
            { "winconCount", wincon.second },
        };

        // Area list: static per-check area (RCAREA_*) — Location::GetArea(), not the runtime
        // ItemLocation::GetRandomArea() the native hint distributor groups by (that needs a live
        // reachability traversal). Phase 3's combined-world foolish/area logic will need its own
        // area assignment either way, since it must span both games; documented as a Phase 2->3 seam.
        nlohmann::json areas = nlohmann::json::array();
        for (int a = 0; a < RCAREA_INVALID; ++a) {
            auto area = static_cast<RandomizerCheckArea>(a);
            areas.push_back({ { "key", static_cast<int>(area) },
                              { "name", RandomizerCheckObjects::GetRCAreaName(area) },
                              { "dungeon", RandomizerCheckObjects::AreaIsDungeon(area) } });
        }
        out["areas"] = std::move(areas);

        // Gossip stone slots (~40 checks) — the stones the combo distributor may target.
        nlohmann::json stones = nlohmann::json::array();
        for (RandomizerCheck rc : Rando::StaticData::GetGossipStoneLocations()) {
            Rando::Location* loc = Rando::StaticData::GetLocation(rc);
            if (loc && !loc->GetName().empty())
                stones.push_back(loc->GetName());
        }
        out["stones"] = std::move(stones);

        // Always-hint candidates (settings-applied: RC_MARKET_10_BIG_POES, Biggoron's Claim Check, etc).
        nlohmann::json always = nlohmann::json::array();
#ifdef COMBO_BUILD
        for (RandomizerCheck rc : GetAlwaysHintCandidates()) {
            Rando::Location* loc = Rando::StaticData::GetLocation(rc);
            if (loc && !loc->GetName().empty())
                always.push_back(loc->GetName());
        }
#endif
        out["alwaysHintChecks"] = std::move(always);

        // Checks the player already knows at start — native SetHintAccesible's two cases in
        // CreateStoneHints (hints.cpp), mirrored 1:1 so stones never target them.
        nlohmann::json startKnown = nlohmann::json::array();
        auto pushStartKnown = [&](RandomizerCheck rc) {
            Rando::Location* loc = Rando::StaticData::GetLocation(rc);
            if (loc && !loc->GetName().empty())
                startKnown.push_back(loc->GetName());
        };
        if (ctx->GetOption(RSK_STARTING_ZELDAS_LETTER) && !ctx->GetOption(RSK_SHUFFLE_ZELDAS_LETTER))
            pushStartKnown(RC_SONG_FROM_IMPA);
        if (ctx->GetOption(RSK_SELECTED_STARTING_AGE).Is(RO_AGE_ADULT) || !ctx->GetOption(RSK_SHUFFLE_MASTER_SWORD))
            pushStartKnown(RC_TOT_MASTER_SWORD);
        out["hintAccessibleChecks"] = std::move(startKnown);

        // Per-check hint text (every static check, not just this settings' shuffled subset — the
        // combo distributor decides which checks are hintable for its combined world).
        nlohmann::json checks = nlohmann::json::array();
        for (int i = 0; i < RC_MAX; ++i) {
            Rando::Location* loc = Rando::StaticData::GetLocation(static_cast<RandomizerCheck>(i));
            if (!loc || loc->GetName().empty())
                continue;
            checks.push_back({ { "name", loc->GetName() },
                               { "area", RandomizerCheckObjects::GetRCAreaName(loc->GetArea()) },
                               { "dungeon", loc->IsDungeon() },
                               { "overworld", loc->IsOverworld() },
                               { "song", loc->GetRCType() == RCTYPE_SONG_LOCATION },
                               { "locationHint", Combo_HintTextToJson(*loc->GetHint()) } });
        }
        out["checks"] = std::move(checks);

        // Per-item hint text (English item name as key, matching the items[] dump elsewhere).
        nlohmann::json items = nlohmann::json::array();
        for (int rg = 0; rg < RG_MAX; ++rg) {
            Rando::Item& item = Rando::StaticData::RetrieveItem(static_cast<RandomizerGet>(rg));
            const std::string& name = item.GetName().GetEnglish();
            if (name.empty())
                continue;
            items.push_back({ { "name", name }, { "hint", Combo_HintTextToJson(item.GetHint()) } });
        }
        out["items"] = std::move(items);

        // Hint-text-fragment table, keyed by RHT_* enum name. Trimmed to Combo_IsUsedHintTemplate's
        // allowlist (the templates CrossHints.h's distributor + altar/end-clause composition can
        // actually emit) instead of every RHT_MAX (~1646) entry — a missing key degrades to an empty
        // {clear:{}} entry on the combo side (PickTemplate's fallbacks), never a crash.
        nlohmann::json templates = nlohmann::json::object();
        for (int k = 0; k < RHT_MAX; ++k) {
            auto key = static_cast<RandomizerHintTextKey>(k);
            auto name = EnumToString(key);
            if (!name.has_value() || !Combo_IsUsedHintTemplate(std::string(*name)))
                continue;
            templates[std::string(*name)] = Combo_HintTextToJson(Rando::StaticData::hintTextTable[key]);
        }
        out["hintTextTable"] = std::move(templates);

        // Resolved Ganon's Trials (FinalizeSettings already rolled RSK_GANONS_TRIALS' random-number
        // case via Random() during SOH_PrepRandoContext, before this function runs) — the combo hint
        // gen must hint the SAME trials the save actually requires.
        nlohmann::json trials = nlohmann::json::array();
        for (auto* t : ctx->GetTrials()->GetTrialList()) {
            if (t->IsRequired())
                trials.push_back(t->GetName().GetEnglish(MF_RAW));
        }
        out["requiredTrials"] = std::move(trials);
        // dump() with a replace error handler so malformed UTF-8 in any authored text can never throw
        // across the DLL boundary (nlohmann::json::type_error.316).
        cached = out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        return cached.c_str();
    } catch (const std::exception& e) { SPDLOG_WARN("[ComboShip] SOH_DumpRandoHintData: {}", e.what()); } catch (...) {
        SPDLOG_WARN("[ComboShip] SOH_DumpRandoHintData: unknown exception");
    }

    cached = nlohmann::json::object().dump();
    return cached.c_str();
}

#ifdef COMBO_BUILD
// ComboShip: whether the seed being applied carries a cross-hint payload (see SOH_ApplyComboHints).
// Set by the launcher just before SOH_ApplyRandoPlacements; back-compat default is "no" so an old
// dropped seed file (generated before Phase 3) still gets the force-off vanilla-hint behavior.
static bool sComboHintsPresent = false;

// ComboShip (#164): RandomizerHint -> the combo checkName it was applied from, so a reveal can be
// reported to the combo Hint Tracker (whose read state is keyed by combo key, not by RandomizerHint).
static std::unordered_map<int, std::string> sComboHintKeys;
static uint64_t sComboHintKeysGen = (uint64_t)-1; // OOT_ForeignMapGen() the map was built for

// ComboShip: one resolution walk over a combo hints payload, shared by apply and by the lazy map
// replay. `isTaken` reports an already-claimed hint slot; `emit` receives each resolution in claiming
// order. Both callers MUST walk this identically or the sentinel -> stone mapping drifts.
static void
Combo_WalkComboHints(const nlohmann::json& hints, const std::function<bool(RandomizerHint)>& isTaken,
                     const std::function<void(RandomizerHint, const std::string&, std::vector<CustomMessage>&)>& emit,
                     int& applied, int& skipped) {
    const std::vector<RandomizerCheck> stones = Rando::StaticData::GetGossipStoneLocations();
    for (auto& entry : hints.value("oot", nlohmann::json::array())) {
        std::string checkName = entry.value("checkName", "");
        std::vector<CustomMessage> messages;
        for (auto& m : entry.value("messages", nlohmann::json::array()))
            messages.emplace_back(m.value("en", ""), m.value("de", ""), m.value("fr", ""));
        if (messages.empty()) {
            ++skipped;
            continue;
        }

        RandomizerHint rh = RH_NONE;
        if (checkName == "__GANONDORF__") {
            rh = RH_GANONDORF_HINT;
        } else if (checkName == "__ALTAR_CHILD__") {
            rh = RH_ALTAR_CHILD;
        } else if (checkName == "__ALTAR_ADULT__") {
            rh = RH_ALTAR_ADULT;
        } else if (checkName.rfind("__", 0) == 0) {
            // "__STONE__N"/"__TRIAL__.../"__JUNK__...": CrossHints.h assigns these to an abstract
            // stone SLOT (count only, not a specific check — combo doesn't pick which physical
            // stone gets which content). Claim the next still-empty gossip-stone check for it.
            for (RandomizerCheck rc : stones) {
                RandomizerHint candidate = Rando::StaticData::gossipStoneCheckToHint.count(rc)
                                               ? Rando::StaticData::gossipStoneCheckToHint[rc]
                                               : RH_NONE;
                if (candidate != RH_NONE && !isTaken(candidate)) {
                    rh = candidate;
                    break;
                }
            }
            if (rh == RH_NONE) {
                ++skipped;
                continue;
            }
        } else {
            auto rcIt = Rando::StaticData::locationNameToEnum.find(checkName);
            if (rcIt == Rando::StaticData::locationNameToEnum.end() ||
                !Rando::StaticData::gossipStoneCheckToHint.count(rcIt->second)) {
                ++skipped;
                continue;
            }
            rh = Rando::StaticData::gossipStoneCheckToHint[rcIt->second];
        }
        if (rh == RH_NONE || isTaken(rh)) {
            ++skipped;
            continue;
        }
        emit(rh, checkName, messages);
        ++applied;
    }
}

// ComboShip (#164): the launcher's combo Hint Tracker reveal sink.
extern "C" void (*gComboHintReveal)(int fileNum, const char* comboKey) = nullptr;

// Rebuild sComboHintKeys from the pushed seed blob when it doesn't match the live one. Covers every
// load path where SOH_ApplyComboHints didn't run in this process; the shared walk keeps it drift-free.
// Builds into a local and commits on success only — a half-built map must never latch as current-gen.
static void Combo_EnsureHintKeyMap() {
    const uint64_t gen = OOT_ForeignMapGen();
    if (sComboHintKeysGen == gen) {
        return;
    }
    std::unordered_map<int, std::string> built;
    try {
        nlohmann::json hints =
            nlohmann::json::parse(ComboRando::g_comboForeignJson).value("hints", nlohmann::json::object());
        int applied = 0, skipped = 0;
        Combo_WalkComboHints(
            hints, [&](RandomizerHint rh) { return built.count(rh) != 0; },
            [&](RandomizerHint rh, const std::string& key, std::vector<CustomMessage>&) { built[rh] = key; }, applied,
            skipped);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[ComboShip] Combo_EnsureHintKeyMap: exception: {} (will retry next reveal)", e.what());
        return;
    } catch (...) {
        SPDLOG_ERROR("[ComboShip] Combo_EnsureHintKeyMap: unknown exception (will retry next reveal)");
        return;
    }
    sComboHintKeys = std::move(built);
    sComboHintKeysGen = gen;
}

// ComboShip (#164): OnRandoHintRevealed subscriber (registered from SOH_LoadComboRando). The hook can
// fire for disabled hints and for native/warp-song hints combo never wrote — both are dropped here.
// Fires from C textbox code, so nothing may escape: the whole body is guarded.
void OOT_ComboHintRevealed(RandomizerHint hintKey) try {
    if (!gComboHintReveal || hintKey == RH_NONE) {
        return;
    }
    auto ctx = OTRGlobals::Instance->gRandoContext;
    if (!ctx || !ctx->GetHint(hintKey)->IsEnabled()) {
        return;
    }
    Combo_EnsureHintKeyMap();
    auto it = sComboHintKeys.find(hintKey);
    if (it == sComboHintKeys.end()) {
        return;
    }
    gComboHintReveal(gSaveContext.fileNum, it->second.c_str());
} catch (const std::exception& e) {
    SPDLOG_ERROR("[ComboShip] OOT_ComboHintRevealed: exception: {}", e.what());
} catch (...) { SPDLOG_ERROR("[ComboShip] OOT_ComboHintRevealed: unknown exception"); }
#endif

extern "C" __declspec(dllexport) void SOH_SetComboHintsPresent(int present) {
#ifdef COMBO_BUILD
    sComboHintsPresent = present != 0;
#endif
}

extern "C" __declspec(dllexport) void SOH_SetComboHintRevealCb(void (*cb)(int, const char*)) {
#ifdef COMBO_BUILD
    gComboHintReveal = cb;
#else
    (void)cb;
#endif
}

// ComboShip: apply combo-generated hints (cross-hint Phase 3). Input: {"oot":[{checkName,messages:
// [{en,de,fr},...]},...], ...} (see combo/rando/CrossHints.h for the generator). checkName is either a
// gossip-stone check name (resolved via locationNameToEnum + gossipStoneCheckToHint) or the sentinel
// "__GANONDORF__"/"__TRIAL__.../"__JUNK__..." handled below. Never throws across the ABI. Must run
// AFTER SOH_ApplyRandoPlacements (placements need to exist for native CreateStaticHints/
// CreateWarpSongTexts, called at the end, to fill in whatever combo didn't pre-populate).
extern "C" __declspec(dllexport) void SOH_ApplyComboHints(const char* json) {
    if (!json)
        return;
#ifdef COMBO_BUILD
    try {
        auto ctx = OTRGlobals::Instance->gRandoContext;
        nlohmann::json hints = nlohmann::json::parse(json);
        int applied = 0, skipped = 0;
        std::unordered_map<int, std::string> built;
        Combo_WalkComboHints(
            hints, [&](RandomizerHint rh) { return ctx->GetHint(rh)->IsEnabled(); },
            [&](RandomizerHint rh, const std::string& checkName, std::vector<CustomMessage>& messages) {
                ctx->AddHint(rh, Rando::Hint(rh, messages));
                built[rh] = checkName;
            },
            applied, skipped);
        // Committed only past the walk — a throw leaves the previous gen stamped so the lazy replay retries.
        sComboHintKeys = std::move(built);
        sComboHintKeysGen = OOT_ForeignMapGen();

        // Native fills whatever combo didn't pre-populate (altar/Biggoron/mask-shop/skulltula-count/
        // ganondorf-joke/etc static hints; each self-skips an already-enabled key) + warp song texts.
        CreateStaticHints();
        CreateWarpSongTexts();
        SPDLOG_INFO("[ComboShip] SOH_ApplyComboHints: applied={} skipped={}", applied, skipped);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[ComboShip] SOH_ApplyComboHints: exception: {}", e.what());
    } catch (...) { SPDLOG_ERROR("[ComboShip] SOH_ApplyComboHints: unknown exception"); }
#endif
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
#ifdef COMBO_BUILD
        // Combo skips GenerateItemPool (OOT's own fill), which is what normally fills the ice-trap
        // disguise pool. The payload carries the dump's curated set ("__iceTrapModels"); old seeds
        // without it fall back to deriving one from the items we place below.
        ctx->possibleIceTrapModels.clear();
        // Combo also skips native Fill()'s HintReset() call — without it, a same-session regenerate
        // would see the PREVIOUS seed's hints still marked enabled and skip re-populating them.
        ctx->HintReset();
#endif

        // ComboShip: ItemReset wipes shop prices + placements, so re-run SoH's shop/scrub/merchant
        // setup here (shuffled slots get a custom price). Non-shuffled slots' min-set Buy items ride
        // in the placement map (the dump emits them as fixed[], which the fill echoes into the
        // spoiler), so the loop below places them like any other check.
#ifdef COMBO_BUILD
        sComboShuffledSlotsCacheValid = false; // reload may carry different settings than the last dump
        sComboMinShopCacheValid = false;       // min-set comes from the placement map here, not the cache
        // Combo seeds carry no NPC hints (CreateAllHints never runs; the combo sphere-hint panel is
        // the hint system). Force the hint settings off so stones/Ganondorf/warp texts behave vanilla
        // instead of reading the empty hint table; the save inherits these from the context (GAP-3).
        // Cross-hint Phase 3: only force hint options off for a seed with no combo hints payload
        // (old pre-hint-system seed files, back-compat). New seeds honor the player's own settings —
        // SOH_ApplyComboHints (called right after this) supplies the actual hint content.
        if (!sComboHintsPresent) {
            ctx->GetOption(RSK_GOSSIP_STONE_HINTS).Set(RO_GOSSIP_STONES_NONE);
            ctx->GetOption(RSK_GANONDORF_HINT).Set(RO_GENERIC_OFF);
            ctx->GetOption(RSK_WARP_SONG_HINTS).Set(RO_GENERIC_OFF);
        }
        Combo_SetupOOTShops();
        Combo_ApplyPriceOverrides(); // spoiler prices win over the re-roll on reload
#endif

        nlohmann::json placements = nlohmann::json::parse(json);
#ifdef COMBO_BUILD
        // ComboShip: reserved key carrying the dump's curated ice-trap disguise set (native parity —
        // e.g. Triforce pieces are never a disguise). Erased so the placement loop never sees it.
        if (auto modelsIt = placements.find("__iceTrapModels"); modelsIt != placements.end()) {
            if (modelsIt->is_array()) {
                for (const auto& mv : *modelsIt) {
                    if (!mv.is_string())
                        continue;
                    const std::string mn = mv.get<std::string>();
                    auto mIt = Rando::StaticData::itemNameToEnum.find(mn);
                    if (mIt == Rando::StaticData::itemNameToEnum.end() || !Rando::Traps::CanBeTrapModel(mIt->second)) {
                        SPDLOG_WARN("[ComboShip] SOH_ApplyRandoPlacements: invalid ice-trap model '{}'", mn);
                        continue;
                    }
                    ctx->possibleIceTrapModels.insert(mIt->second);
                }
            }
            placements.erase("__iceTrapModels");
        }
        // Empty result (empty/garbled list, e.g. a failed dump prep) falls back to deriving a pool below.
        const bool haveCuratedModels = !ctx->possibleIceTrapModels.empty();
#endif
        int placed = 0, skipped = 0;
        // ComboShip: cross-game name collisions are suffixed "(OOT)" in the consolidated spoiler; strip
        // our own suffix before resolving native OOT location/item names.
        auto stripOOT = [](std::string s) {
            static const std::string suf = " (OOT)";
            if (s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0)
                s.resize(s.size() - suf.size());
            return s;
        };
        for (auto& [rawName, itemVal] : placements.items()) {
            if (!itemVal.is_string()) {
                ++skipped;
                continue;
            }
            const std::string name = stripOOT(rawName);
            const std::string itemName = stripOOT(itemVal.get<std::string>());

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
#ifdef COMBO_BUILD
            // Old seeds only (no curated set in the payload): derive a disguise pool from placed items.
            // Triforce is excluded here — native never disguises a trap as one (issue #131).
            if (!haveCuratedModels && rg != RG_ICE_TRAP && rg != RG_COMBO_FOREIGN && rg != RG_NONE &&
                rg != RG_TRIFORCE && rg != RG_TRIFORCE_PIECE && Rando::Traps::CanBeTrapModel(rg)) {
                ctx->possibleIceTrapModels.insert(rg); // candidate ice-trap disguise (only named items)
            }
#endif
        }

#ifdef COMBO_BUILD
        // Assign ice-trap disguise models now (combo's fill never runs CreateItemOverrides). Skip if the
        // pool came out empty so traps keep their default model rather than a null override.
        if (!ctx->possibleIceTrapModels.empty()) {
            ctx->CreateItemOverrides();
        }
        // Combo skips native Fill(), so ItemLocation areas are never assigned; the hint creators below
        // (and SOH_ApplyComboHints' CreateStaticHints) read GetRandomArea/GetFirstArea and assert on an
        // empty set. SetAreas() populates them from the region graph (RA_NONE for disconnected regions).
        SetAreas();
        // Combo never runs CreateStaticHints(), so the ToT altar hint table stays empty ("No Hint").
        // These two are option-composed and self-skip when RSK_TOT_ALTAR_HINT is off; run them here
        // (placements are all applied by now) instead of pulling in the rest of CreateStaticHints().
        // Skipped when a cross-hint payload is coming: SOH_ApplyComboHints composes the altar hint
        // itself (every reward resolved across BOTH games, native's FindItemsAndMarkHinted only sees
        // OOT's own locations) and runs CreateStaticHints() at the end, which self-skips an
        // already-enabled key — calling these here first would let native's version win instead.
        if (!sComboHintsPresent) {
            CreateChildAltarHint();
            CreateAdultAltarHint();
        }
        // ComboShip: combo bypasses Playthrough_Init, so the ctx seed stays 0 and every seed-derived
        // feature (cosmetics/audio randomize-on-gen, save finalSeed, Anchor mismatch) degenerates.
        if (sComboRandoSeedSet) {
            ctx->SetSeed((uint32_t)sComboRandoSeed);
        }
#endif

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

// Current generation phase (ComboGenProgress: 0 Idle, 1 Preparing, 2 Placing, 3 Finalizing), so the
// C file-select can label the post-fill work instead of showing "Generating..." forever.
extern "C" __declspec(dllexport) int SOH_GetComboGenPhase(void) {
    return gComboProgressPtr ? gComboProgressPtr->phase.load() : 0;
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

// ComboShip: path of the most recently generated/loaded combo spoiler, so a restart can reload it
// without regenerating. Mirrors how SoH remembers its own spoiler in CVAR_GENERAL("SpoilerLog").
extern "C" __declspec(dllexport) void SOH_SetComboSpoilerPath(const char* path) {
    CVarSetString(CVAR_GENERAL("ComboSpoiler"), path ? path : "");
    Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
}
extern "C" __declspec(dllexport) const char* SOH_GetComboSpoilerPath(void) {
    return CVarGetString(CVAR_GENERAL("ComboSpoiler"), "");
}

// ComboShip: the active save slot (combo seed key), or -1 if none. Used by the comboui hint system to
// find the loaded seed's per-slot consolidated file.
extern "C" __declspec(dllexport) int SOH_GetActiveFileNum(void) {
    return (gSaveContext.fileNum == 0xFF) ? -1 : (int)gSaveContext.fileNum;
}

#ifdef COMBO_BUILD
// ComboShip (#173): combo owns the timer overlay; these feed it OOT's half. Play time is the in-game
// (non-RTA) value on purpose — the RTA branch is wall clock and would double-count time spent in MM.
extern "C" __declspec(dllexport) uint64_t SOH_GetPlaytimeDeciseconds(void) {
    return (uint64_t)(gSaveContext.ship.stats.playTimer / 2 + gSaveContext.ship.stats.pauseTimer / 3);
}

// Live OOT-only overlay rows, classified here so comboui hardcodes no vanilla enum values.
// Returns 0 with the outputs untouched when there is no PlayState.
// naviPhase 0=prepare 1=active 2=cooldown, naviTicks counts down at 20/s.
// timerKind 0=off 1=hot 2=cold 3=countdown 4=running (no icon) — mirrors TimeDisplayGetTimer.
extern "C" __declspec(dllexport) int SOH_GetOverlayTimers(uint32_t* dayTime, int32_t* isDay, int32_t* naviPhase,
                                                          uint32_t* naviTicks, int32_t* timerKind,
                                                          int32_t* timerSeconds) {
    if (gPlayState == NULL) {
        return 0;
    }
    if (dayTime != NULL) {
        *dayTime = (uint32_t)gSaveContext.dayTime;
    }
    if (isDay != NULL) {
        *isDay = (gSaveContext.dayTime >= DAY_BEGINS && gSaveContext.dayTime < NIGHT_BEGINS) ? 1 : 0;
    }
    if (naviPhase != NULL && naviTicks != NULL) {
        uint32_t t = gSaveContext.naviTimer;
        if (t <= NAVI_PREPARE) {
            *naviPhase = 0;
            *naviTicks = NAVI_PREPARE - t;
        } else if (t <= NAVI_ACTIVE) {
            *naviPhase = 1;
            *naviTicks = NAVI_ACTIVE - t;
        } else {
            *naviPhase = 2;
            *naviTicks = (t <= NAVI_COOLDOWN) ? NAVI_COOLDOWN - t : 0;
        }
    }
    if (timerKind != NULL && timerSeconds != NULL) {
        *timerSeconds = (int32_t)gSaveContext.timerSeconds;
        if (gSaveContext.timerState <= TIMER_STATE_OFF) {
            *timerKind = 0;
        } else if (gSaveContext.timerState <= TIMER_STATE_ENV_HAZARD_TICK) {
            *timerKind = (gPlayState->roomCtx.curRoom.behaviorType2 == ROOM_BEHAVIOR_TYPE2_3) ? 1 : 2;
        } else if (gSaveContext.timerState >= TIMER_STATE_DOWN_PREVIEW) {
            *timerKind = 3;
        } else {
            *timerKind = 4;
        }
    }
    return 1;
}
#endif

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

// ComboShip (#89): leave OOT's game loop before its first Play frame so the launcher can enter MM.
// Clearing init is what ends RunFrame's `while (nextOvl)` (FileChoose queued Play_Init); no save and no
// OnExitGame here — both would break the resume. See docs/deviations/boot-shutdown.md.
extern "C" __declspec(dllexport) void SOH_ParkForComboMMResume(void) {
    if (!gGameState)
        return;
    gGameState->init = nullptr;
    gGameState->running = false;
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
    Combo_ApplyEnabledTricks();                        // ComboShip: honor the player's tricks (see helper)
    ctx->GetLogic()->Reset();
    ctx->FinalizeSettings(Combo_ParseExcludedLocations(), {});
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
#ifdef COMBO_BUILD
    // ItemReset wiped shop/scrub/merchant prices; re-establish them (seeded, so identical every
    // reset) or the wallet gates in GetCheckPrice() evaluate against 0 and every shop is "free".
    Combo_SetupOOTShops();
    Combo_ApplyPriceOverrides();
#endif
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

#ifdef COMBO_BUILD
// ComboShip: OOT->MM portal (Happy Mask Shop) region access, stashed by the search below.
static bool sComboPortalOpen = false;
#endif

extern "C" __declspec(dllexport) const char* Combo_SOH_Rando_GetReachableChecks(void) {
    static std::string buf;
    auto ctx = OTRGlobals::Instance->gRandoContext;
    auto reachable = ReachabilitySearch(ctx->allLocations);
#ifdef COMBO_BUILD
    // RR_MARKET_MASK_SHOP holds no real checks, so the cross-fill gates MM on REGION access. Any age:
    // the requirement lives in the entrance condition, so this survives entrance shuffle moving it.
    Region* portal = RegionTable(RR_MARKET_MASK_SHOP);
    sComboPortalOpen = portal->Child() || portal->Adult();
#endif
    nlohmann::json out = nlohmann::json::array();
    for (RandomizerCheck rc : reachable) {
        const std::string& name = Rando::StaticData::GetLocation(rc)->GetName();
        if (!name.empty())
            out.push_back(name);
    }
    buf = out.dump();
    return buf.c_str();
}

#ifdef COMBO_BUILD
// ComboShip: portal openness for the owned-set of the LAST GetReachableChecks call — callers must query
// it right after that call. Piggybacks on that search; a second traversal would double oracle gen cost.
extern "C" __declspec(dllexport) uint8_t Combo_SOH_Rando_GetPortalOpen(void) {
    return sComboPortalOpen ? 1 : 0;
}
#endif

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

// ComboShip: enabled-trick capture/control for the headless playthrough validator. Tricks are identified
// by their stable NameTag; all three route through the EnabledTricks CVar, which Combo_ApplyEnabledTricks
// pushes into the Context at every SetAllToContext. Dump = the player's list; SetEnabledTricks = replace
// with a list (playthrough Pass 1); SetAllTricks = every trick (Pass 2). Callers run SOH_PrepRandoContext
// (or an oracle Reset) afterward to apply.
extern "C" __declspec(dllexport) const char* SOH_DumpEnabledTricks(void) {
    static std::string buf;
    nlohmann::json out = nlohmann::json::array();
    std::string csv = CVarGetString(CVAR_RANDOMIZER_SETTING("EnabledTricks"), "");
    for (size_t start = 0; start < csv.size();) {
        size_t comma = csv.find(',', start);
        std::string tag = csv.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        start = (comma == std::string::npos) ? csv.size() : comma + 1;
        if (!tag.empty())
            out.push_back(tag);
    }
    buf = out.dump();
    return buf.c_str();
}

extern "C" __declspec(dllexport) void SOH_SetEnabledTricks(const char* namesJson) {
    if (!namesJson)
        return;
    try {
        std::string csv;
        for (const auto& n : nlohmann::json::parse(namesJson))
            csv += n.get<std::string>() + ",";
        CVarSetString(CVAR_RANDOMIZER_SETTING("EnabledTricks"), csv.c_str());
    } catch (...) {}
}

extern "C" __declspec(dllexport) void SOH_SetAllTricks(void) {
    std::string csv;
    for (int i = 0; i < RT_MAX; i++) {
        const std::string& tag =
            Rando::Settings::GetInstance()->GetTrickSetting(static_cast<RandomizerTrick>(i)).GetNameTag();
        if (!tag.empty())
            csv += tag + ",";
    }
    CVarSetString(CVAR_RANDOMIZER_SETTING("EnabledTricks"), csv.c_str());
}

// ComboShip: Link's Pocket is a rando-only check absent from the cross-world dump, so the combined
// fill never assigns it. Decide its item per RSK_LINKS_POCKET here so the launcher can reserve it
// from the cross pool. Returns { "Link's Pocket": {"item":"<name>"} } (dungeon-reward) or
// {"category":"advancement"|"any"} (combo picks); {} for NOTHING. See docs/UPSTREAM_MERGES.md.
extern "C" __declspec(dllexport) const char* SOH_GetForcedPlacements(uint32_t seed) {
    (void)seed; // dungeon-reward pick now read from the placed context, not re-rolled from the seed
    static std::string buf;
    nlohmann::json out = nlohmann::json::object();
    try {
        auto ctx = OTRGlobals::Instance->gRandoContext;
        // No SetAllToContext here: the dump this follows already applied the CVars and finalized, and
        // re-pushing reverts everything FinalizeSettings derived (resolved starting age, Ganon's Trials).
        auto lp = ctx->GetOption(RSK_LINKS_POCKET);
        if (lp.Is(RO_LINKS_POCKET_DUNGEON_REWARD)) {
            // ComboShip: RandomizeDungeonRewards (inside SOH_DumpRandoStaticData->ComboFillConfined, run
            // just before this) already picked+placed Link's Pocket's reward and erased it from the pool.
            // Return THAT exact item; re-rolling a separate LCG orphaned one reward and duplicated another.
            RandomizerGet placed = ctx->GetItemLocation(RC_LINKS_POCKET)->GetPlacedRandomizerGet();
            if (placed != RG_NONE)
                out["Link's Pocket"]["item"] = Rando::StaticData::RetrieveItem(placed).GetName().GetEnglish();
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
