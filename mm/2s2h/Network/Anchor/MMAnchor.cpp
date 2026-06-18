#include "MMAnchor.h"
#ifdef COMBO_BUILD

#include <spdlog/spdlog.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/NameTag/NameTag.h"
#include "BenJsonConversions.hpp" // to_json/from_json for Vec3f/Vec3s/PosRot

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "build.h" // declares gGitCommitHash
extern PlayState* gPlayState;
}

// Shared CVar keys (process-global libultraship store): OOT's Anchor menu writes these and MM reads
// the same values. CVAR_REMOTE_ANCHOR("X") expands to "gRemote.Anchor.X" in ComboShip's SoH; MM has
// no access to that macro, so the literals are spelled out here.
static constexpr const char* kCvarName = "gRemote.Anchor.Name";
static constexpr const char* kCvarColor = "gRemote.Anchor.Color.Value";
static constexpr const char* kCvarTeamId = "gRemote.Anchor.TeamId";
static constexpr const char* kCvarLastClientId = "gRemote.Anchor.LastClientId";

// Packet type strings — must match SoH's Anchor exactly for cross-client interop.
static const std::string PKT_ALL_CLIENT_STATE = "ALL_CLIENT_STATE";
static const std::string PKT_UPDATE_CLIENT_STATE = "UPDATE_CLIENT_STATE";
static const std::string PKT_PLAYER_UPDATE = "PLAYER_UPDATE";
static const std::string PKT_DAMAGE_PLAYER = "DAMAGE_PLAYER";

// Launcher-registered outbound transport (set via MM_SetAnchorSend). Null until the exe wires it.
extern "C" {
void (*gMMComboAnchorSend)(const char* json) = nullptr;
}

MMAnchor* MMAnchor::Instance = nullptr;

// MARK: - Lifecycle

void MMAnchor::Activate() {
    isActive = true;
    // Adopt the shared connection's client id (OOT caches it on ALL_CLIENT_STATE; same socket => same
    // id). Without this MM stamps outbound packets with clientId 0 and peers drop them.
    if (ownClientId == 0) {
        ownClientId = (uint32_t)CVarGetInteger(kCvarLastClientId, 0);
    }
    RegisterHooks();
    SendUpdateClientState();    // announce MM presence / live location
    shouldRefreshActors = true; // spawn puppets for already-known peers
    SPDLOG_INFO("[MMAnchor] activated (ownClientId={})", ownClientId);
}

void MMAnchor::Deactivate() {
    isActive = false;
    SPDLOG_INFO("[MMAnchor] deactivated");
}

bool MMAnchor::IsSaveLoaded() {
    return gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr;
}

void MMAnchor::RegisterHooks() {
    if (hooksRegistered) {
        return;
    }
    hooksRegistered = true;

    // Hooks are registered once and gated by isActive (rather than COND_HOOK's connect/disconnect
    // re-registration), since MM's connection lifecycle is the launcher's, not ours.

    // Re-announce presence + request puppet refresh whenever a scene loads.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([this](s8 sceneId, s8 spawnNum) {
        if (!isActive) {
            return;
        }
        SendUpdateClientState();
        shouldRefreshActors = true;
    });

    // Disguise hook: a freshly spawned ACTOR_PLAYER during RefreshClientActors becomes a puppet.
    GameInteractor::Instance->RegisterGameHookForID<GameInteractor::ShouldActorInit>(
        ACTOR_PLAYER, [this](Actor* actor, bool* should) {
            if (!isActive || !refreshingActors) {
                return;
            }
            // The actor was already added to the ACTORCAT_PLAYER list; move it to NPC and rebind.
            Actor_ChangeCategory(gPlayState, &gPlayState->actorCtx, actor, ACTORCAT_NPC);
            actor->id = ACTOR_ITEM_INBOX;
            actor->category = ACTORCAT_NPC;
            actor->init = DummyPlayer_Init;
            actor->update = DummyPlayer_Update;
            actor->draw = DummyPlayer_Draw;
            actor->destroy = DummyPlayer_Destroy;
        });

    // Local-player update: broadcast our pose and service a pending puppet refresh. Puppets are
    // ACTOR_ITEM_INBOX, so this ACTOR_PLAYER-filtered hook only fires for the real local player.
    GameInteractor::Instance->RegisterGameHookForID<GameInteractor::OnActorUpdate>(
        ACTOR_PLAYER, [this](Actor* actor) {
            if (!isActive) {
                return;
            }
            SendPlayerUpdate();
            if (shouldRefreshActors) {
                shouldRefreshActors = false;
                RefreshClientActors();
            }
        });

    // Drain inbound packets on the MM game thread (the launcher's receive thread only enqueues).
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateUpdate>([this]() {
        if (isActive) {
            ProcessIncomingPacketQueue();
        }
    });
}

// MARK: - Transport

void MMAnchor::SendJson(nlohmann::json payload) {
    if (!isActive || gMMComboAnchorSend == nullptr) {
        return;
    }
    if (ownClientId == 0) {
        ownClientId = (uint32_t)CVarGetInteger(kCvarLastClientId, 0); // late-cache fallback
    }
    payload["clientId"] = ownClientId;
    gMMComboAnchorSend(payload.dump().c_str());
}

void MMAnchor::OnIncomingJson(const std::string& payload) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(payload);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[MMAnchor] failed to parse json: {}", e.what());
        return;
    }
    if (!j.contains("type")) {
        return;
    }
    std::lock_guard<std::mutex> lock(incomingMutex);
    incomingQueue.push(std::move(j));
}

void MMAnchor::ProcessIncomingPacketQueue() {
    std::queue<nlohmann::json> toProcess;
    {
        std::lock_guard<std::mutex> lock(incomingMutex);
        toProcess.swap(incomingQueue);
    }
    while (!toProcess.empty()) {
        nlohmann::json payload = toProcess.front();
        toProcess.pop();
        std::string type = payload["type"].get<std::string>();
        try {
            if (type == PKT_ALL_CLIENT_STATE) {
                HandlePacket_AllClientState(payload);
            } else if (type == PKT_UPDATE_CLIENT_STATE) {
                HandlePacket_UpdateClientState(payload);
            } else if (type == PKT_PLAYER_UPDATE) {
                HandlePacket_PlayerUpdate(payload);
            } else if (type == PKT_DAMAGE_PLAYER) {
                HandlePacket_DamagePlayer(payload);
            }
            // Item/flag packet types handled in later Phase-2 increments.
        } catch (const std::exception& e) {
            SPDLOG_ERROR("[MMAnchor] exception handling packet {}: {}", type, e.what());
        }
    }
}

// MARK: - Client state

nlohmann::json MMAnchor::PrepClientState() {
    nlohmann::json p;
    p["name"] = CVarGetString(kCvarName, "");
    Color_RGB8 c = CVarGetColor24(kCvarColor, { 100, 255, 100 });
    p["color"] = { { "r", c.r }, { "g", c.g }, { "b", c.b } };
    p["clientVersion"] = (char*)gGitCommitHash;
    p["teamId"] = CVarGetString(kCvarTeamId, "default");
    p["online"] = true;

    const bool saveLoaded = IsSaveLoaded();
    if (saveLoaded) {
        s16 rawScene = gPlayState->sceneId;
        p["seed"] = 0; // TODO Phase 3: MM rando seed
        p["isSaveLoaded"] = true;
        p["isGameComplete"] = false;
        // Two scene fields: namespaced "sceneNum" for OOT's roster display (so OOT shows MM peers as
        // "Majora's Mask"), and raw "sceneId" for MM peers' same-scene puppet matching.
        p["sceneNum"] = MM_ANCHOR_SCENE_NAMESPACE + (int32_t)rawScene;
        p["sceneId"] = rawScene;
        p["curRoomNum"] = (int32_t)gPlayState->roomCtx.curRoom.num;
        p["entrance"] = gSaveContext.save.entrance;
        p["entranceIndex"] = MM_ANCHOR_SCENE_NAMESPACE;
    } else {
        p["seed"] = 0;
        p["isSaveLoaded"] = false;
        p["isGameComplete"] = false;
        p["sceneNum"] = MM_ANCHOR_SCENE_NAMESPACE;
        p["sceneId"] = (int32_t)SCENE_MAX;
        p["curRoomNum"] = -1;
        p["entrance"] = 0;
        p["entranceIndex"] = MM_ANCHOR_SCENE_NAMESPACE;
    }
    return p;
}

void MMAnchor::SendUpdateClientState() {
    nlohmann::json payload;
    payload["type"] = PKT_UPDATE_CLIENT_STATE;
    payload["state"] = PrepClientState();
    SendJson(payload);
}

static void ReadClientStateInto(MMAnchorClient& c, uint32_t clientId, const nlohmann::json& s) {
    c.clientId = clientId;
    c.name = s.value("name", c.name.empty() ? std::string("???") : c.name);
    c.clientVersion = s.value("clientVersion", c.clientVersion);
    c.teamId = s.value("teamId", c.teamId);
    c.online = s.value("online", c.online);
    c.seed = s.value("seed", c.seed);
    c.isSaveLoaded = s.value("isSaveLoaded", c.isSaveLoaded);
    c.isGameComplete = s.value("isGameComplete", c.isGameComplete);
    c.sceneId = (s16)s.value("sceneId", (int32_t)c.sceneId); // RAW scene for puppet matching
    c.entrance = s.value("entrance", c.entrance);
}

void MMAnchor::HandlePacket_AllClientState(const nlohmann::json& payload) {
    if (!payload.contains("state") || !payload["state"].is_array()) {
        return;
    }
    for (const auto& entry : payload["state"]) {
        uint32_t clientId = entry.value("clientId", (uint32_t)0);
        MMAnchorClient& c = clients[clientId];
        ReadClientStateInto(c, clientId, entry);
        c.self = entry.value("self", false);
        if (c.self) {
            ownClientId = clientId;
        }
    }
    shouldRefreshActors = true;
    SPDLOG_INFO("[MMAnchor] ALL_CLIENT_STATE: {} client(s), ownClientId={}", clients.size(), ownClientId);
}

void MMAnchor::HandlePacket_UpdateClientState(const nlohmann::json& payload) {
    uint32_t clientId = payload.value("clientId", (uint32_t)0);
    if (!payload.contains("state")) {
        return;
    }
    MMAnchorClient& c = clients[clientId];
    ReadClientStateInto(c, clientId, payload["state"]);
}

// MARK: - Player update (per-frame pose; canonical field set from 2S2H PR #1349)

void MMAnchor::SendPlayerUpdate() {
    if (!IsSaveLoaded()) {
        return;
    }
    // Only send to peers in the SAME raw scene (matches canonical; avoids spamming cross-scene /
    // cross-game peers). Bail early if there are none.
    uint32_t sameScene = 0;
    for (auto& [clientId, client] : clients) {
        if (client.sceneId == gPlayState->sceneId && client.online && client.isSaveLoaded && !client.self) {
            sameScene++;
        }
    }
    if (sameScene == 0) {
        return;
    }

    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json payload;
    payload["type"] = PKT_PLAYER_UPDATE;
    payload["sceneId"] = gPlayState->sceneId;
    payload["entrance"] = gSaveContext.save.entrance;
    payload["roomIndex"] = gPlayState->roomCtx.curRoom.num;
    payload["transformation"] = player->transformation;
    payload["posRot"]["pos"] = player->actor.world.pos;
    payload["posRot"]["rot"] = player->actor.shape.rot;
    // Serialize the 159-byte joint buffers as plain int arrays (nlohmann reserves std::vector<u8>
    // for its binary type, so a u8 array/vector round-trip fails to compile). Wire shape is an
    // array of numbers either way.
    {
        const u8* jt = (const u8*)&player->jointTableBuffer;
        const u8* ujt = (const u8*)&player->jointTableUpperBuffer;
        nlohmann::json jtArr = nlohmann::json::array();
        nlohmann::json ujtArr = nlohmann::json::array();
        for (int i = 0; i < 159; i++) {
            jtArr.push_back((int)jt[i]);
            ujtArr.push_back((int)ujt[i]);
        }
        payload["jointTable"] = jtArr;
        payload["upperJointTable"] = ujtArr;
    }
    payload["currentMask"] = player->currentMask;
    payload["rightHandType"] = player->rightHandType;
    payload["leftHandType"] = player->leftHandType;
    payload["currentShield"] = player->currentShield;
    payload["sheathType"] = player->sheathType;
    payload["heldItemAction"] = player->heldItemAction;
    payload["heldItemId"] = player->heldItemId;
    payload["itemAction"] = player->itemAction;
    payload["stateFlags1"] = player->stateFlags1;
    payload["stateFlags2"] = player->stateFlags2;
    payload["stateFlags3"] = player->stateFlags3;
    payload["unk_B0C"] = player->unk_B0C;
    payload["unk_B28"] = player->unk_B28;
    payload["unk_ACC"] = player->unk_ACC;
    payload["invincibilityTimer"] = player->invincibilityTimer;
    payload["quiet"] = true;

    for (auto& [clientId, client] : clients) {
        if (client.sceneId == gPlayState->sceneId && client.online && client.isSaveLoaded && !client.self) {
            payload["targetClientId"] = clientId;
            SendJson(payload);
        }
    }
}

void MMAnchor::HandlePacket_PlayerUpdate(const nlohmann::json& payload) {
    uint32_t clientId = payload.value("clientId", (uint32_t)0);
    if (clientId == 0 || clientId == ownClientId || !clients.contains(clientId)) {
        return;
    }
    auto& client = clients[clientId];

    if (client.transformation != payload.value("transformation", (uint8_t)0)) {
        shouldRefreshActors = true; // form changed -> skeleton must be reallocated (respawn)
    }

    client.sceneId = payload.value("sceneId", (s16)SCENE_MAX);
    client.entrance = payload.value("entrance", (s32)0);
    client.transformation = payload.value("transformation", (uint8_t)0);
    // No from_json<PosRot> in this project's BenJsonConversions — read pos/rot via the Vec3f/Vec3s
    // converters that do exist.
    if (payload.contains("posRot")) {
        const auto& pr = payload["posRot"];
        if (pr.contains("pos")) {
            client.posRot.pos = pr["pos"].get<Vec3f>();
        }
        if (pr.contains("rot")) {
            client.posRot.rot = pr["rot"].get<Vec3s>();
        }
    }
    if (payload.contains("jointTable") && payload["jointTable"].is_array()) {
        const auto& arr = payload["jointTable"];
        for (int i = 0; i < 159 && i < (int)arr.size(); i++) {
            client.jointTable[i] = (u8)arr[i].get<int>();
        }
    }
    if (payload.contains("upperJointTable") && payload["upperJointTable"].is_array()) {
        const auto& arr = payload["upperJointTable"];
        for (int i = 0; i < 159 && i < (int)arr.size(); i++) {
            client.upperJointTable[i] = (u8)arr[i].get<int>();
        }
    }
    client.currentMask = payload.value("currentMask", (uint8_t)0);
    client.rightHandType = payload.value("rightHandType", (uint8_t)0);
    client.leftHandType = payload.value("leftHandType", (uint8_t)0);
    client.currentShield = payload.value("currentShield", (int8_t)0);
    client.sheathType = payload.value("sheathType", (uint8_t)0);
    client.heldItemAction = payload.value("heldItemAction", (int8_t)0);
    client.heldItemId = payload.value("heldItemId", (uint8_t)0);
    client.itemAction = payload.value("itemAction", (int8_t)0);
    client.stateFlags1 = payload.value("stateFlags1", (uint32_t)0);
    client.stateFlags2 = payload.value("stateFlags2", (uint32_t)0);
    client.stateFlags3 = payload.value("stateFlags3", (uint32_t)0);
    client.unk_B0C = payload.value("unk_B0C", 0.0f);
    client.unk_B28 = payload.value("unk_B28", (int16_t)0);
    client.unk_ACC = payload.value("unk_ACC", (int16_t)0);
    client.invincibilityTimer = payload.value("invincibilityTimer", (int8_t)0);
}

// MARK: - PvP damage (stub plumbing; full PvP is a later increment)

void MMAnchor::SendPacket_DamagePlayer(uint32_t clientId, uint8_t damageEffect, uint8_t damage) {
    nlohmann::json payload;
    payload["type"] = PKT_DAMAGE_PLAYER;
    payload["targetClientId"] = clientId;
    payload["damageEffect"] = damageEffect;
    payload["damage"] = damage;
    SendJson(payload);
}

void MMAnchor::HandlePacket_DamagePlayer(const nlohmann::json& payload) {
    // Applying damage to the local player is a later increment; plumbing kept for parity.
}

// MARK: - Puppet refresh (spawn ACTOR_PLAYER per peer; ShouldActorInit re-tags them)

void MMAnchor::RefreshClientActors() {
    if (!IsSaveLoaded()) {
        return;
    }

    // Kill existing puppets.
    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].first;
    while (actor != NULL) {
        Actor* next = actor->next;
        if (actor->id == ACTOR_ITEM_INBOX && actor->update == DummyPlayer_Update) {
            NameTag_RemoveAllForActor(actor);
            Actor_Kill(actor);
        }
        actor = next;
    }

    actorIndexToClientId.clear();
    refreshingActors = true;
    for (auto& [clientId, client] : clients) {
        if (!client.online || client.self) {
            continue;
        }
        actorIndexToClientId.push_back(clientId);
        // params = index into actorIndexToClientId; DummyPlayer_Init reads it back to find the client.
        Actor* dummy = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, client.posRot.pos.x,
                                   client.posRot.pos.y, client.posRot.pos.z, client.posRot.rot.x,
                                   client.posRot.rot.y, client.posRot.rot.z, (int)actorIndexToClientId.size() - 1);
        client.player = (Player*)dummy;
    }
    refreshingActors = false;
}

// MARK: - Launcher-facing C ABI (mirrors soh's SOH_Anchor_* exports)

extern "C" __declspec(dllexport) void MM_SetAnchorSend(void (*cb)(const char*)) {
    gMMComboAnchorSend = cb;
}

extern "C" __declspec(dllexport) void MM_Anchor_RecvJson(const char* json) {
    if (MMAnchor::Instance && json) {
        MMAnchor::Instance->OnIncomingJson(json);
    }
}

extern "C" __declspec(dllexport) void MM_Anchor_Activate(void) {
    if (MMAnchor::Instance == nullptr) {
        MMAnchor::Instance = new MMAnchor();
    }
    MMAnchor::Instance->Activate();
}

extern "C" __declspec(dllexport) void MM_Anchor_Deactivate(void) {
    if (MMAnchor::Instance) {
        MMAnchor::Instance->Deactivate();
    }
}

#endif // COMBO_BUILD
