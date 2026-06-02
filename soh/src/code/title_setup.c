#include "global.h"

#ifdef COMBO_BUILD
// Set by SOH_ResumeGame (OTRGlobals.cpp) on a combo MM->OOT return. >= 0 means: skip the title /
// file-select screens, load this save slot, and spawn straight into Play at the Mido's-House door.
extern s32 gComboReturnFileNum;
#endif

void TitleSetup_InitImpl(GameState* gameState) {
    osSyncPrintf("ゼルダ共通データ初期化\n"); // "Zelda common data initalization"
    SaveContext_Init();
#ifdef COMBO_BUILD
    if (gComboReturnFileNum >= 0) {
        // Combo MM->OOT return: mirror FileChoose_LoadGame (z_file_choose.c) to load the OOT save and
        // jump directly to Play, then override the entrance so Link spawns exiting Mido's House.
        LUSLOG_INFO("[ComboShip] TitleSetup combo jump -> Play (fileNum=%d)", gComboReturnFileNum);
        gSaveContext.fileNum = gComboReturnFileNum;
        gSaveContext.gameMode = GAMEMODE_NORMAL;
        Sram_OpenSave();
        gSaveContext.entranceIndex = ENTR_KOKIRI_FOREST_OUTSIDE_MIDOS_HOUSE;
        gComboReturnFileNum = -1;
        gameState->running = false;
        SET_NEXT_GAMESTATE(gameState, Play_Init, PlayState);
        return;
    }
#endif
    gameState->running = false;
    SET_NEXT_GAMESTATE(gameState, Title_Init, TitleContext);
}

void TitleSetup_Destroy(GameState* gameState) {
}

void TitleSetup_Init(GameState* gameState) {
    gameState->destroy = TitleSetup_Destroy;
    TitleSetup_InitImpl(gameState);
}
