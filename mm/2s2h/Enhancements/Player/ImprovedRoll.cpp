// ComboShip: Ship of Harkinian's Improved Roll (soh/soh/Enhancements/TimeSavers/ImprovedRoll.cpp) for Majora's Mask.
// A pressed late in a roll starts a new one at full speed; the optional steering turns the roll toward the stick
// (not while Z is held, so Z-target roll setups still work). Hooks are in Player_Action_26 (z_player.c).
#include <libultraship/bridge/consolevariablebridge.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"

extern "C" {
#include "variables.h"
#include "functions.h"
void func_80836B3C(PlayState* play, Player* player, f32 arg2); // z_player.c: start a roll (SoH's Player_SetupRoll)
}

#define CVAR_ROLL_CHAIN_NAME "gEnhancements.Player.ImprovedRoll"
#define CVAR_ROLL_STEER_NAME "gEnhancements.Player.ImprovedRollSteering"
#define CVAR_ROLL_CHAIN CVarGetInteger(CVAR_ROLL_CHAIN_NAME, 0)
#define CVAR_ROLL_STEER CVarGetInteger(CVAR_ROLL_STEER_NAME, 0)

static void RegisterImprovedRoll() {
    COND_VB_SHOULD(VB_PLAYER_ROLL_CHAIN, CVAR_ROLL_CHAIN, {
        Player* player = va_arg(args, Player*);
        PlayState* play = va_arg(args, PlayState*);
        Input* controlInput = va_arg(args, Input*);
        s32 floorType = va_arg(args, s32);
        if ((player->skelAnime.curFrame >= 15.0f) && CHECK_BTN_ALL(controlInput->press.button, BTN_A) &&
            (floorType != FLOOR_TYPE_7)) {
            func_80836B3C(play, player, 0.0f);
            *should = true;
        }
    });

    COND_VB_SHOULD(VB_PLAYER_ROLL_STEER, CVAR_ROLL_CHAIN && CVAR_ROLL_STEER, {
        Player* player = va_arg(args, Player*);
        PlayState* play = va_arg(args, PlayState*);
        s16 yawTarget = (s16)va_arg(args, int);
        if (!CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_Z)) {
            Math_ScaledStepToS(&player->actor.shape.rot.y, yawTarget, 0x200);
        }
        *should = false;
    });
}

static RegisterShipInitFunc initFunc(RegisterImprovedRoll, { CVAR_ROLL_CHAIN_NAME, CVAR_ROLL_STEER_NAME });
