#pragma once

#ifdef __cplusplus

#include <thread>
#include <SDL2/SDL_net.h>
#include <nlohmann/json.hpp>

#ifdef COMBO_BUILD
// ComboShip: under the combo build the persistent socket lives in ComboShip.exe, not here. The
// launcher registers these hooks at boot; Network redirects its transport through them instead of
// owning an SDL_net socket + receive thread, so the connection survives OOT<->MM transitions.
// See docs/UPSTREAM_MERGES.md.
extern "C" {
extern void (*gComboAnchorSend)(const char* json);
extern void (*gComboAnchorConnect)(const char* host, uint16_t port);
extern void (*gComboAnchorDisconnect)(void);
}
#endif

class Network {
  private:
    IPaddress networkAddress;
    TCPsocket networkSocket;
    std::thread receiveThread;
    std::string receivedData;

    void ReceiveFromServer();
    void HandleRemoteData(char payload[512]);
    void HandleRemoteJson(std::string payload);

  public:
    bool isEnabled;
    bool isConnected;

    void Enable(const char* host, uint16_t port);
    void Disable();
    /**
     * Raw data handler
     *
     * If you are developing a new remote, you should probably use the json methods instead. This
     * method requires you to parse the data and ensure packets are complete manually, we cannot
     * gaurentee that the data will be complete, or that it will only contain one packet with this
     */
    virtual void OnIncomingData(char payload[512]);
    /**
     * Json handler
     *
     * This method will be called when a complete json packet is received. All json packets must
     * be delimited by a null terminator (\0).
     */
    virtual void OnIncomingJson(nlohmann::json payload);
    virtual void OnConnected();
    virtual void OnDisconnected();
    virtual void ProcessOutgoingPackets();
    void SendDataToRemote(const char* payload);
    virtual void SendJsonToRemote(nlohmann::json packet);

#ifdef COMBO_BUILD
    // ComboShip: inbound JSON pushed in by the launcher's receive thread (mirrors the socket path),
    // and connection-state transitions driven by the launcher.
    void InjectIncomingJson(const std::string& payload);
    void SetConnectedFromCombo(bool connected);
#endif
};

#endif // __cplusplus
