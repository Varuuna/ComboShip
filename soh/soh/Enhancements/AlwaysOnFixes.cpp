#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "macros.h"
#include "functions.h"
#include "variables.h"
<<<<<<< HEAD
=======
#include "src/overlays/actors/ovl_En_Go2/z_en_go2.h"
#include "src/overlays/actors/ovl_En_Test/z_en_test.h"
>>>>>>> vendor-soh
extern void Player_UseItem(PlayState*, Player*, s32);
extern PlayState* gPlayState;
}

<<<<<<< HEAD
// Dying or using Din's Fire in the Outside Temple of Time area crashes the game.
// In vanilla this can never happen, but with CrowdControl, Sail, Unrestricted Items
// and others this *can* happen. Because it checks for a camId of -1, this code path
// shouldn't ever influence vanilla play regardless.
void RegisterFixOutsideTotCrash() {
=======
// Actor_FindNearby also matches actors killed this same frame (update==NULL) that are
// still in the list, so when the last Stalfos of a group dies alongside its siblings,
// vanilla thinks an enemy remains and BGM never restores (seen in MQ Water Temple).
// Re-check against living actors only.
static bool EnTest_HasLivingNearby(Actor* refActor) {
    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head;
    while (actor != NULL) {
        if (actor != refActor && actor->id == ACTOR_EN_TEST && actor->update != NULL &&
            Actor_WorldDistXYZToActor(refActor, actor) <= 8000.0f) {
            return true;
        }
        actor = actor->next;
    }
    return false;
}

void RegisterAlwaysOnFixes() {
    // Crash on death/Din's Fire outside Temple of Time (crowd control, Sail, unrestricted items). camId -1 path won't
    // affect vanilla.
>>>>>>> vendor-soh
    COND_VB_SHOULD(VB_SHOULD_LOAD_BG_IMAGE, true, {
        int32_t* camId = va_arg(args, int*);
        if (*camId == -1) {
            *should = false;
        }
    });

    // Actor_Item_Shield (dropped Deku Shield) assumes segment 12 still holds Link's
    // gCullBackDList; an intermediate actor using segment 12 (e.g. Jabu tentacles)
    // overwrites it and crashes. Re-set segment 12 before drawing.
    COND_VB_SHOULD(VB_ITEMSHIELD_DRAW, true, {
        GraphicsContext* __gfxCtx = gPlayState->state.gfxCtx;
        gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)SEGMENTED_TO_VIRTUAL(gCullBackDList));
    });

    // Hookshot not spawning softlocks player (child use, memory full). Clear item on no
    // spawn (Player_InitItemAction removes the ranged weapon state elsewhere).
    COND_VB_SHOULD(VB_INIT_HOOKSHOT_IA, true, {
        Player* player = va_arg(args, Player*);
        if (player->heldActor == NULL) {
            Player_UseItem(gPlayState, player, 0xFF);
        }
    });

    // Non-hookshot parent causes fly-land-fly loop (e.g. Moblin grab in water, or Like
    // Like eating player then despawning falling through En_Holl). Validate ACTOR_ARMS_HOOK parent.
    COND_VB_SHOULD(VB_PREVENT_HOOKSHOT_PARENT_SOFTLOCK, true, {
        s16* parentId = va_arg(args, s16*);
        if (*parentId != ACTOR_ARMS_HOOK) {
            *should = false;
        }
    });

    // Goron Link asleep softlocks talk state after leaving range before tunic, since
    // UpdateTalkState cannot run to progress to the question textbox. Force the update.
    COND_VB_SHOULD(VB_PREVENT_GORON_LINK_SOFTLOCK, true, {
        EnGo2* GoronLink = va_arg(args, EnGo2*);
        if (GoronLink->interactInfo.talkState == NPC_TALK_STATE_TALKING) {
            *should = true;
        }
    });

    COND_ID_HOOK(OnActorDestroy, ACTOR_EN_TEST, true, [](void* refActor) {
        Actor* actor = reinterpret_cast<Actor*>(refActor);
        if (actor->params != STALFOS_TYPE_2 && !EnTest_HasLivingNearby(actor)) {
            func_800F5B58();
        }
    });
}

<<<<<<< HEAD
// Vanilla bug: `Actor_Item_Shield` (dropped Deku Shield when burning) assumes that segment 12
// contains Link display list `gCullBackDList`. If an actor is drawn between player and shield
// that uses segment 12 (such as Jabu-Jabu tentacles), the game will crash on Deku Shield drop.
// Fix: Re-set segment 12 to the required display list.
void RegisterFixDekuShieldDropCrash() {
    COND_VB_SHOULD(VB_ITEMSHIELD_DRAW, true, {
        GraphicsContext* __gfxCtx = gPlayState->state.gfxCtx;
        gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)SEGMENTED_TO_VIRTUAL(gCullBackDList));
    });
}

// Vanilla bug: If Hookshot doesn't spawn, player is softlocked. (eg. use as child, no memory left)
// Fix: Change item to none if no spawn. (Ranged weapon state is removed by `Player_InitItemAction`)
void RegisterPreventHookshotNoSpawnSoftlock() {
    COND_VB_SHOULD(VB_INIT_HOOKSHOT_IA, true, {
        Player* player = va_arg(args, Player*);
        if (player->heldActor == NULL) {
            Player_UseItem(gPlayState, player, 0xFF);
        }
    });
}

// Vanilla bug: When pulling out Hookshot, if `this->actor.parent` is set but not Hookshot, player
// is locked into repeated fly-land-fly. Possible with enemies that grab player and set themselves
// as parent (such as Moblin in water, eaten by Like like that despawns falling through En_Holl).
// Fix: Ensure that parent actor has Hookshot actor ID before starting flying.
void RegisterPreventHookshotParentSoftlock() {
    COND_VB_SHOULD(VB_PREVENT_HOOKSHOT_PARENT_SOFTLOCK, true, {
        s16* parentId = va_arg(args, s16*);
        if (*parentId != ACTOR_ARMS_HOOK) {
            *should = false;
        }
    });
}

static RegisterShipInitFunc initFuncFixOutsideTotCrash(RegisterFixOutsideTotCrash, { "" });
static RegisterShipInitFunc initFuncFixDekuShieldDropCrash(RegisterFixDekuShieldDropCrash, { "" });
static RegisterShipInitFunc initFuncHookshotNospawnSoftlock(RegisterPreventHookshotNoSpawnSoftlock, { "" });
static RegisterShipInitFunc initFuncHookshotParentSoftlock(RegisterPreventHookshotParentSoftlock, { "" });
=======
static RegisterShipInitFunc initAlwaysOnFixes(RegisterAlwaysOnFixes, { "" });
>>>>>>> vendor-soh
