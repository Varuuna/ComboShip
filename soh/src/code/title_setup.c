#include "global.h"

#ifdef COMBO_BUILD
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
// Set by SOH_ResumeGame (OTRGlobals.cpp) on a combo MM->OOT return. >= 0 means: skip the title /
// file-select screens, load this save slot, and spawn straight into Play in the Market outside the
// Happy Mask Shop (the fixed arrival point for the cross-game portal return).
extern s32 gComboReturnFileNum;
#endif

void TitleSetup_InitImpl(GameState* gameState) {
    osSyncPrintf("ゼルダ共通データ初期化\n"); // "Zelda common data initalization"
    SaveContext_Init();
#ifdef COMBO_BUILD
    if (gComboReturnFileNum >= 0) {
        // Combo MM->OOT return: mirror FileChoose_LoadGame (z_file_choose.c) to load the OOT save and
        // jump directly to Play, then override the entrance so Link spawns in the Market outside the
        // Happy Mask Shop (fixed arrival for the cross-game portal return — symmetric with the OOT->MM
        // trigger of entering the shop).
        LUSLOG_INFO("[ComboShip] TitleSetup combo jump -> Play (fileNum=%d)", gComboReturnFileNum);
        gSaveContext.fileNum = gComboReturnFileNum;
        gSaveContext.gameMode = GAMEMODE_NORMAL;
        Sram_OpenSave();
        gSaveContext.entranceIndex = ENTR_MARKET_DAY_OUTSIDE_HAPPY_MASK_SHOP;
        // Mirror FileChoose_LoadGame's magic staging so Play_Init refills the meter normally.
        gSaveContext.magicFillTarget = gSaveContext.magic;
        gSaveContext.magic = 0;
        gSaveContext.magicLevel = 0;
        // Fire the exit/load hook pair a real quit+load runs: Save_LoadFile (inside Sram_OpenSave)
        // recreated gRandoContext, and consumers like the check tracker must tear down + re-init.
        GameInteractor_ExecuteOnExitGame(gSaveContext.fileNum);
        GameInteractor_ExecuteOnLoadGame(gSaveContext.fileNum);
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
