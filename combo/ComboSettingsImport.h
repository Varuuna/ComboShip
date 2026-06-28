// combo/ComboSettingsImport.h
// ComboShip: ABI for the first-launch settings-import screen (issue 24; mirrors ComboExtract.h).
// comboui renders the screen + returns chosen paths; the launcher merges (SoH wins) and applies via
// soh's SOH_ApplyImportedConfig. Pure C ABI — no STL across the boundary; fixed-size path buffers.
#pragma once

extern "C" {

// Optional soft validator: returns nonzero if the file looks like the expected game's config. Used
// only to show a hint in the screen, never to block import. May be null.
typedef int (*ComboFnValidateConfig)(const char* path);

typedef struct ComboSettingsImportCallbacks {
    ComboFnValidateConfig sohValidate;
    ComboFnValidateConfig mmValidate;
} ComboSettingsImportCallbacks;

// Filled by the screen. Paths are "" when that slot was left empty.
typedef struct ComboSettingsImportResult {
    int action; // 0 = Skip, 1 = Import
    char sohPath[1024];
    char mmPath[1024];
} ComboSettingsImportResult;

// Implemented in comboui.dll. Renders the screen and blocks until Import or Skip. Returns 1 if the
// user made a decision (action valid), 0 if the window was closed (launcher treats as Skip).
typedef int (*ComboFnRunSettingsImport)(const ComboSettingsImportCallbacks* cb, ComboSettingsImportResult* out);

// Implemented in soh.dll. Applies a merged config (JSON object, UTF-8) to the live Ship::Config and
// reloads CVars + controller mappings. Caller excludes the Window block. Returns nonzero on success.
typedef int (*ComboFnApplyImportedConfig)(const char* mergedJsonUtf8);

} // extern "C"
