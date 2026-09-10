// ComboShip: the launcher-owned Anchor connection. The socket and receive thread live here so the
// connection survives OOT<->MM transitions. See docs/deviations/anchor.md.
#pragma once

#include <cstdint>

namespace ComboAnchor {

void Send(const char* json);
void Connect(const char* host, uint16_t port);
void Disconnect(void);
void Shutdown();
void SetActiveGame(int game);
const char* Combo_Anchor_GetRoster();

// Which game Anchor currently routes to (GameId: 0 = OOT, 1 = MM).
int ActiveGame();
// Consumes the pending on-connect resync flag; true once per connect.
bool TakeResyncPending();

} // namespace ComboAnchor
