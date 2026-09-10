#include "core/ComboAnchorNet.h"

#include <atomic>
#include <queue>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL_net.h>

#include "core/ComboDllApi.h"

namespace ComboAnchor {
static std::thread sThread;
static std::atomic<bool> sEnabled{ false };
static std::atomic<bool> sConnected{ false };
static std::string sHost;
static uint16_t sPort = 0;
static std::mutex sOutMutex;
static std::queue<std::string> sOutQueue;
// Which game inbound packets are dispatched to. 0 = OOT, 1 = MM. Updated by the game-switch loop
// via SetActiveGame on each transition. Phase 3 will route per-packet by TARGET game instead.
static std::atomic<int> sActiveGame{ 0 };
// Finding 4: on-connect resync must run on the game thread (RequestResyncDormantSafe touches
// gPlayState/isDormantApply, which PumpDormant also mutates there). Set here, drained by PumpDormant.
static std::atomic<bool> sResyncPending{ false };

// Combo-owned Anchor roster/presence. A game's Anchor only drains packets while it's the foreground
// game, so its per-game roster goes stale when dormant. The launcher sees every packet on this
// never-dormant thread, so it keeps ONE always-live roster the room window reads (display-only; games
// keep their own roster for puppets/save-apply).
struct ClientInfo {
    uint32_t clientId = 0;
    std::string name = "???";
    uint8_t r = 255, g = 255, b = 255;
    std::string teamId = "default";
    bool online = false;
    uint32_t seed = 0;
    std::string clientVersion;
    bool isSaveLoaded = false;
    bool isGameComplete = false;
    int16_t rawScene = 0;
    int game = 0; // 0 = OOT, 1 = MM
};
struct RoomState {
    uint32_t ownerClientId = 0;
    int pvpMode = 1;
    int showLocationsMode = 1;
    int teleportMode = 1;
    int syncItemsAndFlags = 1;
};
static std::mutex sRosterMutex;
static std::map<uint32_t, ClientInfo> sRoster;
static RoomState sRoomState;
static uint32_t sOwnClientId = 0;

// Fill a ClientInfo from a client JSON object (schema mirrors soh PrepClientState / JsonConversions).
// MM namespaces its sceneNum (>= 1000) and tags packets originGame=="mm"; either flags the MM side.
static void ParseClient(const nlohmann::json& c, const std::string& originGame, ClientInfo& info) {
    info.clientId = c.value("clientId", (uint32_t)0);
    info.name = c.value("name", std::string("???"));
    if (c.contains("color") && c["color"].is_object()) {
        info.r = c["color"].value("r", 255);
        info.g = c["color"].value("g", 255);
        info.b = c["color"].value("b", 255);
    }
    info.clientVersion = c.value("clientVersion", std::string());
    info.teamId = c.value("teamId", std::string("default"));
    info.online = c.value("online", false);
    info.seed = c.value("seed", (uint32_t)0);
    info.isSaveLoaded = c.value("isSaveLoaded", false);
    info.isGameComplete = c.value("isGameComplete", false);
    int32_t sceneNum = c.value("sceneNum", 0);
    bool mm = originGame == "mm" || sceneNum >= 1000;
    info.game = mm ? 1 : 0;
    info.rawScene = (int16_t)(mm ? sceneNum - 1000 : sceneNum);
}

// Parse the presence/room packets into the roster (called on the receive thread, before forwarding).
static void UpdateRosterFromPacket(const std::string& packet) {
    try {
        auto pj = nlohmann::json::parse(packet);
        std::string type = pj.value("type", std::string());
        std::string origin = pj.value("originGame", std::string());
        if (type == "ALL_CLIENT_STATE") {
            std::lock_guard<std::mutex> lk(sRosterMutex);
            sRoster.clear();
            for (auto& c : pj.value("state", nlohmann::json::array())) {
                ClientInfo info;
                ParseClient(c, "", info); // array entries carry no originGame; sceneNum tags MM
                if (c.value("self", false))
                    sOwnClientId = info.clientId;
                sRoster[info.clientId] = info;
            }
        } else if (type == "UPDATE_CLIENT_STATE") {
            uint32_t cid = pj.value("clientId", (uint32_t)0);
            if (cid != 0 && pj.contains("state")) {
                std::lock_guard<std::mutex> lk(sRosterMutex);
                ClientInfo info;
                ParseClient(pj["state"], origin, info);
                info.clientId = cid;
                sRoster[cid] = info;
            }
        } else if (type == "UPDATE_ROOM_STATE" && pj.contains("state")) {
            auto s = pj["state"];
            std::lock_guard<std::mutex> lk(sRosterMutex);
            sRoomState.ownerClientId = s.value("ownerClientId", (uint32_t)0);
            sRoomState.pvpMode = s.value("pvpMode", 1);
            sRoomState.showLocationsMode = s.value("showLocationsMode", 1);
            sRoomState.teleportMode = s.value("teleportMode", 1);
            sRoomState.syncItemsAndFlags = s.value("syncItemsAndFlags", 1);
        }
        // UPDATE_TEAM_STATE (save blob) + PLAYER_UPDATE (high-rate puppet coords) are display-irrelevant.
    } catch (...) {}
}

// Roster snapshot for comboui's room window: { ownClientId, room{...}, clients[...] }. areaVisible +
// isOwner + self are resolved here (the launcher owns room-state); comboui resolves scene NAMES and
// version/seed mismatch itself. ownTeam comes from the self entry's teamId (== gRemote.Anchor.TeamId).
const char* Combo_Anchor_GetRoster() {
    static std::string cached;
    try {
        std::lock_guard<std::mutex> lk(sRosterMutex);
        std::string ownTeam = "default";
        auto selfIt = sRoster.find(sOwnClientId);
        if (selfIt != sRoster.end())
            ownTeam = selfIt->second.teamId;
        int showLoc = sRoomState.showLocationsMode;

        nlohmann::json clients = nlohmann::json::array();
        for (auto& [cid, c] : sRoster) {
            bool isOwnTeam = c.teamId == ownTeam;
            bool areaVisible = c.isSaveLoaded && (showLoc == 2 || (showLoc == 1 && isOwnTeam));
            nlohmann::json e;
            e["clientId"] = cid;
            e["name"] = c.name;
            e["color"] = { { "r", c.r }, { "g", c.g }, { "b", c.b } };
            e["teamId"] = c.teamId;
            e["online"] = c.online;
            e["self"] = (cid == sOwnClientId);
            e["game"] = c.game == 1 ? "mm" : "oot";
            e["rawScene"] = c.rawScene;
            e["isOwner"] = (cid == sRoomState.ownerClientId);
            e["isSaveLoaded"] = c.isSaveLoaded;
            e["isGameComplete"] = c.isGameComplete;
            e["areaVisible"] = areaVisible;
            e["clientVersion"] = c.clientVersion;
            e["seed"] = c.seed;
            clients.push_back(e);
        }
        nlohmann::json out;
        out["ownClientId"] = sOwnClientId;
        out["room"] = { { "ownerClientId", sRoomState.ownerClientId },
                        { "pvpMode", sRoomState.pvpMode },
                        { "showLocationsMode", showLoc },
                        { "teleportMode", sRoomState.teleportMode },
                        { "syncItemsAndFlags", sRoomState.syncItemsAndFlags } };
        out["clients"] = clients;
        cached = out.dump();
    } catch (...) { cached = "{}"; }
    return cached.c_str();
}

// Background loop: connect, then relay outbound packets and feed inbound packets to the active
// game. Mirrors soh's original Network::ReceiveFromServer framing (NUL-delimited JSON), only the
// socket now lives in the launcher so it persists across transitions.
static void ReceiveLoop() {
    IPaddress address;
    if (SDLNet_ResolveHost(&address, sHost.c_str(), sPort) == -1) {
        std::cerr << "[ComboShip][Anchor] ResolveHost failed: " << SDLNet_GetError() << std::endl;
        sEnabled = false;
        return;
    }

    std::string received;
    while (sEnabled) {
        TCPsocket socket = nullptr;
        while (sEnabled && !socket) {
            socket = SDLNet_TCP_Open(&address);
            if (!socket && sEnabled) {
                // Back off between attempts so an unreachable server doesn't spin a core at 100%.
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        if (!sEnabled) {
            if (socket)
                SDLNet_TCP_Close(socket);
            break;
        }

        received.clear();
        sConnected = true;
        // OOT's OnConnected sends the room HANDSHAKE (establishes our client id) regardless of
        // which game is foreground. If MM is the active game (e.g. we connected while already in
        // MM, or resumed straight into it), also activate MM so it announces its presence — MM
        // otherwise only announces on a transition or scene load, neither of which happens here.
        if (SOH_Anchor_OnConnected)
            SOH_Anchor_OnConnected();
        if (sActiveGame.load() == 1 && MM_Anchor_Activate)
            MM_Anchor_Activate();
        // Bug 2: full both-games resync on every (re)connect. Both requests go out unconditionally
        // (dormant-safe), so a late-joiner/reconnect pulls the peer's OOT AND MM progress, and this
        // client's own dormant sibling gets asked too. Deferred to the game thread (finding 4).
        sResyncPending.store(true);

        SDLNet_SocketSet set = SDLNet_AllocSocketSet(1);
        SDLNet_TCP_AddSocket(set, socket);

        while (sEnabled && sConnected) {
            int ready = SDLNet_CheckSockets(set, 0);
            if (ready == -1)
                break;

            // Drain outbound queue (packets handed to us by the game via Send()).
            std::queue<std::string> toSend;
            {
                std::lock_guard<std::mutex> lk(sOutMutex);
                toSend.swap(sOutQueue);
            }
            while (!toSend.empty()) {
                const std::string& p = toSend.front();
                // Include the NUL delimiter in the framing (matches Network::SendDataToRemote).
                SDLNet_TCP_Send(socket, p.c_str(), (int)p.size() + 1);
                toSend.pop();
            }

            if (ready == 0)
                continue;

            char buf[512];
            memset(buf, 0, sizeof(buf));
            int len = SDLNet_TCP_Recv(socket, buf, sizeof(buf));
            if (len <= 0)
                break;
            received.append(buf, len);

            size_t pos = received.find('\0');
            while (pos != std::string::npos) {
                std::string packet = received.substr(0, pos);
                received.erase(0, pos + 1);
                // Keep the always-live combo roster in sync before forwarding (fixes dormant staleness).
                UpdateRosterFromPacket(packet);
                // A6: feed every packet to BOTH games. Each RecvJson only enqueues (thread-safe). The
                // active game drains+handles it on its tick; the dormant game applies its save-affecting
                // subset via PumpDormant (driven by the active game's per-frame pump call), so a
                // teammate's progression registers in the dormant game's save live.
                if (SOH_Anchor_RecvJson)
                    SOH_Anchor_RecvJson(packet.c_str());
                if (MM_Anchor_RecvJson)
                    MM_Anchor_RecvJson(packet.c_str());
                pos = received.find('\0');
            }
        }

        SDLNet_FreeSocketSet(set);
        SDLNet_TCP_Close(socket);
        sConnected = false;
        if (SOH_Anchor_OnDisconnected)
            SOH_Anchor_OnDisconnected();
    }
}

// Registered into the game as the connect request (Network::Enable redirects here).
void Connect(const char* host, uint16_t port) {
    if (sEnabled)
        return;
    static bool sNetInit = false;
    if (!sNetInit) {
        SDLNet_Init();
        sNetInit = true;
    }
    sHost = host ? host : "";
    sPort = port;
    sEnabled = true;
    if (sThread.joinable())
        sThread.join();
    sThread = std::thread(ReceiveLoop);
}

// Registered into the game as the disconnect request (Network::Disable redirects here).
void Disconnect() {
    if (!sEnabled)
        return;
    sEnabled = false;
    sConnected = false;
    if (sThread.joinable())
        sThread.join();
    std::lock_guard<std::mutex> lk(sOutMutex);
    std::queue<std::string> empty;
    sOutQueue.swap(empty);
}

// Registered into the game as the send callback (Network::SendDataToRemote redirects here).
void Send(const char* json) {
    if (!json)
        return;
    // Our own scene/room-state is broadcast OUTBOUND (never echoed inbound), so parse it here too or the
    // self roster row would freeze at its join value.
    UpdateRosterFromPacket(json);
    std::lock_guard<std::mutex> lk(sOutMutex);
    sOutQueue.push(json);
}

// Called during launcher shutdown, BEFORE any game DLL is unloaded: the receive thread calls
// into soh.dll exports, so it must be joined while soh.dll is still mapped (joining across a
// FreeDll boundary would run under the loader lock).
void Shutdown() {
    Disconnect();
}

// Called by the game-switch loop on every transition. Routes inbound packets to the new active
// game and activates/deactivates MM's Anchor. OOT self-reactivates through its own GameInteractor
// hooks (OnSceneSpawnActors/OnPlayerUpdate) when it resumes, so it needs no explicit activate.
void SetActiveGame(int game /* 0 = OOT, 1 = MM */) {
    sActiveGame.store(game);
    if (game == 1) {
        if (MM_Anchor_Activate)
            MM_Anchor_Activate();
    } else {
        if (MM_Anchor_Deactivate)
            MM_Anchor_Deactivate();
    }
}

// sActiveGame/sResyncPending are receive-thread atomics with a single writer; expose reads
// rather than letting other modules touch them directly.
int ActiveGame() {
    return sActiveGame.load();
}

bool TakeResyncPending() {
    return sResyncPending.exchange(false);
}
} // namespace ComboAnchor
