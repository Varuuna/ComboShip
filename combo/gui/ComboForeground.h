// combo/gui/ComboForeground.h
//
// ComboShip-owned: query which game is currently foreground (0 = OOT, 1 = MM). The launcher
// (combo/ComboShip.cpp) calls ComboUI_OnForegroundGame at each transition; ComboTrackerVisibility.cpp
// caches the value here. Used by the audio bridge, the controls reload hook, and dev-tool window
// gating to avoid driving the dormant game's live runtime state.
#pragma once

namespace ComboUI {

// 0 = OOT, 1 = MM. Defaults to OOT (the foreground game at startup after the eager MM boot).
int GetForegroundGame();

inline bool IsGameActive(int game) {
    return GetForegroundGame() == game;
}
inline bool IsMmActive() {
    return GetForegroundGame() == 1;
}

// True once MM has ever been the foreground game this session (its in-memory save is then newer
// than the slot file on disk).
bool MmEverForeground();

} // namespace ComboUI
