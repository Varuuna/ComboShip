# ComboShip Reverse MM→OOT Transition — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the OOT↔MM portal bidirectional — entering MM's Clock Tower interior (`SCENE_INSIDETOWER`) returns to OOT at the Mido's-House door in Kokiri Forest, repeatable indefinitely.

**Architecture:** Mirror the existing forward (OOT→MM) mechanism. `ComboShip.main()` becomes a game-switch loop driven by pending-transition flags. Each game keeps its one-time per-process boot (heaps/threads) but gains a `*_ResumeGame` path that re-arms the shared window, swaps archives back, re-registers factories, re-inits gui/audio, sets the spawn entrance, and re-runs only its `Graph_ThreadEntry` loop. Nothing is torn down/recreated — the single shared `libultraship` context/window persists across switches.

**Tech Stack:** C++ (MSVC, CMake, VS2022 generator), libultraship (Fast3D, ImGui), soh (OOT) + 2ship (MM) DLLs loaded by ComboShip.exe.

**Spec:** `docs/superpowers/specs/2026-06-02-combo-mm-to-oot-reverse-transition-design.md`

**Testing note:** This is engine/gameplay code with no unit-test harness. Each task's "verify" step is **build the affected target(s) and run ComboShip, observing specific log lines / in-game behavior**. Build per-target (don't rebuild everything):
`cmake --build build/x64 --target <libultraship|soh|2ship|ComboShip> --config Debug`.
Diagnostics: `x64/Debug/logs/Ship of Harkinian.log` (shared logger) and `x64/Debug/combo_abort_stack.txt` (SIGABRT/asserts).

---

## File map

- `combo/ComboShip.cpp` — orchestration loop + reverse callback registration (port/launcher).
- `soh/soh/OTRGlobals.cpp` — OOT reverse exports: `SOH_ResumeGame`, `SOH_NotifyComboReturn`, `gComboReturnCallback` setter; OOT resume setup (archive swap-back, factory re-register, gui/audio re-init, entrance). (OOT port layer.)
- `soh/src/code/main.c` — split OOT boot vs loop; guard one-time init so resume re-runs only the loop. (Game source — minimal.)
- `mm/2s2h/BenPort.cpp` / `BenPort.h` — MM reverse trigger hook, `MM_PrepareForTransition`, `MM_ResumeGame`, `MM_SetOnComboReturnCallback` + `gComboReturnCallback`. (MM port layer.)
- `mm/src/code/main.c` — guard `MM_RunMain`'s `DeinitOTR`/one-time init under `COMBO_BUILD`. (Game source — minimal.)

---

## Task 1: MM reverse trigger + return signal (MM stops and returns to main())

Goal: in MM, entering `SCENE_INSIDETOWER` saves MM, signals ComboShip, and cleanly returns from `MM_RunGame` without tearing down the shared context.

**Files:**
- Modify: `mm/2s2h/BenPort.cpp`, `mm/2s2h/BenPort.h`
- Modify: `mm/src/code/main.c` (guard DeinitOTR under COMBO_BUILD)
- Modify: `combo/ComboShip.cpp`

- [ ] **Step 1: Guard MM_RunMain teardown under COMBO_BUILD**

In `mm/src/code/main.c`, `MM_RunMain` (line ~159) currently ends:
```c
    Graph_ThreadEntry(0);
    DeinitOTR();
}
```
Change to (mirrors soh's existing `#ifndef COMBO_BUILD` guard so the shared context survives a transition):
```c
    Graph_ThreadEntry(0);
#ifndef COMBO_BUILD
    DeinitOTR();
#endif
}
```

- [ ] **Step 2: Add reverse-callback plumbing to MM port code**

In `mm/2s2h/BenPort.cpp`, near the other combo statics (top, by `sComboTransitionActive` ~line 120) add:
```cpp
#ifdef COMBO_BUILD
// Set by ComboShip; invoked when MM wants to hand control back to OOT.
extern "C" void (*gComboReturnCallback)(void) = nullptr;
extern "C" __declspec(dllexport) void MM_SetOnComboReturnCallback(void (*cb)(void)) {
    gComboReturnCallback = cb;
}
static bool sComboReturnPending = false;
#endif
```
Declare the export in `mm/2s2h/BenPort.h`:
```cpp
void MM_SetOnComboReturnCallback(void (*cb)(void));
```

- [ ] **Step 3: Register the reverse trigger hooks (in MM's InitOTR, COMBO_BUILD)**

In `mm/2s2h/BenPort.cpp` `InitOTR` (after `ShipInit::InitAll();` ~line 773), add:
```cpp
#ifdef COMBO_BUILD
    // Reverse portal: entering the Clock Tower interior hands control back to OOT.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(
        [](s8 sceneId, s8 spawnNum) {
            if (sceneId == SCENE_INSIDETOWER) {
                sComboReturnPending = true;
            }
        });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateMainStart>([]() {
        if (!sComboReturnPending) return;
        sComboReturnPending = false;
        // Flush MM save for the current slot (gSaveContext.fileNum is 0-indexed; MM files are 1-indexed).
        SaveManager_SaveCurrentForCombo();
        if (gComboReturnCallback) gComboReturnCallback();
        // Stop MM's `while (WindowIsRunning())` loop so MM_RunGame/MM_ResumeGame returns to ComboShip.
        if (auto fast3d = std::dynamic_pointer_cast<Fast::Fast3dWindow>(
                Ship::Context::GetInstance()->GetWindow())) {
            fast3d->SetIsRunning(false);
        }
    });
#endif
```
(Confirm `OnSceneInit` signature `(s8 sceneId, s8 spawnNum)` and `OnGameStateMainStart` `()` against `mm/2s2h/GameInteractor/GameInteractor_HookTable.h`; adjust arg types if they differ.)

- [ ] **Step 4: Add the MM save-flush helper**

In `mm/2s2h/SaveManager/SaveManager.cpp`, add a synchronous combo save helper next to `SaveManager_InitNewSaveForSlot`:
```cpp
// Persist the live gSaveContext to the current slot's MM save file (combo MM->OOT handoff).
void SaveManager_SaveCurrentForCombo() {
    int mmFileNum = (int)gSaveContext.fileNum + 1; // gSaveContext.fileNum is 0-indexed; files are 1-indexed
    nlohmann::json j;
    j["newCycleSave"]["save"] = gSaveContext.save;
    j["version"] = CURRENT_SAVE_VERSION;
    j["type"] = "2S2H_SAVE";
    SaveManager_WriteSaveFile(SaveManager_GetFileName(mmFileNum), j);
}
```
Declare it in `mm/2s2h/SaveManager/SaveManager.h` and call it from BenPort (Step 3 references `SaveManager_SaveCurrentForCombo()`).

- [ ] **Step 5: ComboShip — register the reverse callback and a temporary return stub**

In `combo/ComboShip.cpp`, add the function-pointer typedef + static (near the other `Fn*`/`MM_*` statics ~line 130):
```cpp
typedef void (*FnMMSetReturnCb)(void (*)(void));
static FnMMSetReturnCb MM_SetOnComboReturnCallback = nullptr;
static bool g_pendingOOTReturn = false;
```
Load the symbol in the GetSym block (~line 239):
```cpp
MM_SetOnComboReturnCallback = (FnMMSetReturnCb) GetSym(mmModule, "MM_SetOnComboReturnCallback");
```
Add the callback (near `Combo_OnOOTSceneSwitch` ~line 149):
```cpp
static void Combo_OnMMReturn(void) {
    std::cout << "[ComboShip] MM Clock Tower entered -- returning to OOT" << std::endl;
    g_pendingOOTReturn = true;
}
```
Register it just before launching MM (in the transition block, before `MM_RunGame`, ~line 350):
```cpp
if (MM_SetOnComboReturnCallback) MM_SetOnComboReturnCallback(Combo_OnMMReturn);
```
Temporary (replaced in Task 4): after `MM_RunGame(...)` returns, log the result so this task is verifiable:
```cpp
if (g_pendingOOTReturn) std::cout << "[ComboShip] OOT return pending (loop wiring lands in Task 4)" << std::endl;
```

- [ ] **Step 6: Build and verify**

```
cmake --build build/x64 --target libultraship --config Debug   # only if headers changed; otherwise skip
cmake --build build/x64 --target 2ship --config Debug
cmake --build build/x64 --target ComboShip --config Debug
```
Run ComboShip, go OOT→Mido's House→MM (South Clock Town), then walk into the Clock Tower interior.
Expected: console prints `MM Clock Tower entered -- returning to OOT` and `OOT return pending...`; MM's loop exits; **no** crash in `combo_abort_stack.txt`; the app then exits (full loop not wired yet).

- [ ] **Step 7: Commit**

```
git add mm/2s2h/BenPort.cpp mm/2s2h/BenPort.h mm/src/code/main.c mm/2s2h/SaveManager/SaveManager.cpp mm/2s2h/SaveManager/SaveManager.h combo/ComboShip.cpp
git commit -m "Combo: MM reverse trigger -- entering Clock Tower stops MM and signals return"
```

---

## Task 2: OOT resume path (SOH_ResumeGame) — the core de-risk

Goal: a `SOH_ResumeGame()` export that re-enters OOT's game loop on the shared context, swapping archives back to OOT and spawning at the Mido's-House door. Validate it standalone before wiring the full loop.

**Files:**
- Modify: `soh/soh/OTRGlobals.cpp` (exports + resume setup)
- Modify: `soh/src/code/main.c` (boot-vs-loop split)
- Modify: `combo/ComboShip.cpp` (temporary direct call to validate)

- [ ] **Step 1: Split OOT boot from its re-runnable loop**

In `soh/src/code/main.c`, `Main()` (line 103) does one-time setup then calls `Graph_ThreadEntry(0)` (line 170). Extract the loop re-entry. Add a COMBO_BUILD export that re-arms the window and re-runs only the loop (the heap/thread/IRQ setup in `Main()` already ran once this process and its globals persist):
```c
#ifdef COMBO_BUILD
__declspec(dllexport) void SOH_RunGameLoop(void) {
    Graph_ThreadEntry(0); // OOT's `while (WindowIsRunning()) RunFrame();`
}
#endif
```
(If runtime shows OOT's graph thread/state was torn down when the loop first exited and `Graph_ThreadEntry` can't be re-entered as-is, the minimal fix is to re-init just the graph state here. Validate in Step 5 and iterate; do NOT re-call `Heaps_Alloc`/the `Main()` thread setup.)

- [ ] **Step 2: Add OOT resume + return-callback exports (OTRGlobals.cpp)**

In `soh/soh/OTRGlobals.cpp`, near the other ComboShip exports (~line 2831) add:
```cpp
extern "C" void SOH_RunGameLoop(void); // from main.c

extern "C" void (*gComboReturnCallback)(void) = nullptr; // OOT-side; ComboShip sets via setter below
extern "C" __declspec(dllexport) void SOH_NotifyComboReturn(void) {
    // Marker that the next SOH_ResumeGame is a combo return (kept symmetric with MM_NotifyComboTransition).
}

extern "C" __declspec(dllexport) void SOH_ResumeGame(void) {
    auto ctx = Ship::Context::GetInstance();

    // 1. Swap archives back to OOT and reload OOT resources/factories.
    SOH_ReinitForResume(); // defined in Step 3

    // 2. Re-arm the shared window so OOT's `while (WindowIsRunning())` loop runs.
    if (auto fast3d = std::dynamic_pointer_cast<Fast::Fast3dWindow>(ctx->GetWindow())) {
        fast3d->SetIsRunning(true);
    }

    // 3. Re-sync ImGui current-context for this DLL (per-module GImGui).
    ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());

    // 4. Reload OOT save and spawn at the Mido's-House door in Kokiri Forest.
    SaveManager::Instance->LoadFile((int)gSaveContext.fileNum);
    gSaveContext.save.entranceIndex = ENTR_KOKIRI_FOREST_OUTSIDE_MIDOS_HOUSE; // 0x443, spawn 9
    gSaveContext.respawnFlag = 0;
    gSaveContext.nextTransitionType = TRANS_NEXT_TYPE_DEFAULT;

    // 5. Re-run OOT's game loop (returns when the next transition/quit stops the window).
    SOH_RunGameLoop();
}
```
Declare both new exports in the ComboShip export header used by `combo/ComboShip.cpp` (where `SOH_RunMain` etc. are declared).

- [ ] **Step 3: Implement the archive/factory/gui/audio swap-back helper**

In `soh/soh/OTRGlobals.cpp`, factor the resource-factory registration block (lines 363–432 inside `OTRGlobals::Initialize`) into a reusable function `void RegisterOOTResourceFactories(std::shared_ptr<Ship::ResourceLoader> loader)` and call it from both `Initialize` and the resume helper. Then add:
```cpp
#ifdef COMBO_BUILD
static void SOH_ReinitForResume() {
    auto ctx = Ship::Context::GetInstance();
    // Re-init gui/audio that SOH_PrepareForTransition tore down.
    OTRAudio_Init();                       // counterpart to OTRAudio_Exit in SOH_PrepareForTransition
    SohGui::SetupGuiElements();            // counterpart to SohGui::Destroy
    // Swap archives back to OOT and reload its resources/factories (mirror BenPort's reuse path).
    auto archiveMgr = ctx->GetResourceManager()->GetArchiveManager();
    archiveMgr->SetArchives(sOOTArchivePaths); // capture OOT archive paths at SOH_Init (Step 4)
    ctx->GetResourceManager()->UnloadResources("*");
    RegisterOOTResourceFactories(ctx->GetResourceManager()->GetResourceLoader());
}
#endif
```
(Verify the exact gui/audio init symbols against what `SOH_PrepareForTransition` (`OTRGlobals.cpp:1379`) tears down — pair each `*_Exit`/`Destroy` with its init. Verify `SetArchives` signature on `ArchiveManager`.)

- [ ] **Step 4: Capture OOT archive paths at init**

In `soh/soh/OTRGlobals.cpp` `OTRGlobals::Initialize` where archives are first added, store the OOT archive path list in a file-static `static std::vector<std::string> sOOTArchivePaths;` so the resume can restore them (MM's `SetArchives(nullptr)` + add cleared them). Mirror how `mm/2s2h/BenPort.cpp` builds its `archiveFiles` list.

- [ ] **Step 5: Temporary validation wiring in ComboShip**

In `combo/ComboShip.cpp`, after `MM_RunGame(...)` returns and `g_pendingOOTReturn` is set, temporarily call OOT resume directly (replaced by the loop in Task 4):
```cpp
if (g_pendingOOTReturn && SOH_ResumeGame) {
    if (MM_PrepareForTransition) MM_PrepareForTransition(); // added in Task 3; ok if still null here -> skip
    SOH_ResumeGame();
}
```
Load `SOH_ResumeGame`/`SOH_NotifyComboReturn` via `GetSym` in the symbol block.

- [ ] **Step 6: Build and verify**

```
cmake --build build/x64 --target soh --config Debug
cmake --build build/x64 --target ComboShip --config Debug
```
Run: OOT→Mido's→MM→Clock Tower. Expected: control returns to OOT, Link spawns **outside Mido's House in Kokiri Forest**, OOT is playable, **no new window**, fonts render (RebuildFontTexture path), `combo_abort_stack.txt` not updated. Iterate on Step 1/Step 3 if the OOT loop fails to re-enter or resources are missing (watch the SoH log for resource/archive errors).

- [ ] **Step 7: Commit**

```
git add soh/soh/OTRGlobals.cpp soh/src/code/main.c combo/ComboShip.cpp
git commit -m "Combo: SOH_ResumeGame -- re-enter OOT on shared context at Mido's-House door"
```

---

## Task 3: MM_PrepareForTransition + MM_ResumeGame (second+ MM entry)

Goal: symmetric MM side so MM can be re-entered after the first boot without re-running its one-time heap/thread init.

**Files:**
- Modify: `mm/2s2h/BenPort.cpp`, `mm/2s2h/BenPort.h`
- Modify: `mm/src/code/main.c`

- [ ] **Step 1: MM_PrepareForTransition (stop audio + destroy gui, keep context)**

In `mm/2s2h/BenPort.cpp` add (mirrors `SOH_PrepareForTransition`):
```cpp
#ifdef COMBO_BUILD
extern "C" __declspec(dllexport) void MM_PrepareForTransition(void) {
    // Stop MM audio and tear down MM gui WITHOUT destroying the shared context/window.
    AudioExit();          // verify exact MM audio-exit symbol used at MM shutdown
    // (MM gui teardown counterpart, if any, mirroring what InitOTR set up)
}
#endif
```
Declare in `BenPort.h`. (Verify the MM audio exit symbol; pair with whatever MM re-inits on resume.)

- [ ] **Step 2: Split MM boot from loop (SOH-style)**

In `mm/src/code/main.c`, expose a loop-only re-entry mirroring Task 2 Step 1:
```c
#ifdef COMBO_BUILD
__declspec(dllexport) void MM_RunGameLoop(void) {
    Graph_ThreadEntry(0); // MM's `while (WindowIsRunning()) RunFrame();`
}
#endif
```

- [ ] **Step 3: MM_ResumeGame**

In `mm/2s2h/BenPort.cpp` add:
```cpp
#ifdef COMBO_BUILD
extern "C" void MM_RunGameLoop(void);
extern "C" __declspec(dllexport) void MM_ResumeGame(int fileNum) {
    auto ctx = Ship::Context::GetInstance();
    // Swap archives back to MM + re-register MM factories (reuse the existing forward reuse-path code).
    MM_ReinitForResume(fileNum); // factor out the sComboTransitionActive setup from InitOTR
    if (auto fast3d = std::dynamic_pointer_cast<Fast::Fast3dWindow>(ctx->GetWindow())) {
        fast3d->SetIsRunning(true);
    }
    ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());
    gComboStartFileNum = fileNum; // title_setup combo path loads this slot + spawns South Clock Town
    MM_RunGameLoop();
}
#endif
```
Declare `MM_ResumeGame` and `MM_PrepareForTransition` in `BenPort.h`. Factor the archive-swap/factory-register portion of the existing `sComboTransitionActive` branch (BenPort.cpp ~192–280) into `MM_ReinitForResume(int fileNum)` so both first-entry and resume share it.

- [ ] **Step 4: Build and verify (still using Task 2's temporary wiring)**

```
cmake --build build/x64 --target 2ship --config Debug
cmake --build build/x64 --target ComboShip --config Debug
```
Expected: builds clean; exports present (`dumpbin /exports x64\Debug\2ship.dll | findstr Resume`). Functional ping-pong is verified in Task 4.

- [ ] **Step 5: Commit**

```
git add mm/2s2h/BenPort.cpp mm/2s2h/BenPort.h mm/src/code/main.c
git commit -m "Combo: MM_ResumeGame + MM_PrepareForTransition for re-entry"
```

---

## Task 4: ComboShip game-switch loop (full bidirectional)

Goal: replace the linear `main()` tail with the alternating loop.

**Files:**
- Modify: `combo/ComboShip.cpp`

- [ ] **Step 1: Replace the linear tail (lines ~335–360) with the loop**

```cpp
// --- Game-switch loop ---
enum ComboGame { GAME_OOT, GAME_MM };
ComboGame current = GAME_OOT;
bool ootBooted = false, mmBooted = false;

for (;;) {
    if (current == GAME_OOT) {
        g_PendingMMFileNum = -1;
        if (!ootBooted) { std::cout << "[ComboShip] OOT boot\n"; SOH_RunMain(argc, argv); ootBooted = true; }
        else            { std::cout << "[ComboShip] OOT resume\n"; SOH_ResumeGame(); }
        if (g_PendingMMFileNum >= 0 && MM_RunGame) {
            if (SOH_PrepareForTransition) SOH_PrepareForTransition();
            if (MM_NotifyComboTransition) MM_NotifyComboTransition();
            if (MM_SetOnComboReturnCallback) MM_SetOnComboReturnCallback(Combo_OnMMReturn);
            current = GAME_MM;
        } else break; // real quit
    } else {
        g_pendingOOTReturn = false;
        if (!mmBooted) { std::cout << "[ComboShip] MM boot\n"; MM_RunGame(g_PendingMMFileNum); mmBooted = true; }
        else           { std::cout << "[ComboShip] MM resume\n"; MM_ResumeGame(g_PendingMMFileNum); }
        if (g_pendingOOTReturn) {
            if (MM_PrepareForTransition) MM_PrepareForTransition();
            if (SOH_NotifyComboReturn) SOH_NotifyComboReturn();
            current = GAME_OOT;
        } else break; // real quit
    }
}

if (SOH_Deinit) SOH_Deinit();
```
Remove the Task-1/Task-2 temporary stubs. Load `MM_ResumeGame`, `MM_PrepareForTransition`, `SOH_ResumeGame`, `SOH_NotifyComboReturn` via `GetSym`.

- [ ] **Step 2: Build and verify the full round-trip**

```
cmake --build build/x64 --target ComboShip --config Debug
```
Run and ping-pong **at least twice**: OOT(Mido's)→MM(SCT)→ClockTower→OOT(Mido's door)→Mido's→MM→ClockTower→OOT. Expected console: `OOT boot` / `MM boot` / `OOT resume` / `MM resume` in order; no new window; fonts intact each entry; saves intact; `combo_abort_stack.txt` not updated. Closing the window from either game exits the app cleanly (hits `SOH_Deinit`).

- [ ] **Step 3: Commit**

```
git add combo/ComboShip.cpp
git commit -m "Combo: bidirectional game-switch loop in ComboShip.main()"
```

---

## Task 5: Save persistence across round-trips

Goal: confirm/Fix that OOT and MM progress each survive a round trip.

**Files:** verification-only unless a fix is needed (`soh/soh/OTRGlobals.cpp`, `mm/2s2h/SaveManager/SaveManager.cpp`).

- [ ] **Step 1: Verify OOT save round-trips**

Run: in OOT collect something trackable (e.g. a rupee count / sword), enter Mido's→MM→ClockTower→OOT. Expected: OOT state restored from its save (the forward switch already calls `SaveManager::SaveFile`; `SOH_ResumeGame` reloads it). If lost, ensure the forward path saved and `SOH_ResumeGame` Step 4 `LoadFile` uses the right slot. Log: grep SoH log for `Load File`/`Save File`.

- [ ] **Step 2: Verify MM save round-trips**

Run: in MM change something (e.g. pick up an item), enter ClockTower→OOT→Mido's→MM. Expected: MM state restored (`SaveManager_SaveCurrentForCombo` on exit + `Combo_LoadMMSaveFile` on entry). Note: `title_setup.c` force-spawns South Clock Town, so position resets by design; inventory/flags should persist. Grep log for `LoadSaveFile`/`Wrote MM save file`.

- [ ] **Step 3: Commit (only if a fix was made)**

```
git add -A && git commit -m "Combo: fix save persistence across OOT<->MM round-trips"
```

---

## Task 6: Hardening — boundary logging, quit paths, audio-exit crash

**Files:** `combo/ComboShip.cpp`, `soh/soh/OTRGlobals.cpp`, `mm/2s2h/BenPort.cpp`.

- [ ] **Step 1: Boundary logging**

Ensure each of these logs once with a clear `[ComboShip]` prefix: forward trigger, reverse trigger, archive swap (each direction), entrance set, loop boot vs resume. (Most added inline above — confirm coverage; add any missing.)

- [ ] **Step 2: Quit from either game**

Verify: closing the window while in MM exits the app (not just returns to OOT). The loop's `else break` handles it because no pending flag is set; confirm `MM_RunGame`/`MM_ResumeGame` actually return on window-close (they do — same `WindowIsRunning()` loop). Test both: quit-from-OOT and quit-from-MM.

- [ ] **Step 3: Audio-exit crash check**

After several round-trips, close the app and check `combo_abort_stack.txt` for the known exit crash (`atexit dtor 'audio'` → `std::thread::~thread` → `terminate`). If repeated audio init/exit makes it reliably reproducible, fix the audio thread to be joined/detached before destruction in the MM/OOT audio-exit path (the joinable-`std::thread`-at-shutdown bug). If unchanged from baseline, leave for separate work (out of scope per spec).

- [ ] **Step 4: Commit**

```
git add -A && git commit -m "Combo: round-trip hardening -- boundary logging + quit paths"
```

---

## Self-review notes

- **Spec coverage:** control-flow loop (Task 4), reverse trigger (Task 1), OOT resume (Task 2), MM resume (Task 3), handoff/archives/save (Tasks 2,3,5), edge cases/audio (Task 6), entrance `ENTR_KOKIRI_FOREST_OUTSIDE_MIDOS_HOUSE` (Task 2). All spec sections mapped.
- **Symbol consistency:** exports `SOH_ResumeGame()`/`SOH_NotifyComboReturn()`/`MM_ResumeGame(int)`/`MM_PrepareForTransition()`/`MM_SetOnComboReturnCallback(cb)`; flags `g_PendingMMFileNum`/`g_pendingOOTReturn`; loops `SOH_RunGameLoop`/`MM_RunGameLoop` — used consistently across tasks.
- **Known iteration points (not placeholders — engine-refactor reality):** OOT/MM loop re-entry (Task 2 Step 1, Task 3 Step 2) and the exact gui/audio init↔exit pairings (Task 2 Step 3, Task 3 Step 1) require verifying symbols against the source and compiler/runtime iteration; each has a concrete approach + verification step. Do Task 2 first as the de-risk; if OOT loop re-entry proves infeasible without deeper engine changes, stop and revisit the design before proceeding.
