#include "SaveManager.h"

#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "BenJsonConversions.hpp"
#include "BenPort.h"
#include "2s2h/BenGui/Notification.h"
#include <ship/window/Window.h>
#include <libultraship/bridge/consolevariablebridge.h>

extern "C" {
#include "z64save.h"
#include "macros.h"
#include "functions.h" // Flags_SetWeekEventReg (used by SET_WEEKEVENTREG)
#include "variables.h" // gItemSlots (used by INV_CONTENT)
#include "src/overlays/gamestates/ovl_file_choose/z_file_select.h"
extern FileSelectState* gFileSelectState;
extern SaveContext gSaveContext;
}

// This entire thing is temporary until we have a more robust save system that
// supports backwards compatibility, migrations, threaded saving, save sections, etc.

#define FLASH_SAVE_UNAVAILABLE ((FlashSave)-1)

#undef GET_NEWF

#define GET_NEWF(save, index) (save.saveInfo.playerData.newf[index])

#define IS_VALID_FILE(save)                                                                    \
    ((GET_NEWF(save, 0) == 'Z') && (GET_NEWF(save, 1) == 'E') && (GET_NEWF(save, 2) == 'L') && \
     (GET_NEWF(save, 3) == 'D') && (GET_NEWF(save, 4) == 'A') && (GET_NEWF(save, 5) == '3'))

const std::filesystem::path savesFolderPath(Ship::Context::GetPathRelativeToAppDirectory("saves", appShortName));

#ifdef COMBO_BUILD
#include "rando/CrossForeign.h" // ComboShip: merged-save IO callback typedefs + GameId

// ComboShip: launcher-provided .combosav IO. When set, a main per-slot file{N}.json read/write routes
// into the container's "mm" section; unset -> the vendored disk IO below. Both primitives funnel here.
static ComboRando::FnComboReadSave gComboReadGameSave = nullptr;
static ComboRando::FnComboWriteSave gComboWriteGameSave = nullptr;
void SaveManager_SetComboSaveIO(ComboRando::FnComboReadSave r, ComboRando::FnComboWriteSave w) {
    gComboReadGameSave = r;
    gComboWriteGameSave = w;
}

// ComboShip: classify a save filename. Returns the 1-based slot N for a "file{N}.json" (main or
// backup), 0 otherwise; isBackup/isGlobal flag the non-main forms that stay on disk.
static int SaveManager_ClassifySaveFile(const std::filesystem::path& fileName, bool& isBackup, bool& isGlobal) {
    isBackup = isGlobal = false;
    std::string name = fileName.filename().string();
    if (name == "global.json") {
        isGlobal = true;
        return 0;
    }
    if (name.rfind("file", 0) != 0)
        return 0;
    size_t p = 4;
    int n = 0;
    while (p < name.size() && name[p] >= '0' && name[p] <= '9')
        n = n * 10 + (name[p++] - '0');
    if (p == 4)
        return 0; // no digits
    std::string rest = name.substr(p);
    if (rest == "backup.json") {
        isBackup = true;
        return n;
    }
    return rest == ".json" ? n : 0;
}
#endif

// Migrations
// The idea here is that we can read in any version of the save as generic JSON, then apply migrations
// to the JSON to ensure it's in the correct shape for the current to_json/from_json helpers to convert
// it to the current struct that the game uses.
//
// To add a new migration:
// - Increment CURRENT_SAVE_VERSION
// - Create the migration file in the Migrations folder with the name `{CURRENT_SAVE_VERSION}.cpp`
// - Add the migration function definition below and add it to the `migrations` map with the key being the previous
// version
const uint32_t CURRENT_SAVE_VERSION = 7;

void SaveManager_Migration_1(nlohmann::json& j);
void SaveManager_Migration_2(nlohmann::json& j);
void SaveManager_Migration_3(nlohmann::json& j);
void SaveManager_Migration_4(nlohmann::json& j);
void SaveManager_Migration_5(nlohmann::json& j);
void SaveManager_Migration_6(nlohmann::json& j);
void SaveManager_Migration_7(nlohmann::json& j);

const std::unordered_map<uint32_t, std::function<void(nlohmann::json&)>> migrations = {
    // Pre-1.0.0 Migrations, deprecated
    { 0, SaveManager_Migration_1 },
    { 1, SaveManager_Migration_2 },
    { 2, SaveManager_Migration_3 },
    { 3, SaveManager_Migration_4 },
    // Base Migration
    { 4, SaveManager_Migration_5 },
    { 5, SaveManager_Migration_6 },
    { 6, SaveManager_Migration_7 },
};

int SaveManager_MigrateSave(nlohmann::json& j) {
    try {
        int version = j.value("version", 0);

        if (version > (int)CURRENT_SAVE_VERSION) {
            SPDLOG_ERROR("Save version is greater than current version");
            return -1;
        }

        if (version >= 4 && !j.contains("newCycleSave") && !j.contains("owlSave")) {
            SPDLOG_ERROR("Save file is missing newCycleSave and owlSave");
            return -1;
        }

        while (version < (int)CURRENT_SAVE_VERSION) {
            if (migrations.contains(version)) {
                auto migration = migrations.at(version);
                if (version < 4) {
                    migration(j); // Pre-1.0.0 Migrations, deprecated
                } else {
                    // In the case of copying files, the owl save is copied first, so the new cycle may not exist yet.
                    if (j.contains("newCycleSave")) {
                        migration(j["newCycleSave"]);
                    }
                    // Only migrate the owl save if it exists
                    if (j.contains("owlSave")) {
                        migration(j["owlSave"]);
                    }
                }
            }
            version = j["version"] = version + 1;
        }
        return 0;
    } catch (std::exception& e) {
        SPDLOG_ERROR("Failed to migrate save file: {}", e.what());
        return -1;
    } catch (...) {
        SPDLOG_ERROR("Failed to migrate save file");
        return -1;
    }
}

void SaveManager_WriteSaveFile(const std::filesystem::path& fileName, nlohmann::json j) {
#ifdef COMBO_BUILD
    // ComboShip: route the main per-slot file into the .combosav container; drop the redundant backup.
    if (gComboWriteGameSave) {
        bool isBackup, isGlobal;
        int n = SaveManager_ClassifySaveFile(fileName, isBackup, isGlobal);
        if (isBackup)
            return; // container's atomic write makes MM's per-slot backup redundant
        if (n > 0 && !isGlobal) {
            gComboWriteGameSave(ComboRando::GAME_MM, n - 1, j.dump().c_str());
            return;
        }
        // global.json falls through to disk (per-game globals stay separate).
    }
#endif
    const std::filesystem::path filePath = savesFolderPath / fileName;

    // create_directories (plural) makes any missing parent dirs too; create_directory (single) fails
    // silently if a parent is missing, after which the ofstream open below silently no-ops.
    std::error_code ec;
    std::filesystem::create_directories(savesFolderPath, ec);
    if (ec) {
        SPDLOG_ERROR("[ComboShip] Could not create saves folder {}: {}",
                     std::filesystem::absolute(savesFolderPath).string(), ec.message());
    }

    try {
        std::ofstream o(filePath);
        if (!o.is_open()) {
            // std::ofstream does NOT throw on open failure — check explicitly or the failure is silent.
            SPDLOG_ERROR("[ComboShip] Could not open save file for writing: {}",
                         std::filesystem::absolute(filePath).string());
            return;
        }
        o << std::setw(4) << j << std::endl;
        o.close();
        SPDLOG_INFO("[ComboShip] Wrote MM save file: {}", std::filesystem::absolute(filePath).string());
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[ComboShip] Failed to write save file {}: {}", filePath.string(), e.what());
    } catch (...) { SPDLOG_ERROR("[ComboShip] Failed to write save file {}", filePath.string()); }
}

int SaveManager_ReadSaveFile(const std::filesystem::path& fileName, nlohmann::json& j);

#ifdef COMBO_BUILD
// ComboShip (#182): the slot whose owlSave blob populated gSaveContext, so the writers below know
// whether the blob is still current (refresh it) or has been overtaken by live state (drop it).
extern "C" int gComboOwlBlobSlot = -1;
#endif

void SaveManager_InitNewSaveForSlot(int mmFileNum, const unsigned char* ootName8) {
    Sram_InitNewSave();
#ifdef COMBO_BUILD
    // ComboShip: carry the OOT-entered file name over (same font codes in both games; anything
    // outside the shared 0x00-0x3F range, e.g. JP glyphs, becomes a space).
    if (ootName8 != nullptr) {
        for (int i = 0; i < 8; i++) {
            gSaveContext.save.saveInfo.playerData.playerName[i] = (ootName8[i] <= 0x3F) ? ootName8[i] : 0x3E;
        }
    }
    // ComboShip: a fresh MM save is always entered mid-playthrough from OOT, never via MM's title/intro.
    // Set it up post-first-cycle — Human Link in South Clock Town, no intro, no Tatl arrival — mirroring
    // the SkipIntroSequence + SkipFirstCycle enhancements and the Rando port's OnFileCreate. gPlayState
    // doesn't exist yet at save-creation, so items go in via INV_CONTENT instead of Item_Give. Scene
    // flags go in permanentSceneFlags because title_setup.c copies those into cycleSceneFlags on load.
    gSaveContext.save.hasTatl = true;
    gSaveContext.save.isFirstCycle = true;
    gSaveContext.save.playerForm = PLAYER_FORM_HUMAN;
    gSaveContext.save.saveInfo.playerData.isMagicAcquired = true;
    gSaveContext.save.saveInfo.playerData.threeDayResetCount = 1;
    INV_CONTENT(ITEM_OCARINA_OF_TIME) = ITEM_OCARINA_OF_TIME;
    INV_CONTENT(ITEM_MASK_DEKU) = ITEM_MASK_DEKU;
    gSaveContext.save.saveInfo.inventory.questItems |= (1 << QUEST_SONG_TIME) | (1 << QUEST_SONG_HEALING);
    gSaveContext.save.saveInfo.permanentSceneFlags[SCENE_INSIDETOWER].switch0 |= (1 << 0); // Happy Mask Salesman
    SET_WEEKEVENTREG(WEEKEVENTREG_59_04); // Tatl: entered South Clock Town — gates the first-entry conversation
    SET_WEEKEVENTREG(WEEKEVENTREG_31_04); // Tatl
    SET_WEEKEVENTREG(WEEKEVENTREG_ENTERED_EAST_CLOCK_TOWN);
    SET_WEEKEVENTREG(WEEKEVENTREG_ENTERED_WEST_CLOCK_TOWN);
    SET_WEEKEVENTREG(WEEKEVENTREG_ENTERED_NORTH_CLOCK_TOWN);
    // ComboShip: stamp RANDO before the write, not after. Every caller re-stamps it moments later, but
    // this function PERSISTS, so leaving it VANILLA opened a window where the container briefly held a
    // vanilla MM save, which silently disables every IS_RANDO hook. Combo has no vanilla mode.
    gSaveContext.save.shipSaveInfo.saveType = SAVETYPE_RANDO;
#endif
    nlohmann::json j;
    // Fresh json with no owlSave, and Combo_WriteGameSave replaces the whole mm section — that is what
    // drops a stale owl blob on a new file or a rebake. Do not turn this into a read-modify-write.
    j["newCycleSave"]["save"] = gSaveContext.save;
    j["version"] = CURRENT_SAVE_VERSION;
    j["type"] = "2S2H_SAVE";
#ifdef COMBO_BUILD
    gComboOwlBlobSlot = -1;
#endif
    SaveManager_WriteSaveFile(SaveManager_GetFileName(mmFileNum), j);
}

void SaveManager_SaveCurrentForCombo() {
    int mmFileNum = (int)gSaveContext.fileNum + 1;
    std::string fileName = SaveManager_GetFileName(mmFileNum);
    nlohmann::json j;
#ifdef COMBO_BUILD
    // ComboShip (#182): read-modify-write so an owl blob is either kept in step or dropped — never
    // left behind to shadow newer state. A read/migrate failure just starts from an empty document.
    if (SaveManager_ReadSaveFile(fileName, j) != 0 || SaveManager_MigrateSave(j) != 0) {
        j = nlohmann::json{};
    }
#endif
    j["newCycleSave"]["save"] = gSaveContext.save;
#ifdef COMBO_BUILD
    if (j.contains("owlSave")) {
        if (gComboOwlBlobSlot == mmFileNum) {
            // gSaveContext descends from this blob, so refresh the WHOLE SaveContext — same shape the
            // owl writer emits. Refreshing only ["save"] would leave the blob's eventInf and bottle
            // timers frozen while save moved on, a mix no vanilla writer can produce.
            try {
                // at() so a malformed blob throws into the catch below; operator[] would insert
                // nulls and leave a blob that only fails later, at load.
                nlohmann::json keep = j.at("owlSave").at("save");
                j["owlSave"] = gSaveContext;
                // Blob-owned: live RAM clears these right after every owl write.
                j["owlSave"]["save"]["isOwlSave"] = keep.at("isOwlSave");
                j["owlSave"]["save"]["shipSaveInfo"]["pauseSaveEntrance"] =
                    keep.at("shipSaveInfo").at("pauseSaveEntrance");
                j["owlSave"]["save"]["shipSaveInfo"]["respawn"] = keep.at("shipSaveInfo").at("respawn");
            } catch (...) {
                SPDLOG_ERROR("[ComboShip] Owl blob refresh failed; dropping it");
                j.erase("owlSave");
                gComboOwlBlobSlot = -1;
            }
        } else {
            j.erase("owlSave"); // live state is newer than the blob
        }
    }
#endif
    j["version"] = CURRENT_SAVE_VERSION;
    j["type"] = "2S2H_SAVE";
    SaveManager_WriteSaveFile(fileName, j);
}

#ifdef COMBO_BUILD
// ComboShip (#182): vanilla consumes the owl save on continue (func_80147314), but that needs
// sramCtx->saveBuf and gPlayState, neither of which exists at Setup_InitImpl. Clearing the flag makes
// the write below take the erase path, and it promotes the continued state into newCycleSave.
extern "C" void Combo_MMDropOwlSaveBlob(void) try {
    gComboOwlBlobSlot = -1;
    SaveManager_SaveCurrentForCombo();
} catch (...) { // called from C — never unwind past this frame
    SPDLOG_ERROR("[ComboShip] Combo_MMDropOwlSaveBlob threw");
}

// ComboShip (#182): the launcher can replace a slot's mm section behind MM's back (copy/erase/evict),
// which would leave the descent flag pointing at a blob gSaveContext never came from.
extern "C" __declspec(dllexport) void MM_InvalidateOwlBlobSlot(void) {
    gComboOwlBlobSlot = -1;
}
#endif

// ComboShip: nothing usable was loaded, so leave gSaveContext pointing at NO slot. 0xFF is the "no save"
// sentinel every dormant writer tests (Combo_MM_GiveDormantResolved, MM_MarkForeignObtained, MMAnchor's
// PumpDormant), so a stray write lands nowhere instead of persisting the PREVIOUS slot's save — or
// zeroed vanilla BSS — into the failed slot. Clearing saveType makes IS_RANDO false for the same reason:
// the peek trackers must not keep drawing the previous slot's save as if it were this one.
static int SaveManager_LoadFailedForCombo(int code) {
    gSaveContext.fileNum = 0xFF;
    gSaveContext.save.shipSaveInfo.saveType = SAVETYPE_VANILLA;
    return code;
}

// ComboShip: loading never creates or persists. A missing/broken MM half used to be replaced with a
// fresh SAVETYPE_VANILLA save, permanently poisoning the slot; now every failure just returns a code,
// loudly. Nothing repairs the slot — re-create the file. 0 ok, -1 missing, -2 unreadable, -3 migrate,
// -4 no usable save page (neither key, or owlSave unparseable with no newCycleSave), -5 parse.
int SaveManager_LoadSaveFile(int mmFileNum) {
    std::string fileName = SaveManager_GetFileName(mmFileNum);
    nlohmann::json j;
    int result = SaveManager_ReadSaveFile(fileName, j);
#ifdef COMBO_BUILD
    // ComboShip: the read routes through the launcher into the .combosav container, not saves/file{N}.json.
    SPDLOG_INFO("[ComboShip] LoadSaveFile slot {} <- .combosav container (read result={})", mmFileNum, result);
#else
    SPDLOG_INFO("[ComboShip] LoadSaveFile slot {} -> {} (read result={}, savesFolder={})", mmFileNum,
                std::filesystem::absolute(savesFolderPath / fileName).string(), result,
                std::filesystem::absolute(savesFolderPath).string());
#endif
    if (result != 0) {
        SPDLOG_ERROR("[ComboShip] MM save file {} missing or unreadable (read result={})", fileName, result);
        return SaveManager_LoadFailedForCombo(result); // ReadSaveFile only returns 0/-1/-2
    }
#ifdef COMBO_BUILD
    gComboOwlBlobSlot = -1;
    // Claim the slot up front: every failure path below still boots into Play_Init, and leaving
    // fileNum at 0 would make the next save write this slot's state over slot 1.
    gSaveContext.fileNum = (s16)(mmFileNum - 1);
#endif
    result = SaveManager_MigrateSave(j);
    if (result != 0) {
        SPDLOG_ERROR("[ComboShip] Failed to migrate MM save file: {}", fileName);
        return SaveManager_LoadFailedForCombo(-3);
    }
#ifdef COMBO_BUILD
    // ComboShip (#182): combo skips Sram_OpenSave, which is where vanilla picks the owl page over the
    // new-cycle one. Presence of the key is the discriminator — save.isOwlSave is not reliable (every
    // owl writer restores it in RAM afterwards). An owl-only slot is legal, same as SaveManager_MigrateSave.
    if (!j.contains("newCycleSave") && !j.contains("owlSave")) {
        SPDLOG_ERROR("[ComboShip] MM save file has neither newCycleSave nor owlSave: {}", fileName);
        return SaveManager_LoadFailedForCombo(-4);
    }
    if (j.contains("owlSave")) {
        try {
            // Whole SaveContext, unlike newCycleSave's bare Save — it carries the live cycle state
            // (eventInf, bottle timers, pictoPhotoI5). Mirrors SaveManager_SysFlashrom_ReadData's owl branch.
            SaveContext sc = j["owlSave"];
            sc.save.saveInfo.checksum = 0;
            sc.save.saveInfo.checksum = Sram_CalcChecksum(&sc, offsetof(SaveContext, fileNum));
            memcpy(&gSaveContext, &sc, offsetof(SaveContext, fileNum));
            gSaveContext.fileNum = (s16)(mmFileNum - 1);
            gComboOwlBlobSlot = mmFileNum;
            SPDLOG_INFO("[ComboShip] Loaded owl save for slot {}", mmFileNum);
            return 0;
        } catch (nlohmann::json::exception& je) {
            // Fall through to newCycleSave; the next write drops the dead blob.
            SPDLOG_ERROR("[ComboShip] Failed to parse MM owl save: {}", je.what());
        } catch (...) { SPDLOG_ERROR("[ComboShip] Failed to parse MM owl save"); }
    }
    if (!j.contains("newCycleSave")) {
        SPDLOG_ERROR("[ComboShip] MM save file has no usable newCycleSave: {}", fileName);
        return SaveManager_LoadFailedForCombo(-4);
    }
#else
    if (!j.contains("newCycleSave")) {
        SPDLOG_ERROR("[ComboShip] MM save file missing newCycleSave: {}", fileName);
        return SaveManager_LoadFailedForCombo(-4);
    }
#endif
    try {
        Save save = j["newCycleSave"]["save"];
        save.saveInfo.checksum = 0;
        save.saveInfo.checksum = Sram_CalcChecksum(&save, sizeof(Save));
        gSaveContext.save = save;
        gSaveContext.fileNum = (s16)(mmFileNum - 1);
    } catch (nlohmann::json::exception& je) {
        SPDLOG_ERROR("[ComboShip] Failed to parse MM save: {}", je.what());
        return SaveManager_LoadFailedForCombo(-5);
    } catch (...) {
        SPDLOG_ERROR("[ComboShip] Failed to parse MM save");
        return SaveManager_LoadFailedForCombo(-5);
    }
    return 0;
}

void SaveManager_DeleteSaveFile(const std::filesystem::path& fileName) {
#ifdef COMBO_BUILD
    // ComboShip: clear the container's mm section (write null) instead of removing a nonexistent
    // saves/file{N}.json. Reached e.g. when the flashrom path invalidates a new-cycle save.
    if (gComboWriteGameSave) {
        bool isBackup, isGlobal;
        int n = SaveManager_ClassifySaveFile(fileName, isBackup, isGlobal);
        if (n > 0 && !isBackup && !isGlobal) {
            gComboWriteGameSave(ComboRando::GAME_MM, n - 1, "null");
            return;
        }
        if (isBackup)
            return; // backups are never written
    }
#endif
    const std::filesystem::path filePath = savesFolderPath / fileName;

    try {
        if (std::filesystem::exists(filePath)) {
            std::filesystem::remove(filePath);
        }
    } catch (...) { SPDLOG_ERROR("Failed to delete save file"); }
}

int SaveManager_ReadSaveFile(const std::filesystem::path& fileName, nlohmann::json& j) {
#ifdef COMBO_BUILD
    // ComboShip: read the main per-slot section from the .combosav container; "" -> same code as the
    // file-missing path below, which makes LoadSaveFile refuse the slot (it never creates one).
    if (gComboReadGameSave) {
        bool isBackup, isGlobal;
        int n = SaveManager_ClassifySaveFile(fileName, isBackup, isGlobal);
        if (n > 0 && !isBackup && !isGlobal) {
            const char* section = gComboReadGameSave(ComboRando::GAME_MM, n - 1);
            if (!section || section[0] == '\0')
                return -1;
            try {
                j = nlohmann::json::parse(section);
                return 0;
            } catch (...) {
                SPDLOG_ERROR("[ComboShip] Failed to parse MM container section for slot {}", n);
                return -2;
            }
        }
    }
#endif
    const std::filesystem::path filePath = savesFolderPath / fileName;

    if (!std::filesystem::exists(filePath)) {
        return -1;
    }

    try {
        std::ifstream i(filePath);
        i >> j;
        i.close();
        return 0;
    } catch (...) {
        SPDLOG_ERROR("Failed to read save file");
        return -2;
    }
}

// Special "auto save" to prevent save scumming Saria's Song hint. This can be more generic if we
// find another use case for this functionality.
void SaveManager_PersistSariaHintsAvailable() {
    std::string fileName = SaveManager_GetFileName(gSaveContext.fileNum + 1);
    nlohmann::json j;

    if (SaveManager_ReadSaveFile(fileName, j) != 0) {
        return;
    }

    try {
        u8 hintsAvailable = gSaveContext.save.shipSaveInfo.rando.sariaHintsAvailable;

        if (j.contains("newCycleSave")) {
            j["newCycleSave"]["save"]["shipSaveInfo"]["rando"]["sariaHintsAvailable"] = hintsAvailable;
        }

        if (j.contains("owlSave")) {
            j["owlSave"]["save"]["shipSaveInfo"]["rando"]["sariaHintsAvailable"] = hintsAvailable;
        }
    } catch (...) {
        SPDLOG_ERROR("Failed to patch sariaHintsAvailable into save file");
        return;
    }

    SaveManager_WriteSaveFile(fileName, j);
}

void SaveManager_MoveInvalidSaveFile(const std::filesystem::path& fileName, const std::string& message) {
    const std::filesystem::path filePath = savesFolderPath / fileName;
    const std::filesystem::path backupFilePath =
        savesFolderPath / (fileName.stem().string() + "_invalid_" + std::to_string(std::time(nullptr)) + ".json");

    try {
        if (std::filesystem::exists(filePath)) {
            std::filesystem::rename(filePath, backupFilePath);
        }

        SPDLOG_INFO("{}", message.c_str());
        Notification::Emit({ .message = message });
    } catch (...) { SPDLOG_ERROR("Failed to move invalid save file"); }
}

// ComboShip: only the drag-drop MM-save import uses this; not a combo flow (combo MM saves are
// created from OOT, never imported), so the on-disk probe below is vestigial under COMBO_BUILD.
int SaveManager_GetOpenFileSlot() {
    std::string fileName = "file1.json";
    if (!std::filesystem::exists(savesFolderPath / fileName)) {
        return 1;
    }

    fileName = "file2.json";
    if (!std::filesystem::exists(savesFolderPath / fileName)) {
        return 2;
    }

    fileName = "file3.json";
    if (!std::filesystem::exists(savesFolderPath / fileName)) {
        return 3;
    }

    return -1;
}

FlashSave SaveManager_GetFlashSaveFromPages(u32 pageNum, u32 pageCount) {
    FlashSave flashSave = FLASH_SAVE_UNAVAILABLE;

    for (u32 i = 0; i < FLASH_SAVE_MAX; i++) {
        // Verify that the requested pages align with expected values
        if (pageNum == (u32)gFlashSaveStartPages[i] &&
            (pageCount == (u32)gFlashSaveNumPages[i] || pageCount == (u32)gFlashSpecialSaveNumPages[i])) {
            flashSave = static_cast<FlashSave>(i);
            break;
        }
    }

    return flashSave;
}

std::string SaveManager_GetFileName(int fileNum, bool isBackup) {
    return "file" + std::to_string(fileNum) + (isBackup ? "backup" : "") + ".json";
}

std::string SaveManager_GetFileNameFromFlashSave(FlashSave flashSave) {
    if (flashSave == FLASH_SAVE_UNAVAILABLE)
        return "invalid";

    // The global options now live in the config file rather than a save file. This name is only still
    // used for a one time migration.
    if (flashSave == FLASH_SAVE_SRAM_HEADER || flashSave == FLASH_SAVE_SRAM_HEADER_BACKUP) {
        return "global.json";
    }

    bool isBackup =
        flashSave == FLASH_SAVE_FILE_1_NEW_CYCLE_SAVE_BACKUP || flashSave == FLASH_SAVE_FILE_1_OWL_SAVE_BACKUP ||
        flashSave == FLASH_SAVE_FILE_2_NEW_CYCLE_SAVE_BACKUP || flashSave == FLASH_SAVE_FILE_2_OWL_SAVE_BACKUP ||
        flashSave == FLASH_SAVE_FILE_3_NEW_CYCLE_SAVE_BACKUP || flashSave == FLASH_SAVE_FILE_3_OWL_SAVE_BACKUP;

    int fileNum = -1;
    switch (flashSave) {
        case FLASH_SAVE_FILE_1_NEW_CYCLE_SAVE:
        case FLASH_SAVE_FILE_1_NEW_CYCLE_SAVE_BACKUP:
        case FLASH_SAVE_FILE_1_OWL_SAVE:
        case FLASH_SAVE_FILE_1_OWL_SAVE_BACKUP:
            fileNum = 1;
            break;
        case FLASH_SAVE_FILE_2_NEW_CYCLE_SAVE:
        case FLASH_SAVE_FILE_2_NEW_CYCLE_SAVE_BACKUP:
        case FLASH_SAVE_FILE_2_OWL_SAVE:
        case FLASH_SAVE_FILE_2_OWL_SAVE_BACKUP:
            fileNum = 2;
            break;
        case FLASH_SAVE_FILE_3_NEW_CYCLE_SAVE:
        case FLASH_SAVE_FILE_3_NEW_CYCLE_SAVE_BACKUP:
        case FLASH_SAVE_FILE_3_OWL_SAVE:
        case FLASH_SAVE_FILE_3_OWL_SAVE_BACKUP:
            fileNum = 3;
            break;
        default:
            break;
    }

    bool isOwlSave = flashSave == FLASH_SAVE_FILE_1_OWL_SAVE || flashSave == FLASH_SAVE_FILE_1_OWL_SAVE_BACKUP ||
                     flashSave == FLASH_SAVE_FILE_2_OWL_SAVE || flashSave == FLASH_SAVE_FILE_2_OWL_SAVE_BACKUP ||
                     flashSave == FLASH_SAVE_FILE_3_OWL_SAVE || flashSave == FLASH_SAVE_FILE_3_OWL_SAVE_BACKUP;

    return "file" + std::to_string(fileNum) + (isBackup ? "backup" : "") + ".json";
}

bool SaveManager_HandleFileDropped(char* filePath) {
    try {
        std::ifstream fileStream(filePath);

        if (!fileStream.is_open()) {
            return false;
        }

        // Check if first byte is "{"
        if (fileStream.peek() != '{') {
            return false;
        }

        nlohmann::json j;
        try {
            fileStream >> j;
        } catch (nlohmann::json::exception& e) { return false; }

        if (!j.contains("type") || j["type"] != "2S2H_SAVE") {
            return false;
        }

        int saveSlot = SaveManager_GetOpenFileSlot();
        if (saveSlot == -1) {
            SPDLOG_ERROR("No save slot available");
            Notification::Emit({ .message = "No save slot available" });
            return true;
        }

        std::string fileName = SaveManager_GetFileName(saveSlot);

        SaveManager_WriteSaveFile(fileName, j);

        // Reset the file select state to reload the save metadata
        if (gFileSelectState != NULL) {
            STOP_GAMESTATE(&gFileSelectState->state);
            SET_NEXT_GAMESTATE(&gFileSelectState->state, FileSelect_Init, sizeof(FileSelectState));
        }

        SPDLOG_INFO("Successfully imported save into slot {}", saveSlot);
        Notification::Emit({ .message = "Successfully imported save into slot", .suffix = std::to_string(saveSlot) });

        return true;
    } catch (std::exception& e) {
        SPDLOG_ERROR("Failed to load file: {}", e.what());
        Notification::Emit({ .message = "Failed to load file" });
        return false;
    } catch (...) {
        SPDLOG_ERROR("Failed to load file");
        Notification::Emit({ .message = "Failed to load file" });
        return false;
    }
}

#define SAVE_OPTIONS_VALID_ID 0xA51D
#define CVAR_AUDIO_SETTING "gSettings.AudioSetting"
#define CVAR_ZTARGET_SETTING "gSettings.ZTargetSetting"

void SaveManager_WriteGlobalOptions(const SaveOptions& saveOptions) {
    CVarSetInteger(CVAR_AUDIO_SETTING, saveOptions.audioSetting);
    CVarSetInteger(CVAR_ZTARGET_SETTING, saveOptions.zTargetSetting);
    CVarSave();
}

bool SaveManager_ReadGlobalOptions(SaveOptions& saveOptions) {
    // If these are nullptr, we might not have migrated yet.
    if (CVarGet(CVAR_AUDIO_SETTING) == nullptr && CVarGet(CVAR_ZTARGET_SETTING) == nullptr) {
        return false;
    }

    saveOptions.optionId = SAVE_OPTIONS_VALID_ID;
    saveOptions.language = LANGUAGE_ENG;
    saveOptions.audioSetting = CVarGetInteger(CVAR_AUDIO_SETTING, SAVE_AUDIO_STEREO);
    saveOptions.languageSetting = 0;
    saveOptions.zTargetSetting = CVarGetInteger(CVAR_ZTARGET_SETTING, 0);
    return true;
}

bool SaveManager_MigrateGlobalOptions(const std::filesystem::path& fileName, SaveOptions& saveOptions) {
    nlohmann::json j;
    int result = SaveManager_ReadSaveFile(fileName, j);

    if (result == -2) {
        SaveManager_MoveInvalidSaveFile(
            fileName, "Something went wrong trying to read global save file, the original file has been backed up.");
        return false;
    } else if (result != 0) {
        return false;
    }

    try {
        saveOptions = j;
    } catch (nlohmann::json::exception& je) {
        SPDLOG_ERROR("Failed to parse global settings json: {}", je.what());
        SaveManager_MoveInvalidSaveFile(fileName,
                                        "Failed to parse global settings json, the original file has been backed up.");
        return false;
    }

    bool isValid = saveOptions.optionId == SAVE_OPTIONS_VALID_ID;

    if (isValid) {
        SaveManager_WriteGlobalOptions(saveOptions);
        SPDLOG_INFO("Migrated global options out of {} and into the config file", fileName.string());
    }

    SaveManager_DeleteSaveFile(fileName);

    return isValid;
}

extern "C" void SaveManager_SysFlashrom_WriteData(u8* saveBuffer, u32 pageNum, u32 pageCount) {
    FlashSave flashSave = SaveManager_GetFlashSaveFromPages(pageNum, pageCount);
    std::string fileName = SaveManager_GetFileNameFromFlashSave(flashSave);

    bool isBackup = false;

    if (flashSave == FLASH_SAVE_UNAVAILABLE) {
        return;
    }

    if (flashSave == FLASH_SAVE_SRAM_HEADER || flashSave == FLASH_SAVE_SRAM_HEADER_BACKUP) {
        SaveOptions saveOptions;
        memcpy(&saveOptions, saveBuffer, sizeof(SaveOptions));

        SaveManager_WriteGlobalOptions(saveOptions);
        return;
    }

    // A new cycle save with the "special" page count means that both the regular slot and the backup slot should be
    // saved together. We replicate that here by running the save again on the matching backup slot.
    // Note: This is not accounting for the sram header writing a disk backup. It does not feel important to do so.
    // If we ever feel like we want a global save backup, then we just need to add it to this condition.
    if ((flashSave == FLASH_SAVE_FILE_1_NEW_CYCLE_SAVE || flashSave == FLASH_SAVE_FILE_2_NEW_CYCLE_SAVE ||
         flashSave == FLASH_SAVE_FILE_3_NEW_CYCLE_SAVE) &&
        pageCount == (u32)gFlashSpecialSaveNumPages[flashSave]) {
        SaveManager_SysFlashrom_WriteData(saveBuffer, gFlashSaveStartPages[flashSave + 1],
                                          gFlashSaveNumPages[flashSave + 1]);
    }

    switch (flashSave) {
        case FLASH_SAVE_FILE_1_NEW_CYCLE_SAVE_BACKUP:
        case FLASH_SAVE_FILE_2_NEW_CYCLE_SAVE_BACKUP:
        case FLASH_SAVE_FILE_3_NEW_CYCLE_SAVE_BACKUP:
            isBackup = true;
            // fallthrough
        case FLASH_SAVE_FILE_1_NEW_CYCLE_SAVE:
        case FLASH_SAVE_FILE_2_NEW_CYCLE_SAVE:
        case FLASH_SAVE_FILE_3_NEW_CYCLE_SAVE: {
            Save save;
            memcpy(&save, saveBuffer, sizeof(Save));

            if (IS_VALID_FILE(save)) {
                nlohmann::json j;

                // Read the existing save file to preserve the owl save
                int result = SaveManager_ReadSaveFile(fileName, j);
                if (result == -2) {
                    SaveManager_MoveInvalidSaveFile(
                        fileName,
                        "Something went wrong trying to preserve the owl save. Original save file has been backed up.");
                } else if (result == 0) {
                    result = SaveManager_MigrateSave(j);

                    if (result != 0) {
                        SaveManager_MoveInvalidSaveFile(
                            fileName, "Failed to migrate owl save. Original save file has been backed up.");
                    }
                }

                j["newCycleSave"]["save"] = save;
#ifdef COMBO_BUILD
                // ComboShip (#182): this branch preserves owlSave, so a blob that predates this write
                // would shadow it on the next combo load. Drop it unless it is the one we came from.
                // Reached by a game-over save prompt, or a pause save with Pause Menu Save off.
                if (j.contains("owlSave") && gComboOwlBlobSlot != (int)gSaveContext.fileNum + 1) {
                    j.erase("owlSave");
                }
#endif
                j["version"] = CURRENT_SAVE_VERSION;
                j["type"] = "2S2H_SAVE";

                SaveManager_WriteSaveFile(fileName, j);
            } else {
                // If IS_VALID_FILE fails, we should delete the save file, even if there is an owl save in it, because
                // they just deleted the new cycle save
                SaveManager_DeleteSaveFile(fileName);
            }
            break;
        }
        case FLASH_SAVE_FILE_1_OWL_SAVE_BACKUP:
        case FLASH_SAVE_FILE_2_OWL_SAVE_BACKUP:
        case FLASH_SAVE_FILE_3_OWL_SAVE_BACKUP:
            isBackup = true;
            // fallthrough
        case FLASH_SAVE_FILE_1_OWL_SAVE:
        case FLASH_SAVE_FILE_2_OWL_SAVE:
        case FLASH_SAVE_FILE_3_OWL_SAVE: {
            SaveContext saveContext;
            memcpy(&saveContext, saveBuffer, offsetof(SaveContext, fileNum));

            nlohmann::json j;
            // Read the existing save file to preserve the new cycle save
            int result = SaveManager_ReadSaveFile(fileName, j);
            if (result == -2) {
                SaveManager_MoveInvalidSaveFile(fileName, "Something went wrong trying to preserve the new cycle save. "
                                                          "Original save file has been backed up.");
            } else if (result == 0) {
                result = SaveManager_MigrateSave(j);

                if (result != 0) {
                    SaveManager_MoveInvalidSaveFile(
                        fileName, "Failed to migrate new cycle save. Original save file has been backed up.");
                }
            }

            if (IS_VALID_FILE(saveContext.save)) {
                j["owlSave"] = saveContext;
#ifdef COMBO_BUILD
                // ComboShip: if the pre-read failed, this would write an owlSave-only section, which the
                // load path can only refuse. Seed newCycleSave from the owl snapshot — resumed a little
                // late beats a dead slot. Guarded: upstream's file select reads owl-only files legitimately.
                if (!j.contains("newCycleSave")) {
                    SPDLOG_ERROR("[ComboShip] owl save for {} had no new cycle save to preserve; seeding one from the "
                                 "owl snapshot",
                                 fileName);
                    j["newCycleSave"]["save"] = saveContext.save;
                }
#endif
                j["version"] = CURRENT_SAVE_VERSION;
                j["type"] = "2S2H_SAVE";
#ifdef COMBO_BUILD
                gComboOwlBlobSlot = (int)gSaveContext.fileNum + 1; // #182: gSaveContext now matches the blob
#endif
                SaveManager_WriteSaveFile(fileName, j);
            } else {
#ifdef COMBO_BUILD
                gComboOwlBlobSlot = -1; // #182: func_80147314 is deleting the blob
#endif
                // If IS_VALID_FILE fails, and there is still a new cycle save present, we just want to only remove the
                // owl save and write the new cycle save back
                if (j.contains("newCycleSave")) {
                    if (j.contains("owlSave")) {
                        j.erase("owlSave");
                    }
                    j["version"] = CURRENT_SAVE_VERSION;
                    j["type"] = "2S2H_SAVE";
                    SaveManager_WriteSaveFile(fileName, j);
                    // If there is no new cycle save, we should just delete the save file
                } else {
                    SaveManager_DeleteSaveFile(fileName);
                }
            }
            break;
        }
        default:
            break;
    }
}

extern "C" s32 SaveManager_SysFlashrom_ReadData(void* saveBuffer, u32 pageNum, u32 pageCount) {
    FlashSave flashSave = SaveManager_GetFlashSaveFromPages(pageNum, pageCount);
    std::string fileName = SaveManager_GetFileNameFromFlashSave(flashSave);

    if (flashSave == FLASH_SAVE_UNAVAILABLE) {
        return -1;
    }

    if (flashSave == FLASH_SAVE_SRAM_HEADER || flashSave == FLASH_SAVE_SRAM_HEADER_BACKUP) {
        SaveOptions saveOptions = {};

        if (!SaveManager_ReadGlobalOptions(saveOptions) && !SaveManager_MigrateGlobalOptions(fileName, saveOptions)) {
            return -1;
        }

        memcpy(saveBuffer, &saveOptions, sizeof(SaveOptions));
        return 0;
    }

    bool isOwlSave = flashSave == FLASH_SAVE_FILE_1_OWL_SAVE || flashSave == FLASH_SAVE_FILE_1_OWL_SAVE_BACKUP ||
                     flashSave == FLASH_SAVE_FILE_2_OWL_SAVE || flashSave == FLASH_SAVE_FILE_2_OWL_SAVE_BACKUP ||
                     flashSave == FLASH_SAVE_FILE_3_OWL_SAVE || flashSave == FLASH_SAVE_FILE_3_OWL_SAVE_BACKUP;

    nlohmann::json j;
    int result = SaveManager_ReadSaveFile(fileName, j);
    if (result == -2) {
        SaveManager_MoveInvalidSaveFile(
            fileName, "Something went wrong trying to read save file, the original file has been backed up.");
        return -1;
    } else if (result != 0) {
        return result;
    }

    result = SaveManager_MigrateSave(j);

    if (result != 0) {
        SaveManager_MoveInvalidSaveFile(fileName, "Failed to migrate save file, the original file has been backed up.");
        return -1;
    }

    if (isOwlSave) {
        if (!j.contains("owlSave")) {
            return -1;
        }

        try {
            SaveContext saveContext = j["owlSave"];

            // Recompute the checksum in case the save was edited externally or a migration changed it
            // By doing this we sacrifice "real" checksum verification of the save,
            saveContext.save.saveInfo.checksum = 0;
            saveContext.save.saveInfo.checksum = Sram_CalcChecksum(&saveContext, offsetof(SaveContext, fileNum));

            memcpy(saveBuffer, &saveContext, offsetof(SaveContext, fileNum));
            return 0;
        } catch (nlohmann::json::exception& je) {
            SPDLOG_ERROR("Failed to parse owl save json: {}", je.what());
            SaveManager_MoveInvalidSaveFile(fileName,
                                            "Failed to parse save json, the original file has been backed up.");
            return -1;
        } catch (...) {
            SPDLOG_ERROR("Failed to parse owl save json");
            SaveManager_MoveInvalidSaveFile(fileName,
                                            "Failed to parse save json, the original file has been backed up.");
            return -1;
        }
    } else {
        if (!j.contains("newCycleSave")) {
            return -1;
        }

        try {
            Save save = j["newCycleSave"]["save"];

            // Recompute the checksum, see message above
            save.saveInfo.checksum = 0;
            save.saveInfo.checksum = Sram_CalcChecksum(&save, sizeof(Save));

            memcpy(saveBuffer, &save, sizeof(Save));
            return 0;
        } catch (nlohmann::json::exception& je) {
            SPDLOG_ERROR("Failed to parse new cycle save json: {}", je.what());
            SaveManager_MoveInvalidSaveFile(fileName,
                                            "Failed to parse save json, the original file has been backed up.");
            return -1;
        } catch (...) {
            SPDLOG_ERROR("Failed to parse new cycle save json");
            SaveManager_MoveInvalidSaveFile(fileName,
                                            "Failed to parse save json, the original file has been backed up.");
            return -1;
        }
    }
}
