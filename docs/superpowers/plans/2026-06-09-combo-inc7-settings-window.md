# ComboShip Increment 7 — Combined Settings Window + Generate UX Implementation Plan

> **SUPERSEDED 2026-06-09** by the unified-menu design (`docs/superpowers/specs/2026-06-09-combo-unified-settings-menu-design.md`). Do not execute this plan. The cross-world Generate UX below is carried into that design's **Combo** tab; the standalone soh.dll-hosted window is dropped in favor of a combo-owned `comboui.dll`. A new plan will be written from the unified-menu spec.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A single combo-owned ImGui window (hosted in `soh.dll`) that configures both games' randomizer options and runs one decoupled, threaded, progress-reporting cross-world Generate, replacing the current auto-fill-at-save with hardcoded seed `12345u`.

**Architecture:** A `Ship::GuiWindow` subclass (`ComboRandoWindow`, source in `combo/gui/`, compiled into `soh.dll`) renders two settings tabs (OOT / MM) from a uniform `OptionDesc` model plus a persistent action bar (seed field · Generate · status). OOT options come from `Rando::Settings` in-DLL; MM options come from a new `2ship.dll` export `MM_DumpRandoOptions` whose JSON the combo exe pushes into `soh.dll` via `SOH_SetMMRandoOptionsJson`. Generate calls an in-DLL trampoline that invokes a callback registered by the combo exe; the exe launches a worker thread running the refactored fill, reporting live progress through a shared `ComboGenProgress` struct. Save creation no longer generates — it only consumes the already-applied OOT placements and the stashed MM slice.

**Tech Stack:** C++17, ImGui (via libultraship `Ship::GuiWindow`), nlohmann::json, CVar store (shared across DLLs), Windows DLL C-ABI exports, `std::thread`/`std::atomic`. Build: multi-config Visual Studio CMake generator in `build/x64`.

---

## Testing Note (read before starting)

This project has **no unit-test harness** for the rando/GUI/DLL code (parent spec §8 establishes manual verification only — GUI + cross-DLL + game-runtime). Strict TDD with failing unit tests does not apply here. Per the writing-plans skill's "user instructions / project reality override," each task's verification is a **build + observe** step with explicit expected output, not a unit test. Treat the build succeeding + the stated observable behavior as the green bar.

**Build commands** (from repo root `E:\Git\ComboShip\Combo`, PowerShell):
- soh DLL: `scripts/build-soh.ps1` (Debug default) — or `cmake --build build/x64 --target soh --config Debug`
- 2ship DLL: `scripts/build-2ship.ps1` — or `cmake --build build/x64 --target 2ship --config Debug`
- launcher exe: `scripts/build-comboship.ps1` — or `cmake --build build/x64 --target ComboShip --config Debug`

**NEVER build `soh` and `2ship` in parallel** (they race on shared `OTRExporter.lib`/`ZAPD`). Build serialized, soh first. Runtime artifacts land in `build/x64/<sub>/Debug/` and are mirrored to `x64/Debug/`. Run from `build/x64/combo/Debug/ComboShip.exe`.

**Commit after each task** (project preference: frequent commits). End commit messages with the Co-Authored-By trailer.

---

## File Structure

**Created (combo-owned, no upstream):**
- `combo/gui/ComboGenProgress.h` — `struct ComboGenProgress` (atomics + error buffer + phase labels). Included by both `soh.dll` (the window) and the combo exe (the worker). Both already have `../combo` on their include path.
- `combo/gui/ComboRandoWindow.h` / `.cpp` — the `Ship::GuiWindow` subclass: option model, tabs, sidebar, widgets, action bar, progress overlay. Compiled into `soh.dll`.

**Modified (vendored, thin, `COMBO_BUILD`-guarded — all documented in `docs/UPSTREAM_MERGES.md` + `// ComboShip:` comments):**
- `soh/soh/OTRGlobals.cpp` — new exports: `SOH_SetOnComboGenerateRequestCallback`, `SOH_TriggerComboGenerate`, `SOH_SetSeedGenerated`, `SOH_SetMMRandoOptionsJson`, `SOH_GetMMRandoOptionsJson` (in-DLL accessor for the window).
- `soh/soh/OTRGlobals.h` — C declarations for the new exports.
- `soh/soh/SohGui/SohGui.cpp` — register `ComboRandoWindow` in `SetupGuiElements`.
- `soh/soh/SohGui/SohMenuDevTools.cpp` (or wherever a combo-appropriate sidebar lives) — menu button to open the window.
- `soh/CMakeLists.txt` — add `../combo/gui/ComboRandoWindow.cpp` to the soh target sources.
- `soh/src/code/z_sram.c` — neutralize the save-time generate call inside the existing `COMBO_BUILD` block; keep forcing `QUEST_RANDOMIZER`.
- `mm/2s2h/Rando/Menu.cpp` — new export `MM_DumpRandoOptions` (placed here for direct access to the file-scope label maps).
- `mm/2s2h/BenPort.cpp` — (only if a header decl is needed; the export itself lives in Menu.cpp).
- `combo/ComboShip.cpp` — resolve new soh/MM exports; register `Combo_OnGenerateRequest`; push MM options JSON into soh; refactor `Combo_OnGenerate` into a seed+progress worker; stop registering the old save-time generate callback.

---

## Task 1: Shared progress struct (`ComboGenProgress.h`)

**Files:**
- Create: `combo/gui/ComboGenProgress.h`

- [ ] **Step 1: Write the header**

```cpp
// combo/gui/ComboGenProgress.h
// ComboShip Inc7: live progress for the threaded cross-world Generate.
// Single instance owned by ComboRandoWindow (in soh.dll); written by the
// combo exe's worker thread, polled each frame by the window. All fields the
// worker writes while the window may read are atomic; the error string is only
// read by the window AFTER `done` flips true (release/acquire via `done`).
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

namespace ComboRando {

struct ComboGenProgress {
    // phase: 0 Idle, 1 Preparing pools, 2 Placing items, 3 Finalizing
    std::atomic<int>      phase{ 0 };
    std::atomic<int>      placed{ 0 };
    std::atomic<int>      total{ 0 };
    std::atomic<bool>     running{ false };
    std::atomic<bool>     done{ false };
    std::atomic<bool>     success{ false };
    std::atomic<uint32_t> seed{ 0 };
    std::atomic<int>      foreignCount{ 0 };
    char                  error[256] = { 0 };

    void Reset() {
        phase.store(0);
        placed.store(0);
        total.store(0);
        success.store(false);
        seed.store(0);
        foreignCount.store(0);
        error[0] = '\0';
        // `done`/`running` set by the worker lifecycle, not here.
    }

    void SetError(const char* msg) {
        if (!msg) { error[0] = '\0'; return; }
        std::strncpy(error, msg, sizeof(error) - 1);
        error[sizeof(error) - 1] = '\0';
    }

    static const char* PhaseLabel(int p) {
        switch (p) {
            case 1:  return "Preparing pools";
            case 2:  return "Placing items";
            case 3:  return "Finalizing";
            default: return "Idle";
        }
    }
};

} // namespace ComboRando
```

- [ ] **Step 2: Verify it compiles standalone**

Run (PowerShell, repo root):
```
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat' > $null; cl /std:c++17 /EHsc /c combo/gui/ComboGenProgress.h /Fo:$env:TEMP\cgp.obj
```
Expected: compiles with no errors (header treated as a TU). If `cl` is not directly available, skip this micro-check — Task 2's soh build will exercise it.

- [ ] **Step 3: Commit**

```
git add combo/gui/ComboGenProgress.h
git commit -m "Inc7: add shared ComboGenProgress struct"
```

---

## Task 2: Add progress reporting to `CrossWorldCombinedFill`

**Files:**
- Modify: `combo/rando/CrossWorldRando.h` (signature at lines 81-88; fill loop at line 164; pre-loop and post-loop)

The fill is a single assumed-fill loop, not the spec's 4 named phases. Report `phase`/`placed`/`total` against the real loop. Add an optional trailing `ComboGenProgress*` parameter (default `nullptr`) so existing callers keep working.

- [ ] **Step 1: Include the progress header**

At the top of `combo/rando/CrossWorldRando.h`, after the existing includes (after line 17 `#include <nlohmann/json.hpp>`):

```cpp
#include "gui/ComboGenProgress.h"
```

- [ ] **Step 2: Extend the signature**

Replace the signature block (lines 81-88) with:

```cpp
inline CombinedFillResult CrossWorldCombinedFill(
    const std::string& sohDumpJson,
    const std::string& mmDumpJson,
    uint32_t masterSeed,
    const OracleFns& ootOracle,
    const OracleFns& mmOracle,
    const std::string& portalCheckName = "",
    ComboGenProgress* progress = nullptr
) {
```

- [ ] **Step 3: Report "Preparing pools" before the loop**

Immediately after `result.success = false;` (currently line 90), add:

```cpp
    if (progress) { progress->phase.store(1); progress->placed.store(0); progress->total.store(0); }
```

- [ ] **Step 4: Report "Placing items" + per-item progress inside the loop**

Just before the placement loop `for (size_t idx = 0; idx < allItems.size(); ++idx) {` (currently line 164), add:

```cpp
    if (progress) {
        progress->phase.store(2);
        progress->total.store(static_cast<int>(allItems.size()));
    }
```

As the **first** statement inside that loop body (right after the `for (...) {`), add:

```cpp
        if (progress) progress->placed.store(static_cast<int>(idx));
```

- [ ] **Step 5: Report "Finalizing" before building the spoiler**

Just before `// --- Build spoiler ...` (currently line 211), add:

```cpp
    if (progress) { progress->placed.store(static_cast<int>(allItems.size())); progress->phase.store(3); }
```

- [ ] **Step 6: Build soh + ComboShip to confirm the header-only change compiles**

Run:
```
scripts/build-soh.ps1
scripts/build-comboship.ps1
```
Expected: both succeed. (`CrossWorldRando.h` is included by the combo exe; `ComboGenProgress.h` now pulled in transitively.) Confirm the `ComboShip.exe` mtime advanced.

- [ ] **Step 7: Commit**

```
git add combo/rando/CrossWorldRando.h
git commit -m "Inc7: report live progress from CrossWorldCombinedFill"
```

---

## Task 3: soh exports — generate-request callback, trampoline, seed setter, MM-options bridge

**Files:**
- Modify: `soh/soh/OTRGlobals.cpp` (add near the existing combo exports, ~line 2722 where `gComboGenerateCallback` / `SOH_SetOnComboGenerateCallback` live)
- Modify: `soh/soh/OTRGlobals.h` (add C decls in the `#ifndef __cplusplus` export block, ~lines 81-140)

These exports let: the window invoke the exe's generate handler (`SOH_TriggerComboGenerate`), the exe register that handler (`SOH_SetOnComboGenerateRequestCallback`) and set seed-generated (`SOH_SetSeedGenerated`), and the exe hand MM's options JSON to the in-DLL window (`SOH_SetMMRandoOptionsJson` + in-DLL `SOH_GetMMRandoOptionsJson`).

- [ ] **Step 1: Add the export definitions in OTRGlobals.cpp**

Insert after the existing `SOH_SetOnComboGenerateCallback` definition (after line ~2725):

```cpp
#ifdef COMBO_BUILD
#include "gui/ComboGenProgress.h"

// ComboShip Inc7: the combo exe registers a handler that runs the threaded fill.
// The window (in this DLL) calls SOH_TriggerComboGenerate, which forwards to it.
extern "C" void (*gComboGenerateRequestCallback)(const char* inputSeed,
                                                 ComboRando::ComboGenProgress* progress) = nullptr;

extern "C" __declspec(dllexport) void SOH_SetOnComboGenerateRequestCallback(
    void (*cb)(const char* inputSeed, ComboRando::ComboGenProgress* progress)) {
    gComboGenerateRequestCallback = cb;
}

// Called by ComboRandoWindow's Generate button. Returns immediately; the handler
// launches a worker thread. No-op (logged) if no handler is registered.
extern "C" __declspec(dllexport) void SOH_TriggerComboGenerate(
    const char* inputSeed, ComboRando::ComboGenProgress* progress) {
    if (gComboGenerateRequestCallback) {
        gComboGenerateRequestCallback(inputSeed, progress);
    } else {
        SPDLOG_WARN("[ComboShip] SOH_TriggerComboGenerate: no handler registered");
    }
}

// Lets the combo exe flip OOT's seed-gate without going through ApplyRandoPlacements.
extern "C" __declspec(dllexport) void SOH_SetSeedGenerated(uint8_t generated) {
    if (OTRGlobals::Instance && OTRGlobals::Instance->gRandoContext) {
        OTRGlobals::Instance->gRandoContext->SetSeedGenerated(generated != 0);
    }
}

// MM options JSON bridge: the window is in soh.dll but MM_DumpRandoOptions is a
// 2ship.dll export, so the exe fetches it and pushes it here for the window.
static std::string sComboMMOptionsJson;
extern "C" __declspec(dllexport) void SOH_SetMMRandoOptionsJson(const char* json) {
    sComboMMOptionsJson = (json ? json : "");
}
// In-DLL accessor (NOT exported) used by ComboRandoWindow.
const std::string& SOH_GetMMRandoOptionsJson() {
    return sComboMMOptionsJson;
}
#endif
```

- [ ] **Step 2: Declare the C-ABI exports in OTRGlobals.h**

Inside the `#ifndef __cplusplus` block (near line 121 where `Randomizer_IsSeedGenerated` is declared), the worker-facing ones are called from the exe's C++ — but to keep them visible regardless of TU, add a `#ifdef COMBO_BUILD` block at the end of the export declarations (still inside the header, outside the `#ifndef __cplusplus` if they need C++ types). Add:

```cpp
#ifdef COMBO_BUILD
// ComboShip Inc7 exports (see OTRGlobals.cpp). ComboGenProgress is forward-declared
// here so the exe and DLL agree on the pointer type without pulling the full header
// into C translation units.
namespace ComboRando { struct ComboGenProgress; }
extern "C" __declspec(dllexport) void SOH_SetOnComboGenerateRequestCallback(
    void (*cb)(const char* inputSeed, ComboRando::ComboGenProgress* progress));
extern "C" __declspec(dllexport) void SOH_TriggerComboGenerate(
    const char* inputSeed, ComboRando::ComboGenProgress* progress);
extern "C" __declspec(dllexport) void SOH_SetSeedGenerated(uint8_t generated);
extern "C" __declspec(dllexport) void SOH_SetMMRandoOptionsJson(const char* json);
// In-DLL only:
const std::string& SOH_GetMMRandoOptionsJson();
#endif
```

(Place this block where it is reached by C++ TUs — OTRGlobals.h is included as C++ in the soh tree. If the surrounding region is `extern "C"`-for-C only, put it after the `#endif` that closes the `#ifndef __cplusplus` region so the `namespace`/`std::string` are valid C++.)

- [ ] **Step 3: Build soh**

Run: `scripts/build-soh.ps1`
Expected: builds clean. Then confirm the exports exist:
```
dumpbin /exports build/x64/soh/Debug/soh.dll | Select-String "SOH_TriggerComboGenerate|SOH_SetOnComboGenerateRequestCallback|SOH_SetSeedGenerated|SOH_SetMMRandoOptionsJson"
```
Expected: all four symbols listed.

- [ ] **Step 4: Commit**

```
git add soh/soh/OTRGlobals.cpp soh/soh/OTRGlobals.h
git commit -m "Inc7: add soh exports for generate-request, seed-gate, MM options bridge"
```

---

## Task 4: MM export `MM_DumpRandoOptions`

**Files:**
- Modify: `mm/2s2h/Rando/Menu.cpp` (add the export at file scope; reuse the existing label maps `logicOptions`, `accessDungeonOptions`, `accessTrialsOptions` at lines 18-48)

The export returns a JSON array; each element is one renderable option. It iterates `Rando::StaticData::Options` and attaches type/labels/range/category from an explicit lookup (the label maps are hardcoded per-option in render code — there is no data-driven id→label link, see the `TODO` at Menu.cpp:17, so the association is rebuilt here).

JSON element shape (consumed by ComboRandoWindow Task 6):
```json
{ "id": "RO_LOGIC", "cvar": "gRando.Options.RO_LOGIC", "default": 0,
  "type": "combo|bool|int", "label": "Logic",
  "valueLabels": ["Glitchless","No Logic",...],   // type==combo
  "min": 0, "max": 100,                            // type==int
  "category": "Logic" }
```

- [ ] **Step 1: Add a file-scope clock-mode label map (currently a function-static)**

Near the other label maps (after line 48 in Menu.cpp), add:

```cpp
// ComboShip Inc7: file-scope copy of the clock-mode labels (the original is a
// function-static inside DrawItemsTab) so MM_DumpRandoOptions can reference it.
std::unordered_map<int32_t, const char*> comboClockModeOptions = {
    { RO_CLOCK_SHUFFLE_RANDOM, "Random" },
    { RO_CLOCK_SHUFFLE_ASCENDING, "Progressive: Ascending" },
    { RO_CLOCK_SHUFFLE_DESCENDING, "Progressive: Descending" },
};
```

- [ ] **Step 2: Add the export at the bottom of Menu.cpp**

Append to `mm/2s2h/Rando/Menu.cpp`:

```cpp
#ifdef COMBO_BUILD
#include <nlohmann/json.hpp>

// ComboShip Inc7: dump MM rando options (id/cvar/default + type/labels/range/category)
// as JSON for the combo settings window. Lives in Menu.cpp to reach the file-scope
// label maps; the id->label/range/category association mirrors the per-option render
// code above (there is no data-driven mapping in StaticData — see TODO at top of file).
extern "C" __declspec(dllexport) const char* MM_DumpRandoOptions(void) {
    static std::string cached;
    if (!cached.empty()) return cached.c_str();

    // Combobox options: option id -> its label map.
    const std::unordered_map<RandoOptionId, std::unordered_map<int32_t, const char*>*> comboMaps = {
        { RO_LOGIC,                    &logicOptions },
        { RO_ACCESS_DUNGEONS,          &accessDungeonOptions },
        { RO_ACCESS_TRIALS,            &accessTrialsOptions },
        { RO_CLOCK_SHUFFLE_PROGRESSIVE,&comboClockModeOptions },
    };
    // Integer options: option id -> {min, max}.
    const std::unordered_map<RandoOptionId, std::pair<int,int>> intRanges = {
        { RO_ACCESS_MAJORA_REMAINS_COUNT, {0, 4} },
        { RO_ACCESS_MAJORA_MASKS_COUNT,   {0, 20} },
        { RO_ACCESS_MOON_REMAINS_COUNT,   {0, 4} },
        { RO_ACCESS_MOON_MASKS_COUNT,     {0, 20} },
        { RO_SKULLTULA_TOKENS_REQUIRED,   {1, 100} },
        { RO_SKULLTULA_TOKENS_MAX,        {1, 100} },
        { RO_STRAY_FAIRIES_REQUIRED,      {1, 15} },
        { RO_STRAY_FAIRIES_MAX,           {1, 15} },
        { RO_TRIFORCE_PIECES_REQUIRED,    {1, 1000} },
        { RO_TRIFORCE_PIECES_MAX,         {1, 1000} },
        { RO_TRAP_AMOUNT,                 {1, 100} },
        { RO_CLOCK_TERMINAL_TIME,         {0, 359} },
        { RO_STARTING_HEALTH,             {1, 20} },
    };
    // Category by option id (mirrors which tab the option renders under).
    auto categoryFor = [](RandoOptionId id) -> const char* {
        switch (id) {
            case RO_LOGIC: case RO_ACCESS_DUNGEONS: case RO_ACCESS_TRIALS:
            case RO_ACCESS_MAJORA_REMAINS_COUNT: case RO_ACCESS_MAJORA_MASKS_COUNT:
            case RO_ACCESS_MOON_REMAINS_COUNT: case RO_ACCESS_MOON_MASKS_COUNT:
                return "Logic & Access";
            case RO_SHUFFLE_OWL_STATUES: case RO_SHUFFLE_SHOPS: case RO_SHUFFLE_TINGLE_SHOPS:
            case RO_SHUFFLE_BOSS_REMAINS: case RO_SHUFFLE_COWS: case RO_SHUFFLE_GOLD_SKULLTULAS:
            case RO_SKULLTULA_TOKENS_REQUIRED: case RO_SKULLTULA_TOKENS_MAX:
            case RO_STRAY_FAIRIES_REQUIRED: case RO_STRAY_FAIRIES_MAX:
            case RO_SHUFFLE_TRIFORCE_PIECES: case RO_TRIFORCE_PIECES_REQUIRED: case RO_TRIFORCE_PIECES_MAX:
            case RO_SHUFFLE_POT_DROPS: case RO_SHUFFLE_CRATE_DROPS: case RO_SHUFFLE_BARREL_DROPS:
            case RO_SHUFFLE_SNOWBALL_DROPS: case RO_SHUFFLE_GRASS_DROPS: case RO_SHUFFLE_TREE_DROPS:
            case RO_SHUFFLE_FROGS: case RO_SHUFFLE_FREESTANDING_ITEMS:
                return "Shuffles";
            case RO_SHUFFLE_SWIM: case RO_SHUFFLE_OCARINA_BUTTONS: case RO_SHUFFLE_SONG_DOUBLE_TIME:
            case RO_SHUFFLE_SONG_INVERTED_TIME: case RO_SHUFFLE_SONG_SUN: case RO_SHUFFLE_SONG_SARIA:
            case RO_SHUFFLE_TYCOON_WALLET: case RO_PLENTIFUL_ITEMS: case RO_SHUFFLE_BOSS_SOULS:
            case RO_SHUFFLE_ENEMY_DROPS: case RO_SHUFFLE_ENEMY_SOULS: case RO_CLOCK_SHUFFLE:
            case RO_CLOCK_SHUFFLE_PROGRESSIVE: case RO_CLOCK_TERMINAL_TIME:
            case RO_SHUFFLE_TRAPS: case RO_TRAP_AMOUNT:
                return "Items & Traps";
            case RO_STARTING_RUPEES: case RO_STARTING_CONSUMABLES: case RO_STARTING_MAPS_AND_COMPASSES:
            case RO_STARTING_HEALTH:
                return "Starting";
            default:
                return "Other";
        }
    };

    nlohmann::json arr = nlohmann::json::array();
    for (auto& [id, opt] : Rando::StaticData::Options) {
        if (!opt.name || opt.name[0] == '\0' || !opt.cvar || opt.cvar[0] == '\0') continue;
        nlohmann::json e;
        e["id"]       = opt.name;            // e.g. "RO_LOGIC"
        e["cvar"]     = opt.cvar;
        e["default"]  = (uint32_t)opt.defaultValue;
        e["label"]    = opt.name;            // no friendly label in StaticData; UI may prettify
        e["category"] = categoryFor(id);

        auto ci = comboMaps.find(id);
        auto ii = intRanges.find(id);
        if (ci != comboMaps.end()) {
            e["type"] = "combo";
            // Emit labels indexed 0..N-1 (enum values for MM options are 0-based and contiguous
            // for these maps). Fill gaps with the numeric value as a fallback label.
            int maxKey = 0;
            for (auto& [k, v] : *ci->second) maxKey = std::max(maxKey, k);
            nlohmann::json labels = nlohmann::json::array();
            for (int k = 0; k <= maxKey; ++k) {
                auto it = ci->second->find(k);
                labels.push_back(it != ci->second->end() ? it->second : std::to_string(k));
            }
            e["valueLabels"] = labels;
        } else if (ii != intRanges.end()) {
            e["type"] = "int";
            e["min"]  = ii->second.first;
            e["max"]  = ii->second.second;
        } else {
            e["type"] = "bool";
            e["valueLabels"] = nlohmann::json::array({ "Off", "On" });
        }
        arr.push_back(std::move(e));
    }

    cached = arr.dump();
    return cached.c_str();
}
#endif
```

- [ ] **Step 3: Build 2ship**

Run: `scripts/build-2ship.ps1`
Expected: builds clean. Confirm the export:
```
dumpbin /exports build/x64/mm/Debug/2ship.dll | Select-String "MM_DumpRandoOptions"
```
Expected: `MM_DumpRandoOptions` listed.

- [ ] **Step 4: Commit**

```
git add mm/2s2h/Rando/Menu.cpp
git commit -m "Inc7: add MM_DumpRandoOptions export (id/cvar/default/type/labels/category)"
```

---

## Task 5: OOT option-model adapter (free function, exercised before the window)

**Files:**
- Create (initial stub for the adapter inside the window header is fine, but to verify in isolation, add a free function in the window cpp in Task 6). This task defines the shared `OptionDesc` type used by both adapters.

We fold the OOT adapter into the window cpp (Task 6) since it needs `Rando::Settings` (in-DLL). This task only nails down the shared `OptionDesc` type so Tasks 6 are unambiguous. No build artifact on its own — it is a definition that Task 6 consumes.

- [ ] **Step 1: Record the canonical `OptionDesc` (used verbatim in Task 6 header)**

```cpp
struct OptionDesc {
    std::string id;                       // stable key (enum name)
    std::string cvar;                     // CVar to read/write
    enum Type { Combo, Bool, Int } type;
    std::string label;                    // display name
    std::vector<std::string> valueLabels; // Combo/Bool: index -> label
    int minValue = 0, maxValue = 0;       // Int
    int defaultValue = 0;
    std::string category;                 // sidebar grouping
};
```

(No standalone build/commit — this type is created in Task 6 Step 1.)

---

## Task 6: `ComboRandoWindow` — option model, tabs, sidebar, widgets, action bar, progress overlay

**Files:**
- Create: `combo/gui/ComboRandoWindow.h`
- Create: `combo/gui/ComboRandoWindow.cpp`
- Modify: `soh/CMakeLists.txt` (add the cpp to soh target sources)
- Modify: `soh/soh/SohGui/SohGui.cpp` (register the window in `SetupGuiElements`)
- Modify: `soh/soh/SohGui/SohMenuDevTools.cpp` (menu button)

- [ ] **Step 1: Write the header**

```cpp
// combo/gui/ComboRandoWindow.h
// ComboShip Inc7: combo-owned settings window, compiled into soh.dll.
#pragma once

#include <libultraship/libultraship.h>
#include <string>
#include <vector>
#include "gui/ComboGenProgress.h"

namespace ComboRando {

struct OptionDesc {
    std::string id;
    std::string cvar;
    enum Type { Combo, Bool, Int } type;
    std::string label;
    std::vector<std::string> valueLabels;
    int minValue = 0, maxValue = 0;
    int defaultValue = 0;
    std::string category;
};

class ComboRandoWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    ~ComboRandoWindow() override {}

  protected:
    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override {}

  private:
    void BuildOptionModels();                       // fills mOotOptions / mMmOptions
    void DrawTab(const std::vector<OptionDesc>& opts, const char* tabId);
    void DrawOptionWidget(const OptionDesc& o);
    void DrawActionBar();
    void DrawProgressOverlay();

    std::vector<OptionDesc> mOotOptions;
    std::vector<OptionDesc> mMmOptions;
    bool mModelsBuilt = false;

    char mSeedBuf[128] = { 0 };
    std::string mSelectedOotCategory;
    std::string mSelectedMmCategory;

    ComboGenProgress mProgress;   // single instance; pointer handed to the exe worker
    std::string mStatusLine;
};

} // namespace ComboRando
```

- [ ] **Step 2: Write the cpp — option-model adapters**

```cpp
// combo/gui/ComboRandoWindow.cpp
#include "ComboRandoWindow.h"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/randomizer/settings.h"
#include "soh/Enhancements/randomizer/option.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>

namespace ComboRando {

// OOT adapter: read Rando::Settings in-DLL. Category = top-level OptionGroup name.
static void BuildOotModel(std::vector<OptionDesc>& out) {
    out.clear();
    auto settings = Rando::Settings::GetInstance();
    if (!settings) return;

    // Map cvar -> category by walking the top-level option groups recursively.
    std::unordered_map<std::string, std::string> cvarToCategory;
    std::function<void(const Rando::OptionGroup&, const std::string&)> walk =
        [&](const Rando::OptionGroup& g, const std::string& topName) {
            for (auto* opt : g.GetOptions()) {
                if (opt && !opt->GetCVarName().empty())
                    cvarToCategory[opt->GetCVarName()] = topName;
            }
            for (auto* sub : g.GetSubGroups()) {
                if (sub) walk(*sub, topName);
            }
        };
    for (const auto& g : settings->GetOptionGroups()) {
        walk(g, g.GetName());
    }

    for (const auto& opt : settings->GetAllOptions()) {
        if (opt.GetCVarName().empty()) continue;
        if (opt.IsHidden()) continue;
        if (!opt.IsCategory(OptionCategory::Setting)) continue;  // skip UI-only toggles
        const size_t n = opt.GetOptionCount();
        if (n == 0) continue;

        OptionDesc d;
        d.id    = opt.GetName();
        d.cvar  = opt.GetCVarName();
        d.label = opt.GetName();
        d.defaultValue = 0;
        auto cit = cvarToCategory.find(d.cvar);
        d.category = (cit != cvarToCategory.end()) ? cit->second : "Other";

        for (size_t i = 0; i < n; ++i) d.valueLabels.push_back(opt.GetOptionText(i));
        const bool isOnOff = (n == 2 && d.valueLabels[0] == "Off" && d.valueLabels[1] == "On");
        d.type = isOnOff ? OptionDesc::Bool : OptionDesc::Combo;
        out.push_back(std::move(d));
    }
}

// MM adapter: parse the JSON the exe pushed in via SOH_SetMMRandoOptionsJson.
static void BuildMmModel(std::vector<OptionDesc>& out) {
    out.clear();
    const std::string& json = SOH_GetMMRandoOptionsJson();
    if (json.empty()) return;
    nlohmann::json arr;
    try { arr = nlohmann::json::parse(json); } catch (...) { return; }
    for (auto& e : arr) {
        OptionDesc d;
        d.id       = e.value("id", "");
        d.cvar     = e.value("cvar", "");
        d.label    = e.value("label", d.id);
        d.category = e.value("category", "Other");
        d.defaultValue = e.value("default", 0);
        std::string t = e.value("type", "bool");
        if (t == "int") {
            d.type = OptionDesc::Int;
            d.minValue = e.value("min", 0);
            d.maxValue = e.value("max", 0);
        } else {
            d.type = (t == "combo") ? OptionDesc::Combo : OptionDesc::Bool;
            for (auto& l : e.value("valueLabels", nlohmann::json::array()))
                d.valueLabels.push_back(l.get<std::string>());
            if (d.valueLabels.empty()) { d.valueLabels = { "Off", "On" }; }
        }
        if (d.cvar.empty()) continue;
        out.push_back(std::move(d));
    }
}

void ComboRandoWindow::BuildOptionModels() {
    BuildOotModel(mOotOptions);
    BuildMmModel(mMmOptions);
    mModelsBuilt = true;
}
```

- [ ] **Step 3: Write the cpp — widget rendering**

Append to ComboRandoWindow.cpp:

```cpp
void ComboRandoWindow::DrawOptionWidget(const OptionDesc& o) {
    ImGui::PushID(o.cvar.c_str());
    if (o.type == OptionDesc::Bool) {
        bool v = CVarGetInteger(o.cvar.c_str(), o.defaultValue) != 0;
        if (ImGui::Checkbox(o.label.c_str(), &v)) {
            CVarSetInteger(o.cvar.c_str(), v ? 1 : 0);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    } else if (o.type == OptionDesc::Int) {
        int v = CVarGetInteger(o.cvar.c_str(), o.defaultValue);
        if (ImGui::SliderInt(o.label.c_str(), &v, o.minValue, o.maxValue)) {
            v = std::clamp(v, o.minValue, o.maxValue);
            CVarSetInteger(o.cvar.c_str(), v);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    } else { // Combo
        int v = CVarGetInteger(o.cvar.c_str(), o.defaultValue);
        if (v < 0 || v >= (int)o.valueLabels.size()) v = 0;
        const char* preview = o.valueLabels.empty() ? "" : o.valueLabels[v].c_str();
        if (ImGui::BeginCombo(o.label.c_str(), preview)) {
            for (int i = 0; i < (int)o.valueLabels.size(); ++i) {
                bool sel = (i == v);
                if (ImGui::Selectable(o.valueLabels[i].c_str(), sel)) {
                    CVarSetInteger(o.cvar.c_str(), i);
                    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
    ImGui::PopID();
}

void ComboRandoWindow::DrawTab(const std::vector<OptionDesc>& opts, const char* tabId) {
    // Ordered, de-duplicated category list.
    std::vector<std::string> cats;
    std::set<std::string> seen;
    for (const auto& o : opts) if (seen.insert(o.category).second) cats.push_back(o.category);

    std::string& selected = (std::string(tabId) == "oot") ? mSelectedOotCategory : mSelectedMmCategory;
    if (selected.empty() && !cats.empty()) selected = cats.front();

    ImGui::BeginChild((std::string("sidebar_") + tabId).c_str(), ImVec2(180, 0), true);
    for (const auto& c : cats) {
        if (ImGui::Selectable(c.c_str(), selected == c)) selected = c;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild((std::string("opts_") + tabId).c_str(), ImVec2(0, 0), true);
    for (const auto& o : opts) {
        if (o.category != selected) continue;
        DrawOptionWidget(o);
    }
    ImGui::EndChild();
}

void ComboRandoWindow::DrawActionBar() {
    ImGui::Separator();
    ImGui::Text("Seed");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(240);
    ImGui::InputTextWithHint("##comboSeed", "(blank = random)", mSeedBuf, sizeof(mSeedBuf));
    ImGui::SameLine();
    const bool busy = mProgress.running.load();
    if (busy) ImGui::BeginDisabled();
    if (ImGui::Button("Generate")) {
        mProgress.Reset();
        mProgress.done.store(false);
        mProgress.running.store(true);
        mStatusLine.clear();
        SOH_TriggerComboGenerate(mSeedBuf, &mProgress);   // returns immediately
    }
    if (busy) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!mStatusLine.empty()) ImGui::TextUnformatted(mStatusLine.c_str());
}

void ComboRandoWindow::DrawProgressOverlay() {
    // Poll: when running, show modal-ish overlay; on done, latch status + clear running.
    if (mProgress.done.load() && mProgress.running.load()) {
        if (mProgress.success.load()) {
            char buf[160];
            snprintf(buf, sizeof(buf), "Seed 0x%X - %d cross-world placements",
                     mProgress.seed.load(), mProgress.foreignCount.load());
            mStatusLine = std::string("✓ ") + buf;
        } else {
            mStatusLine = std::string("Error: ") + mProgress.error;
        }
        mProgress.running.store(false);
    }

    if (mProgress.running.load()) {
        int placed = mProgress.placed.load();
        int total  = mProgress.total.load();
        float frac = (total > 0) ? (float)placed / (float)total : 0.0f;
        ImGui::OpenPopup("Generating##combo");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Generating##combo", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Text("%s", ComboGenProgress::PhaseLabel(mProgress.phase.load()));
            ImGui::ProgressBar(frac, ImVec2(360, 0));
            ImGui::Text("%d / %d", placed, total);
            if (mProgress.done.load()) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
}

void ComboRandoWindow::InitElement() {
    // Models are built lazily on first draw (Rando::Settings + MM JSON must be ready).
}

void ComboRandoWindow::DrawElement() {
    if (!mModelsBuilt) BuildOptionModels();

    if (ImGui::BeginTabBar("ComboRandoTabs")) {
        if (ImGui::BeginTabItem("OOT Settings")) { DrawTab(mOotOptions, "oot"); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("MM Settings"))  { DrawTab(mMmOptions, "mm");  ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    DrawActionBar();
    DrawProgressOverlay();
}

} // namespace ComboRando
```

- [ ] **Step 4: Add the cpp to soh's target sources**

In `soh/CMakeLists.txt`, find the source list that feeds `add_library(${PROJECT_NAME} SHARED ${ALL_FILES})` (around line 215-226 the `set(ALL_FILES ...)`). Add the combo window cpp. Append after the existing source-group vars (and guard with COMBO_BUILD so a non-combo soh build is unaffected):

```cmake
# ComboShip Inc7: combo-owned settings window compiled into soh.dll.
set(COMBO_GUI_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../combo/gui/ComboRandoWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../combo/gui/ComboRandoWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../combo/gui/ComboGenProgress.h
)
```

Then add `${COMBO_GUI_FILES}` to the `set(ALL_FILES ...)` list. (The `../combo` include dir is already on soh's PRIVATE include path — confirmed at `soh/CMakeLists.txt` ~line 332 — so `#include "gui/ComboGenProgress.h"` and `#include "Rando/..."` style soh includes resolve.)

- [ ] **Step 5: Register the window in SetupGuiElements**

In `soh/soh/SohGui/SohGui.cpp`, inside `SetupGuiElements()` (after the existing `mAudioEditorWindow` registration ~line 195), add:

```cpp
#ifdef COMBO_BUILD
    mComboRandoWindow = std::make_shared<ComboRando::ComboRandoWindow>(
        CVAR_WINDOW("ComboRando"), "Combo Randomizer", ImVec2(900, 640));
    gui->AddGuiWindow(mComboRandoWindow);
#endif
```

Add the member declaration where the other `mXxxWindow` shared_ptrs are declared in SohGui.cpp (file-scope statics block), and the include at the top:

```cpp
#ifdef COMBO_BUILD
#include "gui/ComboRandoWindow.h"
static std::shared_ptr<ComboRando::ComboRandoWindow> mComboRandoWindow;
#endif
```

- [ ] **Step 6: Add the menu button**

In `soh/soh/SohGui/SohMenuDevTools.cpp`, after the "Console" popout entry (~line 158), add:

```cpp
#ifdef COMBO_BUILD
    path.sidebarName = "Combo Randomizer";
    AddSidebarEntry("Dev Tools", path.sidebarName, 1);
    AddWidget(path, "Open Combo Randomizer", WIDGET_WINDOW_BUTTON)
        .CVar(CVAR_WINDOW("ComboRando"))
        .WindowName("Combo Randomizer")
        .Options(WindowButtonOptions().Tooltip("Configure both games' rando options and Generate."));
#endif
```

- [ ] **Step 7: Build soh**

Run: `scripts/build-soh.ps1`
Expected: builds clean (resolve any include-path nits for `Rando::Settings`/`option.h` — they live under `soh/soh/Enhancements/randomizer/`, reachable via the soh include root). Confirm `soh.dll` mtime advanced.

- [ ] **Step 8: Manual verification (window renders)**

Run `build/x64/combo/Debug/ComboShip.exe`. Open the Dev Tools menu → "Combo Randomizer". Expected:
- Window titled "Combo Randomizer" with two tabs (OOT Settings / MM Settings).
- OOT tab: a category sidebar; selecting a category shows CVar-bound widgets.
- MM tab: may be empty until Task 7 pushes the MM JSON (the bridge is set at exe startup in Task 7). For now OOT tab proves the model + renderer.
- Action bar with Seed field + Generate button (Generate is a no-op-with-warning until Task 7 registers the handler).

- [ ] **Step 9: Commit**

```
git add combo/gui/ComboRandoWindow.h combo/gui/ComboRandoWindow.cpp soh/CMakeLists.txt soh/soh/SohGui/SohGui.cpp soh/soh/SohGui/SohMenuDevTools.cpp
git commit -m "Inc7: add ComboRandoWindow (option model, tabs, widgets, action bar, progress overlay)"
```

---

## Task 7: Generate worker thread + MM-options bridge in the combo exe

**Files:**
- Modify: `combo/ComboShip.cpp` (function-pointer decls ~lines 100-140; symbol resolution ~lines 356-373; refactor `Combo_OnGenerate` ~lines 144-237; callback registration ~lines 499-512)

Refactor the existing `Combo_OnGenerate(int fileNum)` body into a reusable `RunComboFill(const char* inputSeed, ComboGenProgress*)` that takes a seed + progress, then launch it on a worker thread from the registered request handler. Resolve and call the new soh/MM exports.

- [ ] **Step 1: Add includes + new function-pointer typedefs/decls**

Near the top of ComboShip.cpp (with the other includes), add:
```cpp
#include "gui/ComboGenProgress.h"
#include "rando/CrossWorldRando.h"   // if not already included
#include <thread>
#include <atomic>
```

With the other `static Fn... = nullptr;` declarations (~lines 100-140), add:
```cpp
// ComboShip Inc7
typedef void (*FnSetGenReqCb)(void (*)(const char*, ComboRando::ComboGenProgress*));
typedef void (*FnSetSeedGenerated)(uint8_t);
typedef void (*FnSetMMOptionsJson)(const char*);
typedef const char* (*FnDumpOptions)(void);

static FnSetGenReqCb       SOH_SetOnComboGenerateRequestCallback = nullptr;
static FnSetSeedGenerated  SOH_SetSeedGenerated                  = nullptr;
static FnSetMMOptionsJson   SOH_SetMMRandoOptionsJson            = nullptr;
static FnDumpOptions        MM_DumpRandoOptions                  = nullptr;

static std::thread        g_GenerateThread;
static std::atomic<bool>  g_GenerateBusy{ false };
```

- [ ] **Step 2: Resolve the new symbols**

In the symbol-resolution block (~lines 356-373), add:
```cpp
    SOH_SetOnComboGenerateRequestCallback = (FnSetGenReqCb)      GetSym(sohModule, "SOH_SetOnComboGenerateRequestCallback");
    SOH_SetSeedGenerated                  = (FnSetSeedGenerated) GetSym(sohModule, "SOH_SetSeedGenerated");
    SOH_SetMMRandoOptionsJson             = (FnSetMMOptionsJson) GetSym(sohModule, "SOH_SetMMRandoOptionsJson");
    MM_DumpRandoOptions                   = (FnDumpOptions)      GetSym(mmModule,  "MM_DumpRandoOptions");
```

- [ ] **Step 3: Refactor `Combo_OnGenerate` into `RunComboFill(inputSeed, progress)`**

Replace `Combo_OnGenerate(int fileNum)` (lines 144-237) with a seed+progress version. Key changes vs the original: (a) seed derived from `inputSeed` via `Ship_Hash` (replacing `12345u`); (b) `&progress` passed to `CrossWorldCombinedFill`; (c) writes to a fixed slot file `slotGEN.spoiler.json` plus the active slot is unknown at generate time — use slot `0` as the canonical foreign-map slot for now (matches single-active-seed model; the apply path is slot-agnostic for OOT, and the MM slice is stashed and consumed by whatever slot the user then creates). (d) on completion, sets `progress` result fields + `progress.done`.

```cpp
// Resolve Ship_Hash / Ship_Random from libultraship (already used elsewhere in ComboShip).
// If not already declared in this file, declare:
extern "C" uint32_t Ship_Hash(const char* str);     // libultraship export
extern "C" int      Ship_Random(int min, int max);  // libultraship export

static void RunComboFill(std::string inputSeed, ComboRando::ComboGenProgress* progress) {
    auto fail = [&](const char* msg) {
        if (progress) { progress->SetError(msg); progress->success.store(false); progress->done.store(true); }
        std::cerr << "[ComboShip] RunComboFill: " << msg << "\n";
        g_GenerateBusy.store(false);
    };

    if (!SOH_DumpRandoStaticData || !MM_DumpRandoStaticData) { fail("dump functions not resolved"); return; }
    std::string sohDump = SOH_DumpRandoStaticData();
    std::string mmDump  = MM_DumpRandoStaticData();
    if (sohDump.empty() || mmDump.empty()) { fail("empty static-data dump"); return; }

    // Master seed (blank = random), via the games' own primitives.
    if (inputSeed.empty()) inputSeed = std::to_string(Ship_Random(0, 1000000));
    uint32_t masterSeed = Ship_Hash(inputSeed.c_str());

    std::string spoiler;
    bool usedCombinedFill = false;
    if (Combo_SOH_Rando_Reset && Combo_SOH_Rando_SetOwnedItems && Combo_SOH_Rando_GetReachableChecks &&
        Combo_SOH_Rando_PlaceItem && Combo_MM_Rando_Reset && Combo_MM_Rando_SetOwnedItems &&
        Combo_MM_Rando_GetReachableChecks && Combo_MM_Rando_PlaceItem && Combo_MM_Rando_Restore) {

        ComboRando::OracleFns ootOracle = { Combo_SOH_Rando_Reset, Combo_SOH_Rando_SetOwnedItems,
                                            Combo_SOH_Rando_GetReachableChecks, Combo_SOH_Rando_PlaceItem };
        ComboRando::OracleFns mmOracle  = { Combo_MM_Rando_Reset, Combo_MM_Rando_SetOwnedItems,
                                            Combo_MM_Rando_GetReachableChecks, Combo_MM_Rando_PlaceItem };

        auto result = ComboRando::CrossWorldCombinedFill(sohDump, mmDump, masterSeed, ootOracle, mmOracle, "", progress);
        if (result.success) { spoiler = result.spoilerJson; usedCombinedFill = true; }
        else { fail((std::string("combined fill failed: ") + result.error).c_str()); Combo_MM_Rando_Restore(); return; }
        Combo_MM_Rando_Restore();
    }
    if (!usedCombinedFill) { spoiler = ComboRando::CrossWorldGenerateSpoiler(sohDump, mmDump, masterSeed); }

    const int kCanonicalSlot = 0;
    try {
        std::error_code ec;
        std::filesystem::create_directories("saves/combo", ec);
        std::ofstream(std::string("saves/combo/slot") + std::to_string(kCanonicalSlot) + ".spoiler.json",
                      std::ios::trunc) << spoiler;

        auto j = nlohmann::json::parse(spoiler);
        auto foreignArr = j.value("foreign", nlohmann::json::array());
        ComboRando::WriteForeignFromAnnotations(kCanonicalSlot, foreignArr);

        nlohmann::json ootApply = j.value("oot", nlohmann::json::object());
        nlohmann::json mmApply  = j.value("mm",  nlohmann::json::object());
        for (const auto& fm : foreignArr) {
            std::string cg = fm.value("checkGame", ""), cn = fm.value("checkName", "");
            if (cn.empty()) continue;
            if (cg == "oot")     ootApply[cn] = ComboRando::kForeignSentinelNameOOT;
            else if (cg == "mm") mmApply[cn]  = ComboRando::kForeignSentinelNameMM;
        }

        if (SOH_ApplyRandoPlacements) SOH_ApplyRandoPlacements(ootApply.dump().c_str());  // sets seed-generated
        else if (SOH_SetSeedGenerated) SOH_SetSeedGenerated(1);

        g_PendingMMPlacements = mmApply.dump();

        if (progress) {
            progress->seed.store(masterSeed);
            progress->foreignCount.store((int)foreignArr.size());
            progress->success.store(true);
            progress->done.store(true);
        }
        std::cout << "[ComboShip] RunComboFill: done, seed=0x" << std::hex << masterSeed << std::dec
                  << " foreign=" << foreignArr.size() << "\n";
    } catch (const std::exception& e) {
        fail((std::string("post-fill exception: ") + e.what()).c_str());
        return;
    }
    g_GenerateBusy.store(false);
}

// Request handler invoked (via SOH_TriggerComboGenerate) from the window's Generate button.
static void Combo_OnGenerateRequest(const char* inputSeed, ComboRando::ComboGenProgress* progress) {
    if (g_GenerateBusy.exchange(true)) {
        // A run is already in flight; ignore re-entry. (Button is also disabled while running.)
        return;
    }
    if (g_GenerateThread.joinable()) g_GenerateThread.join();
    std::string seed = inputSeed ? inputSeed : "";
    g_GenerateThread = std::thread(RunComboFill, seed, progress);
}
```

Note `g_PendingMMPlacements` is the existing static (ComboShip.cpp:140). `Combo_OnOOTSaveInit` (lines 239-249) consumes it unchanged.

- [ ] **Step 4: Register the new callback + push MM options JSON; stop registering the old save-time generator**

In the startup wiring block (~lines 499-512), replace the old generate-callback registration:

```cpp
    // ComboShip Inc7: register the decoupled generate-REQUEST handler (window-driven),
    // replacing the old save-time auto-generate callback.
    if (SOH_SetOnComboGenerateRequestCallback) {
        SOH_SetOnComboGenerateRequestCallback(Combo_OnGenerateRequest);
        std::cout << "[ComboShip] Combo generate-request handler registered." << std::endl;
    }
    // Bridge MM's options JSON into soh.dll so the in-DLL window can render the MM tab.
    if (MM_DumpRandoOptions && SOH_SetMMRandoOptionsJson) {
        SOH_SetMMRandoOptionsJson(MM_DumpRandoOptions());
        std::cout << "[ComboShip] MM rando options pushed to soh." << std::endl;
    }
```

Delete (or comment out, documented) the old block:
```cpp
    // REMOVED Inc7: save-time auto-generate. Generation is now window-driven.
    // if (SOH_SetOnComboGenerateCallback) { SOH_SetOnComboGenerateCallback(Combo_OnGenerate); ... }
```

Keep `SOH_SetOnNewSaveCallback(Combo_OnOOTSaveInit)` and the scene-switch callback as-is.

- [ ] **Step 5: Build ComboShip**

Run: `scripts/build-comboship.ps1`
Expected: builds clean. (If `Ship_Hash`/`Ship_Random` are already declared via a libultraship header included in ComboShip.cpp, drop the local `extern "C"` decls to avoid a redefinition.)

- [ ] **Step 6: Manual verification (Generate works, progress shows, MM tab populated)**

Run the exe. Open Combo Randomizer:
- MM Settings tab now lists options (Logic & Access, Shuffles, Items & Traps, Starting categories).
- Enter a seed (e.g. `test1`), press Generate. Expected: a modal progress bar that **moves** through "Placing items" then closes; status line `✓ Seed 0x… · N cross-world placements`.
- Press Generate again with the same seed → same seed value in status; blank seed → different each run.
- Console log shows `RunComboFill: done, seed=…`.

- [ ] **Step 7: Commit**

```
git add combo/ComboShip.cpp
git commit -m "Inc7: window-driven threaded Generate worker + MM options bridge"
```

---

## Task 8: Decouple save creation from generation

**Files:**
- Modify: `soh/src/code/z_sram.c` (existing `COMBO_BUILD` block at lines 274-277 inside `Sram_InitSave`)

The OOT placements + seed-generated are now applied by the worker (Task 7) before the player can press "Start Game" (the seed-gate enforces ordering). Save creation must stop invoking the fill but must STILL force `QUEST_RANDOMIZER`.

- [ ] **Step 1: Neutralize the generate call, keep questType forcing**

Replace the existing block (lines 274-277):
```c
    if (gComboGenerateCallback != NULL) {
        gComboGenerateCallback((int)gSaveContext.fileNum);
        fileChooseCtx->questType[fileChooseCtx->buttonIndex] = QUEST_RANDOMIZER;
    }
```
with:
```c
    // ComboShip Inc7: generation is now window-driven (decoupled from save creation).
    // The OOT placements + seed-generated flag are already applied by the time the
    // player can create a save (the seed-gate enforces this). Here we only force the
    // rando quest type; the old gComboGenerateCallback fill is intentionally not run.
    fileChooseCtx->questType[fileChooseCtx->buttonIndex] = QUEST_RANDOMIZER;
    (void)gComboGenerateCallback; // retained symbol; no longer invoked under Inc7
```

- [ ] **Step 2: Build soh + ComboShip**

Run:
```
scripts/build-soh.ps1
scripts/build-comboship.ps1
```
Expected: clean.

- [ ] **Step 3: Manual verification (gate + consume)**

Run the exe. On the file-select screen, BEFORE pressing Generate:
- Attempt "Start Randomizer" / create a file. Expected: **blocked** by the seed-gate (`Randomizer_IsSeedGenerated()` false) — no preset modal / file-name entry.

Press Generate (window) → wait for success. Then:
- Create the file. Expected: file creates; OOT save uses the generated placements; `Combo_OnOOTSaveInit` logs creating a RANDO MM save from the stashed slice (`MM_InitRandoSaveFile`).
- Verify in-game that a few shuffled OOT checks match `saves/combo/slot0.spoiler.json` and cross-world (foreign) checks route through the mailbox.

**Verification checkpoint (spec §3):** confirm OOT's rando `Context` (`itemLocationTable`) is NOT reset between the Generate press and save creation. If the placements are lost (checks back to vanilla), add re-apply of the stashed OOT placements at save-init: stash `ootApply.dump()` in a `g_PendingOOTPlacements` (mirror of the MM stash) in Task 7 Step 3, and call `SOH_ApplyRandoPlacements` again from `Combo_OnOOTSaveInit` before MM init. Only do this if the checkpoint fails.

- [ ] **Step 4: Commit**

```
git add soh/src/code/z_sram.c
git commit -m "Inc7: decouple save creation from generation (window-driven)"
```

---

## Task 9: Master-seed propagation for in-game RNG parity

**Files:**
- Modify: `combo/ComboShip.cpp` (`RunComboFill`, after computing `masterSeed`)

So in-game randomization (traps, cosmetics) is deterministic from the one master seed, propagate `inputSeed` to each game's own seed model before its save is written: set MM's `gRando.InputSeed` CVar and OOT's input-seed CVar from the same `inputSeed`.

- [ ] **Step 1: Set the per-game input-seed CVars**

In `RunComboFill`, immediately after `uint32_t masterSeed = Ship_Hash(inputSeed.c_str());`, add:
```cpp
    // ComboShip Inc7: propagate the master input seed into each game's own seed model
    // so in-game RNG (traps/cosmetics) is deterministic from the one seed.
    // CVar store is shared across DLLs, so these are visible to both games.
    CVarSetString("gRando.InputSeed", inputSeed.c_str());           // MM's model (OnFileCreate reads this)
    CVarSetString("gRandomizerSeed", inputSeed.c_str());            // OOT input seed (verify exact CVar name)
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
```

**Verify the OOT input-seed CVar name** before relying on it: grep OOT's `GenerateRandomizer` for the CVar it reads (`CVarGetString`). Use that exact name in place of `gRandomizerSeed` if different. If `Ship::Context`/`CVarSetString` aren't already included in ComboShip.cpp, prefer the plain `CVarSetString` C-API (libultraship export) without the GUI save call, since the exe may not own the GUI instance — in that case drop the `SaveConsoleVariablesNextFrame()` line.

- [ ] **Step 2: Build ComboShip**

Run: `scripts/build-comboship.ps1`
Expected: clean.

- [ ] **Step 3: Manual verification (determinism)**

Generate twice with the same fixed seed; create a save each time. Expected: identical `slot0.spoiler.json` both runs; in-game trap/cosmetic behavior consistent. Blank seed → differs per run.

- [ ] **Step 4: Commit**

```
git add combo/ComboShip.cpp
git commit -m "Inc7: propagate master seed to per-game RNG for parity"
```

---

## Task 10: Document seams + final verification

**Files:**
- Modify: `docs/UPSTREAM_MERGES.md` (one entry per vendored seam, with WHY — per the `document-post-merge-changes` memory)

- [ ] **Step 1: Add UPSTREAM_MERGES.md entries**

Append a "## Inc7 — Combined Settings Window" section documenting each vendored edit and why it's combo-only:
- `soh/soh/OTRGlobals.cpp` + `.h`: new exports `SOH_SetOnComboGenerateRequestCallback`, `SOH_TriggerComboGenerate`, `SOH_SetSeedGenerated`, `SOH_SetMMRandoOptionsJson` (+ in-DLL `SOH_GetMMRandoOptionsJson`). WHY: decoupled window-driven generation + cross-DLL MM-options bridge.
- `soh/soh/SohGui/SohGui.cpp`: register `ComboRandoWindow`. WHY: combo window hosted in soh.dll (owns the menu slot at boot).
- `soh/soh/SohGui/SohMenuDevTools.cpp`: menu button. WHY: reachable before file-select.
- `soh/CMakeLists.txt`: compile `combo/gui/ComboRandoWindow.cpp` into soh. WHY: window source is combo-owned but hosted in soh.dll.
- `soh/src/code/z_sram.c`: neutralized the save-time generate call; still forces `QUEST_RANDOMIZER`. WHY: generation moved to the window; save only consumes.
- `mm/2s2h/Rando/Menu.cpp`: `MM_DumpRandoOptions` export + `comboClockModeOptions`. WHY: surface MM option metadata + Menu.cpp label maps to the combo window.

Confirm each code seam carries a `// ComboShip:` comment (added in earlier tasks).

- [ ] **Step 2: Full clean verification pass (spec §8 checklist)**

Build all three in order, then run:
```
scripts/build-libultraship.ps1   # only if libultraship changed (it didn't — skip)
scripts/build-soh.ps1
scripts/build-2ship.ps1
scripts/build-comboship.ps1
```
Then verify in the running exe:
- [ ] Toggling an option changes which checks are shuffled in `saves/combo/slot0.spoiler.json`.
- [ ] Fixed seed twice → identical spoiler; blank seed → differs each run.
- [ ] Generate shows a moving progress bar, then status line (seed + foreign count); an induced error surfaces in the status line.
- [ ] "Start Game"/file create is blocked until Generate has run; allowed after.
- [ ] Window opens before file-select and persists across an OOT↔MM transition.
- [ ] No crash; if one occurs, capture `x64/Debug/combo_abort_stack.txt`.

- [ ] **Step 3: Commit**

```
git add docs/UPSTREAM_MERGES.md
git commit -m "Inc7: document settings-window seams in UPSTREAM_MERGES"
```

- [ ] **Step 4: Finish the branch**

Use superpowers:finishing-a-development-branch to decide merge/PR/cleanup for the `randomizer` branch work.

---

## Open Risks / Checkpoints (carried from spec §9)

| Risk | Where addressed |
|------|-----------------|
| MM oracle called from a worker thread (cross-DLL, MM paused) is thread-unsafe | Task 7. If it crashes/corrupts, fall back to cooperative main-thread stepping (a few placements/frame driven from `UpdateElement`), still updating the bar. |
| OOT rando Context reset between Generate and save creation | Task 8 Step 3 checkpoint; re-apply stashed OOT placements at save-init if it fails. |
| MM `OnFileCreate` self-generation double-runs | `MM_InitRandoSaveFile` already writes the MM save headlessly (combo path). Verify in Task 8 Step 3 that `OnFileCreate`'s self-generate doesn't also run for the combo slot; gate under `COMBO_BUILD` in `mm/2s2h/Rando/MiscBehavior/OnFileCreate.cpp` if it does. |
| OOT input-seed CVar name guess wrong | Task 9 Step 1 — verify the exact `CVarGetString` key in OOT's `GenerateRandomizer` before relying on it. |
| MM combo enum values not 0-based/contiguous | Task 4 emits a numeric fallback label for gaps; verify the four combo options render correct labels in Task 7 Step 6. |
```

