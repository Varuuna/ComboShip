#include "SaveManager.h"

#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "BenJsonConversions.hpp"
#include "BenPort.h"
#include <ship/window/Window.h>

extern "C" {
#include "z64save.h"
#include "macros.h"
#include "functions.h" // Flags_SetWeekEventReg (used by SET_WEEKEVENTREG)
#include "variables.h"  // gItemSlots (used by INV_CONTENT)
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
    } catch (...) {
        SPDLOG_ERROR("[ComboShip] Failed to write save file {}", filePath.string());
    }
}

void SaveManager_InitNewSaveForSlot(int mmFileNum) {
    Sram_InitNewSave();
#ifdef COMBO_BUILD
    // ComboShip: a fresh MM save is always entered mid-playthrough from OOT, never via MM's own title/
    // intro. Set it up post-first-cycle — Human Link in South Clock Town with no first-cycle intro and no
    // Tatl arrival conversation — mirroring the SkipIntroSequence + SkipFirstCycle enhancements and the
    // Rando port's OnFileCreate. Kept here in PORT code (not MM game source). gPlayState doesn't exist at
    // save-creation time, so items are written directly via INV_CONTENT instead of Item_Give. Scene flags
    // go in permanentSceneFlags because title_setup.c copies those into cycleSceneFlags when the combo
    // save is loaded.
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
#endif
    nlohmann::json j;
    j["newCycleSave"]["save"] = gSaveContext.save;
    j["version"] = CURRENT_SAVE_VERSION;
    j["type"] = "2S2H_SAVE";
    SaveManager_WriteSaveFile(SaveManager_GetFileName(mmFileNum), j);
}

void SaveManager_SaveCurrentForCombo() {
    int mmFileNum = (int)gSaveContext.fileNum + 1;
    nlohmann::json j;
    j["newCycleSave"]["save"] = gSaveContext.save;
    j["version"] = CURRENT_SAVE_VERSION;
    j["type"] = "2S2H_SAVE";
    SaveManager_WriteSaveFile(SaveManager_GetFileName(mmFileNum), j);
}

int SaveManager_ReadSaveFile(const std::filesystem::path& fileName, nlohmann::json& j);

void SaveManager_LoadSaveFile(int mmFileNum) {
    std::string fileName = SaveManager_GetFileName(mmFileNum);
    nlohmann::json j;
    int result = SaveManager_ReadSaveFile(fileName, j);
    SPDLOG_INFO("[ComboShip] LoadSaveFile slot {} -> {} (read result={}, savesFolder={})", mmFileNum,
                std::filesystem::absolute(savesFolderPath / fileName).string(), result,
                std::filesystem::absolute(savesFolderPath).string());
    if (result != 0) {
        // No MM save yet for this slot (e.g. the OOT->MM new-save callback never fired because the
        // player loaded an existing OOT save rather than creating one). Create and persist a fresh
        // MM save now so the transition runs on a valid, saved file instead of an uninitialized one.
        SPDLOG_WARN("[ComboShip] MM save file {} missing; creating a new one", fileName);
        SaveManager_InitNewSaveForSlot(mmFileNum);
        gSaveContext.fileNum = (s16)(mmFileNum - 1);
        return;
    }
    result = SaveManager_MigrateSave(j);
    if (result != 0) {
        SPDLOG_ERROR("[ComboShip] Failed to migrate MM save file: {}", fileName);
        return;
    }
    if (!j.contains("newCycleSave")) {
        SPDLOG_ERROR("[ComboShip] MM save file missing newCycleSave: {}", fileName);
        return;
    }
    try {
        Save save = j["newCycleSave"]["save"];
        save.saveInfo.checksum = 0;
        save.saveInfo.checksum = Sram_CalcChecksum(&save, sizeof(Save));
        gSaveContext.save = save;
        gSaveContext.fileNum = (s16)(mmFileNum - 1);
    } catch (nlohmann::json::exception& je) {
        SPDLOG_ERROR("[ComboShip] Failed to parse MM save: {}", je.what());
    } catch (...) {
        SPDLOG_ERROR("[ComboShip] Failed to parse MM save");
    }
}

void SaveManager_DeleteSaveFile(const std::filesystem::path& fileName) {
    const std::filesystem::path filePath = savesFolderPath / fileName;

    try {
        if (std::filesystem::exists(filePath)) {
            std::filesystem::remove(filePath);
        }
    } catch (...) { SPDLOG_ERROR("Failed to delete save file"); }
}

int SaveManager_ReadSaveFile(const std::filesystem::path& fileName, nlohmann::json& j) {
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

void SaveManager_MoveInvalidSaveFile(const std::filesystem::path& fileName, const std::string& message) {
    const std::filesystem::path filePath = savesFolderPath / fileName;
    const std::filesystem::path backupFilePath =
        savesFolderPath / (fileName.stem().string() + "_invalid_" + std::to_string(std::time(nullptr)) + ".json");

    try {
        if (std::filesystem::exists(filePath)) {
            std::filesystem::rename(filePath, backupFilePath);
        }

        SPDLOG_INFO("{}", message.c_str());
        auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
        gui->GetGameOverlay()->TextDrawNotification(30.0f, true, message.c_str());
    } catch (...) { SPDLOG_ERROR("Failed to move invalid save file"); }
}

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
            auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
            gui->GetGameOverlay()->TextDrawNotification(30.0f, true, "No save slot available");
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
        auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
        gui->GetGameOverlay()->TextDrawNotification(30.0f, true, "Successfully imported save into slot %d", saveSlot);

        return true;
    } catch (std::exception& e) {
        SPDLOG_ERROR("Failed to load file: {}", e.what());
        auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
        gui->GetGameOverlay()->TextDrawNotification(30.0f, true, "Failed to load file");
        return false;
    } catch (...) {
        SPDLOG_ERROR("Failed to load file");
        auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
        gui->GetGameOverlay()->TextDrawNotification(30.0f, true, "Failed to load file");
        return false;
    }
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

        SaveManager_WriteSaveFile(fileName, saveOptions);
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
                j["version"] = CURRENT_SAVE_VERSION;
                j["type"] = "2S2H_SAVE";

                SaveManager_WriteSaveFile(fileName, j);
            } else {
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
        nlohmann::json j;
        int result = SaveManager_ReadSaveFile(fileName, j);
        if (result == -2) {
            SaveManager_MoveInvalidSaveFile(
                fileName,
                "Something went wrong trying to read global save file, the original file has been backed up.");
            return -1;
        } else if (result != 0) {
            return result;
        }

        try {
            SaveOptions saveOptions = j;

            memcpy(saveBuffer, &saveOptions, sizeof(SaveOptions));
            return 0;
        } catch (nlohmann::json::exception& je) {
            SPDLOG_ERROR("Failed to parse global settings json: {}", je.what());
            SaveManager_MoveInvalidSaveFile(
                fileName, "Failed to parse global settings json, the original file has been backed up.");
            return -1;
        }
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
