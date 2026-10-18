// combo/rando/CrossFw.h
// ComboShip: OoTMM's "Cross-Games Farore's Wind" (crossGameFw), with a point per game: each game keeps its own
// Farore's Wind point, and casting in one game offers to warp to the other game's point (which is used up there).
//
// Each DLL exports `int <GAME>_ComboFw(int op)` (SOH_ComboFw in OTRGlobals.cpp / ComboCrossFw.cpp, MM_ComboFw in
// CrossSpells.cpp) acting on its own (possibly dormant) save, and takes a peer callback from the launcher
// (`<GAME>_SetComboFwPeer`) that forwards an op to the OTHER game. The launcher's peer returns -1 unless both saves of
// the current slot are resident. The feature is on when MM's save has RO_CROSS_GAME_FW and RO_CROSS_SPELL_WIND; MM
// answers CFW_STATE with -1 otherwise, which turns the OOT side back into vanilla Farore's Wind.
// Pure C ABI. See docs/deviations/rando.md.
#pragma once

extern "C" {

enum ComboFwOp {
    CFW_STATE = 0,          // -> ComboFwState, or -1 (feature off / no save)
    CFW_MARK_CROSS = 1,     // unused since each game keeps its own point (kept for the ABI)
    CFW_CLEAR_IF_CROSS = 2, // drop a "lives over there" marker saved before that
    CFW_ENTRANCE = 3,       // -> this game's entrance of its own point, or -1
    CFW_ARM_ARRIVAL = 4,    // the next cross-game arrival spawns at (and uses up) this game's point; -1 if none
};

enum ComboFwState {
    CFW_NONE = 0,
    CFW_HERE = 1,  // a real point in this game
    CFW_CROSS = 2, // legacy marker: the point lived in the other game (read as no point)
};

typedef int (*ComboFwPeerFn)(int op);

} // extern "C"
