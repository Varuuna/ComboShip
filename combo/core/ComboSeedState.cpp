#include "core/ComboSeedState.h"

std::atomic<bool> g_GenerateBusy{ false };

// ComboShip: non-blocking generation. The heavy fill runs on g_GenerateThread; the main thread
// keeps rendering + playing music + showing progress, and runs the gSaveContext-mutating apply
// itself via Combo_PollFinalize (see the file-select poll). g_ComboProgress is the single source
// of truth, shared read-only with soh.dll via SOH_SetComboProgressPtr.
std::thread g_GenerateThread;
ComboRando::ComboGenProgress g_ComboProgress;
std::atomic<bool> g_ComboPendingFinalize{ false }; // worker succeeded, main-thread apply not yet run
// Main-thread finalize inputs stashed by the worker (consumed by Combo_FinalizeGenerate).
std::string g_FinalizeOotApply;
std::filesystem::path g_FinalizeSpoilerPath;
uint32_t g_FinalizeDisplaySeed = 0;
uint32_t g_FinalizeMasterSeed = 0; // #169: the gen-roll latch keys off the master seed, not the display hash
// Consolidated spoiler JSON for the just-generated seed. The worker writes the pending file from it;
// Combo_OnOOTSaveInit bakes it into the slot's container and pushes it into both DLLs at Start.
std::string g_ConsolidatedJson;

// ComboShip: a silent auto-reload must not let the pending seed's settings overwrite the user's
// gRando.* CVars on disk. MM reads gRando.* only at slot-bind, so its restore is deferred there
// (not inline in Combo_OnReloadRequest like OOT's). See docs/deviations/rando.md.
std::string g_PendingMMSettingsJson;     // seed's MM settings, applied right before slot-bind
std::string g_UserMMSettingsSnapshot;    // user's MM settings, restored right after slot-bind
bool g_ComboReloadRestoreUserMM = false; // false for an explicit drop (seed settings stick)

int g_PendingMMFileNum = -1;
