
#ifndef SAVE_MANAGER_H
#define SAVE_MANAGER_H

#ifdef __cplusplus
#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>
std::string SaveManager_GetFileName(int fileNum, bool isBackup = false);
bool SaveManager_HandleFileDropped(char* filePath);
bool BinarySaveConverter_HandleFileDropped(char* filePath);
int SaveManager_GetOpenFileSlot();
void SaveManager_WriteSaveFile(const std::filesystem::path& fileName, nlohmann::json j);
void SaveManager_InitNewSaveForSlot(int mmFileNum, const unsigned char* ootName8 = nullptr);
void SaveManager_LoadSaveFile(int mmFileNum);
void SaveManager_SaveCurrentForCombo();
#else
void SaveManager_SysFlashrom_WriteData(u8* addr, u32 pageNum, u32 pageCount);
s32 SaveManager_SysFlashrom_ReadData(void* addr, u32 pageNum, u32 pageCount);
#endif

#endif // SAVE_MANAGER_H
