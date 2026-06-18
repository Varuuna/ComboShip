#ifndef MM_NETWORK_ANCHOR_H
#define MM_NETWORK_ANCHOR_H
#ifdef __cplusplus
#ifdef COMBO_BUILD

#include <cstdint>
#include <string>
#include <map>
#include <vector>
#include <queue>
#include <mutex>
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "variables.h"
#include "z64.h"
}

// ComboShip: MM-side Anchor adapter (Phase 2). The TCP connection lives in ComboShip.exe
// (launcher-owned, shared with OOT, persistent across transitions). MMAnchor has NO socket: it sends
// via the launcher callback (gMMComboAnchorSend), receives via MM_Anchor_RecvJson, and is
// activated/deactivated on transitions. Protocol shapes mirror SoH's Anchor and the canonical MM
// Anchor (2S2H PR #1349) so OOT and MM clients interoperate on one room. See docs/UPSTREAM_MERGES.md
// and combo/ComboShip.cpp (namespace ComboAnchor).

// MM namespaces its scene id in shared CLIENT-STATE (the room roster, for OOT's display) so it can't
// collide with OOT scene ids. PLAYER_UPDATE carries the RAW sceneId (puppet same-scene matching).
static constexpr int32_t MM_ANCHOR_SCENE_NAMESPACE = 1000;

// Puppet (remote player) actor entry points — defined in DummyPlayer.cpp.
void DummyPlayer_Init(Actor* actor, PlayState* play);
void DummyPlayer_Update(Actor* actor, PlayState* play);
void DummyPlayer_Draw(Actor* actor, PlayState* play);
void DummyPlayer_Destroy(Actor* actor, PlayState* play);

struct MMAnchorClient {
    uint32_t clientId = 0;
    std::string name;
    Color_RGB8 color = { 255, 255, 255 };
    std::string clientVersion;
    std::string teamId = "default";
    bool online = false;
    bool self = false;
    uint32_t seed = 0;
    bool isSaveLoaded = false;
    bool isGameComplete = false;
    s16 sceneId = SCENE_MAX; // RAW MM scene (from PLAYER_UPDATE); puppet matches on this
    s32 entrance = 0;

    // Only valid once a PLAYER_UPDATE has arrived (canonical field set; mirrors 2S2H PR #1349).
    uint8_t transformation = 0; // PLAYER_FORM_HUMAN
    PosRot posRot = {};
    u8 jointTable[159] = {};
    u8 upperJointTable[159] = {};
    uint8_t currentMask = 0;
    uint8_t rightHandType = 0;
    uint8_t leftHandType = 0;
    int8_t currentShield = 0;
    uint8_t sheathType = 0;
    int8_t heldItemAction = 0;
    uint8_t heldItemId = 0;
    int8_t itemAction = 0;
    uint32_t stateFlags1 = 0;
    uint32_t stateFlags2 = 0;
    uint32_t stateFlags3 = 0;
    float unk_B0C = 0.0f;
    int16_t unk_B28 = 0;
    int16_t unk_ACC = 0;
    int8_t invincibilityTimer = 0;

    Player* player = nullptr; // the spawned puppet actor (cast), if any
};

struct MMAnchorRoomState {
    uint8_t pvpMode = 0;           // 0 = off, 1 = on (same-team friendly off), 2 = on with friendly fire
    uint8_t syncItemsAndFlags = 1; // 0 = off, 1 = on (Phase 2c)
};

class MMAnchor {
  public:
    static MMAnchor* Instance;

    // Launcher-facing lifecycle / transport.
    void Activate();   // MM became the active game
    void Deactivate(); // MM backgrounded
    void OnIncomingJson(const std::string& payload); // launcher recv thread -> enqueue
    void ProcessIncomingPacketQueue();               // MM game thread -> drain + dispatch

    // Outbound packets.
    void SendUpdateClientState();
    void SendPlayerUpdate();
    void SendPacket_DamagePlayer(uint32_t clientId, uint8_t damageEffect, uint8_t damage);
    // Phase 2c: shared-progression item sync. Broadcast a locally-obtained check's RAW rando item id
    // (+ its check id) to teammates; receivers ConvertItem against their own state and grant.
    void SendPacket_GiveItem(int16_t randoItemId, int32_t randoCheckId);
    bool applyingRemoteItem = false; // guards against re-broadcasting items granted from the network

    bool isActive = false;
    uint32_t ownClientId = 0;
    std::map<uint32_t, MMAnchorClient> clients;
    std::vector<uint32_t> actorIndexToClientId; // params index -> clientId, used while spawning puppets
    MMAnchorRoomState roomState;
    bool refreshingActors = false; // true while RefreshClientActors is spawning (gates the init hook)

    bool IsSaveLoaded();

  private:
    void RegisterHooks();
    void RefreshClientActors();
    nlohmann::json PrepClientState();
    void SendJson(nlohmann::json payload);

    void HandlePacket_AllClientState(const nlohmann::json& payload);
    void HandlePacket_UpdateClientState(const nlohmann::json& payload);
    void HandlePacket_PlayerUpdate(const nlohmann::json& payload);
    void HandlePacket_DamagePlayer(const nlohmann::json& payload);
    void HandlePacket_GiveItem(const nlohmann::json& payload);

    bool hooksRegistered = false;
    bool shouldRefreshActors = false;
    std::queue<nlohmann::json> incomingQueue;
    std::mutex incomingMutex;
};

// Puppet hit-response types (mirror canonical PlayerDamageResponseType from 2S2H PR #1349).
typedef enum {
    /* 0 */ DUMMY_PLAYER_HIT_RESPONSE_NONE,
    /* 1 */ DUMMY_PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE,
    /* 2 */ DUMMY_PLAYER_HIT_RESPONSE_KNOCKBACK_SMALL,
    /* 3 */ DUMMY_PLAYER_HIT_RESPONSE_ICE_TRAP,
    /* 4 */ DUMMY_PLAYER_HIT_RESPONSE_ELECTRIC_SHOCK,
    /* 5 */ DUMMY_PLAYER_HIT_RESPONSE_STUN,
    /* 6 */ DUMMY_PLAYER_HIT_RESPONSE_FIRE,
    /* 7 */ DUMMY_PLAYER_HIT_RESPONSE_NORMAL,
} DummyPlayerDamageResponseType;

#endif // COMBO_BUILD
#endif // __cplusplus
#endif // MM_NETWORK_ANCHOR_H
