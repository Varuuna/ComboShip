#include "MMAnchor.h"
#ifdef COMBO_BUILD

#include <spdlog/spdlog.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/NameTag/NameTag.h"
#include "2s2h/Rando/Rando.h" // Rando::GiveItem / ConvertItem / CurrentJunkItem / RANDO_SAVE_CHECKS / IS_RANDO
#include "2s2h/Rando/CheckTracker/CheckTracker.h" // Phase 2d: re-derive after team-state apply
#include "2s2h/Rando/ActorBehavior/ActorBehavior.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h/BenGui/Notification.h"
#include "BenJsonConversions.hpp" // to_json/from_json for Vec3f/Vec3s/PosRot AND Save (Phase 2d)

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
static const std::string PKT_GIVE_ITEM = "GIVE_ITEM";
static const std::string PKT_UPDATE_TEAM_STATE = "UPDATE_TEAM_STATE";
static const std::string PKT_REQUEST_TEAM_STATE = "REQUEST_TEAM_STATE";
// Issue #3: cross-game item delivery. ComboShip-private packet type (the public server relays unknown
// types peer-to-peer, so no server change is needed).
static const std::string PKT_CROSS_ITEM = "COMBO_CROSS_ITEM";

// Launcher-registered outbound transport (set via MM_SetAnchorSend). Null until the exe wires it.
extern "C" {
void (*gMMComboAnchorSend)(const char* json) = nullptr;
}

// Issue #3: cross-game delivery seams, defined in BenPort.cpp and registered by the launcher. Route
// a received cross-game item into the TARGET game's save, and mark the SOURCE check obtained.
extern "C" void (*gMMComboCrossDeliver)(int targetGame, const char* itemName);
extern "C" void (*gMMComboMarkForeignObtained)(int srcGame, const char* checkName);

// ComboShip A6: launcher pump fn (set via MM_SetPumpDormant). The ACTIVE game calls it each frame so
// the launcher can drive the DORMANT sibling's save-affecting packet apply on this (game) thread.
extern "C" void (*gMMComboPumpDormant)() = nullptr;
// Dormant-safe give of a resolved MM rando item (BenPort.cpp): trap-defer or GiveItemForOracle + persist.
void Combo_MM_GiveDormantResolved(RandoItemId rid);

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
    if (IsSaveLoaded()) {
        SendPacket_RequestTeamState(); // connected while already in-game -> catch up to the team
    }
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
    GameInteractor::Instance->RegisterGameHookForID<GameInteractor::OnActorUpdate>(ACTOR_PLAYER, [this](Actor* actor) {
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
            // A6: while MM is foreground, drive the dormant sibling's dormant-safe apply on this thread.
            if (gMMComboPumpDormant) {
                gMMComboPumpDormant();
            }
        }
    });

    // Phase 2d: catch up to the team's progression when loading a file; push our state when we save.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSaveLoad>([this](s16 fileNum) {
        if (isActive) {
            SendPacket_RequestTeamState();
        }
    });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::AfterEndOfCycleSave>([this]() {
        if (isActive && gPlayState != nullptr) {
            SendPacket_UpdateTeamState(CVarGetString(kCvarTeamId, "default"));
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
            } else if (type == PKT_GIVE_ITEM) {
                HandlePacket_GiveItem(payload);
            } else if (type == PKT_CROSS_ITEM) {
                HandlePacket_CrossItem(payload);
            } else if (type == PKT_UPDATE_TEAM_STATE) {
                HandlePacket_UpdateTeamState(payload);
            } else if (type == PKT_REQUEST_TEAM_STATE) {
                HandlePacket_RequestTeamState(payload);
            }
            // Flag sync intentionally deferred (mirrors canonical, which stubs it).
        } catch (const std::exception& e) {
            SPDLOG_ERROR("[MMAnchor] exception handling packet {}: {}", type, e.what());
        }
    }
}

// A6: called on the ACTIVE sibling's game thread while MM is dormant. Drain the queue and apply only
// save-data-only, dormant-safe packets (co-op GIVE_ITEM). Presence/puppet/team-state are dropped —
// team-state re-runs OnFileLoad/ShipInit, which isn't safe while dormant, and is re-requested on MM
// activation anyway. gSaveContext here is MM's frozen save; MM isn't ticking, so no concurrent writer.
void MMAnchor::PumpDormant() {
    std::queue<nlohmann::json> toProcess;
    {
        std::lock_guard<std::mutex> lock(incomingMutex);
        toProcess.swap(incomingQueue);
    }
    while (!toProcess.empty()) {
        nlohmann::json payload = toProcess.front();
        toProcess.pop();
        try {
            if (payload.value("type", std::string()) == PKT_GIVE_ITEM) {
                ApplyDormantGiveItem(payload);
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("[MMAnchor] dormant apply exception: {}", e.what());
        }
    }
}

// A6: dormant-safe variant of HandlePacket_GiveItem — marks the check obtained and grants the item
// through the save-only path (no gPlayState). Junk is left unresolved (GiveItemForOracle no-ops it;
// CurrentJunkItem reads gPlayState and isn't dormant-safe), but its check is still registered.
void MMAnchor::ApplyDormantGiveItem(const nlohmann::json& payload) {
    if (!payload.contains("randoCheckId")) {
        return; // reject soh's GIVE_ITEM (modId shape, different item-id space)
    }
    if (payload.value("targetTeamId", std::string("default")) != CVarGetString(kCvarTeamId, "default")) {
        return;
    }
    if (payload.value("clientId", (uint32_t)0) == ownClientId) {
        return;
    }
    int32_t checkId = payload.value("randoCheckId", -1);
    RandoCheckId rc = (checkId >= 0 && checkId < RC_MAX) ? (RandoCheckId)checkId : RC_UNKNOWN;
    if (rc != RC_UNKNOWN && RANDO_SAVE_CHECKS[rc].obtained) {
        return; // idempotent: already collected locally
    }
    RandoItemId rid = Rando::ConvertItem((RandoItemId)payload.value("getItemId", (int)RI_JUNK), rc);
    if (rc != RC_UNKNOWN) {
        RANDO_SAVE_CHECKS[rc].obtained = true;
        RANDO_SAVE_CHECKS[rc].cycleObtained = true;
        RANDO_SAVE_CHECKS[rc].eligible = false;
    }
    Combo_MM_GiveDormantResolved(rid);
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
    // Re-announce now that we know our client id + the roster. This is what makes presence work when
    // Anchor is enabled while already in MM (no transition/scene-load to trigger the announce, and the
    // initial Activate announce ran before we had a client id).
    SendUpdateClientState();
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

// MARK: - Item sync (shared-progression co-op; Phase 2c)

void MMAnchor::SendPacket_GiveItem(int16_t randoItemId, int32_t randoCheckId) {
    if (!isActive || !roomState.syncItemsAndFlags) {
        return;
    }
    nlohmann::json payload;
    payload["type"] = PKT_GIVE_ITEM;
    payload["targetTeamId"] = CVarGetString(kCvarTeamId, "default");
    payload["getItemId"] = randoItemId;     // RAW rando item id; receiver ConvertItems for its own state
    payload["randoCheckId"] = randoCheckId; // so receivers can mark the check obtained (no double-collect)
    SendJson(payload);
}

void MMAnchor::HandlePacket_GiveItem(const nlohmann::json& payload) {
    if (!roomState.syncItemsAndFlags || !IsSaveLoaded()) {
        return;
    }
    uint32_t clientId = payload.value("clientId", (uint32_t)0);
    if (clientId == ownClientId) {
        return; // never re-apply our own broadcast
    }
    // Shared progression is per-team; ignore items for other teams.
    if (payload.value("targetTeamId", std::string("default")) != CVarGetString(kCvarTeamId, "default")) {
        return;
    }

    int32_t checkId = payload.value("randoCheckId", -1);
    RandoCheckId rc = (checkId >= 0 && checkId < RC_MAX) ? (RandoCheckId)checkId : RC_UNKNOWN;
    // Idempotent per check: if we already collected this check locally (both players reached it, or a
    // duplicate/late broadcast), don't grant its item again.
    if (rc != RC_UNKNOWN && RANDO_SAVE_CHECKS[rc].obtained) {
        return;
    }
    RandoItemId raw = (RandoItemId)payload.value("getItemId", (int)RI_JUNK);
    RandoItemId rid = Rando::ConvertItem(raw, rc);
    if (rid == RI_JUNK) {
        rid = Rando::CurrentJunkItem(rc);
    }

    // Guard against the grant path re-triggering a broadcast.
    applyingRemoteItem = true;
    Rando::GiveItem(rid);
    applyingRemoteItem = false;

    // Mark the originating check obtained so this client won't also collect it and double-grant.
    if (rc != RC_UNKNOWN) {
        RANDO_SAVE_CHECKS[rc].obtained = true;
        RANDO_SAVE_CHECKS[rc].cycleObtained = true;
        RANDO_SAVE_CHECKS[rc].eligible = false;
    }
}

// Called from the rando check-obtain seam (CheckQueue.cpp) under COMBO_BUILD when the LOCAL player
// collects a check. Broadcasts the raw item to teammates; no-op when Anchor is inactive or we're
// currently applying a remotely-received item (prevents a re-broadcast loop).
extern "C" void MMAnchor_BroadcastCheckItem(int randoCheckId, int randoItemId) {
    if (MMAnchor::Instance && MMAnchor::Instance->isActive && !MMAnchor::Instance->applyingRemoteItem) {
        MMAnchor::Instance->SendPacket_GiveItem((int16_t)randoItemId, randoCheckId);
    }
}

// MARK: - Cross-game item delivery (issue #3)

void MMAnchor::SendPacket_CrossItem(int targetGame, const char* itemName, const char* srcCheckName) {
    if (!isActive || !roomState.syncItemsAndFlags || !itemName || !srcCheckName) {
        return;
    }
    nlohmann::json payload;
    payload["type"] = PKT_CROSS_ITEM;
    payload["targetTeamId"] = CVarGetString(kCvarTeamId, "default");
    payload["targetGame"] = targetGame; // 0 = OOT, 1 = MM (item's home game)
    payload["srcGame"] = 1;             // collected in MM
    payload["itemName"] = itemName;     // neutral CrossForeign name, in the target game's namespace
    payload["srcCheckName"] = srcCheckName;
    SendJson(payload);
}

void MMAnchor::HandlePacket_CrossItem(const nlohmann::json& payload) {
    if (!roomState.syncItemsAndFlags) {
        return;
    }
    uint32_t clientId = payload.value("clientId", (uint32_t)0);
    if (clientId == ownClientId) {
        return; // never re-apply our own broadcast
    }
    // Shared progression is per-team; ignore items for other teams.
    if (payload.value("targetTeamId", std::string("default")) != CVarGetString(kCvarTeamId, "default")) {
        return;
    }
    int targetGame = payload.value("targetGame", 0);
    int srcGame = payload.value("srcGame", 0);
    std::string itemName = payload.value("itemName", std::string());
    std::string srcCheckName = payload.value("srcCheckName", std::string());
    if (itemName.empty()) {
        return;
    }
    // Grant into the TARGET game's save (routes through the launcher to whichever DLL owns it; the
    // grant export bypasses the check-collect path, so this won't re-broadcast).
    if (gMMComboCrossDeliver) {
        gMMComboCrossDeliver(targetGame, itemName.c_str());
    }
    // Mark the source check obtained so we won't physically collect it later and double-deliver.
    if (gMMComboMarkForeignObtained && !srcCheckName.empty()) {
        gMMComboMarkForeignObtained(srcGame, srcCheckName.c_str());
    }
    // This handler only runs while MM is the active game, so a targetGame==1 item is one the MM
    // player just received — announce it (the save-only grant is otherwise silent).
    if (targetGame == 1) {
        Notification::Emit({ .message = "Received:", .suffix = itemName });
    }
}

// Called from the rando foreign-check seam (CheckQueue.cpp) under COMBO_BUILD when the LOCAL player
// collects a check whose item belongs to the OTHER game. Shares it with teammates; no-op when Anchor
// is inactive.
extern "C" void MMAnchor_BroadcastCrossItem(int targetGame, const char* itemName, const char* srcCheckName) {
    if (MMAnchor::Instance && MMAnchor::Instance->isActive) {
        MMAnchor::Instance->SendPacket_CrossItem(targetGame, itemName, srcCheckName);
    }
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
                                   client.posRot.pos.y, client.posRot.pos.z, client.posRot.rot.x, client.posRot.rot.y,
                                   client.posRot.rot.z, (int)actorIndexToClientId.size() - 1);
        client.player = (Player*)dummy;
    }
    refreshingActors = false;
}

// MARK: - Team state resync (late-join / reconnect; Phase 2d, ported from 2S2H PR #1349)

void MMAnchor::SendPacket_RequestTeamState() {
    if (!isActive || !roomState.syncItemsAndFlags) {
        return;
    }
    nlohmann::json payload;
    payload["type"] = PKT_REQUEST_TEAM_STATE;
    payload["targetTeamId"] = CVarGetString(kCvarTeamId, "default");
    SendJson(payload);
}

void MMAnchor::HandlePacket_RequestTeamState(const nlohmann::json& payload) {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags) {
        return;
    }
    SendPacket_UpdateTeamState(CVarGetString(kCvarTeamId, "default"));
}

void MMAnchor::SendPacket_UpdateTeamState(const std::string& targetTeamId) {
    if (!isActive || !roomState.syncItemsAndFlags) {
        return;
    }
    nlohmann::json payload;
    payload["type"] = PKT_UPDATE_TEAM_STATE;
    payload["targetTeamId"] = targetTeamId;
    payload["queue"] = nlohmann::json::array(); // assume our team queue is now empty
    payload["state"] = gSaveContext.save;       // to_json(Save) from BenJsonConversions

    // Byte-reduction: replace the rando check array with a compact array-of-arrays. 7 fields here —
    // ComboShip's RandoSaveCheck has no multiWorldTeamIndex (shared-progression, not multiworld).
    if (IS_RANDO) {
        auto& rando = payload["state"]["shipSaveInfo"]["rando"];
        rando.erase("randoSaveChecks");
        rando["randoSaveChecksCopy"] = nlohmann::json::array();
        for (int i = 0; i < RC_MAX; i++) {
            nlohmann::json e = nlohmann::json::array();
            e[0] = RANDO_SAVE_CHECKS[i].randoItemId;
            e[1] = (u8)RANDO_SAVE_CHECKS[i].shuffled;
            e[2] = (u8)RANDO_SAVE_CHECKS[i].eligible;
            e[3] = (u8)RANDO_SAVE_CHECKS[i].cycleObtained;
            e[4] = (u8)RANDO_SAVE_CHECKS[i].obtained;
            e[5] = (u8)RANDO_SAVE_CHECKS[i].skipped;
            e[6] = RANDO_SAVE_CHECKS[i].price;
            rando["randoSaveChecksCopy"][i] = e;
        }
    }
    SendJson(payload);
}

void MMAnchor::HandlePacket_UpdateTeamState(nlohmann::json& payload) {
    if (!roomState.syncItemsAndFlags || !payload.contains("state")) {
        return;
    }

    // Unpack the compact rando check array back into the shape from_json(Save) expects.
    if (IS_RANDO && payload["state"]["shipSaveInfo"].contains("rando")) {
        auto stuff =
            payload["state"]["shipSaveInfo"]["rando"]["randoSaveChecksCopy"].get<std::vector<std::vector<s32>>>();
        for (int i = 0; i < RC_MAX; i++) {
            payload["state"]["shipSaveInfo"]["rando"]["randoSaveChecks"][i] = RandoSaveCheck{
                (RandoItemId)stuff[i][0], (bool)stuff[i][1], (bool)stuff[i][2], (bool)stuff[i][3],
                (bool)stuff[i][4],        (bool)stuff[i][5], (u16)stuff[i][6],
            };
        }
    }

    Save loadedData = payload["state"].get<Save>();

    // Restore bottle contents (unless Deku Princess).
    for (int i = 0; i < 6; i++) {
        if (gSaveContext.save.saveInfo.inventory.items[SLOT_BOTTLE_1 + i] != ITEM_NONE &&
            gSaveContext.save.saveInfo.inventory.items[SLOT_BOTTLE_1 + i] != ITEM_DEKU_PRINCESS) {
            loadedData.saveInfo.inventory.items[SLOT_BOTTLE_1 + i] =
                gSaveContext.save.saveInfo.inventory.items[SLOT_BOTTLE_1 + i];
        }
    }
    // Restore non-zero ammo, except beans.
    for (int i = 0; i < ARRAY_COUNT(gSaveContext.save.saveInfo.inventory.ammo); i++) {
        if (gSaveContext.save.saveInfo.inventory.ammo[i] != 0 && i != SLOT(ITEM_MAGIC_BEANS)) {
            loadedData.saveInfo.inventory.ammo[i] = gSaveContext.save.saveInfo.inventory.ammo[i];
        }
    }
    // Restore receiver-local fields that shouldn't be synced.
    loadedData.saveInfo.checksum = gSaveContext.save.saveInfo.checksum;
    loadedData.shipSaveInfo.fileCreatedAt = gSaveContext.save.shipSaveInfo.fileCreatedAt;
    memcpy(loadedData.saveInfo.playerData.newf, gSaveContext.save.saveInfo.playerData.newf,
           sizeof(loadedData.saveInfo.playerData.newf));
    memcpy(&loadedData.shipSaveInfo.dpadEquips, &gSaveContext.save.shipSaveInfo.dpadEquips,
           sizeof(loadedData.shipSaveInfo.dpadEquips));
    memcpy(loadedData.saveInfo.equips.cButtonSlots, gSaveContext.save.saveInfo.equips.cButtonSlots,
           sizeof(loadedData.saveInfo.equips.cButtonSlots));
    memcpy(loadedData.saveInfo.equips.buttonItems, gSaveContext.save.saveInfo.equips.buttonItems,
           sizeof(loadedData.saveInfo.equips.buttonItems));
    memcpy(loadedData.saveInfo.playerData.playerName, gSaveContext.save.saveInfo.playerData.playerName,
           sizeof(loadedData.saveInfo.playerData.playerName));
#ifdef COMBO_BUILD
    // Pending cross-world traps are receiver-local; a teammate's count must not clobber ours.
    loadedData.shipSaveInfo.rando.pendingTrapCount = gSaveContext.save.shipSaveInfo.rando.pendingTrapCount;
#endif

    // Commit only the two progression sub-structs; top-level Save fields (scene/entrance/time/day/
    // playerForm/cycle) are intentionally left untouched so the receiver isn't relocated.
    gSaveContext.save.saveInfo = loadedData.saveInfo;
    gSaveContext.save.shipSaveInfo = loadedData.shipSaveInfo;

    Notification::Emit({
        .message = "Save updated from team",
    });
    Rando::CheckTracker::OnFileLoad();
    Rando::ActorBehavior::OnFileLoad();
    ShipInit::Init("IS_RANDO");

    // Replay any packets queued on the server while we were away, through the normal incoming path.
    if (payload.contains("queue") && payload["queue"].is_array()) {
        std::lock_guard<std::mutex> lock(incomingMutex);
        for (auto& item : payload["queue"]) {
            try {
                incomingQueue.push(nlohmann::json::parse(item.get<std::string>()));
            } catch (const std::exception& e) {
                SPDLOG_ERROR("[MMAnchor] failed to parse queued team-state packet: {}", e.what());
            }
        }
    }
}

// MARK: - Launcher-facing C ABI (mirrors soh's SOH_Anchor_* exports)

extern "C" __declspec(dllexport) void MM_SetAnchorSend(void (*cb)(const char*)) {
    gMMComboAnchorSend = cb;
}

// A6: launcher registers its per-frame dormant-pump fn; MM calls it each active frame (see hook).
extern "C" __declspec(dllexport) void MM_SetPumpDormant(void (*cb)()) {
    gMMComboPumpDormant = cb;
}

// A6: launcher calls this (on the active sibling's thread) when MM is the dormant game.
extern "C" __declspec(dllexport) void MM_Anchor_PumpDormant(void) {
    if (MMAnchor::Instance) {
        MMAnchor::Instance->PumpDormant();
    }
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
