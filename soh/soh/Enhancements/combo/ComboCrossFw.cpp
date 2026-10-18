// ComboShip: OoTMM's "Cross-Games Farore's Wind" (crossGameFw), OOT side, with a point per game; see
// combo/rando/CrossFw.h. The setting is MM's RO_CROSS_GAME_FW (it needs MM's Farore's Wind); MM's answer to CFW_STATE
// tells OOT whether it is on.
//
// OOT's own point is gSaveContext.fw (per age in SoH) and works as always. Termina keeps its own. Casting in OOT
// (z_player.c Player_ActionHandler_13 / Player_Action_8085063C ask this file):
// - Hyrule's point only: OOT's own prompt (Return / Dispel / Exit).
// - Termina's point only: "Warp to Termina / Set a warp point here / Exit" (TEXT_COMBO_FW_THERE).
// - Both: OOT's prompt, and Return then asks which point (TEXT_COMBO_FW_WHICH).
// Warping to Termina's point goes through the teleport songs' cross switch; MM spawns there and uses it up. Arriving
// from Termina's cast: TitleSetup spawns Link at OOT's point with respawnFlag 3, the way OOT's own return does (and the
// point is used up the same way). A "lives in Termina" marker saved before points were per game (entranceIndex -2)
// is dropped when cast.
#ifdef COMBO_BUILD
#include "soh/OTRGlobals.h"
#include "soh/SaveManager.h"
#include "ComboExport.h"
#include "rando/CrossFw.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/custom-message/CustomMessageTypes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "sfx.h"
void Combo_RequestCrossSwitch(int mmEntrance); // OTRGlobals.cpp (teleport songs)
}

bool Combo_OotIsForeground(void); // OTRGlobals.cpp

namespace {

constexpr s32 kCrossEntrance = -2; // the legacy marker's entrance (see DropLegacyMarker)
ComboFwPeerFn sFwPeer = nullptr;   // launcher: forwards an op to MM (-1 when off or MM's save isn't resident)
bool sFwArrivalArmed = false;

int FwPeer(int op) {
    return (IS_RANDO && (sFwPeer != nullptr)) ? sFwPeer(op) : -1;
}

int OotFwState() {
    if (!gSaveContext.fw.set) {
        return CFW_NONE;
    }
    return (gSaveContext.fw.entranceIndex == kCrossEntrance) ? CFW_CROSS : CFW_HERE;
}

void ClearOotFw() {
    gSaveContext.fw.set = 0;
    gSaveContext.respawn[RESPAWN_MODE_TOP].data = 0;
}

void Persist() {
    if (!Combo_OotIsForeground() && SaveManager::Instance && (gSaveContext.fileNum <= 2)) {
        SaveManager::Instance->SaveFile(gSaveContext.fileNum); // dormant: persist now, like a dormant grant
    }
}

} // namespace

extern "C" COMBO_EXPORT int SOH_ComboFw(int op) {
    if ((gSaveContext.fileNum > 2) || !IS_RANDO) {
        return -1;
    }
    switch (op) {
        case CFW_STATE:
            return OotFwState();
        case CFW_MARK_CROSS:
            return 0; // unused since each game keeps its own point
        case CFW_CLEAR_IF_CROSS:
            if (OotFwState() == CFW_CROSS) {
                ClearOotFw();
                Persist();
            }
            return 0;
        case CFW_ENTRANCE:
            return (OotFwState() == CFW_HERE) ? gSaveContext.fw.entranceIndex : -1;
        case CFW_ARM_ARRIVAL:
            if (OotFwState() != CFW_HERE) {
                return -1;
            }
            sFwArrivalArmed = true;
            return 0;
        default:
            return -1;
    }
}

extern "C" COMBO_EXPORT void SOH_SetComboFwPeer(ComboFwPeerFn peer) {
    sFwPeer = peer;
}

// A "lives in Termina" marker from before points were per game: no point here.
static void DropLegacyMarker() {
    if (OotFwState() == CFW_CROSS) {
        ClearOotFw();
    }
}

static bool TerminaHasPoint() {
    return FwPeer(CFW_STATE) == CFW_HERE;
}

// Player_ActionHandler_13, casting with no point of OOT's own: 1 to open the prompt anyway (Termina has one).
extern "C" int Combo_Fw_OotWantsPrompt(void) {
    DropLegacyMarker();
    return TerminaHasPoint() ? 1 : 0;
}

// Player_Action_8085063C: 0 = OOT's own prompt only, 1 = Termina's point only, 2 = both.
extern "C" int Combo_Fw_OotPromptKind(void) {
    DropLegacyMarker();
    const bool here = gSaveContext.fw.set && (gSaveContext.respawn[RESPAWN_MODE_TOP].data > 0);
    if (!TerminaHasPoint()) {
        return 0;
    }
    return here ? 2 : 1;
}

static CustomMessage sThereMsg;
static CustomMessage sWhichMsg;

extern "C" void Combo_Fw_OotStartThere(PlayState* play) {
    sThereMsg = CustomMessage(std::string("There is a warp point in %gTermina%w.&") +
                                  CustomMessage::THREE_WAY_CHOICE() + "%gWarp to Termina&Set a warp point here&Exit%w",
                              TEXTBOX_TYPE_BLUE);
    sThereMsg.Format();
    Message_StartTextbox(play, TEXT_COMBO_FW_THERE, NULL);
}

extern "C" void Combo_Fw_OotStartWhich(PlayState* play) {
    // Two lines of question: OOT only indents a two-choice box's lines from the third one on.
    sWhichMsg = CustomMessage(std::string("Which warp point&will you go to?&") + CustomMessage::TWO_WAY_CHOICE() +
                                  "%gHyrule&Termina%w",
                              TEXTBOX_TYPE_BLUE);
    sWhichMsg.Format();
    Message_StartTextbox(play, TEXT_COMBO_FW_WHICH, NULL);
}

// "Warp to Termina" chosen: 1 when the warp is under way (the caller ends the cast), 0 if Termina's point is gone.
extern "C" int Combo_Fw_OotWarpThere(PlayState* play) {
    const int entrance = TerminaHasPoint() ? FwPeer(CFW_ENTRANCE) : -1;
    if ((entrance >= 0) && (FwPeer(CFW_ARM_ARRIVAL) >= 0)) {
        Sfx_PlaySfxCentered(NA_SE_PL_MAGIC_WIND_VANISH);
        Combo_RequestCrossSwitch(entrance); // persist + switch on the next clean frame
        return 1;
    }
    Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
    return 0;
}

// z_player.c: a new point was just set here. Termina keeps its own; only a marker saved before that goes.
extern "C" void Combo_Fw_OotOnSet(void) {
    FwPeer(CFW_CLEAR_IF_CROSS);
}

// z_player.c: the point is about to be dispelled. Termina's point stays.
extern "C" void Combo_Fw_OotOnDispel(void) {
}

static RegisterShipInitFunc sFwMessagesInit(
    []() {
        COND_ID_HOOK(OnOpenText, TEXT_COMBO_FW_THERE, IS_RANDO, [](uint16_t* textId, bool* loadFromMessageTable) {
            sThereMsg.LoadIntoFont();
            *loadFromMessageTable = false;
        });
        COND_ID_HOOK(OnOpenText, TEXT_COMBO_FW_WHICH, IS_RANDO, [](uint16_t* textId, bool* loadFromMessageTable) {
            sWhichMsg.LoadIntoFont();
            *loadFromMessageTable = false;
        });
    },
    { "IS_RANDO" });

// title_setup.c, after a cross-game arrival entrance was applied: an armed arrival spawns at OOT's point.
extern "C" void Combo_OoT_ApplyFwArrival(void) {
    if (!sFwArrivalArmed) {
        return;
    }
    sFwArrivalArmed = false;
    if (OotFwState() != CFW_HERE) {
        return;
    }
    gSaveContext.entranceIndex = gSaveContext.fw.entranceIndex;
    gSaveContext.respawnFlag = 3; // RESPAWN_MODE_TOP + 1, as OOT's own return
    RespawnData* top = &gSaveContext.respawn[RESPAWN_MODE_TOP];
    top->pos.x = (f32)gSaveContext.fw.pos.x;
    top->pos.y = (f32)gSaveContext.fw.pos.y;
    top->pos.z = (f32)gSaveContext.fw.pos.z;
    top->yaw = (s16)gSaveContext.fw.yaw;
    top->playerParams = (s16)gSaveContext.fw.playerParams;
    top->entranceIndex = (s16)gSaveContext.fw.entranceIndex;
    top->roomIndex = (u8)gSaveContext.fw.roomIndex;
    top->tempSwchFlags = (u32)gSaveContext.fw.tempSwchFlags;
    top->tempCollectFlags = (u32)gSaveContext.fw.tempCollectFlags;
    top->data = 0x28;
}
#endif
