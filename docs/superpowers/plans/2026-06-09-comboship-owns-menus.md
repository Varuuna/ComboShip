# ComboShip Owns the Menus — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make comboui.dll own and render both games' settings menus from combo-owned state, so OOT and MM settings work regardless of which game is foreground, with no dependency on a backgrounded game's live render state.

**Architecture:** Each game walks its existing `menuEntries` tree and emits it as a flat **C-ABI descriptor array** (no C++ structs cross the DLL boundary). comboui ingests the array once and renders declarative widgets itself (own framework/font/shared ImGui context, shared CVar store). Non-declarative slices — `WIDGET_CUSTOM` draw, `preFunc` disable-evaluation, and `.Callback()` apply — are invoked **by index** back into the owning (resident) game DLL, wrapped in a new `ResourceManagerScope` RAII guard. Behavior callbacks follow a three-bucket policy; game-loop-dependent actions are gated to the foreground game. Cross-game items remain out of scope (existing CrossMailbox).

**Tech Stack:** C++20, CMake, libultraship (shared engine), Dear ImGui, GoogleTest (libultraship test target only), Windows DLLs (`GetModuleHandleA`/`GetProcAddress` resolution).

**Reference spec:** `docs/superpowers/specs/2026-06-09-comboship-owns-menus-design.md`

---

## Verification Model (read first)

This is a game-engine UI project. There is **no UI unit-test harness**. Verification per task is one of:

- **BUILD**: `cmake --build build --target <t>` for the affected target(s) — must compile/link clean.
- **GTEST**: only for logic added to libultraship (`-DLUS_BUILD_TESTS=ON`, then `ctest --test-dir build --output-on-failure -R <name>`).
- **RUNTIME**: launch `ComboShip.exe`, perform the stated interaction, observe the stated result (no crash, correct render/behavior). Runtime checks are explicit checkpoints, not skippable.

Build targets (from README): `libultraship`, `soh`, `2ship`, `comboui`, `ComboShip`. Build only the targets a task touches (per memory: don't rebuild everything).

Commit after every task. Branch is `feat/combo-unified-menu`.

---

## File Structure

**New (combo-owned, shared C-ABI — included by all three modules):**
- `combo/menu/ComboMenuABI.h` — flat C-ABI descriptor structs + export function signatures. One responsibility: the cross-DLL menu contract. No C++ containers, only PODs + `const char*` + indices.

**New (libultraship engine primitive):**
- `libultraship/include/ship/resource/ResourceManagerScope.h` — RAII active-RM swap/restore.
- `libultraship/tests/resource_manager_scope_tests.cpp` — gtest for the guard.

**New (combo render of neutral data):**
- `combo/gui/ComboWidgetRender.h` / `.cpp` — renders one `ComboWidgetDesc` using combo's own UIWidgets calls + shared CVars; routes custom/pre/callback by index. One responsibility: neutral-desc → ImGui.
- `combo/gui/ComboMenuModel.h` / `.cpp` — holds the ingested descriptor arrays per game and the resolved invoke-export pointers. One responsibility: own the captured menu data + game export handles.

**Modified — OOT (soh.dll):**
- `soh/soh/SohGui/SohMenu.h` / `.cpp` — add a tree-walk emitter + by-index invoke helpers.
- `soh/soh/OTRGlobals.cpp` — add `SOH_ExportMenu` / `SOH_MenuInvoke*` exports near `SOH_DrawSettings` (2552); later remove `SOH_DrawSettings` delegation.
- `soh/soh/SohGui/SohGui.cpp` — remove the in-port menu install under `COMBO_BUILD`.

**Modified — MM (2ship.dll):**
- `mm/2s2h/BenGui/BenMenu.h` / `.cpp` — same emitter + invoke helpers.
- `mm/2s2h/BenPort.cpp` — add `MM_ExportMenu` / `MM_MenuInvoke*` near `MM_DrawSettings` (3082); later remove `MM_DrawSettings` + the interim guard.
- `mm/2s2h/BenGui/BenGui.cpp` — remove the `COMBO_BUILD` `SetupMenu`/`ActivateMenu` install path.

**Modified — combo:**
- `combo/gui/ComboMenu.h` / `.cpp` — switch game tabs from `SOH_/MM_DrawSettings` to model-driven render.
- `combo/CMakeLists.txt` — add new comboui sources; add `combo/menu` include dir to soh/2ship/comboui.

---

## Phase 0 — Spike: prove the core bet

The riskiest assumption is that a per-frame callback into a **backgrounded** game DLL can render a `WIDGET_CUSTOM` widget and evaluate a `preFunc` without faulting, under combo's ImGui context + an RM scope. Prove it before building the pipeline. The spike is throwaway and reverted at the end of the phase.

### Task 0.1: Temporary spike exports in MM

**Files:**
- Modify: `mm/2s2h/BenPort.cpp` (near `MM_DrawSettings`, ~3082)

- [ ] **Step 1: Add two throwaway exports that exercise a known custom widget + a preFunc-gated widget**

```cpp
// ComboShip SPIKE (Phase 0, revert in Task 0.3): prove a backgrounded MM DLL can
// render a custom widget and evaluate a preFunc under comboui's ImGui context.
extern "C" __declspec(dllexport) void MM_SpikeDrawWarpCustom(void) {
    auto sctx = Ship::Context::GetInstance();
    if (sctx && sctx->GetWindow() && sctx->GetWindow()->GetGui()) {
        ImGui::SetCurrentContext(sctx->GetWindow()->GetGui()->GetImGuiContext());
    }
    // "Warp Point" custom body (mm/2s2h/BenGui/BenMenu.cpp:2053 calls RenderWarpPointSection()).
    RenderWarpPointSection();
}

// Returns 1 if the widget would be disabled (preFunc set info.options->disabled), else 0.
// Uses a "Generate"-style preFunc condition that reads gPlayState/save — must not fault when MM is dormant.
extern "C" __declspec(dllexport) int MM_SpikeEvalDisabled(void) {
    return (gPlayState == nullptr) ? 1 : 0; // mirrors DISABLE_FOR_NULL_PLAY_STATE evaluation
}
```

- [ ] **Step 2: Build MM**

Run: `cmake --build build --target 2ship`
Expected: links clean (`RenderWarpPointSection` is in BenMenu.cpp scope; if static, temporarily expose it).

- [ ] **Step 3: Commit**

```bash
git add mm/2s2h/BenPort.cpp
git commit -m "spike: temp MM exports to prove backgrounded custom/preFunc render"
```

### Task 0.2: Drive the spike from comboui and runtime-verify

**Files:**
- Modify: `combo/gui/ComboMenu.cpp` (MM tab path, `DrawGamePanel`)

- [ ] **Step 1: Temporarily call the spike exports from the MM tab**

```cpp
// ComboShip SPIKE: resolve + call the throwaway MM exports instead of MM_DrawSettings.
typedef void (*FnVoid)(void);
typedef int  (*FnInt)(void);
static FnVoid sSpikeDraw = nullptr;
static FnInt  sSpikeEval = nullptr;
if (!sSpikeDraw) {
    HMODULE h = GetModuleHandleA("2ship.dll");
    if (h) { sSpikeDraw = (FnVoid)GetProcAddress(h, "MM_SpikeDrawWarpCustom");
             sSpikeEval = (FnInt)GetProcAddress(h, "MM_SpikeEvalDisabled"); }
}
if (sSpikeEval) ImGui::Text("MM preFunc disabled = %d", sSpikeEval());
if (sSpikeDraw) sSpikeDraw();
```

- [ ] **Step 2: Build comboui**

Run: `cmake --build build --target comboui`
Expected: links clean.

- [ ] **Step 3: RUNTIME — verify on the BACKGROUNDED game**

Run: launch `ComboShip.exe`, stay in **OOT** (MM is backgrounded), open the menu, select the **MM** tab.
Expected: the MM Warp custom section renders with no crash; `MM preFunc disabled` shows `1` while MM has no loaded save. Switch to MM, return — still no crash. This confirms the per-frame callback into a dormant DLL is safe under comboui's context.

- [ ] **Step 4: Commit the spike result note**

```bash
git add combo/gui/ComboMenu.cpp
git commit -m "spike: drive MM custom/preFunc from comboui MM tab (backgrounded) — verified"
```

### Task 0.3: Revert the spike

- [ ] **Step 1: Revert both spike commits, keep the knowledge**

```bash
git revert --no-edit HEAD HEAD~1
```

- [ ] **Step 2: Build to confirm clean baseline**

Run: `cmake --build build --target 2ship --target comboui`
Expected: clean. Spike proven; pipeline build begins.

---

## Phase 1 — `ResourceManagerScope` engine primitive

### Task 1.1: Add the RAII guard

**Files:**
- Create: `libultraship/include/ship/resource/ResourceManagerScope.h`

- [ ] **Step 1: Write the guard**

```cpp
// libultraship/include/ship/resource/ResourceManagerScope.h
// ComboShip: temporarily make a specific ResourceManager the Context's active one,
// restoring the previous one on scope exit. Safe because render and game loop are
// sequential on one thread (no concurrent active-RM access). See
// docs/superpowers/specs/2026-06-09-comboship-owns-menus-design.md.
#pragma once

#include <memory>
#include "ship/Context.h"
#include "ship/resource/ResourceManager.h"

namespace Ship {
class ResourceManagerScope {
  public:
    explicit ResourceManagerScope(std::shared_ptr<ResourceManager> target) {
        auto ctx = Context::GetInstance();
        if (ctx && target) {
            mPrevious = ctx->GetResourceManager();
            if (mPrevious != target) {
                ctx->SetResourceManager(target);
                mSwapped = true;
            }
        }
    }
    ~ResourceManagerScope() {
        if (mSwapped) {
            if (auto ctx = Context::GetInstance()) {
                ctx->SetResourceManager(mPrevious);
            }
        }
    }
    ResourceManagerScope(const ResourceManagerScope&) = delete;
    ResourceManagerScope& operator=(const ResourceManagerScope&) = delete;

  private:
    std::shared_ptr<ResourceManager> mPrevious;
    bool mSwapped = false;
};
} // namespace Ship
```

- [ ] **Step 2: BUILD libultraship**

Run: `cmake --build build --target libultraship`
Expected: clean (header-only; verify includes resolve — adjust `ship/Context.h` path to match the repo's include layout if needed).

### Task 1.2: GTest the guard

**Files:**
- Create: `libultraship/tests/resource_manager_scope_tests.cpp`
- Modify: `libultraship/tests/CMakeLists.txt` (add the file to `lus_tests`)

- [ ] **Step 1: Write the failing test**

```cpp
#include <gtest/gtest.h>
#include "ship/Context.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/ResourceManagerScope.h"

// Verifies the active RM is swapped within scope and restored after.
TEST(ResourceManagerScope, SwapsAndRestores) {
    auto ctx = Ship::Context::GetInstance();
    ASSERT_NE(ctx, nullptr);
    auto original = ctx->GetResourceManager();
    auto other = std::make_shared<Ship::ResourceManager>();
    {
        Ship::ResourceManagerScope scope(other);
        EXPECT_EQ(ctx->GetResourceManager(), other);
    }
    EXPECT_EQ(ctx->GetResourceManager(), original);
}
```

- [ ] **Step 2: Add to test target**

In `libultraship/tests/CMakeLists.txt`, add `resource_manager_scope_tests.cpp` to the `add_executable(lus_tests ...)` list.

- [ ] **Step 3: Configure with tests + build**

Run: `cmake -DLUS_BUILD_TESTS=ON -S libultraship -B build-lus-tests && cmake --build build-lus-tests --target lus_tests`
Expected: compiles. (If `Context::GetInstance()` is null in the test harness, gate the test with `if (!ctx) GTEST_SKIP();` and instead assert restore semantics against a mocked Context — keep the swap/restore assertion.)

- [ ] **Step 4: Run the test**

Run: `ctest --test-dir build-lus-tests --output-on-failure -R ResourceManagerScope`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libultraship/include/ship/resource/ResourceManagerScope.h libultraship/tests/resource_manager_scope_tests.cpp libultraship/tests/CMakeLists.txt
git commit -m "feat(lus): ResourceManagerScope RAII active-RM guard + test"
```

---

## Phase 2 — The C-ABI menu contract

### Task 2.1: Define `ComboMenuABI.h`

**Files:**
- Create: `combo/menu/ComboMenuABI.h`
- Modify: `combo/CMakeLists.txt` (add `${CMAKE_SOURCE_DIR}/combo/menu` to include dirs of soh, 2ship, comboui)

- [ ] **Step 1: Write the contract header**

```c
/* combo/menu/ComboMenuABI.h
 * ComboShip: flat C-ABI menu contract. The games' C++ WidgetInfo / std::function /
 * options structs CANNOT cross the DLL boundary (ABI-diverged: std::function vs raw
 * ptr, differing enums/fields). So each game emits this POD array; comboui renders
 * declarative rows itself and invokes custom/preFunc/callback BY INDEX back into the
 * owning DLL. Strings point into the game's static storage (valid for process life).
 */
#ifndef COMBO_MENU_ABI_H
#define COMBO_MENU_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Normalized widget kind — the UNION of OOT and MM WidgetType. comboui renders these. */
typedef enum {
    CW_SEPARATOR = 0,
    CW_SEPARATOR_TEXT,
    CW_TEXT,
    CW_CHECKBOX,        /* CVar bool */
    CW_SLIDER_INT,      /* CVar int  */
    CW_SLIDER_FLOAT,    /* CVar float */
    CW_COMBOBOX,        /* CVar int, choices in options */
    CW_INPUT_TEXT,      /* OOT WIDGET_INPUT/CVAR_INPUT */
    CW_COLOR,           /* color picker (useAlpha flag distinguishes 24/32) */
    CW_BUTTON,          /* fires callback by index */
    CW_WINDOW_BUTTON,   /* toggles a window CVar */
    CW_AUDIO_BACKEND,
    CW_VIDEO_BACKEND,
    CW_CUSTOM           /* drawn entirely by the owning game via index */
} CwKind;

typedef struct {
    const char* label;       /* "" choices for combobox are pipe-delimited in choices */
    int32_t     value;       /* combobox: CVar value for this entry */
} CwChoice;

typedef struct {
    int32_t      index;        /* stable index into the game's flat widget list (the invoke key) */
    CwKind       kind;
    const char*  name;         /* display label */
    const char*  cvar;         /* CVar backing this widget, or "" */
    const char*  tooltip;      /* "" if none */
    const char*  windowName;   /* for CW_WINDOW_BUTTON, else "" */
    /* numeric ranges (sliders) */
    float        fMin, fMax, fStep, fDefault;
    int32_t      iMin, iMax, iStep, iDefault;
    int32_t      bDefault;     /* checkbox default */
    int32_t      useAlpha;     /* CW_COLOR: 1 if 32-bit */
    /* combobox choices */
    const CwChoice* choices;
    int32_t         choiceCount;
    /* capability flags */
    int32_t      hasCallback;  /* widget has a .Callback() to run after a CVar change */
    int32_t      hasPreFunc;   /* widget has a per-frame disable evaluation */
    int32_t      sameLine;
    int32_t      hideInSearch;
} CwWidget;

typedef struct {
    const char*       sidebarName;
    uint32_t          columnCount;
    const CwWidget*   widgets;    /* flattened across columns; column carried in colIndex */
    int32_t           widgetCount;
} CwSidebar;

typedef struct {
    const char*       sectionLabel;   /* e.g. "Settings", "Enhancements" */
    const char*       sidebarCvar;
    const CwSidebar*  sidebars;
    int32_t           sidebarCount;
} CwSection;

typedef struct {
    int32_t           version;        /* ABI version; bump on layout change */
    const CwSection*  sections;
    int32_t           sectionCount;
} CwMenu;

/* === Per-game exports (implemented in soh.dll / 2ship.dll) ===
 * Export: build the CwMenu once (cached) and return a stable pointer (lives for the
 * process). Invoke: run the owning game's logic for the widget at `index`.
 */
typedef const CwMenu* (*Fn_ExportMenu)(void);
typedef void (*Fn_MenuInvokeCallback)(int32_t index); /* run .Callback() after CVar write */
typedef int32_t (*Fn_MenuEvalDisabled)(int32_t index, const char** outReason); /* run preFunc; 1=disabled */
typedef void (*Fn_MenuDrawCustom)(int32_t index);     /* draw WIDGET_CUSTOM body */

#ifdef __cplusplus
}
#endif
#endif /* COMBO_MENU_ABI_H */
```

- [ ] **Step 2: Wire include dirs**

In `combo/CMakeLists.txt`, add to the `target_include_directories` of `comboui` (and ensure soh/2ship targets also see it — add via their CMake or a shared interface):

```cmake
target_include_directories(comboui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/menu)
```

For soh/2ship, add the same `combo/menu` path to their include dirs (in their respective CMake target or the top-level wiring).

- [ ] **Step 3: BUILD comboui (header compiles)**

Run: `cmake --build build --target comboui`
Expected: clean (header included by a temporary `#include` in ComboMenu.cpp to force compile; remove after).

- [ ] **Step 4: Commit**

```bash
git add combo/menu/ComboMenuABI.h combo/CMakeLists.txt
git commit -m "feat(combo): C-ABI menu contract (ComboMenuABI.h)"
```

---

## Phase 3 — Per-game emitters + invoke-by-index

This phase is symmetric across OOT and MM. Do OOT first (Task 3.1–3.2), then MM (Task 3.3–3.4) with the same structure. The emitter walks the existing `menuEntries` map and a flat `std::vector<WidgetInfo*>` built in declaration order; the index into that vector is the ABI invoke key.

### Task 3.1: OOT emitter — flatten `menuEntries` to `CwMenu`

**Files:**
- Modify: `soh/soh/SohGui/SohMenu.h` (add public emitter + flat-list accessor)
- Modify: `soh/soh/SohGui/SohMenu.cpp`

- [ ] **Step 1: Add a flat widget index + emitter declaration**

In `SohMenu.h` (public):

```cpp
// ComboShip: emit this menu as flat C-ABI data for comboui, and expose by-index invoke.
const CwMenu* ExportComboMenu();          // builds + caches; pointer stable for process life
void   InvokeCallbackByIndex(int32_t i);  // runs widget i's .Callback()
int32_t EvalDisabledByIndex(int32_t i, const char** outReason); // runs preFunc; 1=disabled
void   DrawCustomByIndex(int32_t i);      // runs widget i's customFunction
```

Add `#include "ComboMenuABI.h"` to `SohMenu.h`.

- [ ] **Step 2: Implement the emitter (walk + flatten + map kinds)**

In `SohMenu.cpp`. Key logic: iterate `menuEntries` (and its `sidebarOrder`/`columnWidgets`), push each `WidgetInfo*` to a member `std::vector<WidgetInfo*> mFlat;`, and fill parallel `std::vector<CwWidget>` / `CwSidebar` / `CwSection` members (kept alive as members so the returned pointers stay valid). Map OOT `WidgetType` → `CwKind`:

```cpp
static CwKind MapKind(WidgetType t) {
    switch (t) {
        case WIDGET_SEPARATOR: return CW_SEPARATOR;
        case WIDGET_SEPARATOR_TEXT: return CW_SEPARATOR_TEXT;
        case WIDGET_TEXT: return CW_TEXT;
        case WIDGET_CHECKBOX: case WIDGET_CVAR_CHECKBOX: return CW_CHECKBOX;
        case WIDGET_SLIDER_INT: case WIDGET_CVAR_SLIDER_INT: return CW_SLIDER_INT;
        case WIDGET_SLIDER_FLOAT: case WIDGET_CVAR_SLIDER_FLOAT: return CW_SLIDER_FLOAT;
        case WIDGET_COMBOBOX: case WIDGET_CVAR_COMBOBOX: case WIDGET_CVAR_BTN_SELECTOR: return CW_COMBOBOX;
        case WIDGET_INPUT: case WIDGET_CVAR_INPUT: return CW_INPUT_TEXT;
        case WIDGET_CVAR_COLOR_PICKER: case WIDGET_COLOR_PICKER: return CW_COLOR;
        case WIDGET_BUTTON: return CW_BUTTON;
        case WIDGET_WINDOW_BUTTON: return CW_WINDOW_BUTTON;
        case WIDGET_AUDIO_BACKEND: return CW_AUDIO_BACKEND;
        case WIDGET_VIDEO_BACKEND: return CW_VIDEO_BACKEND;
        case WIDGET_SEARCH: return CW_TEXT;   // search box rendered combo-side, skip game search
        case WIDGET_CUSTOM: default: return CW_CUSTOM;
    }
}
```

For each `WidgetInfo& w` at flat index `i`: set `CwWidget{ .index=i, .kind=MapKind(w.type), .name=w.name.c_str(), .cvar=w.cVar?w.cVar:"", .tooltip=w.options?w.options->tooltip.c_str():"", .hasCallback=(w.callback!=nullptr), .hasPreFunc=(w.preFunc!=nullptr), .sameLine=w.sameLine, .hideInSearch=w.hideInSearch, ... }`. Pull numeric ranges and combobox choices by `dynamic_pointer_cast` on `w.options` to the concrete options type (`IntSliderOptions`, `FloatSliderOptions`, `ComboboxOptions`, `ColorPickerOptions`) and copy fields (`min/max/step/defaultValue`, `comboMap` → owned `std::vector<CwChoice>` members). Cache the built `CwMenu` (build once, return cached pointer thereafter).

- [ ] **Step 3: Implement the three by-index invokers**

```cpp
void SohMenu::InvokeCallbackByIndex(int32_t i) {
    if (i < 0 || i >= (int)mFlat.size()) return;
    WidgetInfo* w = mFlat[i];
    if (w && w->callback) w->callback(*w);
}
int32_t SohMenu::EvalDisabledByIndex(int32_t i, const char** outReason) {
    if (i < 0 || i >= (int)mFlat.size()) return 0;
    WidgetInfo* w = mFlat[i];
    if (!w || !w->preFunc) return 0;
    w->ResetDisables();
    w->preFunc(*w);
    if (w->options && w->options->disabled && outReason) *outReason = w->options->disabledTooltip.c_str();
    return (w && w->options && w->options->disabled) ? 1 : 0;
}
void SohMenu::DrawCustomByIndex(int32_t i) {
    if (i < 0 || i >= (int)mFlat.size()) return;
    WidgetInfo* w = mFlat[i];
    if (w && w->customFunction) w->customFunction(*w);
}
```

- [ ] **Step 4: BUILD soh**

Run: `cmake --build build --target soh`
Expected: clean.

- [ ] **Step 5: Commit**

```bash
git add soh/soh/SohGui/SohMenu.h soh/soh/SohGui/SohMenu.cpp
git commit -m "feat(soh): emit menu as C-ABI + invoke-by-index helpers"
```

### Task 3.2: OOT C exports

**Files:**
- Modify: `soh/soh/OTRGlobals.cpp` (near `SOH_DrawSettings`, 2552)

- [ ] **Step 1: Add the exports delegating to the SohMenu singleton**

```cpp
// ComboShip: combo-owned menu data + by-index invoke (see ComboMenuABI.h).
extern "C" __declspec(dllexport) const CwMenu* SOH_ExportMenu(void) {
    return SohGui::mSohMenu ? SohGui::mSohMenu->ExportComboMenu() : nullptr;
}
extern "C" __declspec(dllexport) void SOH_MenuInvokeCallback(int32_t i) {
    if (SohGui::mSohMenu) SohGui::mSohMenu->InvokeCallbackByIndex(i);
}
extern "C" __declspec(dllexport) int32_t SOH_MenuEvalDisabled(int32_t i, const char** outReason) {
    return SohGui::mSohMenu ? SohGui::mSohMenu->EvalDisabledByIndex(i, outReason) : 0;
}
extern "C" __declspec(dllexport) void SOH_MenuDrawCustom(int32_t i) {
    auto ctx = Ship::Context::GetInstance();
    if (ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui())
        ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());
    // Phase 0 spike contract: comboui owns the menu slot, so the Gui loop never drives this
    // menu's lifecycle. A custom widget reads THEME_COLOR (menuThemeIndex), which is set in
    // UpdateElement() — so Init()+Update() BEFORE invoking, else ColorValues.at() throws
    // out_of_range. Mirror this in MM_MenuDrawCustom.
    if (SohGui::mSohMenu) {
        SohGui::mSohMenu->Init();
        SohGui::mSohMenu->Update();
        SohGui::mSohMenu->DrawCustomByIndex(i);
    }
}
```

(Confirm the accessor for the SohMenu instance — `SohGui::mSohMenu` per SohGui.cpp; adjust to the actual symbol. The instance must still be constructed at boot even though it no longer calls `gui->SetMenu` — keep `SetupMenu()` building it.)

- [ ] **Step 2: BUILD soh**

Run: `cmake --build build --target soh`
Expected: clean; exports present.

- [ ] **Step 3: Commit**

```bash
git add soh/soh/OTRGlobals.cpp
git commit -m "feat(soh): export SOH_ExportMenu + SOH_MenuInvoke* for comboui"
```

### Task 3.3: MM emitter

**Files:**
- Modify: `mm/2s2h/BenGui/BenMenu.h`, `mm/2s2h/BenGui/BenMenu.cpp`

- [ ] **Step 1: Mirror Task 3.1 for MM.** Same public methods (`ExportComboMenu`, `InvokeCallbackByIndex`, `EvalDisabledByIndex`, `DrawCustomByIndex`), same flat `mFlat` vector. Adjust the kind map for MM's enum differences: MM has `WIDGET_COLOR_24`/`WIDGET_COLOR_32` (→ `CW_COLOR`, set `useAlpha=1` for `_32`) and **no** `WIDGET_INPUT`/`WIDGET_CVAR_INPUT`/separate color-picker types. For MM's `ComboboxOptions` read the `std::variant<ComboMap_t, ComboVec_t> comboVariant` (not OOT's `std::map`) when filling choices. MM `WidgetInfo` has no `raceDisable` — ignore it. MM `WidgetFunc` is a raw pointer; calling `w->preFunc(*w)` etc. is identical syntactically.

```cpp
static CwKind MapKindMM(WidgetType t) {
    switch (t) {
        case WIDGET_COLOR_24: case WIDGET_COLOR_32: return CW_COLOR;
        /* ...same as OOT for the shared values... */
        case WIDGET_CUSTOM: default: return CW_CUSTOM;
    }
}
```

- [ ] **Step 2: BUILD 2ship**

Run: `cmake --build build --target 2ship`
Expected: clean.

- [ ] **Step 3: Commit**

```bash
git add mm/2s2h/BenGui/BenMenu.h mm/2s2h/BenGui/BenMenu.cpp
git commit -m "feat(mm): emit menu as C-ABI + invoke-by-index helpers"
```

### Task 3.4: MM C exports

**Files:**
- Modify: `mm/2s2h/BenPort.cpp` (near `MM_DrawSettings`, 3082; under existing `#ifdef COMBO_BUILD`)

- [ ] **Step 1: Add exports mirroring Task 3.2 (`MM_ExportMenu`, `MM_MenuInvokeCallback`, `MM_MenuEvalDisabled`, `MM_MenuDrawCustom`)**, delegating to the `mBenMenu` instance (kept built via the existing combo `ActivateMenu`/`SetupMenu` path, 219056e73). Set the shared ImGui context in `MM_MenuDrawCustom` as in 3.2.

- [ ] **Step 2: BUILD 2ship**

Run: `cmake --build build --target 2ship`
Expected: clean.

- [ ] **Step 3: Commit**

```bash
git add mm/2s2h/BenPort.cpp
git commit -m "feat(mm): export MM_ExportMenu + MM_MenuInvoke* for comboui"
```

---

## Phase 4 — comboui model + declarative render

### Task 4.1: `ComboMenuModel` — resolve exports + ingest

**Files:**
- Create: `combo/gui/ComboMenuModel.h`, `combo/gui/ComboMenuModel.cpp`
- Modify: `combo/CMakeLists.txt` (add to `COMBOUI_SOURCES`)

- [ ] **Step 1: Declare the model**

```cpp
// combo/gui/ComboMenuModel.h
#pragma once
#include "ComboMenuABI.h"
#include <memory>
namespace Ship { class ResourceManager; }

namespace ComboRando {
struct GameMenu {
    const CwMenu* menu = nullptr;
    Fn_MenuInvokeCallback invokeCallback = nullptr;
    Fn_MenuEvalDisabled   evalDisabled = nullptr;
    Fn_MenuDrawCustom     drawCustom = nullptr;
    std::shared_ptr<Ship::ResourceManager> rm; // this game's RM, for ResourceManagerScope
};
class ComboMenuModel {
  public:
    static ComboMenuModel& Get();
    void EnsureLoaded();           // resolve exports + cache CwMenu pointers (idempotent)
    const GameMenu& Oot() const { return mOot; }
    const GameMenu& Mm()  const { return mMm; }
  private:
    GameMenu mOot, mMm;
    bool mLoaded = false;
};
} // namespace ComboRando
```

- [ ] **Step 2: Implement resolution (GetModuleHandleA/GetProcAddress, mirroring existing `ResolveDraw`)**

In `ComboMenuModel.cpp`: resolve `SOH_ExportMenu`/`SOH_MenuInvokeCallback`/`SOH_MenuEvalDisabled`/`SOH_MenuDrawCustom` from `soh.dll`, the `MM_*` from `2ship.dll`. Call the export-menu fns to cache `CwMenu*`. Obtain each game's RM: add exports `SOH_GetResourceManager()` / `MM_GetResourceManager()` returning `Ship::ResourceManager*` (the cached `sOOTResourceManager`/`sMMResourceManager` raw ptr), or resolve via Context if already exposed — wrap in a non-owning `shared_ptr` alias for the scope guard. (If a raw ptr, change `ResourceManagerScope` to accept `ResourceManager*`; keep the `shared_ptr` overload for the gtest.)

- [ ] **Step 3: BUILD comboui**

Run: `cmake --build build --target comboui`
Expected: clean.

- [ ] **Step 4: Commit**

```bash
git add combo/gui/ComboMenuModel.h combo/gui/ComboMenuModel.cpp combo/CMakeLists.txt
git commit -m "feat(combo): ComboMenuModel resolves game exports + caches CwMenu"
```

### Task 4.2: `ComboWidgetRender` — render one declarative widget

**Files:**
- Create: `combo/gui/ComboWidgetRender.h`, `combo/gui/ComboWidgetRender.cpp`
- Modify: `combo/CMakeLists.txt`

- [ ] **Step 1: Declare the renderer**

```cpp
// combo/gui/ComboWidgetRender.h
#pragma once
#include "ComboMenuABI.h"
namespace ComboRando {
struct GameMenu;
// Renders one widget. For CW_CUSTOM / preFunc / callback it calls back into the owning
// game via `game` under the appropriate ResourceManagerScope. Returns nothing.
void RenderWidget(const CwWidget& w, const GameMenu& game);
}
```

- [ ] **Step 2: Implement render for declarative kinds + routing**

In `ComboWidgetRender.cpp`. Pattern per kind, reading/writing the **shared CVar** with `CVarGetInteger/Float`/`CVarSetInteger/Float` and combo's own ImGui calls (do NOT call the game for declarative draw):

```cpp
void RenderWidget(const CwWidget& w, const GameMenu& game) {
    // 1) preFunc disable evaluation (per frame) — call into the owning game under RM scope.
    bool disabled = false; const char* reason = "";
    if (w.hasPreFunc && game.evalDisabled) {
        Ship::ResourceManagerScope scope(game.rm);
        disabled = game.evalDisabled(w.index, &reason) != 0;
    }
    ImGui::BeginDisabled(disabled);

    bool changed = false;
    switch (w.kind) {
        case CW_CHECKBOX: {
            bool v = CVarGetInteger(w.cvar, w.bDefault) != 0;
            if (ImGui::Checkbox(w.name, &v)) { CVarSetInteger(w.cvar, v); changed = true; }
        } break;
        case CW_SLIDER_INT: {
            int v = CVarGetInteger(w.cvar, w.iDefault);
            if (ImGui::SliderInt(w.name, &v, w.iMin, w.iMax)) { CVarSetInteger(w.cvar, v); changed = true; }
        } break;
        case CW_SLIDER_FLOAT: {
            float v = CVarGetFloat(w.cvar, w.fDefault);
            if (ImGui::SliderFloat(w.name, &v, w.fMin, w.fMax)) { CVarSetFloat(w.cvar, v); changed = true; }
        } break;
        case CW_COMBOBOX: {
            int v = CVarGetInteger(w.cvar, 0);
            if (ImGui::BeginCombo(w.name, /* current label */ "")) {
                for (int c = 0; c < w.choiceCount; ++c) {
                    bool sel = (w.choices[c].value == v);
                    if (ImGui::Selectable(w.choices[c].label, sel)) { CVarSetInteger(w.cvar, w.choices[c].value); changed = true; }
                }
                ImGui::EndCombo();
            }
        } break;
        case CW_BUTTON: {
            if (ImGui::Button(w.name)) changed = true; // button => fire callback
        } break;
        case CW_SEPARATOR: ImGui::Separator(); break;
        case CW_SEPARATOR_TEXT: ImGui::SeparatorText(w.name); break;
        case CW_TEXT: ImGui::TextUnformatted(w.name); break;
        case CW_CUSTOM: {
            if (game.drawCustom) { Ship::ResourceManagerScope scope(game.rm); game.drawCustom(w.index); }
        } break;
        // CW_COLOR / CW_INPUT_TEXT / CW_WINDOW_BUTTON / CW_AUDIO_BACKEND / CW_VIDEO_BACKEND:
        // render with combo equivalents; same CVar read/write pattern.
        default: ImGui::TextDisabled("%s", w.name); break;
    }

    // 2) callback (apply) — run the game's .Callback() AFTER a CVar change, under RM scope.
    if (changed && w.hasCallback && game.invokeCallback) {
        Ship::ResourceManagerScope scope(game.rm);
        game.invokeCallback(w.index);
    }
    ImGui::EndDisabled();
    if (w.tooltip[0] && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", w.tooltip);
}
```

- [ ] **Step 3: BUILD comboui**

Run: `cmake --build build --target comboui`
Expected: clean.

- [ ] **Step 4: Commit**

```bash
git add combo/gui/ComboWidgetRender.h combo/gui/ComboWidgetRender.cpp combo/CMakeLists.txt
git commit -m "feat(combo): render one C-ABI widget; route custom/pre/callback by index"
```

### Task 4.3: Wire game tabs to the model (replace draw-delegation)

**Files:**
- Modify: `combo/gui/ComboMenu.cpp` (`DrawGamePanel`, `DrawSharedPanel`)
- Modify: `combo/gui/ComboMenu.h` (include model/render headers)

- [ ] **Step 1: Render the section/sidebar tree from the model**

Replace the `SOH_/MM_DrawSettings` calls in `DrawGamePanel` with: `ComboMenuModel::Get().EnsureLoaded();` then pick `Oot()`/`Mm()`, iterate `menu->sections[].sidebars[].widgets[]`, and call `RenderWidget(w, game)`. Render the sidebar list as a left column / tab set matching the existing layout. Keep `DrawSharedPanel` rendering the engine sidebars (still fine to read shared CVars; can also be model-driven from OOT's `Settings`/`Graphics` sections filtered by name).

- [ ] **Step 2: BUILD comboui**

Run: `cmake --build build --target comboui`
Expected: clean.

- [ ] **Step 3: RUNTIME — both tabs render from combo**

Run: launch `ComboShip.exe`. In OOT: open menu → OOT tab shows OOT settings; MM tab shows MM settings (MM backgrounded) with no crash. Toggle a CVar checkbox on each tab; confirm it takes effect (e.g. a graphics toggle visible immediately on the foreground game). Switch to MM, reopen — MM tab still correct, OOT tab (now backgrounded) renders fine.

- [ ] **Step 4: Commit**

```bash
git add combo/gui/ComboMenu.cpp combo/gui/ComboMenu.h
git commit -m "feat(combo): game tabs render from C-ABI model, not draw-delegation"
```

---

## Phase 5 — Behavior-callback bucket policy

### Task 5.1: Gate game-loop-dependent actions to the foreground game

**Files:**
- Modify: `combo/gui/ComboWidgetRender.cpp`
- Modify: `combo/menu/ComboMenuABI.h` (add `int32_t gameLoopDependent;` to `CwWidget`)
- Modify: `soh/soh/SohGui/SohMenu.cpp`, `mm/2s2h/BenGui/BenMenu.cpp` (set the flag for the known category-(d) widgets)

- [ ] **Step 1: Add the flag to the ABI struct** (`gameLoopDependent`) and bump `CwMenu.version`.

- [ ] **Step 2: Mark category-(d) widgets in each emitter.** From the design's findings, set `gameLoopDependent=1` for: OOT Switch Age, Hyper-Bosses toggle, Time-Travel; MM Warp/execute, Set Warp Point. Match by widget `name` during emission (a small allow-list in the emitter).

- [ ] **Step 3: Enforce in the renderer**

```cpp
// A game-loop-dependent action only runs on the foreground game; otherwise disable with a reason.
bool isForeground = (game.rm == Ship::Context::GetInstance()->GetResourceManager());
if (w.gameLoopDependent && !isForeground) {
    ImGui::BeginDisabled(true);
    ImGui::Button(w.name);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Available only while this game is active.");
    return;
}
```

- [ ] **Step 4: BUILD all affected targets**

Run: `cmake --build build --target soh --target 2ship --target comboui`
Expected: clean.

- [ ] **Step 5: RUNTIME — gating works**

Run: in OOT, open MM tab → MM "Warp" is disabled with the tooltip; switch to MM, MM "Warp" is enabled. Confirm OOT "Switch Age" is enabled in OOT and disabled when OOT is backgrounded.

- [ ] **Step 6: Commit**

```bash
git add combo/menu/ComboMenuABI.h combo/gui/ComboWidgetRender.cpp soh/soh/SohGui/SohMenu.cpp mm/2s2h/BenGui/BenMenu.cpp
git commit -m "feat(combo): gate game-loop-dependent menu actions to the foreground game"
```

### Task 5.2: Verify resource-touching callback under RM scope

**Files:** none (verification of existing routing)

- [ ] **Step 1: RUNTIME — authentic-gfx patch on backgrounded OOT**

Run: in MM, open the OOT tab, toggle OOT's "Fix Out of Bounds Textures" (authentic-gfx). It runs under `ResourceManagerScope(oot.rm)` via the callback route. Switch to OOT and confirm the patch applied (no MM resource corruption, no crash). This validates bucket (2).

- [ ] **Step 2: Commit a verification note**

```bash
git commit --allow-empty -m "test: resource-touching callback applies on backgrounded game via RM scope — verified"
```

---

## Phase 6 — Remove port-side menu install + delegation

Now that combo renders everything, retire the old paths. Do this last so each prior phase stayed runnable.

### Task 6.1: Remove OOT in-port menu install + draw delegation

**Files:**
- Modify: `soh/soh/SohGui/SohGui.cpp` (the `COMBO_BUILD` guard that suppressed `gui->SetMenu` — keep building `mSohMenu`, ensure it's never installed)
- Modify: `soh/soh/OTRGlobals.cpp` (remove `SOH_DrawSettings` body or reduce to a no-op stub; keep the new exports)

- [ ] **Step 1: Ensure `mSohMenu` is constructed at boot but not installed** (the emitter/invoke needs the instance + its `disabledMap`). Remove any `gui->SetMenu(mSohMenu)`.

- [ ] **Step 2: Delete `SOH_DrawSettings`** (and its callers are already gone from comboui in Phase 4).

- [ ] **Step 3: BUILD soh + comboui**

Run: `cmake --build build --target soh --target comboui`
Expected: clean (no unresolved `SOH_DrawSettings`).

- [ ] **Step 4: Commit**

```bash
git add soh/soh/SohGui/SohGui.cpp soh/soh/OTRGlobals.cpp
git commit -m "refactor(soh): drop in-port menu install + SOH_DrawSettings delegation"
```

### Task 6.2: Remove MM in-port menu install + draw delegation + interim guard

**Files:**
- Modify: `mm/2s2h/BenGui/BenGui.cpp` (the `COMBO_BUILD` `ActivateMenu`/`SetupMenu` install — keep building `mBenMenu`, never `SetMenu`)
- Modify: `mm/2s2h/BenPort.cpp` (delete `MM_DrawSettings` and the interim "reworking" guard from dcba542a1; keep the new exports)

- [ ] **Step 1: Ensure `mBenMenu` is built at boot but not installed.**
- [ ] **Step 2: Delete `MM_DrawSettings` + interim guard.**
- [ ] **Step 3: BUILD 2ship + comboui**

Run: `cmake --build build --target 2ship --target comboui`
Expected: clean.

- [ ] **Step 4: RUNTIME — full regression**

Run: launch, exercise OOT/MM/Shared/Combo tabs in both foreground states; toggle CVar widgets, a combobox, a slider, a custom widget (e.g. MM Warp render), and a gated action. No crashes; settings persist across a transition (CVar shared store).

- [ ] **Step 5: Commit**

```bash
git add mm/2s2h/BenGui/BenGui.cpp mm/2s2h/BenPort.cpp
git commit -m "refactor(mm): drop in-port menu install + MM_DrawSettings + interim guard"
```

### Task 6.3: Document the deviations

**Files:**
- Modify: `docs/UPSTREAM_MERGES.md` (per memory: every post-merge deviation documented with the WHY)

- [ ] **Step 1: Record the menu-ownership deviation** — each game now exposes `*_ExportMenu`/`*_MenuInvoke*` instead of installing its own menu; comboui owns render. Note the `ComboShip:` comment locations.

- [ ] **Step 2: Commit**

```bash
git add docs/UPSTREAM_MERGES.md
git commit -m "docs: record combo-owned-menu deviation in UPSTREAM_MERGES"
```

---

## Self-Review (completed by author)

- **Spec coverage:** ownership split → Phase 4/6; neutral-data capture → Phase 2/3; per-frame callback for custom + preFunc → Phase 3 (invokers) + Phase 4.2 (routing); ResourceManagerScope → Phase 1; three-bucket policy → Phase 4.2 (CVar/resource) + Phase 5 (game-loop gating); port-code refactor → Phase 6; spike-first → Phase 0; cross-game items out of scope → untouched. All sections covered.
- **Placeholder scan:** custom/preFunc/callback handling is mechanical by-index (no per-widget placeholder). The only "fill to taste" is per-kind ImGui rendering for CW_COLOR/CW_INPUT_TEXT/CW_WINDOW_BUTTON/backends — each uses the identical CVar read/write pattern shown for the other kinds; not a placeholder, a repeat of a demonstrated pattern.
- **Type consistency:** ABI names (`CwMenu`/`CwSection`/`CwSidebar`/`CwWidget`/`CwChoice`/`CwKind`) and export signatures (`SOH_/MM_ExportMenu`, `*_MenuInvokeCallback`, `*_MenuEvalDisabled`, `*_MenuDrawCustom`) are used identically across Phases 2–6. `ResourceManagerScope` ctor accepts the game RM in both gtest (shared_ptr) and render (adjust to ptr in Task 4.1 if exports return raw) — flagged explicitly.
- **Open verification risk:** SohMenu/BenMenu instance accessors (`SohGui::mSohMenu`, `mBenMenu`) must remain constructed post-boot; Tasks 6.1/6.2 keep construction, drop only installation.
