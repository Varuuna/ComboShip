#include "Network.h"
#include <spdlog/spdlog.h>
#include <libultraship/libultraship.h>

#ifdef COMBO_BUILD
// ComboShip: transport hooks registered by ComboShip.exe at boot (see Network.h). When unset (e.g.
// a standalone non-launcher run), Enable/Disable/Send become no-ops rather than touching a socket.
extern "C" {
void (*gComboAnchorSend)(const char* json) = nullptr;
void (*gComboAnchorConnect)(const char* host, uint16_t port) = nullptr;
void (*gComboAnchorDisconnect)(void) = nullptr;
}
#endif

// MARK: - Public

void Network::Enable(const char* host, uint16_t port) {
    if (isEnabled) {
        return;
    }

#ifdef COMBO_BUILD
    // ComboShip: ask the launcher to open/keep the shared connection; no local socket or thread.
    isEnabled = true;
    if (gComboAnchorConnect) {
        gComboAnchorConnect(host, port);
    }
#else
    if (SDLNet_ResolveHost(&networkAddress, host, port) == -1) {
        SPDLOG_ERROR("[Network] SDLNet_ResolveHost: {}", SDLNet_GetError());
    }

    isEnabled = true;

    // First check if there is a thread running, if so, join it
    if (receiveThread.joinable()) {
        receiveThread.join();
    }

    receiveThread = std::thread(&Network::ReceiveFromServer, this);
#endif
}

void Network::Disable() {
    if (!isEnabled) {
        return;
    }

#ifdef COMBO_BUILD
    // ComboShip: ask the launcher to drop the shared connection.
    isEnabled = false;
    isConnected = false;
    if (gComboAnchorDisconnect) {
        gComboAnchorDisconnect();
    }
#else
    isEnabled = false;
    receiveThread.join();
#endif
}

void Network::OnIncomingData(char payload[512]) {
}

void Network::OnIncomingJson(nlohmann::json payload) {
}

void Network::OnConnected() {
}

void Network::OnDisconnected() {
}

void Network::ProcessOutgoingPackets() {
}

void Network::SendDataToRemote(const char* payload) {
#ifdef COMBO_BUILD
    // ComboShip: hand the payload to the launcher's outgoing queue; it owns the socket + framing.
    if (gComboAnchorSend) {
        gComboAnchorSend(payload);
    }
#else
    SPDLOG_DEBUG("[Network] Sending data: {}", payload);
    SDLNet_TCP_Send(networkSocket, payload, static_cast<int>(strlen(payload) + 1));
<<<<<<< HEAD
#endif
=======
>>>>>>> vendor-soh
}

void Network::SendJsonToRemote(nlohmann::json payload) {
    SendDataToRemote(payload.dump().c_str());
}

#ifdef COMBO_BUILD
// ComboShip: launcher receive-thread entry points (replace the local socket + ReceiveFromServer).

void Network::InjectIncomingJson(const std::string& payload) {
    // Reuse the same parse + dispatch path the socket build uses (parse, OnIncomingJson, try/catch).
    HandleRemoteJson(payload);
}

void Network::SetConnectedFromCombo(bool connected) {
    bool wasConnected = isConnected;
    isConnected = connected;
    if (connected && !wasConnected) {
        OnConnected();
    } else if (!connected && wasConnected) {
        OnDisconnected();
    }
}
#endif

// MARK: - Private

void Network::ReceiveFromServer() {
    while (isEnabled) {
        while (!isConnected && isEnabled) {
            SPDLOG_TRACE("[Network] Attempting to make connection to server...");
            networkSocket = SDLNet_TCP_Open(&networkAddress);

            if (networkSocket) {
                isConnected = true;
                receivedData.clear();
                SPDLOG_INFO("[Network] Connection to server established!");

                OnConnected();
                break;
            }
        }

        SDLNet_SocketSet socketSet = SDLNet_AllocSocketSet(1);
        if (networkSocket) {
            SDLNet_TCP_AddSocket(socketSet, networkSocket);
        }

        // Listen to socket messages
        while (isConnected && networkSocket && isEnabled) {
            // we check first if socket has data, to not block in the TCP_Recv
            int socketsReady = SDLNet_CheckSockets(socketSet, 0);

            if (socketsReady == -1) {
                SPDLOG_ERROR("[Network] SDLNet_CheckSockets: {}", SDLNet_GetError());
                break;
            }

            // Always process outgoing packets
            ProcessOutgoingPackets();

            if (socketsReady == 0) {
                // No incoming data
                continue;
            }

            char remoteDataReceived[512];
            memset(remoteDataReceived, 0, sizeof(remoteDataReceived));
            int len = SDLNet_TCP_Recv(networkSocket, &remoteDataReceived, sizeof(remoteDataReceived));
            if (!len || !networkSocket || len == -1) {
                SPDLOG_ERROR("[Network] SDLNet_TCP_Recv: {}", SDLNet_GetError());
                break;
            }

            HandleRemoteData(remoteDataReceived);

            receivedData.append(remoteDataReceived, len);

            // Proess all complete packets
            size_t delimiterPos = receivedData.find('\0');
            while (delimiterPos != std::string::npos) {
                // Extract the complete packet until the delimiter
                std::string packet = receivedData.substr(0, delimiterPos);
                // Remove the packet (including the delimiter) from the received data
                receivedData.erase(0, delimiterPos + 1);
                HandleRemoteJson(packet);
                // Find the next delimiter
                delimiterPos = receivedData.find('\0');
            }
        }

        if (socketSet) {
            SDLNet_FreeSocketSet(socketSet);
        }

        if (isConnected) {
            SDLNet_TCP_Close(networkSocket);
            networkSocket = nullptr;
            isConnected = false;
            receivedData.clear();
            OnDisconnected();
            SPDLOG_INFO("[Network] Ending receiving thread...");
        }
    }
}

void Network::HandleRemoteData(char payload[512]) {
    OnIncomingData(payload);
}

void Network::HandleRemoteJson(std::string payload) {
    SPDLOG_DEBUG("[Network] Received json: {}", payload);
    nlohmann::json jsonPayload;
    try {
        jsonPayload = nlohmann::json::parse(payload);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Network] Failed to parse json: \n{}\n{}\n", payload, e.what());
        return;
    }

    try {
        OnIncomingJson(jsonPayload);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Network] Exception handling incoming JSON: {}", e.what());
    } catch (...) { SPDLOG_ERROR("[Network] Unknown exception handling incoming JSON"); }
}
