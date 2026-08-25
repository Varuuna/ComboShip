#include "z_title_setup.h"
#include "overlays/gamestates/ovl_title/z_title.h"
#ifdef COMBO_BUILD
#include "global.h"
#include "BenPort.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#endif
#include "z64save.h"

void Setup_InitRegs(void) {
    XREG(2) = 0;
    XREG(10) = 26;
    XREG(11) = 20;
    XREG(12) = 14;
    XREG(13) = 0;
    R_A_BTN_Y_OFFSET = 0;
    R_MAGIC_CONSUME_TIMER_GIANTS_MASK = 80;

    R_THREE_DAY_CLOCK_Y_POS = 64596;
    R_THREE_DAY_CLOCK_SUN_MOON_CUTOFF = 215;
    R_THREE_DAY_CLOCK_HOUR_DIGIT_CUTOFF = 218;

    XREG(68) = 0x61;
    XREG(69) = 0x93;
    XREG(70) = 0x28;
    XREG(73) = 0x1E;
    XREG(74) = 0x42;
    XREG(75) = 0x1E;
    XREG(76) = 0x1C;
    XREG(77) = 0x3C;
    XREG(78) = 0x2F;
    XREG(79) = 0x62;
    R_PAUSE_OWL_WARP_ALPHA = 0;
    XREG(88) = 0x56;
    XREG(89) = 0x258;
    XREG(90) = 0x1C2;

    R_STORY_FILL_SCREEN_ALPHA = 0;
    R_PLAYER_FLOOR_REVERSE_INDEX = 0;
    R_MINIMAP_DISABLED = false;

    R_PICTO_FOCUS_BORDER_TOPLEFT_X = 80;
    R_PICTO_FOCUS_BORDER_TOPLEFT_Y = 60;
    R_PICTO_FOCUS_BORDER_TOPRIGHT_X = 220;
    R_PICTO_FOCUS_BORDER_TOPRIGHT_Y = 60;
    R_PICTO_FOCUS_BORDER_BOTTOMLEFT_X = 80;
    R_PICTO_FOCUS_BORDER_BOTTOMLEFT_Y = 160;
    R_PICTO_FOCUS_BORDER_BOTTOMRIGHT_X = 220;
    R_PICTO_FOCUS_BORDER_BOTTOMRIGHT_Y = 160;
    R_PICTO_FOCUS_ICON_X = 142;
    R_PICTO_FOCUS_ICON_Y = 108;
    R_PICTO_FOCUS_TEXT_X = 204;
    R_PICTO_FOCUS_TEXT_Y = 177;
}

void Setup_InitImpl(SetupState* this) {
    SysFlashrom_InitFlash();
    SaveContext_Init();
    Sram_LoadGlobalOptions();
    Setup_InitRegs();

#ifdef COMBO_BUILD
    s32 loadedOwlSave;

    if (gComboStartFileNum >= 0) {
        // Sram_LoadGlobalOptions above found no global.json in combo, so take OOT's settings.
        Combo_AdoptOOTGlobalOptions();
        // Set flashSaveAvailable so Sram_Alloc allocates saveBuf (normally set by the title screen).
        gSaveContext.flashSaveAvailable = true;
        // Load the MM save that matches the OOT slot (OOT slot N → MM file N+1). ComboShip never blocks
        // entry: a nonzero result is logged inside and leaves the fail-closed sentinel, and play proceeds.
        Combo_LoadMMSaveFile(gComboStartFileNum + 1);
        // ComboShip (#182): an owl save is the newest state whenever it exists, so it wins on both
        // entry kinds — but only a resume follows it to where it was saved.
        loadedOwlSave = (gComboOwlBlobSlot == gComboStartFileNum + 1);
        if (loadedOwlSave) {
            Combo_ApplyOwlSaveOpen(gComboEntryIsResume);
            if (!gComboEntryIsResume) {
                // Arriving in South Clock Town runs neither owl-arrival path, so clear isOwlSave here
                // or it sticks — and then owl-save write timing stays armed for the whole session.
                gSaveContext.save.isOwlSave = false;
                gSaveContext.save.entrance = ENTRANCE(SOUTH_CLOCK_TOWN, 0);
            }
        } else if (gComboEntryIsResume && CVarGetInteger("gEnhancements.Saving.RememberSaveLocation", 0) &&
                   gSaveContext.save.shipSaveInfo.pauseSaveEntrance != -1) {
            // South Clock Town is the default arrival for both portal entry and a resume. Only a resume
            // with Remember Save Location on returns to the stored spot (set by SavingEnhancements).
            gSaveContext.save.entrance = gSaveContext.save.shipSaveInfo.pauseSaveEntrance;
        } else {
            gSaveContext.save.entrance = ENTRANCE(SOUTH_CLOCK_TOWN, 0);
        }
        gSaveContext.save.cutsceneIndex = 0;
        // Reset magicLevel like Sram_OpenSave does — re-arms the magic meter grow animation
        // (Interface_Update only steps magicCapacity when magicLevel == 0).
        gSaveContext.save.saveInfo.playerData.magicLevel = 0;
        // Copy permanent flags into cycle flags, the same way Sram_OpenSave does.
        for (int i = 0; i < ARRAY_COUNT(gSaveContext.cycleSceneFlags); i++) {
            gSaveContext.cycleSceneFlags[i].chest = gSaveContext.save.saveInfo.permanentSceneFlags[i].chest;
            gSaveContext.cycleSceneFlags[i].switch0 = gSaveContext.save.saveInfo.permanentSceneFlags[i].switch0;
            gSaveContext.cycleSceneFlags[i].switch1 = gSaveContext.save.saveInfo.permanentSceneFlags[i].switch1;
            gSaveContext.cycleSceneFlags[i].clearedRoom = gSaveContext.save.saveInfo.permanentSceneFlags[i].clearedRoom;
            gSaveContext.cycleSceneFlags[i].collectible = gSaveContext.save.saveInfo.permanentSceneFlags[i].collectible;
        }
        // ComboShip: fire OnSaveLoad like every vanilla load path does once the save is in
        // gSaveContext. This arms the rando runtime: OnSaveLoadHandler re-registers all
        // IS_RANDO-conditioned hooks (grass/pot/crate shuffles, check tracker, clock shuffle, etc.)
        // and re-runs ShipInit::Init("IS_RANDO"). COND_* macros are unregister-then-register, so it's
        // idempotent. Without it the combo force-spawn loads a rando save whose behaviors never activate.
        GameInteractor_ExecuteOnSaveLoad(gSaveContext.fileNum);

        // ComboShip (#182): consume the owl save like Sram_OpenSave does, so it can't shadow the newer
        // state we just continued into. Vetoed for Persistent Owl Saves and for pause/auto saves.
        if (loadedOwlSave && GameInteractor_Should(VB_DELETE_OWL_SAVE, true)) {
            Combo_MMDropOwlSaveBlob();
        }

        gComboStartFileNum = -1;
        STOP_GAMESTATE(&this->state);
        SET_NEXT_GAMESTATE(&this->state, Play_Init, sizeof(PlayState));
        return;
    }
#endif

    STOP_GAMESTATE(&this->state);
    SET_NEXT_GAMESTATE(&this->state, ConsoleLogo_Init, sizeof(ConsoleLogoState));
}

void Setup_Destroy(GameState* thisx) {
}

void Setup_Init(GameState* thisx) {
    SetupState* this = (SetupState*)thisx;

    this->state.destroy = Setup_Destroy;
    Setup_InitImpl(this);
}
