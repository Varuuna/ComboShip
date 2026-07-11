#include "Anchor.h"
#include <nlohmann/json.hpp>
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/nametag.h"
#include "soh/ObjectExtension/ObjectExtension.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Notification/Notification.h"

extern "C" {
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

#ifdef COMBO_BUILD
// Issue #3: cross-game item delivery over Anchor. ComboShip-private packet type; the public hm64
// server relays unknown types peer-to-peer, so no server change is needed. Implemented as free
// functions (using only Anchor's public members) to keep the vendored Anchor footprint minimal —
// the only edit to the class's dispatch is one branch in ProcessIncomingPacketQueue. The launcher
// seams (defined in OTRGlobals.cpp) route a delivered item into the TARGET game and mark the SOURCE
// check obtained. See docs/UPSTREAM_MERGES.md.
static const std::string COMBO_CROSS_ITEM = "COMBO_CROSS_ITEM";
extern "C" void (*gComboCrossDeliver)(int targetGame, const char* itemName);
extern "C" void (*gComboMarkForeignObtained)(int srcGame, const char* checkName);

// Broadcast a locally-collected foreign item to teammates (called from hook_handlers.cpp). No-op
// when Anchor is disconnected or item sync is off.
extern "C" void Anchor_BroadcastCrossItem(int targetGame, const char* itemName, const char* srcCheckName) {
    if (!Anchor::Instance || !Anchor::Instance->isConnected || !itemName || !srcCheckName) {
        return;
    }
    if (!Anchor::Instance->roomState.syncItemsAndFlags) {
        return;
    }
    nlohmann::json payload;
    payload["type"] = COMBO_CROSS_ITEM;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["targetGame"] = targetGame; // 0 = OOT, 1 = MM (item's home game)
    payload["srcGame"] = 0;             // collected in OOT
    payload["itemName"] = itemName;     // neutral CrossForeign name, in the target game's namespace
    payload["srcCheckName"] = srcCheckName;
    Anchor::Instance->SendJsonToRemote(payload);
}

// Apply a cross-game item received from a teammate. Routes through the launcher (DeliverCrossItem),
// which grants into the target game's resident save; the grant export bypasses the check-collect
// path, so this never re-broadcasts.
static void Anchor_HandleCrossItemPacket(const nlohmann::json& payload) {
    if (!Anchor::Instance || !Anchor::Instance->roomState.syncItemsAndFlags) {
        return;
    }
    uint32_t clientId = payload.value("clientId", (uint32_t)0);
    if (clientId == Anchor::Instance->ownClientId) {
        return; // never re-apply our own broadcast
    }
    if (payload.value("targetTeamId", std::string("default")) !=
        CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default")) {
        return; // shared progression is per-team
    }
    int targetGame = payload.value("targetGame", 0);
    int srcGame = payload.value("srcGame", 0);
    std::string itemName = payload.value("itemName", std::string());
    std::string srcCheckName = payload.value("srcCheckName", std::string());
    if (itemName.empty()) {
        return;
    }
    if (gComboCrossDeliver) {
        gComboCrossDeliver(targetGame, itemName.c_str());
    }
    if (gComboMarkForeignObtained && !srcCheckName.empty()) {
        gComboMarkForeignObtained(srcGame, srcCheckName.c_str());
    }
    // This handler only runs while OOT is the active game, so a targetGame==0 item is one the OOT
    // player just received — announce it (the save-only grant is otherwise silent).
    if (targetGame == 0) {
        std::string name = itemName;
        auto it = Rando::StaticData::itemNameToEnum.find(itemName);
        if (it != Rando::StaticData::itemNameToEnum.end()) {
            name = Rando::StaticData::RetrieveItem(it->second).GetName().english;
        }
        Notification::Emit({ .message = "Received:", .suffix = name });
    }
}
#endif

// MARK: - Overrides

void Anchor::Enable() {
    Network::Enable(CVarGetString(CVAR_REMOTE_ANCHOR("Host"), "anchor.hm64.org"),
                    CVarGetInteger(CVAR_REMOTE_ANCHOR("Port"), 43383));
    ownClientId = CVarGetInteger(CVAR_REMOTE_ANCHOR("LastClientId"), 0);
    roomState.ownerClientId = 0;
}

void Anchor::Disable() {
    Network::Disable();

    clients.clear();
    RefreshClientActors();
}

void Anchor::OnConnected() {
    SendPacket_Handshake();
    RegisterHooks();

    if (IsSaveLoaded()) {
        SendPacket_RequestTeamState();
    }
}

void Anchor::OnDisconnected() {
    RegisterHooks();
}

void Anchor::ProcessOutgoingPackets() {
    // Copy all queued packets while holding the lock, then send them after releasing
    std::queue<nlohmann::json> packetsToSend;
    {
        std::lock_guard<std::mutex> lock(outgoingPacketQueueMutex);
        packetsToSend.swap(outgoingPacketQueue);
    }

    // Send packets without holding the lock
    while (!packetsToSend.empty()) {
        nlohmann::json payload = packetsToSend.front();
        packetsToSend.pop();

        if (!payload.contains("quiet")) {
            SPDLOG_DEBUG("[Anchor] Sending payload:\n{}", payload.dump());
        }
        Network::SendJsonToRemote(payload);
    }
}

void Anchor::SendJsonToRemote(nlohmann::json payload) {
    if (!isConnected) {
        return;
    }

    payload["clientId"] = ownClientId;
    if (!payload.contains("quiet")) {
        SPDLOG_DEBUG("[Anchor] Queuing payload:\n{}", payload.dump());
    }

#ifdef COMBO_BUILD
    // ComboShip: the launcher owns the socket and its own thread-safe outgoing queue, so hand every
    // packet straight to it. There is no game-side network thread to drain a local queue under the
    // combo build (ProcessOutgoingPackets is never pumped). See docs/UPSTREAM_MERGES.md.
    Network::SendJsonToRemote(payload);
    return;
#else
    if (payload["type"] == HANDSHAKE) {
        Network::SendJsonToRemote(payload);
        return;
    }

    // Queue the packet to be sent on the network thread
    std::lock_guard<std::mutex> lock(outgoingPacketQueueMutex);
    outgoingPacketQueue.push(payload);
#endif
}

void Anchor::OnIncomingJson(nlohmann::json payload) {
    // If it doesn't contain a type, it's not a valid payload
    if (!payload.contains("type")) {
        return;
    }

    // If it's not a quiet payload, log it
    if (!payload.contains("quiet")) {
        SPDLOG_DEBUG("[Anchor] Received payload:\n{}", payload.dump());
    }

    std::string packetType = payload["type"].get<std::string>();

    // Ignore packets from mismatched clients, except for ALL_CLIENT_STATE, UPDATE_CLIENT_STATE, and PLAYER_UPDATE
    if (packetType != ALL_CLIENT_STATE && packetType != UPDATE_CLIENT_STATE && packetType != PLAYER_UPDATE) {
        if (payload.contains("clientId")) {
            uint32_t clientId = payload["clientId"].get<uint32_t>();
            if (clients.contains(clientId) && clients[clientId].clientVersion != clientVersion) {
                return;
            }
        }
    }

    // Queue all packets to be processed on the game thread
    std::lock_guard<std::mutex> lock(incomingPacketQueueMutex);
    incomingPacketQueue.push(payload);
}

void Anchor::ProcessIncomingPacketQueue() {
    // Copy all queued packets while holding the lock, then process them after releasing
    std::queue<nlohmann::json> packetsToProcess;
    {
        std::lock_guard<std::mutex> lock(incomingPacketQueueMutex);
        packetsToProcess.swap(incomingPacketQueue);
    }

    // Process packets without holding the lock
    while (!packetsToProcess.empty()) {
        nlohmann::json payload = packetsToProcess.front();
        packetsToProcess.pop();

        std::string packetType = payload["type"].get<std::string>();

        isProcessingIncomingPacket = true;

        try {
            // packetType here is a string so we can't use a switch statement
            if (packetType == ALL_CLIENT_STATE)
                HandlePacket_AllClientState(payload);
            else if (packetType == DAMAGE_PLAYER)
                HandlePacket_DamagePlayer(payload);
            else if (packetType == DISABLE_ANCHOR)
                HandlePacket_DisableAnchor(payload);
            else if (packetType == ENTRANCE_DISCOVERED)
                HandlePacket_EntranceDiscovered(payload);
            else if (packetType == GAME_COMPLETE)
                HandlePacket_GameComplete(payload);
            else if (packetType == GIVE_ITEM)
                HandlePacket_GiveItem(payload);
#ifdef COMBO_BUILD
            else if (packetType == COMBO_CROSS_ITEM)
                Anchor_HandleCrossItemPacket(payload);
#endif
            else if (packetType == OCARINA_SFX)
                HandlePacket_OcarinaSfx(payload);
            else if (packetType == PLAYER_UPDATE)
                HandlePacket_PlayerUpdate(payload);
            else if (packetType == PLAYER_SFX)
                HandlePacket_PlayerSfx(payload);
            else if (packetType == UPDATE_TEAM_STATE)
                HandlePacket_UpdateTeamState(payload);
            else if (packetType == REQUEST_TEAM_STATE)
                HandlePacket_RequestTeamState(payload);
            else if (packetType == REQUEST_TELEPORT)
                HandlePacket_RequestTeleport(payload);
            else if (packetType == SERVER_MESSAGE)
                HandlePacket_ServerMessage(payload);
            else if (packetType == SET_CHECK_STATUS)
                HandlePacket_SetCheckStatus(payload);
            else if (packetType == SET_FLAG)
                HandlePacket_SetFlag(payload);
            else if (packetType == TELEPORT_TO)
                HandlePacket_TeleportTo(payload);
            else if (packetType == UNSET_FLAG)
                HandlePacket_UnsetFlag(payload);
            else if (packetType == UPDATE_BEANS_COUNT)
                HandlePacket_UpdateBeansCount(payload);
            else if (packetType == UPDATE_CLIENT_STATE)
                HandlePacket_UpdateClientState(payload);
            else if (packetType == UPDATE_ROOM_STATE)
                HandlePacket_UpdateRoomState(payload);
            else if (packetType == UPDATE_DUNGEON_ITEMS)
                HandlePacket_UpdateDungeonItems(payload);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("[Anchor] Exception while processing incoming packet {}", e.what());
            SPDLOG_ERROR("[Anchor] Packet: {}", payload.dump());
        }

        isProcessingIncomingPacket = false;
    }
}

#ifdef COMBO_BUILD
// A6: called on the ACTIVE sibling's game thread while OOT is dormant. Apply only save-data-only,
// dormant-safe co-op items (GIVE_ITEM); OOT's Item_Give/Randomizer_Item_Give write gSaveContext and
// null-guard gPlayState (same rationale as SOH_GrantCrossItem). MM-shaped GIVE_ITEM (no modId) throws
// and is skipped; cross-items to a dormant OOT are applied by the active game's cross-item handler.
void Anchor::PumpDormant() {
    std::queue<nlohmann::json> packetsToProcess;
    {
        std::lock_guard<std::mutex> lock(incomingPacketQueueMutex);
        packetsToProcess.swap(incomingPacketQueue);
    }
    while (!packetsToProcess.empty()) {
        nlohmann::json payload = packetsToProcess.front();
        packetsToProcess.pop();
        if (payload.value("type", std::string()) != GIVE_ITEM) {
            continue; // drop presence/puppet/team-state while dormant (re-requested on activation)
        }
        isProcessingIncomingPacket = true;
        try {
            HandlePacket_GiveItem(payload);
        } catch (const std::exception& e) { SPDLOG_ERROR("[Anchor] dormant apply exception: {}", e.what()); }
        isProcessingIncomingPacket = false;
    }
}
#endif

// MARK: - Misc/Helpers

// Kills all existing anchor actors and respawns them with the new client data

struct DummyPlayerClientId {
    uint32_t clientId = 0;
};
static ObjectExtension::Register<DummyPlayerClientId> DummyPlayerClientIdRegister;

uint32_t Anchor::GetDummyPlayerClientId(const Actor* actor) {
    const DummyPlayerClientId* clientId = ObjectExtension::GetInstance().Get<DummyPlayerClientId>(actor);
    return clientId != nullptr ? clientId->clientId : 0;
}

void Anchor::SetDummyPlayerClientId(const Actor* actor, uint32_t clientId) {
    ObjectExtension::GetInstance().Set<DummyPlayerClientId>(actor, DummyPlayerClientId{ clientId });
}

void Anchor::RefreshClientActors() {
    if (!IsSaveLoaded()) {
        return;
    }

    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head;

    while (actor != NULL) {
        if (actor->id == ACTOR_EN_OE2 && actor->update == DummyPlayer_Update) {
            NameTag_RemoveAllForActor(actor);
            Actor_Kill(actor);
        }
        actor = actor->next;
    }

    for (auto& [clientId, client] : clients) {
        if (!client.online || client.self) {
            continue;
        }

        spawningDummyPlayerForClientId = clientId;
        // We are using a hook `ShouldActorInit` to override the init/update/draw/destroy functions of the Player we
        // spawn We quickly store a mapping of "index" to clientId, then within the init function we use this to get the
        // clientId and store it on player->zTargetActiveTimer (unused s32 for the dummy) for convenience
        auto dummy =
            Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, client.posRot.pos.x, client.posRot.pos.y,
                        client.posRot.pos.z, client.posRot.rot.x, client.posRot.rot.y, client.posRot.rot.z, 0);
        client.player = (Player*)dummy;
    }
    spawningDummyPlayerForClientId = 0;
}

bool Anchor::IsSaveLoaded() {
    if (gPlayState == nullptr) {
        return false;
    }

    if (GET_PLAYER(gPlayState) == nullptr) {
        return false;
    }

    if (gSaveContext.fileNum < 0 || gSaveContext.fileNum > 2) {
        return false;
    }

    if (gSaveContext.gameMode != GAMEMODE_NORMAL) {
        return false;
    }

    return true;
}
