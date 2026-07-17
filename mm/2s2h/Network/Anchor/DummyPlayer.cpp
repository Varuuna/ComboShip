#include "MMAnchor.h"
#ifdef COMBO_BUILD

#include "2s2h/NameTag/NameTag.h"

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "z64malloc.h"
extern PlayState* gPlayState;

extern PlayerAgeProperties sPlayerAgeProperties[PLAYER_FORM_MAX];
void Player_DrawGameplay(PlayState* play, Player* player, s32 lod, Gfx* cullDList,
                         OverrideLimbDrawFlex overrideLimbDraw);
void Player_Anim_PlayOnceMorph(PlayState* play, Player* player, PlayerAnimationHeader* anim);
PlayerAnimationHeader* Player_GetIdleAnim(Player* player);
}

// ComboShip: MM remote-player ("puppet") actor — a port of 2S2H PR #1349's DummyPlayer onto the
// launcher-connected MMAnchor. A real ACTOR_PLAYER is spawned then re-tagged to ACTOR_ITEM_INBOX in
// the ACTORCAT_NPC list (see MMAnchor::RegisterHooks ShouldActorInit), so the engine allocates the
// full Player/skeleton/segments while GET_PLAYER still returns the local player. All five forms
// render by initializing with gPlayerSkeletons[transformation] and reusing vanilla Player_DrawGameplay.

// Hijack player->zTargetActiveTimer (unused on the puppet) to store the clientId.
#define DUMMY_CLIENT_ID player->zTargetActiveTimer

static DamageTable DummyPlayerDamageTable = {
    /* Deku Nut       */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_STUN),
    /* Deku Stick     */ DMG_ENTRY(2, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Horse trample  */ DMG_ENTRY(2, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Explosives     */ DMG_ENTRY(2, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Zora boomerang */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_STUN),
    /* Normal arrow   */ DMG_ENTRY(2, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* UNK_DMG_0x06   */ DMG_ENTRY(2, DUMMY_PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE),
    /* Hookshot       */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_STUN),
    /* Goron punch    */ DMG_ENTRY(4, DUMMY_PLAYER_HIT_RESPONSE_KNOCKBACK_SMALL),
    /* Sword          */ DMG_ENTRY(4, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Goron pound    */ DMG_ENTRY(4, DUMMY_PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE),
    /* Fire arrow     */ DMG_ENTRY(2, DUMMY_PLAYER_HIT_RESPONSE_FIRE),
    /* Ice arrow      */ DMG_ENTRY(4, DUMMY_PLAYER_HIT_RESPONSE_ICE_TRAP),
    /* Light arrow    */ DMG_ENTRY(2, DUMMY_PLAYER_HIT_RESPONSE_ELECTRIC_SHOCK),
    /* Goron spikes   */ DMG_ENTRY(2, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Deku spin      */ DMG_ENTRY(2, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Deku bubble    */ DMG_ENTRY(2, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Deku launch    */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_KNOCKBACK_SMALL),
    /* UNK_DMG_0x12   */ DMG_ENTRY(3, DUMMY_PLAYER_HIT_RESPONSE_ICE_TRAP),
    /* Zora barrier   */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_ELECTRIC_SHOCK),
    /* Normal shield  */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_NONE),
    /* Light ray      */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_NONE),
    /* Thrown object  */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Zora punch     */ DMG_ENTRY(2, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Spin attack    */ DMG_ENTRY(2, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Sword beam     */ DMG_ENTRY(4, DUMMY_PLAYER_HIT_RESPONSE_ELECTRIC_SHOCK),
    /* Normal Roll    */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_NONE),
    /* UNK_DMG_0x1B   */ DMG_ENTRY(4, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* UNK_DMG_0x1C   */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_NONE),
    /* Unblockable    */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_NONE),
    /* UNK_DMG_0x1E   */ DMG_ENTRY(4, DUMMY_PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE),
    /* Powder Keg     */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
};

void DummyPlayer_Init(Actor* actor, PlayState* play) {
    Player* player = (Player*)actor;

    uint32_t clientId = MMAnchor::Instance->actorIndexToClientId[actor->params];
    DUMMY_CLIENT_ID = clientId;

    if (!MMAnchor::Instance->clients.contains(DUMMY_CLIENT_ID)) {
        Actor_Kill(actor);
        return;
    }

    MMAnchorClient& client = MMAnchor::Instance->clients[DUMMY_CLIENT_ID];

    player->actor.room = -1;
    player->csId = CS_ID_NONE;
    player->transformation = client.transformation;
    player->ageProperties = &sPlayerAgeProperties[player->transformation];
    player->heldItemAction = PLAYER_IA_NONE;
    player->heldItemId = ITEM_OCARINA_OF_TIME;

    Player_SetModelGroup(player, PLAYER_MODELGROUP_DEFAULT);
    play->playerInit(player, play, gPlayerSkeletons[player->transformation]);

    player->maskObjectSegment = ZeldaArena_Malloc(0x3800);
    Player_Anim_PlayOnceMorph(play, player, Player_GetIdleAnim(player));
    player->yaw = player->actor.shape.rot.y;

    // Always update/draw even when culled / out of distance.
    actor->flags =
        ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED | ACTOR_FLAG_INSIDE_CULLING_VOLUME;
    player->cylinder.base.acFlags = AC_ON | AC_TYPE_PLAYER;
    player->cylinder.base.ocFlags2 = OC2_TYPE_1;
    player->cylinder.elem.acElemFlags = ACELEM_ON | ACELEM_HOOKABLE | ACELEM_NO_HITMARK;
    player->actor.flags |= ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER;
    player->cylinder.dim.radius = 30;
    player->actor.colChkInfo.damageTable = &DummyPlayerDamageTable;

    bool isGlobalRoom = (std::string("soh-global") == CVarGetString("gRemote.Anchor.RoomId", ""));
    if (!isGlobalRoom) {
        // ComboShip: use the client's Anchor color instead of the dark default (mirrors soh's puppet).
        NameTag_RegisterForActorWithOptions(
            actor, client.name.c_str(),
            { .yOffset = 30, .textColor = { client.color.r, client.color.g, client.color.b, 255 } });
    }
}

void DummyPlayer_Update(Actor* actor, PlayState* play) {
    Player* player = (Player*)actor;

    if (!MMAnchor::Instance->clients.contains(DUMMY_CLIENT_ID)) {
        Actor_Kill(actor);
        return;
    }

    MMAnchorClient& client = MMAnchor::Instance->clients[DUMMY_CLIENT_ID];

    if (client.sceneId != gPlayState->sceneId || !client.online || !client.isSaveLoaded) {
        actor->world.pos.x = -9999.0f;
        actor->world.pos.y = -9999.0f;
        actor->world.pos.z = -9999.0f;
        actor->shape.shadowAlpha = 0;
        return;
    }

    actor->shape.shadowAlpha = 255;
    Math_Vec3s_Copy(&actor->shape.rot, &client.posRot.rot);
    Math_Vec3f_Copy(&actor->world.pos, &client.posRot.pos);
    memcpy(&player->jointTableBuffer, &client.jointTable, 159);
    memcpy(&player->jointTableUpperBuffer, &client.upperJointTable, 159);
    player->maskObjectLoadState = 0;
    player->maskId = player->currentMask;
    player->currentMask = client.currentMask;
    player->rightHandType = client.rightHandType;
    player->leftHandType = client.leftHandType;
    player->currentShield = client.currentShield;
    player->sheathType = client.sheathType;
    player->heldItemAction = client.heldItemAction;
    player->heldItemId = client.heldItemId;
    player->itemAction = client.itemAction;
    player->stateFlags1 = client.stateFlags1;
    player->stateFlags2 = client.stateFlags2;
    player->stateFlags3 = client.stateFlags3;
    player->unk_B0C = client.unk_B0C;
    player->unk_B28 = client.unk_B28;
    player->unk_ACC = client.unk_ACC;
    player->invincibilityTimer = client.invincibilityTimer;
    Player_SetModels(player, Player_ActionToModelGroup(player, (PlayerItemAction)player->itemAction));

    // PvP off (or same team in friendly mode) -> no collision interactions; don't hijack Z-targeting.
    if (MMAnchor::Instance->roomState.pvpMode == 0 ||
        (MMAnchor::Instance->roomState.pvpMode == 1 &&
         client.teamId == CVarGetString("gRemote.Anchor.TeamId", "default"))) {
        actor->flags |= ACTOR_FLAG_LOCK_ON_DISABLED;
        return;
    }

    actor->flags &= ~ACTOR_FLAG_LOCK_ON_DISABLED;

    if (player->cylinder.base.acFlags & AC_HIT && player->invincibilityTimer == 0) {
        MMAnchor::Instance->SendPacket_DamagePlayer(client.clientId, player->actor.colChkInfo.damageEffect,
                                                    player->actor.colChkInfo.damage);
        if (player->actor.colChkInfo.damageEffect == DUMMY_PLAYER_HIT_RESPONSE_STUN) {
            Actor_SetColorFilter(&player->actor, 0, 0xFF, 0, 24);
        } else {
            player->invincibilityTimer = 20;
        }
    }

    Collider_UpdateCylinder(&player->actor, &player->cylinder);

    if (!(player->stateFlags2 & PLAYER_STATE2_4000)) {
        if (!(player->stateFlags1 & (PLAYER_STATE1_4 | PLAYER_STATE1_DEAD | PLAYER_STATE1_2000 | PLAYER_STATE1_4000 |
                                     PLAYER_STATE1_800000))) {
            CollisionCheck_SetOC(play, &play->colChkCtx, &player->cylinder.base);
        }

        if (!(player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_4000000)) &&
            (player->invincibilityTimer <= 0)) {
            CollisionCheck_SetAC(play, &play->colChkCtx, &player->cylinder.base);

            if (player->invincibilityTimer < 0) {
                CollisionCheck_SetAT(play, &play->colChkCtx, &player->cylinder.base);
            }
        }
    }

    if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_10000000 | PLAYER_STATE1_20000000)) {
        player->actor.colChkInfo.mass = MASS_IMMOVABLE;
    } else {
        player->actor.colChkInfo.mass = 50;
    }

    Collider_ResetCylinderAC(play, &player->cylinder.base);
}

void DummyPlayer_Draw(Actor* actor, PlayState* play) {
    Player* player = (Player*)actor;

    if (!MMAnchor::Instance->clients.contains(DUMMY_CLIENT_ID)) {
        Actor_Kill(actor);
        return;
    }

    MMAnchorClient& client = MMAnchor::Instance->clients[DUMMY_CLIENT_ID];

    if (client.sceneId != gPlayState->sceneId || !client.online || !client.isSaveLoaded) {
        return;
    }

    Player_DrawGameplay(play, player, 1, gCullBackDList, Player_OverrideLimbDrawGameplayDefault);
}

void DummyPlayer_Destroy(Actor* actor, PlayState* play) {
}

#endif // COMBO_BUILD
