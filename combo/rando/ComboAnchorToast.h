// combo/rando/ComboAnchorToast.h — ComboShip: shared "Save updated from team" toast debounce.
// OOT and MM each request a team resync (on connect and on the manual Resync button), and the server
// answers each request, so a single resync produces a burst of UPDATE_TEAM_STATE replies across BOTH
// game DLLs. A per-DLL static can't dedup a cross-game OOT+MM pair, so gate the toast on a shared,
// process-global CVar timestamp: the first reply in a 2s window toasts, the rest are silent.
// Included by soh (Packets/UpdateTeamState.cpp) and MM (MMAnchor.cpp).
#pragma once

#include <chrono>
#include <string>
#include <libultraship/bridge/consolevariablebridge.h>

// Wall clock (not steady_clock): the timestamp CVar may be persisted, and a steady_clock epoch resets
// per process, which would wedge the debounce across restarts.
inline bool ComboAnchor_ShouldToastResync() {
    long long nowMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    long long lastMs = 0;
    try {
        lastMs = std::stoll(CVarGetString("gCombo.Anchor.LastResyncToastMs", "0"));
    } catch (...) {}
    if (nowMs - lastMs > 2000 || nowMs < lastMs) { // second guard: clock moved backwards
        CVarSetString("gCombo.Anchor.LastResyncToastMs", std::to_string(nowMs).c_str());
        return true;
    }
    return false;
}
