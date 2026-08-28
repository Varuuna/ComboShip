// ComboShip: state the generation worker hands to the main thread.
//
// g_ComboPendingFinalize is the handoff barrier: the worker writes every plain field below BEFORE
// storing true, and the main thread reads them only after exchange(false) returns true. The default
// seq_cst ordering IS that synchronisation - do not weaken it, and do not add a mutex (the reload
// path already takes g_containerMutex, so a second lock would create an ordering to reason about).
//
// g_ComboProgress's ADDRESS is handed to soh.dll (SOH_SetComboProgressPtr) and must stay valid for
// the whole process - never make it a local or move it into a reassigned struct.
//
// The main-thread writer in Combo_OnReloadRequest is guarded only by its early-out on
// g_GenerateBusy / g_ComboPendingFinalize.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

#include "gui/ComboGenProgress.h"

extern std::atomic<bool> g_GenerateBusy;
extern std::thread g_GenerateThread;
extern ComboRando::ComboGenProgress g_ComboProgress;
extern std::atomic<bool> g_ComboPendingFinalize;

// Written by the worker before the barrier; read by the main thread after it.
extern std::string g_FinalizeOotApply;
extern std::filesystem::path g_FinalizeSpoilerPath;
extern uint32_t g_FinalizeDisplaySeed;
extern uint32_t g_FinalizeMasterSeed;
extern std::string g_ConsolidatedJson;

// Main-thread only (reload -> slot-bind handoff).
extern std::string g_PendingMMSettingsJson;
extern std::string g_UserMMSettingsSnapshot;
extern bool g_ComboReloadRestoreUserMM;
