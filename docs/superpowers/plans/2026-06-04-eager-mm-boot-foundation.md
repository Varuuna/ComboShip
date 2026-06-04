# Eager MM Boot Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Boot MM once, for real, at OOT startup (without running MM's game loop) so the cross-world rando oracle runs against a fully-initialized MM, and delete the fragile headless `MM_InitRandoLogic` warm-up that crashes.

**Architecture:** ComboShip already runs OOT and MM over one shared libultraship Context with per-game ResourceManagers that both stay resident; the active RM is swapped on each OOT↔MM transition. Today MM is booted lazily on the first portal transition, and a headless `ShipInit::InitAll()` "warm-up" at startup tries to fake enough of MM to build its rando region graph — but `InitAll()` runs MM's *entire* UI/cosmetic/audio init surface, which needs a booted MM and crashes (null `GameInteractor::Instance`, then `ResourceManager` loads of MM assets through OOT's RM). This plan replaces the warm-up by performing a real OOT→MM→OOT transition **once at startup**, with MM's blocking game loop skipped, reusing the existing transition machinery. After this, MM is genuinely booted (region graph built by the real `InitAll()`), its RM resident, and the first portal transition becomes a normal `MM_ResumeGame`.

**Tech Stack:** C++17 (combo launcher + soh/2ship port code), C (game `main.c`), MSVC, CMake. Shared `libultraship.dll`. No unit-test harness exists for combo/game C++ — **verification is per-target build + in-game manual launch** (documented click-paths below), per the project's established practice.

---

## Background: how boot/transition works today (read before starting)

Exact references (verified 2026-06-04):

- **Combo main loop:** `combo/ComboShip.cpp:506-546`. `enum ComboGame { GAME_OOT, GAME_MM }`, flags `ootBooted`/`mmBooted`. OOT first boot = `SOH_RunMain(argc, argv)`; OOT resume = `SOH_ResumeGame()`. MM first boot = `MM_RunGame(fileNum)`; MM resume = `MM_ResumeGame(fileNum)`. Transition triggers: `g_PendingMMFileNum >= 0` (OOT→MM), `g_pendingOOTReturn` (MM→OOT).
- **OOT startup (pre-loop):** `combo/ComboShip.cpp` calls `SOH_Init()` (~line 431), then the **warm-up block** `if (MM_InitRandoLogic) { MM_InitRandoLogic(); ... }` (~lines 447-452), then static-data dumps, then the `for(;;)` loop.
- **`SOH_Init()`** (`soh/soh/OTRGlobals.cpp:2481-2485`) = `InitOTR(0, nullptr)` — creates OOT's Context + RM (active), GameInteractor, GUI, audio.
- **`SOH_RunMain()`** (`soh/src/code/main.c:52`) does **not** re-run `InitOTR` (comment line 54). It runs `GameConsole_Init`, crash handler, OOT's `Main` body (heap/threads), and the game loop.
- **`SOH_PrepareForTransition()`** (`soh/soh/OTRGlobals.cpp:1645`) — `SaveManager_ThreadPoolWait()`, `OTRAudio_Exit()`, `SohGui::Destroy()`. Keeps Context/window/RM alive.
- **`SOH_ReinitForResume()`** (static, `soh/soh/OTRGlobals.cpp:2513-2529`) — `ctx->SetResourceManager(sOOTResourceManager)`, `OTRAudio_Init()`, `SohGui::SetupGuiElements()`, `SetMenu(SohGui::GetSohMenu())`.
- **`SOH_ResumeGame()`** (`soh/soh/OTRGlobals.cpp:2541-2573`) — `SOH_ReinitForResume()` + window re-arm + `ImGui::SetCurrentContext` + `gComboReturnFileNum` + `SOH_ResetFrameLoopForResume()` + `SOH_RunGameLoop()`.
- **`MM_RunMain()`** (`mm/src/code/main.c:162-200`) — `InitOTR(0, NULL)`, `Heaps_Alloc()`, screen dims, `Nmi_Init`/`Fault_Init`/region+expansion checks, `SystemHeap_Init`, `Regs_Init`, message queues, `PadMgr_Init`, `AudioMgr_Init`, then **`Graph_ThreadEntry(0)`** (the blocking `while (WindowIsRunning()) RunFrame()` loop) as its final call. For `COMBO_BUILD` it does **not** call `DeinitOTR()` (lines 197-199).
- **MM context reuse:** `OTRGlobals::OTRGlobals()` (`mm/2s2h/BenPort.cpp:175-283`) reuses OOT's Context when `sComboTransitionActive` is true, creates MM's own RM, makes it active, and sets the shared window `IsRunning = true` (line ~276) + rebuilds the font atlas.
- **`MM_NotifyComboTransition()`** (`mm/2s2h/BenPort.cpp:135-137`) — sets `sComboTransitionActive = true`.
- **`MM_RunGame(fileNum)`** (`mm/2s2h/BenPort.cpp:2504-2507`) — `gComboStartFileNum = fileNum; MM_RunMain();`.
- **`MM_PrepareForTransition()`** (`mm/2s2h/BenPort.cpp:2524-2532`) — `SaveManager_ThreadPoolWait()`, `OTRAudio_Exit()`. Does **not** destroy the GUI. Keeps Context/window/RM alive.
- **`MM_ResumeGame(fileNum)`** (`mm/2s2h/BenPort.cpp:2537-2579`) — reactivates MM's RM, restarts MM audio, re-syncs ImGui, `BenGui::ActivateMenu()`, window re-arm, heap/frame reset, `MM_RunGameLoop()`.
- **The warm-up to delete:** `MM_InitRandoLogic()` (`mm/2s2h/BenPort.cpp:2723+`) calls `ShipInit::InitAll()` + `Rando::StaticData::PopulateCheckNames()`. (A prior commit `f54b3cece` added throwaway `GameInteractor::Instance`/`AudioCollection::Instance` creation here — that whole function is deleted by this plan.)

**Why eager boot is safe at startup:** both games are extracted before `SOH_Init()` (`combo/ComboShip.cpp:381-431`), so MM's archives exist. MM's threads (PadMgr/AudioMgr) already coexist with OOT's during normal transitions, so booting them earlier is not a new condition. The oracle only needs MM's region graph (built by the real `InitAll()` inside `InitOTR`) + `Rando::StaticData` (static-init) + MM's `gSaveContext` (snapshotted/restored by the oracle) — none of which need MM's RM to be the *active* one at generate time.

---

## File structure

| File | Change |
|------|--------|
| `mm/src/code/main.c` | Gate `MM_RunMain`'s final `Graph_ThreadEntry(0)` on a new `gComboBootOnly` flag (COMBO_BUILD only). |
| `mm/2s2h/BenPort.cpp` | Define `gComboBootOnly`; add `MM_BootForCombo()` export; **delete** `MM_InitRandoLogic()`. |
| `soh/soh/OTRGlobals.cpp` | Add `SOH_ResumeForeground()` export (reactivate OOT as foreground, no game loop). |
| `combo/ComboShip.cpp` | Resolve the two new exports; replace the warm-up block with the eager-boot sequence; start the loop with `mmBooted = true`; drop `MM_InitRandoLogic` resolution. |
| `docs/UPSTREAM_MERGES.md`, progress doc, memory | Document the change. |

---

## Phase 1 — Eager MM boot + delete the warm-up

### Task 1: Gate MM's game loop on `gComboBootOnly` (MM game source)

**Files:**
- Modify: `mm/src/code/main.c:195-200` (the tail of `MM_RunMain`)

- [ ] **Step 1: Add the flag declaration near the top of `main.c`**

Find the existing forward declarations block (around `mm/src/code/main.c:50`, where `void InitOTR(int argc, char* argv[]);` is declared) and add directly after it:

```c
#ifdef COMBO_BUILD
// ComboShip: when nonzero, MM_RunMain initializes MM but skips its blocking game loop
// (Graph_ThreadEntry). Set by MM_BootForCombo (mm/2s2h/BenPort.cpp) for the eager OOT-startup boot.
extern int gComboBootOnly;
#endif
```

- [ ] **Step 2: Gate the loop in `MM_RunMain`**

Replace the current tail of `MM_RunMain` (lines 195-200):

```c
    Graph_ThreadEntry(0);

#ifndef COMBO_BUILD
    DeinitOTR();
#endif
}
```

with:

```c
#ifdef COMBO_BUILD
    // ComboShip: MM_BootForCombo sets gComboBootOnly to initialize MM at OOT startup WITHOUT running
    // its blocking game loop — the cross-world oracle only needs the region graph + runtime. The loop
    // runs later via MM_ResumeGame on the first portal transition.
    if (!gComboBootOnly) {
        Graph_ThreadEntry(0);
    }
#else
    Graph_ThreadEntry(0);
    DeinitOTR();
#endif
}
```

- [ ] **Step 3: Build 2ship to verify the edit compiles**

Run: `.\scripts\build-2ship.ps1 --Debug > build-2ship.log 2>&1; "EXIT=$LASTEXITCODE"`
Expected: `EXIT=0`. Then check no errors: `Select-String -Path build-2ship.log -Pattern 'error|LNK|fatal' -CaseSensitive:$false`
Expected: no matches. (At this point `gComboBootOnly` is `extern`-declared but not yet defined; it links because nothing references it as a defined symbol until Task 2 — if the linker complains about an unresolved `gComboBootOnly`, proceed to Task 2 and build again. To avoid a transient unresolved symbol, do Task 1 + Task 2 together before building.)

- [ ] **Step 4: Commit (combined with Task 2 — see Task 2 Step 4)**

---

### Task 2: Add `MM_BootForCombo` and delete `MM_InitRandoLogic` (MM port code)

**Files:**
- Modify: `mm/2s2h/BenPort.cpp` (add `gComboBootOnly` + `MM_BootForCombo` near `MM_RunGame` ~line 2500-2507; delete `MM_InitRandoLogic` ~line 2723)

- [ ] **Step 1: Define `gComboBootOnly` and add `MM_BootForCombo` next to `MM_RunGame`**

Find `MM_RunGame` (`mm/2s2h/BenPort.cpp:2500-2507`):

```cpp
extern "C" void MM_RunMain(void);

// Full MM initialization + game loop, entered after OOT has exited.
// fileNum is the OOT 0-indexed slot; we map it to the same MM slot.
extern "C" __declspec(dllexport) void MM_RunGame(int fileNum) {
    gComboStartFileNum = fileNum;
    MM_RunMain();
}
```

Replace it with:

```cpp
extern "C" void MM_RunMain(void);

// Full MM initialization + game loop, entered after OOT has exited.
// fileNum is the OOT 0-indexed slot; we map it to the same MM slot.
extern "C" __declspec(dllexport) void MM_RunGame(int fileNum) {
    gComboStartFileNum = fileNum;
    MM_RunMain();
}

// ComboShip: read by mm/src/code/main.c's MM_RunMain to skip the blocking game loop. Set only for
// the duration of MM_BootForCombo.
extern "C" int gComboBootOnly = 0;

// ComboShip: eagerly boot MM at OOT startup (called once after SOH_Init) so the cross-world rando
// oracle runs against a real, fully-initialized MM (region graph built by the real ShipInit::InitAll,
// real GameInteractor/AudioCollection/RM) instead of a fragile headless fake. Reuses OOT's shared
// Context (sComboTransitionActive) and runs MM_RunMain's full init while skipping its game loop
// (gComboBootOnly). The caller (ComboShip main) brackets this with SOH_PrepareForTransition (before)
// and MM_PrepareForTransition + SOH_ResumeForeground (after) to hand the foreground back to OOT.
extern "C" __declspec(dllexport) void MM_BootForCombo(void) {
    gComboStartFileNum = -1;        // boot only — no save load / Play jump
    sComboTransitionActive = true;  // OTRGlobals ctor reuses OOT's Context + creates MM's own RM
    gComboBootOnly = 1;
    MM_RunMain();                   // full init; main.c skips Graph_ThreadEntry due to gComboBootOnly
    gComboBootOnly = 0;
}
```

> Note: `sComboTransitionActive` is the same file-static `MM_NotifyComboTransition` sets (BenPort.cpp:135-137); setting it here means ComboShip does **not** need a separate `MM_NotifyComboTransition()` call for the eager boot.

- [ ] **Step 2: Delete `MM_InitRandoLogic`**

Find and delete the entire `MM_InitRandoLogic` function (`mm/2s2h/BenPort.cpp`, ~line 2718-2735, including its `// ComboShip Inc3:` doc comment block). It looks like:

```cpp
// ComboShip Inc3: warm up MM's rando logic engine headlessly ...
// ... (doc comment) ...
extern "C" __declspec(dllexport) void MM_InitRandoLogic(void) {
    static bool inited = false;
    if (inited) return;
    inited = true;

    // ComboShip: ShipInit::InitAll() runs EVERY registered init func ...
    if (GameInteractor::Instance == nullptr) {
        GameInteractor::Instance = new GameInteractor();
    }
    if (AudioCollection::Instance == nullptr) {
        AudioCollection::Instance = new AudioCollection();
    }

    ShipInit::InitAll();
    Rando::StaticData::PopulateCheckNames();

    SPDLOG_INFO("[ComboShip] MM_InitRandoLogic: Regions={} checks={} items={}",
                Rando::Logic::Regions.size(),
                Rando::StaticData::Checks.size(),
                Rando::StaticData::Items.size());
}
```

Delete the whole block. (`PopulateCheckNames()` now runs inside MM's real `InitOTR`/`ShipInit::InitAll` during the eager boot; if a later build shows MM check names are empty, confirm `PopulateCheckNames` is registered as a ShipInit func — it is invoked during normal MM boot — and if not, move that single call into `MM_BootForCombo` after `MM_RunMain()`.)

- [ ] **Step 3: Build 2ship**

Run: `.\scripts\build-2ship.ps1 --Debug > build-2ship.log 2>&1; "EXIT=$LASTEXITCODE"; Select-String -Path build-2ship.log -Pattern 'error|LNK|fatal' -CaseSensitive:$false`
Expected: `EXIT=0`, no error matches, `x64\Debug\2ship.dll` mtime advances.

- [ ] **Step 4: Commit Tasks 1 + 2 together**

```bash
git add mm/src/code/main.c mm/2s2h/BenPort.cpp
git commit -m "feat(combo): MM_BootForCombo (init MM without game loop); drop headless warm-up

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Add `SOH_ResumeForeground` (OOT port code)

**Files:**
- Modify: `soh/soh/OTRGlobals.cpp` (add export inside the COMBO_BUILD block, after `SOH_ResumeGame` ~line 2573)

- [ ] **Step 1: Add the export**

Immediately after the closing brace of `SOH_ResumeGame` (`soh/soh/OTRGlobals.cpp:2573`) and before the `#endif` at line 2574, insert:

```cpp

// ComboShip: re-activate OOT as the foreground game WITHOUT entering its game loop. Used once at
// startup right after MM is eagerly booted (MM_BootForCombo), which left MM's RM active and tore down
// OOT's audio/GUI (via SOH_PrepareForTransition). This restores OOT's RM/audio/GUI/menu so OOT's first
// real boot (SOH_RunMain) renders correctly. Mirrors SOH_ResumeGame minus the frame-loop reset and
// SOH_RunGameLoop — SOH_RunMain runs the loop.
extern "C" __declspec(dllexport) void SOH_ResumeForeground(void) {
    auto ctx = Ship::Context::GetInstance();
    SOH_ReinitForResume();  // OOT RM active, OOT audio, OOT GUI + menu
    // Re-sync this DLL's ImGui current-context (GImGui is per-module).
    ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());
}
```

- [ ] **Step 2: Build soh**

Run: `.\scripts\build-soh.ps1 --Debug > build-soh.log 2>&1; "EXIT=$LASTEXITCODE"; Select-String -Path build-soh.log -Pattern 'error|LNK|fatal' -CaseSensitive:$false`
Expected: `EXIT=0`, no error matches, `x64\Debug\soh.dll` mtime advances.

- [ ] **Step 3: Commit**

```bash
git add soh/soh/OTRGlobals.cpp
git commit -m "feat(combo): SOH_ResumeForeground (reactivate OOT as foreground, no game loop)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Wire eager boot into the combo launcher; remove the warm-up call

**Files:**
- Modify: `combo/ComboShip.cpp` (export resolution near other `GetSym` calls; the warm-up block ~lines 447-452; the main-loop init `bool ootBooted = false, mmBooted = false;` at line 508)

- [ ] **Step 1: Find how exports are resolved and declared**

Read `combo/ComboShip.cpp` around the function-pointer typedefs/declarations and the `GetSym`/`GetProcAddress` resolution block (search for `MM_InitRandoLogic`, `MM_RunGame`, `SOH_ResumeGame`). Note the exact pattern used (typedef + static fn-pointer + `GetSym(mmModule, "...")` / `GetSym(sohModule, "...")`).

- [ ] **Step 2: Declare the two new function pointers**

Next to the existing MM/SOH transition pointers (where `MM_RunGame`, `MM_ResumeGame`, `SOH_ResumeGame` are declared), add:

```cpp
// ComboShip: eager MM boot at startup (replaces the headless MM_InitRandoLogic warm-up).
typedef void (*FnVoidVoid)(void);
static FnVoidVoid MM_BootForCombo    = nullptr;
static FnVoidVoid SOH_ResumeForeground = nullptr;
```

> If `combo/ComboShip.cpp` already has a `void(*)(void)` typedef (e.g. `FnVoid`), reuse it instead of adding `FnVoidVoid`.

- [ ] **Step 3: Resolve the symbols; remove `MM_InitRandoLogic` resolution**

In the resolution block, add (mirroring the existing `GetSym` calls):

```cpp
MM_BootForCombo      = (FnVoidVoid)GetSym(mmModule,  "MM_BootForCombo");
SOH_ResumeForeground = (FnVoidVoid)GetSym(sohModule, "SOH_ResumeForeground");
```

Then **delete** the line that resolves `MM_InitRandoLogic` (e.g. `MM_InitRandoLogic = (...)GetSym(mmModule, "MM_InitRandoLogic");`) and its static declaration/typedef if used nowhere else.

- [ ] **Step 4: Replace the warm-up block with the eager-boot sequence**

Replace the current block (`combo/ComboShip.cpp:447-452`):

```cpp
    // ComboShip Inc3: warm up MM's rando-logic engine (Regions + StaticData) now that
    // the shared libultraship Context exists. Must happen before any oracle queries.
    if (MM_InitRandoLogic) {
        MM_InitRandoLogic();
        std::cout << "[ComboShip] MM rando-logic warm-up complete." << std::endl;
    }
```

with:

```cpp
    // ComboShip: eagerly boot MM now (after OOT init) so the cross-world rando oracle runs against a
    // real, fully-initialized MM. This performs an OOT->MM->OOT transition once, with MM's game loop
    // skipped: hand the foreground from OOT to MM (SOH_PrepareForTransition), boot MM without its loop
    // (MM_BootForCombo), then hand the foreground back to OOT (MM_PrepareForTransition stops MM's
    // audio; SOH_ResumeForeground re-activates OOT's RM/audio/GUI). MM stays booted + resident, so the
    // first portal transition is a normal MM_ResumeGame.
    if (MM_BootForCombo && SOH_PrepareForTransition && MM_PrepareForTransition && SOH_ResumeForeground) {
        std::cout << "[ComboShip] Eager MM boot: begin" << std::endl;
        SOH_PrepareForTransition();   // stop OOT audio + tear down OOT GUI (Context/RM kept alive)
        MM_BootForCombo();            // full MM init on the shared Context, MM's RM active, no loop
        MM_PrepareForTransition();    // stop MM's audio (MM started it during InitOTR)
        SOH_ResumeForeground();       // re-activate OOT's RM/audio/GUI as the foreground game
        mmEagerBooted = true;
        std::cout << "[ComboShip] Eager MM boot: complete" << std::endl;
    } else {
        std::cerr << "[ComboShip] Eager MM boot: required exports missing — oracle will be unavailable"
                  << std::endl;
    }
```

- [ ] **Step 5: Carry the eager-boot result into the main loop's `mmBooted`**

Add a declaration before the eager-boot block (near where `SOH_Init()` is called), so it is in scope at the loop:

```cpp
    bool mmEagerBooted = false;
```

Then change the main-loop init line (`combo/ComboShip.cpp:508`):

```cpp
    bool ootBooted = false, mmBooted = false;
```

to:

```cpp
    bool ootBooted = false;
    // MM was already booted at startup (eager boot) — the first portal transition must RESUME MM,
    // not run MM_RunGame (which would re-run MM_RunMain on an already-initialized MM).
    bool mmBooted = mmEagerBooted;
```

- [ ] **Step 6: Build ComboShip (relinks exe; builds soh dependency)**

Run: `.\scripts\build-comboship.ps1 --Debug > build-combo.log 2>&1; "EXIT=$LASTEXITCODE"; Select-String -Path build-combo.log -Pattern 'error|LNK|fatal' -CaseSensitive:$false`
Expected: `EXIT=0`, no error matches, `build\x64\combo\Debug\ComboShip.exe` mtime advances.

- [ ] **Step 7: Commit**

```bash
git add combo/ComboShip.cpp
git commit -m "feat(combo): eager MM boot at startup; first transition becomes a resume

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Runtime verification (manual — no test harness)

**Files:** none (verification only).

- [ ] **Step 1: Launch and confirm clean startup**

Run the app (needs the user's `oot.o2r` + `mm.o2r` present in the run dir):

```
! .\build\x64\combo\Debug\ComboShip.exe
```

Expected in console: `[ComboShip] OOT initialized.`, then `[ComboShip] Eager MM boot: begin`, `[ComboShip] Eager MM boot: complete`, and the game reaches **OOT file-select with no crash**. (Previously this crashed in `MM_InitRandoLogic` → `InitAll` → CosmeticEditor/BenInputEditorWindow.)

- [ ] **Step 2: Confirm the oracle/generation works**

Create a new save in OOT. Expected console: `[ComboShip] Combo_OnGenerate: combined-logic fill succeeded` (not the `falling back to no-logic` path). Confirm `saves/combo/slot{N}.spoiler.json` contains a non-empty `mm` section and `foreign` markers — proving MM's region graph was real and reachable checks were found.

- [ ] **Step 3: Confirm the OOT↔MM round trip still works (regression)**

Play to the portal (Mido's House / mask shop) → confirm transition to MM (console `[ComboShip] MM resume`, since `mmBooted` started true). Return via Clock Tower interior → confirm return to OOT (`[ComboShip] OOT resume`). No crash, both games render correctly (RM swap intact).

- [ ] **Step 4: Confirm Increment 6 cross-world delivery (the original goal)**

With a generated seed: collect an OOT check holding an MM item → "Sent to Termina" toast → portal to MM → "Received from Hyrule" and the item is granted. And the reverse. (If a crash or wrong behavior appears here, it is Increment 6 logic, not this foundation — triage separately.)

- [ ] **Step 5: If any step fails, capture and triage**

Read `x64/Debug/combo_abort_stack.txt` (crash capture) and the console log. Do NOT layer fixes — identify the failing layer (eager-boot sequence vs. oracle vs. Inc6) first.

---

### Task 6: Documentation

**Files:**
- Modify: `docs/UPSTREAM_MERGES.md` (new section), the progress doc, and the memory file.

- [ ] **Step 1: Append to `docs/UPSTREAM_MERGES.md`**

Add a section documenting the game-source deviation and the eager-boot model:

```markdown
## Cross-World Randomizer — Eager MM boot (replaces headless warm-up) (2026-06-04)

The MM rando oracle needs MM's region graph at OOT-generate time, before MM would normally boot.
The Inc3 approach (`MM_InitRandoLogic` -> `ShipInit::InitAll()` at startup) faked a headless MM and
crashed: `InitAll()` runs MM's entire UI/cosmetic/audio init surface, which dereferences a null
`GameInteractor::Instance` and then `ResourceManager`-loads MM assets through OOT's RM. Replaced by
**eagerly booting MM for real at startup** (one OOT->MM->OOT transition with MM's game loop skipped).

**Game-source deviation (additive, COMBO_BUILD-guarded — preserve on future mm merges):**
- `mm/src/code/main.c` (`MM_RunMain` tail): the final `Graph_ThreadEntry(0)` is gated on
  `gComboBootOnly` so `MM_BootForCombo` can run MM's full init without entering the blocking loop.
  `extern int gComboBootOnly;` declared near the InitOTR forward-decl.

**MM port code (`mm/2s2h/BenPort.cpp`):**
- `extern "C" int gComboBootOnly` definition; `MM_BootForCombo()` export (sets `sComboTransitionActive`
  + `gComboBootOnly`, runs `MM_RunMain`, clears the flag).
- **Deleted** `MM_InitRandoLogic()` (and the throwaway-singleton workaround from `f54b3cece`).

**OOT port code (`soh/soh/OTRGlobals.cpp`):** `SOH_ResumeForeground()` export = `SOH_ReinitForResume()`
+ `ImGui::SetCurrentContext`, no game loop (reactivates OOT as foreground after the eager MM boot).

**combo (`combo/ComboShip.cpp`):** the warm-up block is replaced by the eager-boot sequence
(`SOH_PrepareForTransition` -> `MM_BootForCombo` -> `MM_PrepareForTransition` -> `SOH_ResumeForeground`);
the main loop starts with `mmBooted = true` so the first portal transition is a `MM_ResumeGame`.
```

- [ ] **Step 2: Update the progress doc**

In `docs/superpowers/plans/2026-06-04-crossworld-randomizer-combined-logic-progress.md`, note under a new "Foundation" heading that the headless warm-up was replaced by eager MM boot, and that the Inc6 crash class is resolved.

- [ ] **Step 3: Update memory**

In `C:\Users\medum\.claude\projects\E--Git-ComboShip-Combo\memory\comboship-crossgame-randomizer.md`, replace the "MM warm-up: ShipInit::InitAll() after SOH_Init()" decision with: "MM is eagerly booted for real at startup (`MM_BootForCombo`, game loop skipped via `gComboBootOnly`); the headless warm-up was removed." Add a `[[comboship-reverse-transition]]` link since this reuses that machinery.

- [ ] **Step 4: Commit**

```bash
git add docs/UPSTREAM_MERGES.md docs/superpowers/plans/2026-06-04-crossworld-randomizer-combined-logic-progress.md
git commit -m "docs(combo): eager MM boot replaces headless warm-up

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Phase 2 — Cross-game resource rendering (follow-on; design notes, not yet task-stepped)

This phase enables rendering an MM item model in an OOT chest (and vice-versa) — the eventual goal that
motivated the dual-resident direction. It is **out of scope for Phase 1** and should get its own spec +
plan after Phase 1 lands and is verified. Captured here so the foundation isn't lost.

**What's already true (from the transition trace):** both ResourceManagers are resident simultaneously
(`sOOTResourceManager` in soh.dll, `sMMResourceManager` in 2ship.dll); only the *active* one serves
lookups, because resource loads go through `Ship::Context::GetInstance()->GetResourceManager()->LoadResource(path)`.

**The work:** make a draw pull from the *other* game's RM within one frame. Two candidate approaches:
1. **Explicit RM addressing** — export each game's RM handle and add a `LoadResourceFromRM(path, rm)`
   path; the foreign-item draw resolves its model against the foreign game's RM explicitly.
2. **Fallback chain** — `LoadResource` tries the active RM, then the other game's RM on miss.

**Open questions for the Phase 2 spec:** how OOT's `GetItemEntry`/draw pipeline would invoke an MM
draw function + MM object with MM's gfx; resource-cache/name-collision behavior across two RMs;
whether the foreign model needs MM's gfx setup active for that draw call. This replaces the Inc6
"generic gift model" stub for foreign items (spec section: foreign-item presentation).

---

## Self-review notes

- **Spec coverage:** Phase 1 covers the crash root cause (warm-up runs MM's full init headlessly) by
  booting MM for real; deletes the warm-up; preserves the existing transition machinery; keeps Inc6
  intact. Phase 2 captures the cross-render follow-on the user asked to document.
- **Type/symbol consistency:** new exports are `MM_BootForCombo` (void→void), `SOH_ResumeForeground`
  (void→void), flag `gComboBootOnly` (`extern "C" int`, defined in BenPort.cpp, declared in main.c).
  `mmEagerBooted` (combo local) feeds `mmBooted`. These names are used identically across Tasks 1-4.
- **No placeholders:** every code edit shows the full replacement against verified current code.
- **Verification:** build steps per target (serialized soh→2ship→ComboShip) + a concrete manual
  launch click-path, since no automated harness exists.
```
